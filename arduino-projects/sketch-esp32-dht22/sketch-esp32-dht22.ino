/**
 * DHT22_Example.ino
 * DHT22 传感器使用示例
 * 
 * 硬件连接:
 * - DHT22 数据引脚 -> GPIO 4
 * - LED 指示灯 -> GPIO 2 (可选)
 * 
 * 依赖库:
 * - DHT sensor library by Adafruit
 * - Adafruit Unified Sensor
 * 
 * 安装方法:
 * Arduino IDE -> 工具 -> 管理库 -> 搜索 "DHT sensor library"
 */

#include "DHT22Sensor.h"

// 引脚定义
#define DHT_PIN 4       // DHT22 数据引脚
#define LED_PIN 8       // LED 指示灯引脚 (ESP32-C3 板载 LED)

// 创建 DHT22 传感器对象
DHT22Sensor* dhtSensor;

// 读取间隔 (毫秒)
const unsigned long READ_INTERVAL = 60000;  // 每 60 秒读取一次
unsigned long lastReadTime = 0;

void setup() {
    // 初始化串口
    Serial.begin(115200);
    while (!Serial) {
        delay(10);  // 等待串口准备好
    }
    
    Serial.println("\n========================================");
    Serial.println("  DHT22 温湿度传感器示例");
    Serial.println("========================================\n");
    
    // 创建 DHT22 传感器实例
    // 参数: (数据引脚, LED引脚)
    // 如果不需要 LED，可以传入 -1: new DHT22Sensor(DHT_PIN, -1)
    dhtSensor = new DHT22Sensor(DHT_PIN, LED_PIN);
    
    Serial.println("初始化完成，开始读取数据...\n");
    
    // 等待传感器稳定
    delay(2000);
}

void loop() {
    unsigned long currentTime = millis();
    
    // 按照设定的间隔读取数据
    if (currentTime - lastReadTime >= READ_INTERVAL) {
        lastReadTime = currentTime;
        
        Serial.println("----------------------------------------");
        Serial.println("开始读取 DHT22 传感器数据...");
        
        // 读取传感器数据
        // 参数: read(重试次数, 重试延迟ms, 看门狗回调函数)
        if (dhtSensor->read(3, 2000)) {
            // 读取成功，获取数据
            float temperature = dhtSensor->getTemperature();
            float humidity = dhtSensor->getHumidity();
            
            // 显示数据
            Serial.println("\n【当前读数】");
            Serial.print("  温度: ");
            Serial.print(temperature, 1);
            Serial.println(" °C");
            
            Serial.print("  湿度: ");
            Serial.print(humidity, 1);
            Serial.println(" %");
            
            // 也可以使用 getLastReading() 获取完整信息
            DHT22Sensor::Reading reading = dhtSensor->getLastReading();
            if (reading.valid) {
                Serial.print("  时间戳: ");
                Serial.print(reading.timestamp);
                Serial.println(" ms");
            }
            
            // 获取统计信息
            DHT22Sensor::Statistics stats = dhtSensor->getStatistics();
            Serial.println("\n【统计信息】");
            Serial.print("  总读取次数: ");
            Serial.println(stats.totalReads);
            
            Serial.print("  错误次数: ");
            Serial.println(stats.errors);
            
            Serial.print("  成功率: ");
            Serial.print(stats.successRate, 1);
            Serial.println(" %");
            
            Serial.print("  异常数据次数: ");
            Serial.println(stats.anomalyCount);
            
            Serial.print("  连续异常次数: ");
            Serial.println(stats.consecutiveAnomalyCount);
            
        } else {
            // 读取失败
            Serial.println("\n❌ 读取失败！");
            Serial.println("请检查:");
            Serial.println("  1. DHT22 传感器连接是否正确");
            Serial.println("  2. 引脚定义是否正确");
            Serial.println("  3. 传感器是否正常工作");
        }
        
        Serial.println("----------------------------------------\n");
    }
    
    // 其他任务...
    delay(100);
}

// 示例: 华氏温度读取
void readFahrenheitExample() {
    float fahrenheit, humidity;
    
    if (dhtSensor->readFahrenheit(fahrenheit, humidity, 3, 2000)) {
        Serial.print("温度 (华氏): ");
        Serial.print(fahrenheit, 1);
        Serial.println(" °F");
        
        Serial.print("湿度: ");
        Serial.print(humidity, 1);
        Serial.println(" %");
    }
}

// 示例: 重置统计信息
void resetStatsExample() {
    Serial.println("重置统计信息...");
    dhtSensor->resetStatistics();
}

// 示例: 看门狗回调函数
void watchdogFeed() {
    // 在这里喂狗
    // 例如: esp_task_wdt_reset();
    Serial.println("喂狗...");
}

// 示例: 使用看门狗的读取
void readWithWatchdogExample() {
    dhtSensor->read(3, 2000, watchdogFeed);
}
