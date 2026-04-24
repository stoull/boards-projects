/**
 * DHT22Sensor.h
 * DHT22 温湿度传感器类 - 头文件
 * 
 * 功能:
 * - 读取温湿度数据
 * - 数据验证和平滑处理
 * - 异常数据检测
 * - LED 状态指示
 * - 统计信息收集
 */

#ifndef DHT22SENSOR_H
#define DHT22SENSOR_H

#include <Arduino.h>
#include <DHT.h>

class DHT22Sensor {
public:
    // 数据结构
    struct Reading {
        float temperature;
        float humidity;
        unsigned long timestamp;
        bool valid;
    };
    
    struct Statistics {
        unsigned long totalReads;
        unsigned long errors;
        float successRate;
        unsigned long anomalyCount;
        unsigned long consecutiveAnomalyCount;
    };
    
    // 构造函数和析构函数
    DHT22Sensor(uint8_t dataPin, int8_t ledPin = -1);
    ~DHT22Sensor();
    
    // 主要功能函数
    bool read(uint8_t retryCount = 3, uint16_t retryDelay = 2000, 
              void (*watchdogCallback)() = nullptr);
    bool readFahrenheit(float& fahrenheit, float& humidity, 
                       uint8_t retryCount = 3, uint16_t retryDelay = 2000);
    
    // 获取数据
    Reading getLastReading();
    float getTemperature() const { return lastTemperature; }
    float getHumidity() const { return lastHumidity; }
    
    // 统计信息
    Statistics getStatistics() const;
    void resetStatistics();
    
    // 清理
    void cleanup();
    
    // 配置常量
    static const float MAX_CHANGE_THRESHOLD;      // 最大变化阈值
    static const uint8_t MAX_ANOMALY_COUNT;       // 最大异常次数
    
private:
    // 硬件对象
    DHT* sensor;
    int8_t ledPin;
    
    // 统计变量
    unsigned long readCount;
    unsigned long errorCount;
    float lastTemperature;
    float lastHumidity;
    unsigned long lastReadTime;
    
    // 数据平滑处理变量
    float lastValidTemperature;
    float lastValidHumidity;
    uint8_t consecutiveAnomalyCount;
    unsigned long anomalyCount;
    
    // 内部辅助函数
    bool validateData(float temperature, float humidity);
    bool checkDataChange(float temperature, float humidity, 
                        float& smoothedTemp, float& smoothedHumidity);
    void ledOn();
    void ledOff();
    void logMessage(const char* message, bool isError = false);
};

#endif // DHT22SENSOR_H
