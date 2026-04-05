/**
 * SHT20Sensor.cpp
 * SHT20 温湿度传感器类 - 实现文件（I2C；ESP32 仅用非 Hold 0xF3/0xF5）
 */

#include "SHT20Sensor.h"
#include <Wire.h>
#include <math.h>

// SHT2x：Hold Master 0xE3/0xE5；非保持触发 0xF3/0xF5；软复位 0xFE
static const uint8_t CMD_TEMP_HOLD = 0xE3;
static const uint8_t CMD_RH_HOLD = 0xE5;
static const uint8_t CMD_TRIG_TEMP = 0xF3;
static const uint8_t CMD_TRIG_RH = 0xF5;
static const uint8_t CMD_SOFT_RESET = 0xFE;

// 非 Hold：多档等待（ms），慢模块/杜邦线宜更长；14bit 温度典型≤85ms
static const uint16_t kTempWaitSteps[] = {100, 150, 220, 350, 500};
static const uint16_t kRhWaitSteps[] = {60, 90, 130, 200, 320};

namespace {
char g_lastBusMsg[120];

// Sensirion SHT2x / HTU21 / Si7021：CRC-8，多项式 x^8+x^5+x^4+1（示例代码用 0x131 参与移位异或）
uint8_t crc8ComputeData(uint8_t msb, uint8_t lsb) {
    uint8_t data[2] = {msb, lsb};
    uint8_t crc = 0;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x131u);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void fmtTxFail(uint8_t cmd, uint8_t e) {
    // ESP32 Arduino Wire：0 成功；1 缓冲溢出；2 地址 NACK；3 数据 NACK；4 其它；5 超时
    const char* hint;
    switch (e) {
        case 1:
            hint = "发送缓冲过长";
            break;
        case 2:
            hint = "地址无应答(NAK)，查接线/7位地址0x40/供电/GND";
            break;
        case 3:
            hint = "数据字节无应答";
            break;
        case 4:
            hint = "其它错误(常因总线卡死或驱动状态异常；已可尝试 recover)";
            break;
        case 5:
            hint = "总线超时";
            break;
        default:
            hint = "未知码";
            break;
    }
    snprintf(g_lastBusMsg, sizeof(g_lastBusMsg), "写 0x%02X 失败 endTransmission=%u: %s", cmd, e,
             hint);
}

void fmtRxShort(uint8_t cmd, int n) {
    snprintf(g_lastBusMsg, sizeof(g_lastBusMsg),
             "读 0x%02X 仅收到 %d 字节(需3)；超时/延展/上拉弱/线长/非SHT2x",
             cmd, n);
}

void fmtCrc(uint8_t cmd, uint8_t msb, uint8_t lsb, uint8_t got, uint8_t calc) {
    snprintf(g_lastBusMsg, sizeof(g_lastBusMsg),
             "CRC 错误 cmd=0x%02X 数据=%02X %02X crc=%02X 计算=%02X (非SHT2x?总线干扰?)", cmd,
             msb, lsb, got, calc);
}

/**
 * Hold Master：写命令后不能发 STOP，应 repeated START 再读。
 * 原先对 0xE3/0xE5 使用默认 endTransmission()（带 STOP）会导致测量/读序错误。
 */
bool holdMasterRead(uint8_t addr, uint8_t cmd, uint16_t& rawOut) {
    Wire.beginTransmission(addr);
    Wire.write(cmd);
    uint8_t e = Wire.endTransmission(false);
    if (e != 0) {
        fmtTxFail(cmd, e);
        return false;
    }
    size_t n = Wire.requestFrom(addr, (size_t)3);
    if (n != 3) {
        fmtRxShort(cmd, (int)n);
        while (Wire.available()) {
            (void)Wire.read();
        }
        return false;
    }
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    uint8_t crc = Wire.read();
    uint8_t calc = crc8ComputeData(msb, lsb);
    if (calc != crc) {
        fmtCrc(cmd, msb, lsb, crc, calc);
        return false;
    }
    rawOut = (uint16_t)((msb << 8) | lsb);
    rawOut &= 0xFFFCu;
    return true;
}
} // namespace

const float SHT20Sensor::MAX_CHANGE_THRESHOLD = 3.0;
const uint8_t SHT20Sensor::MAX_ANOMALY_COUNT = 3;

bool SHT20Sensor::crc8Verify(uint8_t msb, uint8_t lsb, uint8_t crc) {
    return crc8ComputeData(msb, lsb) == crc;
}

SHT20Sensor::SHT20Sensor(int8_t sda, int8_t scl, int8_t led, uint8_t address)
    : i2cAddr(address),
      sdaPin(sda),
      sclPin(scl),
      ledPin(led),
      readCount(0),
      errorCount(0),
      lastTemperature(NAN),
      lastHumidity(NAN),
      lastReadTime(0),
      lastValidTemperature(NAN),
      lastValidHumidity(NAN),
      consecutiveAnomalyCount(0),
      anomalyCount(0) {
    if (sdaPin >= 0 && sclPin >= 0) {
        // 低速更易通过时钟延展；仍失败可查上拉/共地
        Wire.begin((int)sdaPin, (int)sclPin, 50000UL);
    } else {
        Wire.begin();
        Wire.setClock(50000);
    }
#if defined(ARDUINO_ARCH_ESP32)
    Wire.setTimeOut(2000);
#endif

    if (ledPin >= 0) {
        pinMode((uint8_t)ledPin, OUTPUT);
        digitalWrite((uint8_t)ledPin, LOW);
    }

    writeCommand(CMD_SOFT_RESET);
    delay(50);

    Wire.beginTransmission(i2cAddr);
    uint8_t probe = Wire.endTransmission();
    Serial.printf("[SHT20] 7位地址 0x%02X 探针: %s\n", i2cAddr,
                  probe == 0 ? "有应答" : "无应答");
    if (probe != 0) {
        Serial.println("[SHT20] 扫描 I2C 总线 (0x08–0x77)…");
        uint8_t found = 0;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  → 发现从机 0x%02X\n", a);
                found++;
            }
        }
        if (found == 0) {
            Serial.println("  (未发现任何从机，检查 SDA/SCL/GND/上拉/供电)");
        } else {
            Serial.println("  提示: SHT2x 一般为 0x40；若列表中无 0x40，可能不是 SHT20 或地址被拉偏");
        }
    }

    Serial.println("SHT20 传感器已初始化 (I2C)");
}

SHT20Sensor::~SHT20Sensor() {
    cleanup();
}

bool SHT20Sensor::writeCommand(uint8_t cmd) {
    Wire.beginTransmission(i2cAddr);
    Wire.write(cmd);
    return Wire.endTransmission() == 0;
}

void SHT20Sensor::recoverI2cBus() {
#if defined(ARDUINO_ARCH_ESP32)
    if (sdaPin >= 0 && sclPin >= 0) {
        int sda = (int)sdaPin;
        int scl = (int)sclPin;
        pinMode(sda, INPUT_PULLUP);
        pinMode(scl, INPUT_PULLUP);
        delayMicroseconds(20);
        pinMode(scl, OUTPUT);
        for (int i = 0; i < 9; i++) {
            digitalWrite(scl, LOW);
            delayMicroseconds(5);
            digitalWrite(scl, HIGH);
            delayMicroseconds(5);
        }
        pinMode(scl, INPUT_PULLUP);
    }
    Wire.flush();
    (void)Wire.end();
    delay(30);
    if (sdaPin >= 0 && sclPin >= 0) {
        Wire.begin((int)sdaPin, (int)sclPin, 50000UL);
    } else {
        Wire.begin();
        Wire.setClock(50000);
    }
    Wire.setTimeOut(2000);
    writeCommand(CMD_SOFT_RESET);
    delay(50);
#endif
}

bool SHT20Sensor::tryReadRaw(uint8_t triggerCmd, uint16_t waitMs,
                             uint16_t& rawOut) {
    Wire.beginTransmission(i2cAddr);
    Wire.write(triggerCmd);
    uint8_t e = Wire.endTransmission();
    if (e != 0) {
        fmtTxFail(triggerCmd, e);
        return false;
    }
    if (waitMs > 0) {
        delay(waitMs);
    }

    size_t n = Wire.requestFrom(i2cAddr, (size_t)3);
    if (n != 3) {
        fmtRxShort(triggerCmd, (int)n);
        while (Wire.available()) {
            (void)Wire.read();
        }
        return false;
    }
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    uint8_t crc = Wire.read();
    uint8_t calc = crc8ComputeData(msb, lsb);
    if (calc != crc) {
        fmtCrc(triggerCmd, msb, lsb, crc, calc);
        return false;
    }
    rawOut = (uint16_t)((msb << 8) | lsb);
    rawOut &= 0xFFFCu;
    return true;
}

bool SHT20Sensor::readTemperatureRaw(uint16_t& rawOut) {
    for (size_t i = 0; i < sizeof(kTempWaitSteps) / sizeof(kTempWaitSteps[0]); i++) {
        if (tryReadRaw(CMD_TRIG_TEMP, kTempWaitSteps[i], rawOut)) {
            return true;
        }
    }
#if !defined(ARDUINO_ARCH_ESP32)
    // ESP32 上 Hold(0xE3) 常因时钟延展出现 requestFrom=0；仅用非 Hold 即可
    if (holdMasterRead(i2cAddr, CMD_TEMP_HOLD, rawOut)) {
        return true;
    }
#endif
    return false;
}

bool SHT20Sensor::readHumidityRaw(uint16_t& rawOut) {
    for (size_t i = 0; i < sizeof(kRhWaitSteps) / sizeof(kRhWaitSteps[0]); i++) {
        if (tryReadRaw(CMD_TRIG_RH, kRhWaitSteps[i], rawOut)) {
            return true;
        }
    }
#if !defined(ARDUINO_ARCH_ESP32)
    if (holdMasterRead(i2cAddr, CMD_RH_HOLD, rawOut)) {
        return true;
    }
#endif
    return false;
}

bool SHT20Sensor::sampleRaw(float& temperature, float& humidity) {
    uint16_t rawT = 0;
    uint16_t rawH = 0;

    if (!readTemperatureRaw(rawT)) {
        Serial.print("[SHT20] ");
        Serial.println(g_lastBusMsg);
        return false;
    }
    temperature = -46.85f + 175.72f * ((float)rawT / 65536.0f);

    if (!readHumidityRaw(rawH)) {
        Serial.print("[SHT20] ");
        Serial.println(g_lastBusMsg);
        return false;
    }
    humidity = -6.0f + 125.0f * ((float)rawH / 65536.0f);
    if (humidity < 0.0f) {
        humidity = 0.0f;
    }
    if (humidity > 100.0f) {
        humidity = 100.0f;
    }
    return true;
}

bool SHT20Sensor::read(uint8_t retryCount, uint16_t retryDelay,
                       void (*watchdogCallback)()) {
    readCount++;

    for (uint8_t attempt = 0; attempt < retryCount; attempt++) {
        if (watchdogCallback != nullptr) {
            watchdogCallback();
        }

        ledOff();

        float temperature = NAN;
        float humidity = NAN;
        if (!sampleRaw(temperature, humidity)) {
            errorCount++;
            char msg[100];
            snprintf(msg, sizeof(msg), "读取失败 (尝试 %d/%d): I2C/CRC 或超时",
                     attempt + 1, retryCount);

            if (attempt == retryCount - 1) {
                logMessage(msg, true);
                ledOff();
                return false;
            }
            logMessage(msg, false);
#if defined(ARDUINO_ARCH_ESP32)
            recoverI2cBus();
#endif
            delay(retryDelay);
            continue;
        }

        if (isnan(temperature) || isnan(humidity)) {
            errorCount++;
            char msg[100];
            snprintf(msg, sizeof(msg),
                     "读取失败 (尝试 %d/%d): 传感器返回 NaN", attempt + 1,
                     retryCount);
            if (attempt == retryCount - 1) {
                logMessage(msg, true);
                ledOff();
                return false;
            }
            logMessage(msg, false);
            delay(retryDelay);
            continue;
        }

        if (!validateData(temperature, humidity)) {
            errorCount++;
            char msg[100];
            snprintf(msg, sizeof(msg),
                     "数据超出正常范围: 温度=%.2f, 湿度=%.2f (尝试 %d/%d)",
                     temperature, humidity, attempt + 1, retryCount);

            if (attempt == retryCount - 1) {
                logMessage(msg, true);
                ledOff();
                return false;
            }
            logMessage(msg, false);
            delay(retryDelay);
            continue;
        }

        float smoothedTemp = NAN;
        float smoothedHumidity = NAN;
        bool isValid =
            checkDataChange(temperature, humidity, smoothedTemp, smoothedHumidity);

        if (isValid) {
            lastValidTemperature = temperature;
            lastValidHumidity = humidity;
        }

        lastTemperature = smoothedTemp;
        lastHumidity = smoothedHumidity;
        lastReadTime = millis();

        ledOn();

        char msg[100];
        snprintf(msg, sizeof(msg), "读取成功: 温度=%.2f°C, 湿度=%.2f%%",
                 smoothedTemp, smoothedHumidity);
        logMessage(msg, false);

        return true;
    }

    return false;
}

bool SHT20Sensor::readFahrenheit(float& fahrenheit, float& humidity,
                                 uint8_t retryCount, uint16_t retryDelay) {
    if (read(retryCount, retryDelay)) {
        fahrenheit = (lastTemperature * 9.0f / 5.0f) + 32.0f;
        humidity = lastHumidity;
        return true;
    }
    return false;
}

SHT20Sensor::Reading SHT20Sensor::getLastReading() {
    Reading reading;
    reading.temperature = lastTemperature;
    reading.humidity = lastHumidity;
    reading.timestamp = lastReadTime;
    reading.valid = !isnan(lastTemperature) && !isnan(lastHumidity);
    return reading;
}

SHT20Sensor::Statistics SHT20Sensor::getStatistics() const {
    Statistics stats;
    stats.totalReads = readCount;
    stats.errors = errorCount;

    unsigned long successCount = readCount - errorCount;
    stats.successRate =
        (readCount > 0) ? (successCount * 100.0f / (float)readCount) : 0.0f;

    stats.anomalyCount = anomalyCount;
    stats.consecutiveAnomalyCount = consecutiveAnomalyCount;

    return stats;
}

void SHT20Sensor::resetStatistics() {
    readCount = 0;
    errorCount = 0;
    anomalyCount = 0;
    consecutiveAnomalyCount = 0;
    Serial.println("统计信息已重置");
}

void SHT20Sensor::cleanup() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, LOW);
    }
    Serial.println("SHT20 传感器资源已清理");
}

bool SHT20Sensor::validateData(float temperature, float humidity) {
    if (temperature < -40.0f || temperature > 125.0f) {
        return false;
    }
    if (humidity < 0.0f || humidity > 100.0f) {
        return false;
    }
    return true;
}

bool SHT20Sensor::checkDataChange(float temperature, float humidity,
                                  float& smoothedTemp, float& smoothedHumidity) {
    if (isnan(lastValidTemperature) || isnan(lastValidHumidity)) {
        smoothedTemp = temperature;
        smoothedHumidity = humidity;
        return true;
    }

    float tempChange = fabsf(temperature - lastValidTemperature);
    float humidityChange = fabsf(humidity - lastValidHumidity);

    bool isAnomaly = (tempChange > MAX_CHANGE_THRESHOLD ||
                      humidityChange > MAX_CHANGE_THRESHOLD);

    if (isAnomaly) {
        consecutiveAnomalyCount++;
        anomalyCount++;

        char msg[150];
        snprintf(msg, sizeof(msg),
                 "检测到异常数据: 温度变化=%.1f°C, 湿度变化=%.1f%%, 连续异常次数=%d",
                 tempChange, humidityChange, consecutiveAnomalyCount);
        logMessage(msg, false);

        if (consecutiveAnomalyCount > MAX_ANOMALY_COUNT) {
            snprintf(msg, sizeof(msg),
                     "连续异常数据超过%d次，采用当前数据: 温度=%.1f°C, 湿度=%.1f%%",
                     MAX_ANOMALY_COUNT, temperature, humidity);
            logMessage(msg, false);

            consecutiveAnomalyCount = 0;
            smoothedTemp = temperature;
            smoothedHumidity = humidity;
            return true;
        }

        snprintf(msg, sizeof(msg),
                 "丢弃异常数据，使用上次有效数据: 温度=%.1f°C, 湿度=%.1f%%",
                 lastValidTemperature, lastValidHumidity);
        logMessage(msg, false);

        smoothedTemp = lastValidTemperature;
        smoothedHumidity = lastValidHumidity;
        return false;
    }

    if (consecutiveAnomalyCount > 0) {
        logMessage("数据恢复正常，重置异常计数", false);
        consecutiveAnomalyCount = 0;
    }

    smoothedTemp = temperature;
    smoothedHumidity = humidity;
    return true;
}

void SHT20Sensor::ledOn() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, HIGH);
    }
}

void SHT20Sensor::ledOff() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, LOW);
    }
}

void SHT20Sensor::logMessage(const char* message, bool isError) {
    if (isError) {
        Serial.print("[错误] ");
    } else {
        Serial.print("[信息] ");
    }
    Serial.println(message);
}
