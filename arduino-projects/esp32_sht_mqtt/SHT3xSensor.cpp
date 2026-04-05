/**
 * SHT3xSensor.cpp
 * 单次测量：0x2400（高重复性、禁止时钟延展），读 6 字节 T+CRC + RH+CRC
 */

#include "SHT3xSensor.h"
#include <Wire.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

// 单次测量，高重复性，时钟延展关闭（主机延时等待）
static const uint16_t CMD_MEAS_HIGHREP_STRETCH_OFF = 0x2400;
static const uint16_t CMD_SOFT_RESET = 0x30A2;

static const uint16_t MEASURE_WAIT_MS = 20;

namespace {
char g_lastSht3Msg[100];

void setMsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_lastSht3Msg, sizeof(g_lastSht3Msg), fmt, ap);
    va_end(ap);
}
} // namespace

const float SHT3xSensor::MAX_CHANGE_THRESHOLD = 3.0f;
const uint8_t SHT3xSensor::MAX_ANOMALY_COUNT = 3;

uint8_t SHT3xSensor::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 8; b > 0; --b) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31u);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool SHT3xSensor::crc8Check2(const uint8_t* msbLsbCrc) {
    uint8_t buf[2] = {msbLsbCrc[0], msbLsbCrc[1]};
    return crc8(buf, 2) == msbLsbCrc[2];
}

SHT3xSensor::SHT3xSensor(int8_t sda, int8_t scl, int8_t led, uint8_t address)
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
        Wire.begin((int)sdaPin, (int)sclPin, 100000UL);
    } else {
        Wire.begin();
        Wire.setClock(100000);
    }
#if defined(ARDUINO_ARCH_ESP32)
    Wire.setTimeOut(500);
#endif

    if (ledPin >= 0) {
        pinMode((uint8_t)ledPin, OUTPUT);
        digitalWrite((uint8_t)ledPin, LOW);
    }

    writeCmd16(CMD_SOFT_RESET);
    delay(2);

    Wire.beginTransmission(i2cAddr);
    uint8_t probe = Wire.endTransmission();
    Serial.printf("[SHT3x] 7位地址 0x%02X 探针: %s (ADDR脚接GND=0x44 接VDD=0x45)\n", i2cAddr,
                  probe == 0 ? "有应答" : "无应答");

    Serial.println("SHT3x 传感器已初始化 (I2C)");
}

SHT3xSensor::~SHT3xSensor() {
    cleanup();
}

bool SHT3xSensor::writeCmd16(uint16_t cmd) {
    Wire.beginTransmission(i2cAddr);
    Wire.write((uint8_t)(cmd >> 8));
    Wire.write((uint8_t)(cmd & 0xFF));
    return Wire.endTransmission() == 0;
}

void SHT3xSensor::recoverI2cBus() {
#if defined(ARDUINO_ARCH_ESP32)
    Wire.flush();
    (void)Wire.end();
    delay(20);
    if (sdaPin >= 0 && sclPin >= 0) {
        Wire.begin((int)sdaPin, (int)sclPin, 100000UL);
    } else {
        Wire.begin();
        Wire.setClock(100000);
    }
    Wire.setTimeOut(500);
#endif
    writeCmd16(CMD_SOFT_RESET);
    delay(2);
}

bool SHT3xSensor::sampleRaw(float& temperature, float& humidity) {
    if (!writeCmd16(CMD_MEAS_HIGHREP_STRETCH_OFF)) {
        setMsg("写命令 0x2400 失败");
        return false;
    }
    delay(MEASURE_WAIT_MS);

    size_t n = Wire.requestFrom(i2cAddr, (size_t)6);
    if (n != 6) {
        setMsg("读测量仅 %u 字节(需6)", (unsigned)n);
        while (Wire.available()) {
            (void)Wire.read();
        }
        return false;
    }

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) {
        buf[i] = (uint8_t)Wire.read();
    }

    if (!crc8Check2(buf) || !crc8Check2(buf + 3)) {
        setMsg("CRC 失败 T=%02X%02X:%02X RH=%02X%02X:%02X", buf[0], buf[1], buf[2], buf[3],
               buf[4], buf[5]);
        return false;
    }

    uint16_t rawT = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t rawH = (uint16_t)((buf[3] << 8) | buf[4]);

    temperature = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    humidity = 100.0f * ((float)rawH / 65535.0f);
    if (humidity < 0.0f) {
        humidity = 0.0f;
    }
    if (humidity > 100.0f) {
        humidity = 100.0f;
    }
    return true;
}

bool SHT3xSensor::read(uint8_t retryCount, uint16_t retryDelay,
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
            char msg[120];
            snprintf(msg, sizeof(msg), "读取失败 (尝试 %d/%d): %s", attempt + 1, retryCount,
                     g_lastSht3Msg);

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
            snprintf(msg, sizeof(msg), "读取失败 (尝试 %d/%d): 传感器返回 NaN", attempt + 1,
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
                     "数据超出正常范围: 温度=%.2f, 湿度=%.2f (尝试 %d/%d)", temperature,
                     humidity, attempt + 1, retryCount);

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
        snprintf(msg, sizeof(msg), "读取成功: 温度=%.2f°C, 湿度=%.2f%%", smoothedTemp,
                 smoothedHumidity);
        logMessage(msg, false);

        return true;
    }

    return false;
}

bool SHT3xSensor::readFahrenheit(float& fahrenheit, float& humidity,
                                 uint8_t retryCount, uint16_t retryDelay) {
    if (read(retryCount, retryDelay)) {
        fahrenheit = (lastTemperature * 9.0f / 5.0f) + 32.0f;
        humidity = lastHumidity;
        return true;
    }
    return false;
}

SHT3xSensor::Reading SHT3xSensor::getLastReading() {
    Reading reading;
    reading.temperature = lastTemperature;
    reading.humidity = lastHumidity;
    reading.timestamp = lastReadTime;
    reading.valid = !isnan(lastTemperature) && !isnan(lastHumidity);
    return reading;
}

SHT3xSensor::Statistics SHT3xSensor::getStatistics() const {
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

void SHT3xSensor::resetStatistics() {
    readCount = 0;
    errorCount = 0;
    anomalyCount = 0;
    consecutiveAnomalyCount = 0;
    Serial.println("统计信息已重置");
}

void SHT3xSensor::cleanup() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, LOW);
    }
    Serial.println("SHT3x 传感器资源已清理");
}

bool SHT3xSensor::validateData(float temperature, float humidity) {
    if (temperature < -40.0f || temperature > 125.0f) {
        return false;
    }
    if (humidity < 0.0f || humidity > 100.0f) {
        return false;
    }
    return true;
}

bool SHT3xSensor::checkDataChange(float temperature, float humidity,
                                  float& smoothedTemp, float& smoothedHumidity) {
    if (isnan(lastValidTemperature) || isnan(lastValidHumidity)) {
        smoothedTemp = temperature;
        smoothedHumidity = humidity;
        return true;
    }

    float tempChange = fabsf(temperature - lastValidTemperature);
    float humidityChange = fabsf(humidity - lastValidHumidity);

    bool isAnomaly =
        (tempChange > MAX_CHANGE_THRESHOLD || humidityChange > MAX_CHANGE_THRESHOLD);

    if (isAnomaly) {
        consecutiveAnomalyCount++;
        anomalyCount++;

        char msg[150];
        snprintf(msg, sizeof(msg),
                 "检测到异常数据: 温度变化=%.1f°C, 湿度变化=%.1f%%, 连续异常次数=%d", tempChange,
                 humidityChange, consecutiveAnomalyCount);
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

void SHT3xSensor::ledOn() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, HIGH);
    }
}

void SHT3xSensor::ledOff() {
    if (ledPin >= 0) {
        digitalWrite((uint8_t)ledPin, LOW);
    }
}

void SHT3xSensor::logMessage(const char* message, bool isError) {
    if (isError) {
        Serial.print("[错误] ");
    } else {
        Serial.print("[信息] ");
    }
    Serial.println(message);
}
