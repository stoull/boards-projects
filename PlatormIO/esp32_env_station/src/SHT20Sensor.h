/**
 * SHT20Sensor.h
 * SHT20 温湿度传感器类 - 头文件（I2C）
 *
 * 功能:
 * - 读取温湿度数据
 * - 数据验证和平滑处理
 * - 异常数据检测
 * - LED 状态指示
 * - 统计信息收集
 */

#ifndef SHT20SENSOR_H
#define SHT20SENSOR_H

#include <Arduino.h>

class SHT20Sensor {
public:
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

    /**
     * @param sdaPin I2C SDA，-1 表示使用板载默认 SDA（仅 Wire.begin()）
     * @param sclPin I2C SCL，-1 表示使用板载默认 SCL
     * @param ledPin LED 指示灯，-1 表示不使用
     * @param i2cAddress SHT20 7 位地址，一般为 0x40
     */
    SHT20Sensor(int8_t sdaPin = -1, int8_t sclPin = -1, int8_t ledPin = -1,
                uint8_t i2cAddress = 0x40);
    ~SHT20Sensor();

    bool read(uint8_t retryCount = 3, uint16_t retryDelay = 2000,
              void (*watchdogCallback)() = nullptr);
    bool readFahrenheit(float& fahrenheit, float& humidity,
                        uint8_t retryCount = 3, uint16_t retryDelay = 2000);

    Reading getLastReading();
    float getTemperature() const { return lastTemperature; }
    float getHumidity() const { return lastHumidity; }

    Statistics getStatistics() const;
    void resetStatistics();

    void cleanup();

    static const float MAX_CHANGE_THRESHOLD;
    static const uint8_t MAX_ANOMALY_COUNT;

private:
    uint8_t i2cAddr;
    int8_t sdaPin;
    int8_t sclPin;
    int8_t ledPin;

    unsigned long readCount;
    unsigned long errorCount;
    float lastTemperature;
    float lastHumidity;
    unsigned long lastReadTime;

    float lastValidTemperature;
    float lastValidHumidity;
    uint8_t consecutiveAnomalyCount;
    unsigned long anomalyCount;

    static bool crc8Verify(uint8_t msb, uint8_t lsb, uint8_t crc);
    bool writeCommand(uint8_t cmd);
    /** 发送命令、可选延时后读回 2B+CRC；waitMs=0 用于 Hold Master（依赖时钟延展） */
    bool tryReadRaw(uint8_t triggerCmd, uint16_t waitMs, uint16_t& rawOut);
    bool readTemperatureRaw(uint16_t& rawOut);
    bool readHumidityRaw(uint16_t& rawOut);
    bool sampleRaw(float& temperature, float& humidity);
    /** ESP32 上 Hold/异常后恢复 SCL/SDA 并重新 Wire.begin + 软复位 */
    void recoverI2cBus();

    bool validateData(float temperature, float humidity);
    bool checkDataChange(float temperature, float humidity, float& smoothedTemp,
                         float& smoothedHumidity);
    void ledOn();
    void ledOff();
    void logMessage(const char* message, bool isError = false);
};

#endif // SHT20SENSOR_H
