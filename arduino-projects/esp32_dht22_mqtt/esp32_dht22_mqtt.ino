#include <WiFi.h>
#include <ArduinoJson.h>
#include "NetworkUtils.h"
#include "DeviceInfoHelper.h"
#include "DHT22Sensor.h"
#include "MQTTClientManager.h"
#include "Secret.h"

// LED配置
// 引脚定义
const int DHT_PIN = 4;    // DHT22 数据引脚
const int LED_PIN = 8;    // ESP32-C3内置LED引脚（GPIO2）
bool ledState = false;

void setupLED();
void updateLED();
void setupDHT22();
void setupWifiManager();
bool setupMqttManager();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// 全局对象
WiFiManager* wifiManager = nullptr;
NTPTimeSync* ntpSync = nullptr;

// 创建MQTT客户端管理器
MQTTClientManager* mqttManager = nullptr;

// 创建 DHT22 传感器对象
DHT22Sensor* dhtSensor;

// 读取间隔 (毫秒)
const unsigned long READ_INTERVAL = 300000;  // 每 300 秒读取一次
unsigned long lastReadTime = 0;

void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("========================================");
  Serial.println("   ESP32-C3 DHT22 MQTT System Starting  ");
  Serial.println("========================================");
  
  // 仅初始化必要的模块
  setupLED();
  setupDHT22();
  
  Serial.println("========================================");
  Serial.println("   System Initialized Successfully!     ");
  Serial.println("   工作模式: 定时连接模式");
  Serial.println("   采集间隔: " + String(READ_INTERVAL/1000) + " 秒");
  Serial.println("========================================\n");
  
  Serial.println("提示: WiFi 和 MQTT 将在每次数据采集时");
  Serial.println("      按需连接和断开，以节省电量。\n");
}

void loop() {
    unsigned long currentTime = millis();
    
    // 按照设定的间隔读取数据
    if (lastReadTime == 0 || currentTime - lastReadTime >= READ_INTERVAL) {
        lastReadTime = currentTime;

        Serial.println("========================================");
        Serial.println("   开始新的数据采集周期");
        Serial.println("========================================");
        
        // ========== 1. 断开现有连接，释放资源 ==========
        Serial.println("\n【1/5】清理现有连接...");
        if (mqttManager) {
            Serial.println("  断开 MQTT 连接...");
            mqttManager->disconnect();
            delete mqttManager;
            mqttManager = nullptr;
            Serial.println("  ✓ MQTT 资源已释放");
        }
        
        if (wifiManager) {
            Serial.println("  断开 WiFi 连接...");
            wifiManager->disconnect();
            delete wifiManager;
            wifiManager = nullptr;
            Serial.println("  ✓ WiFi 资源已释放");
        }
        
        if (ntpSync) {
            delete ntpSync;
            ntpSync = nullptr;
            Serial.println("  ✓ NTP 资源已释放");
        }
        
        WiFi.disconnect(true);  // 完全断开WiFi
        delay(500);
        Serial.println("  ✓ 清理完成\n");
        
        // ========== 2. 重新连接 WiFi ==========
        Serial.println("【2/5】重新连接 WiFi...");
        wifiManager = new WiFiManager(WIFI_SSID, WIFI_PASSWORD);
        wifiManager->setLogCallback([](const String& msg) {
            Serial.println("[WiFi] " + msg);
        });
        
        if (!wifiManager->connect(30, 3, 5000)) {
            Serial.println("✗ WiFi 连接失败，跳过本次数据采集");
            ledState = false;
            delete wifiManager;
            wifiManager = nullptr;
            return;
        }
        
        Serial.println("✓ WiFi 连接成功");
        ledState = true;
        NetworkInfo info = wifiManager->getNetworkInfo();
        Serial.println("  IP: " + info.ip + ", 信号: " + String(info.rssi) + " dBm\n");
        
        // ========== 3. 同步时间 ==========
        Serial.println("【3/5】同步网络时间...");
        ntpSync = new NTPTimeSync(8);
        ntpSync->setLogCallback([](const String& msg) {
            Serial.println("[NTP] " + msg);
        });
        
        if (ntpSync->sync("ntp.aliyun.com", 3)) {
            Serial.println("✓ 时间同步成功: " + NTPTimeSync::getISO8601TimeWithTimezone(8) + "\n");
        } else {
            Serial.println("✗ 时间同步失败（使用系统时间）\n");
        }
        
        // ========== 4. 重新连接 MQTT ==========
        Serial.println("【4/5】重新连接 MQTT...");
        mqttManager = new MQTTClientManager(MQTT_CLIENT_ID, MQTT_SERVER, MQTT_PORT);
        mqttManager->setAuth(MQTT_USER, MQTT_PASSWORD);
        
        if (!mqttManager->connect(3)) {
            Serial.println("✗ MQTT 连接失败，跳过本次数据采集");
            ledState = false;
            
            // 清理资源
            delete mqttManager;
            mqttManager = nullptr;
            delete ntpSync;
            ntpSync = nullptr;
            wifiManager->disconnect();
            delete wifiManager;
            wifiManager = nullptr;
            WiFi.disconnect(true);
            return;
        }
        
        Serial.println("✓ MQTT 连接成功\n");
        
        // ========== 5. 读取传感器并发布数据 ==========
        Serial.println("【5/5】读取传感器数据并发布...");
        Serial.println("----------------------------------------");
        
        // 读取传感器数据
        if (dhtSensor->read(3, 2000)) {
            // 读取成功，获取数据
            float temperature = dhtSensor->getTemperature();
            float humidity = dhtSensor->getHumidity();

            temperature = ((int)(temperature * 100 + 0.5f)) / 100.0f;
            humidity = ((int)(humidity * 100 + 0.5f)) / 100.0f;

            // 发布DHT22 JSON消息
            StaticJsonDocument<256> dth22_doc;
            dth22_doc["temperature"] = temperature;
            dth22_doc["humidity"] = humidity;
            String iso8601 = NTPTimeSync::getISO8601TimeWithTimezone(8);
            dth22_doc["created_at"] = iso8601;
            
            if (mqttManager->publishJson(MQTT_TOPIC, dth22_doc)) {
                Serial.println("✓ DHT22 数据已发布");
            } else {
                Serial.println("✗ DHT22 数据发布失败");
            }

            // 发布Device info JSON消息
            StaticJsonDocument<1024> all_dev_info = all_device_info();
            all_dev_info["created_at"] = iso8601;
            
            if (mqttManager->publishJson(MQTT_TOPIC_DeviceInfo, all_dev_info)) {
                Serial.println("✓ 设备信息已发布");
            } else {
                Serial.println("✗ 设备信息发布失败");
            }
            
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
        
        Serial.println("----------------------------------------");
        
        // ========== 6. 断开连接，释放资源 ==========
        Serial.println("\n【清理阶段】断开连接并释放资源...");
        
        if (mqttManager) {
            mqttManager->disconnect();
            delay(100);
            delete mqttManager;
            mqttManager = nullptr;
            Serial.println("  ✓ MQTT 已断开并释放");
        }
        
        if (ntpSync) {
            delete ntpSync;
            ntpSync = nullptr;
            Serial.println("  ✓ NTP 资源已释放");
        }
        
        if (wifiManager) {
            wifiManager->disconnect();
            delay(100);
            delete wifiManager;
            wifiManager = nullptr;
            Serial.println("  ✓ WiFi 已断开并释放");
        }
        
        WiFi.disconnect(true);
        delay(200);
        ledState = false;
        
        Serial.println("\n========================================");
        Serial.println("   数据采集周期结束，进入休眠模式");
        Serial.println("   下次采集时间: " + String(READ_INTERVAL/1000) + " 秒后");
        Serial.println("========================================\n");
    }
}

void setupDHT22() {
  // 创建 DHT22 传感器实例
  // 参数: (数据引脚, LED引脚)
  // 如果不需要 LED，可以传入 -1: new DHT22Sensor(DHT_PIN, -1)
  dhtSensor = new DHT22Sensor(DHT_PIN, LED_PIN);
}

void setupWifiManager() {
    // 创建WiFiManager实例（动态分配）
    wifiManager = new WiFiManager(WIFI_SSID, WIFI_PASSWORD);
    
    wifiManager->setLogCallback([](const String& msg) {
        Serial.println("[WiFi] " + msg);
    });
    
    if (wifiManager->connect(30, 3, 5000)) {
        Serial.println("✓ WiFi连接成功");
        
        // 显示网络信息
        NetworkInfo info = wifiManager->getNetworkInfo();
        Serial.println("  IP: " + info.ip);
        Serial.println("  MAC: " + info.mac);
        Serial.println("  信号强度: " + String(info.rssi) + " dBm");
        
        // 时间同步
        Serial.println("\n2. 测试NTP时间同步...");
        ntpSync = new NTPTimeSync(8);  // 北京时间 UTC+8
        
        ntpSync->setLogCallback([](const String& msg) {
            Serial.println("[NTP] " + msg);
        });
        
        // 显示同步前的状态
        Serial.println("同步前:");
        Serial.println("  时间是否已同步: " + String(NTPTimeSync::isTimeSynced() ? "是" : "否"));
        Serial.println("  系统时间: " + NTPTimeSync::getISO8601Time());
        
        // 尝试同步时间
        if (ntpSync->sync("ntp.aliyun.com", 3)) {
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
}

bool setupMqttManager() {
    // 创建MQTT客户端管理器
    mqttManager = new MQTTClientManager(MQTT_CLIENT_ID, MQTT_SERVER, MQTT_PORT);
    mqttManager -> setAuth(MQTT_USER, MQTT_PASSWORD);
    
    // 如果需要认证，设置用户名和密码
    // mqttManager->setAuth(MQTT_USER, MQTT_PASSWORD);
    
    // 连接到MQTT服务器
    if (mqttManager->connect(3)) {
        Serial.println("MQTT 连接成功！");
        
        // 订阅主题
        // mqttManager->subscribe(TOPIC_SUBSCRIBE, mqttCallback);
        
        // // 发布测试消息
        // mqttManager->publish(TOPIC_PUBLISH, "Hello from ESP32!");
        
        // // 发布JSON消息
        // StaticJsonDocument<256> doc;
        // doc["device"] = "ESP32";
        // doc["status"] = "online";
        // doc["ip"] = WiFi.localIP().toString();
        // mqttManager->publishJson(TOPIC_PUBLISH, doc);
        return true;
    } else {
        Serial.println("MQTT 连接失败！");
        return false;
    }
}

// MQTT消息回调函数
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("收到消息 [");
    Serial.print(topic);
    Serial.print("]: ");
    
    // 打印消息内容
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();
    
    // 解析JSON消息（如果是JSON格式）
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (!error) {
        // 成功解析JSON
        if (doc.containsKey("command")) {
            const char* command = doc["command"];
            Serial.print("执行命令: ");
            Serial.println(command);
            
            // 在这里处理命令...
        }
    }
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
