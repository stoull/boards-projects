/**
 * SHT3xSensor.h
 * Sensirion SHT3x-DIS / SHT30 / SHT31 / SHT35（I2C，16 位命令）
 */

#ifndef SHT3XSENSOR_H
#define SHT3XSENSOR_H

#include <Arduino.h>

class SHT3xSensor {
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
     * @param i2cAddress 7 位地址：ADDR 接 GND → 0x44，接 VDD → 0x45
     */
    SHT3xSensor(int8_t sdaPin = -1, int8_t sclPin = -1, int8_t ledPin = -1,
                uint8_t i2cAddress = 0x44);
    ~SHT3xSensor();

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

    static uint8_t crc8(const uint8_t* data, size_t len);
    static bool crc8Check2(const uint8_t* msbLsbCrc);
    bool writeCmd16(uint16_t cmd);
    bool sampleRaw(float& temperature, float& humidity);
    void recoverI2cBus();

    bool validateData(float temperature, float humidity);
    bool checkDataChange(float temperature, float humidity, float& smoothedTemp,
                         float& smoothedHumidity);
    void ledOn();
    void ledOff();
    void logMessage(const char* message, bool isError = false);
};

#endif
