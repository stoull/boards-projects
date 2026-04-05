/**
 * DHT22Sensor.cpp
 * DHT22 温湿度传感器类 - 实现文件
 */

#include "DHT22Sensor.h"

// 静态常量定义
const float DHT22Sensor::MAX_CHANGE_THRESHOLD = 3.0;
const uint8_t DHT22Sensor::MAX_ANOMALY_COUNT = 3;

/**
 * 构造函数
 * @param dataPin DHT22 数据引脚
 * @param ledPin LED 指示灯引脚 (默认 -1 表示不使用)
 */
DHT22Sensor::DHT22Sensor(uint8_t dataPin, int8_t ledPin)
    : ledPin(ledPin),
      readCount(0),
      errorCount(0),
      lastTemperature(NAN),
      lastHumidity(NAN),
      lastReadTime(0),
      lastValidTemperature(NAN),
      lastValidHumidity(NAN),
      consecutiveAnomalyCount(0),
      anomalyCount(0) {
    
    // 初始化 DHT 传感器
    sensor = new DHT(dataPin, DHT22);
    sensor->begin();
    
    // 初始化 LED (如果指定)
    if (ledPin >= 0) {
        pinMode(ledPin, OUTPUT);
        digitalWrite(ledPin, LOW);
    }
    
    Serial.println("DHT22 传感器已初始化");
}

/**
 * 析构函数
 */
DHT22Sensor::~DHT22Sensor() {
    cleanup();
}

/**
 * 读取温湿度数据
 * @param retryCount 重试次数
 * @param retryDelay 重试延迟 (毫秒)
 * @param watchdogCallback 看门狗回调函数
 * @return 成功返回 true，失败返回 false
 */
bool DHT22Sensor::read(uint8_t retryCount, uint16_t retryDelay, 
                       void (*watchdogCallback)()) {
    readCount++;
    
    for (uint8_t attempt = 0; attempt < retryCount; attempt++) {
        // 喂狗 (如果提供了回调)
        if (watchdogCallback != nullptr) {
            watchdogCallback();
        }
        
        // 关闭 LED 表示正在读取
        ledOff();
        
        // 读取传感器
        float temperature = sensor->readTemperature();
        float humidity = sensor->readHumidity();
        
        // 检查读取是否成功
        if (isnan(temperature) || isnan(humidity)) {
            errorCount++;
            char msg[100];
            snprintf(msg, sizeof(msg), 
                    "读取失败 (尝试 %d/%d): 传感器返回 NaN", 
                    attempt + 1, retryCount);
            
            if (attempt == retryCount - 1) {
                logMessage(msg, true);
                ledOff();
                return false;
            } else {
                logMessage(msg, false);
                delay(retryDelay);
                continue;
            }
        }
        
        // 数据验证
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
            } else {
                logMessage(msg, false);
                delay(retryDelay);
                continue;
            }
        }
        
        // 数据平滑处理
        float smoothedTemp, smoothedHumidity;
        bool isValid = checkDataChange(temperature, humidity, 
                                       smoothedTemp, smoothedHumidity);
        
        // 如果是有效的新数据，更新 lastValid 值
        if (isValid) {
            lastValidTemperature = temperature;
            lastValidHumidity = humidity;
        }
        
        // 保存最后读取的值
        lastTemperature = smoothedTemp;
        lastHumidity = smoothedHumidity;
        lastReadTime = millis();
        
        // 开启 LED 表示读取成功
        ledOn();
        
        char msg[100];
        snprintf(msg, sizeof(msg), "读取成功: 温度=%.2f°C, 湿度=%.2f%%", 
                smoothedTemp, smoothedHumidity);
        logMessage(msg, false);
        
        return true;
    }
    
    return false;
}

/**
 * 读取温度 (华氏度) 和湿度
 * @param fahrenheit 输出华氏温度
 * @param humidity 输出湿度
 * @param retryCount 重试次数
 * @param retryDelay 重试延迟
 * @return 成功返回 true
 */
bool DHT22Sensor::readFahrenheit(float& fahrenheit, float& humidity, 
                                 uint8_t retryCount, uint16_t retryDelay) {
    if (read(retryCount, retryDelay)) {
        fahrenheit = (lastTemperature * 9.0 / 5.0) + 32.0;
        humidity = lastHumidity;
        return true;
    }
    return false;
}

/**
 * 获取上次成功读取的数据
 * @return Reading 结构体
 */
DHT22Sensor::Reading DHT22Sensor::getLastReading() {
    Reading reading;
    reading.temperature = lastTemperature;
    reading.humidity = lastHumidity;
    reading.timestamp = lastReadTime;
    reading.valid = !isnan(lastTemperature) && !isnan(lastHumidity);
    return reading;
}

/**
 * 获取统计信息
 * @return Statistics 结构体
 */
DHT22Sensor::Statistics DHT22Sensor::getStatistics() const {
    Statistics stats;
    stats.totalReads = readCount;
    stats.errors = errorCount;
    
    unsigned long successCount = readCount - errorCount;
    stats.successRate = (readCount > 0) ? 
                       (successCount * 100.0 / readCount) : 0.0;
    
    stats.anomalyCount = anomalyCount;
    stats.consecutiveAnomalyCount = consecutiveAnomalyCount;
    
    return stats;
}

/**
 * 重置统计信息
 */
void DHT22Sensor::resetStatistics() {
    readCount = 0;
    errorCount = 0;
    anomalyCount = 0;
    consecutiveAnomalyCount = 0;
    Serial.println("统计信息已重置");
}

/**
 * 清理资源
 */
void DHT22Sensor::cleanup() {
    if (ledPin >= 0) {
        digitalWrite(ledPin, LOW);
    }
    if (sensor != nullptr) {
        delete sensor;
        sensor = nullptr;
    }
    Serial.println("DHT22 传感器资源已清理");
}

/**
 * 验证传感器数据是否在合理范围内
 * DHT22 测量范围: 温度 -40~80°C, 湿度 0~100%
 */
bool DHT22Sensor::validateData(float temperature, float humidity) {
    if (temperature < -40.0 || temperature > 80.0) {
        return false;
    }
    if (humidity < 0.0 || humidity > 100.0) {
        return false;
    }
    return true;
}

/**
 * 检查数据变化是否超出阈值 (数据平滑处理)
 * @param temperature 当前温度
 * @param humidity 当前湿度
 * @param smoothedTemp 输出平滑后的温度
 * @param smoothedHumidity 输出平滑后的湿度
 * @return 数据是否有效
 */
bool DHT22Sensor::checkDataChange(float temperature, float humidity,
                                  float& smoothedTemp, float& smoothedHumidity) {
    // 如果是第一次读取，直接返回当前数据
    if (isnan(lastValidTemperature) || isnan(lastValidHumidity)) {
        smoothedTemp = temperature;
        smoothedHumidity = humidity;
        return true;
    }
    
    // 计算变化量
    float tempChange = abs(temperature - lastValidTemperature);
    float humidityChange = abs(humidity - lastValidHumidity);
    
    // 检查是否超出阈值
    bool isAnomaly = (tempChange > MAX_CHANGE_THRESHOLD || 
                     humidityChange > MAX_CHANGE_THRESHOLD);
    
    if (isAnomaly) {
        // 异常数据计数增加
        consecutiveAnomalyCount++;
        anomalyCount++;
        
        char msg[150];
        snprintf(msg, sizeof(msg),
                "检测到异常数据: 温度变化=%.1f°C, 湿度变化=%.1f%%, 连续异常次数=%d",
                tempChange, humidityChange, consecutiveAnomalyCount);
        logMessage(msg, false);
        
        // 如果连续异常次数超过阈值，使用当前数据
        if (consecutiveAnomalyCount > MAX_ANOMALY_COUNT) {
            snprintf(msg, sizeof(msg),
                    "连续异常数据超过%d次，采用当前数据: 温度=%.1f°C, 湿度=%.1f%%",
                    MAX_ANOMALY_COUNT, temperature, humidity);
            logMessage(msg, false);
            
            // 重置连续异常计数
            consecutiveAnomalyCount = 0;
            smoothedTemp = temperature;
            smoothedHumidity = humidity;
            return true;
        } else {
            // 使用上次的有效数据
            snprintf(msg, sizeof(msg),
                    "丢弃异常数据，使用上次有效数据: 温度=%.1f°C, 湿度=%.1f%%",
                    lastValidTemperature, lastValidHumidity);
            logMessage(msg, false);
            
            smoothedTemp = lastValidTemperature;
            smoothedHumidity = lastValidHumidity;
            return false;
        }
    } else {
        // 数据正常，重置连续异常计数
        if (consecutiveAnomalyCount > 0) {
            logMessage("数据恢复正常，重置异常计数", false);
            consecutiveAnomalyCount = 0;
        }
        
        smoothedTemp = temperature;
        smoothedHumidity = humidity;
        return true;
    }
}

/**
 * LED 指示灯开启
 */
void DHT22Sensor::ledOn() {
    if (ledPin >= 0) {
        digitalWrite(ledPin, HIGH);
    }
}

/**
 * LED 指示灯关闭
 */
void DHT22Sensor::ledOff() {
    if (ledPin >= 0) {
        digitalWrite(ledPin, LOW);
    }
}

/**
 * 日志输出
 */
void DHT22Sensor::logMessage(const char* message, bool isError) {
    if (isError) {
        Serial.print("[错误] ");
    } else {
        Serial.print("[信息] ");
    }
    Serial.println(message);
}
