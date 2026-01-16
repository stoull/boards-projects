/**
 * ESP32-C3 网络工具测试程序
 * 验证修复后的SNTP功能是否正常工作
 */

#include <WiFi.h>
#include "NetworkUtils.h"
// WiFi配置
#include "Secret.h"

// LED配置
const int LED_PIN = 2;  // ESP32-C3内置LED引脚（GPIO2）
bool ledState = false;

void setupLED();
void updateLED();

// 全局对象
WiFiManager* wifiManager = nullptr;
NTPTimeSync* ntpSync = nullptr;

void setup() {
    Serial.begin(115200);
    delay(2000);

    // 初始化LED
    setupLED();
    
    Serial.println("=== ESP32-C3 网络工具测试 ===");
    
    // 测试WiFi连接
    Serial.println("\n1. 测试WiFi连接...");
    WiFiManager wifi(WIFI_SSID, WIFI_PASSWORD);
    wifiManager = &wifi;
    
    wifi.setLogCallback([](const String& msg) {
        Serial.println("[WiFi] " + msg);
    });
    
    if (wifi.connect(30, 3, 5000)) {
        Serial.println("✓ WiFi连接成功");
        
        // 显示网络信息
        NetworkInfo info = wifi.getNetworkInfo();
        Serial.println("  IP: " + info.ip);
        Serial.println("  MAC: " + info.mac);
        Serial.println("  信号强度: " + String(info.rssi) + " dBm");
        
        // 测试时间同步
        Serial.println("\n2. 测试NTP时间同步...");
        NTPTimeSync ntp(8);  // 北京时间 UTC+8
        
        ntp.setLogCallback([](const String& msg) {
            Serial.println("[NTP] " + msg);
        });
        
        // 显示同步前的状态
        Serial.println("同步前:");
        Serial.println("  时间是否已同步: " + String(NTPTimeSync::isTimeSynced() ? "是" : "否"));
        Serial.println("  系统时间: " + NTPTimeSync::getISO8601Time());
        
        // 尝试同步时间
        if (ntp.sync("ntp.aliyun.com", 3)) {
            Serial.println("✓ 时间同步成功");
            
            // 显示同步后的时间
            Serial.println("\n同步后:");
            Serial.println("  本地时间: " + NTPTimeSync::getISO8601Time());
            Serial.println("  带时区时间: " + NTPTimeSync::getISO8601TimeWithTimezone(8));
            Serial.println("  UTC时间: " + NTPTimeSync::getISO8601TimeUTC());
            Serial.println("  时间戳: " + String(NTPTimeSync::getTimestamp()));
            Serial.println("  运行时间: " + String(NTPTimeSync::getUptime() / 1000) + " 秒");
            Serial.println("  格式化时间: " + NTPTimeSync::formatTime("%Y年%m月%d日 %H:%M:%S"));
            
        } else {
            Serial.println("✗ 时间同步失败");
        }
        
        // 测试便捷函数
        Serial.println("\n3. 测试便捷函数...");
        
        bool quickSync = quickSyncTime(8, "pool.ntp.org");
        Serial.println("快速时间同步: " + String(quickSync ? "成功" : "失败"));
        
        Serial.println("网络连接状态: " + String(isNetworkConnected() ? "已连接" : "未连接"));
        Serial.println("网络状态: " + getNetworkStatus());
        
    } else {
        Serial.println("✗ WiFi连接失败");
    }
    
    Serial.println("\n=== 测试完成 ===");
}

void loop() {
    updateLED();  // 更新LED状态
    // 每10秒显示一次当前时间
    static unsigned long lastDisplay = 0;
    
    if (millis() - lastDisplay > 10000) {
        lastDisplay = millis();
        
        if (NTPTimeSync::isTimeSynced()) {
            //Serial.println("当前时间: " + NTPTimeSync::getISO8601Time());
        } else {
            Serial.println("时间未同步");
        }
    }
    
    delay(1000);
}

// ==================== LED控制函数 ====================

void setupLED() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // 初始状态：LED关闭
    ledState = false;
    Serial.println("[LED] LED初始化完成，引脚: " + String(LED_PIN));
}

void updateLED() {
    bool shouldLightUp = (wifiManager && wifiManager->isConnected());
    
    if (shouldLightUp != ledState) {
        ledState = shouldLightUp;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        
        if (ledState) {
            Serial.println("[LED] ✓ 网络已连接 - LED点亮");
        } else {
            Serial.println("[LED] ❌ 网络断开 - LED熄灭");
        }
    }
}