/**
 * MQTTClientManager 使用示例
 * 演示如何在Arduino/ESP32中使用MQTT客户端管理器
 */

#include <WiFi.h>
#include "MQTTClientManager.h"
#include <ArduinoJson.h>

// WiFi配置
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASSWORD = "your_wifi_password";

// MQTT配置
const char* MQTT_SERVER = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP32_Client";
const char* MQTT_USER = "your_username";      // 如果需要认证
const char* MQTT_PASSWORD = "your_password";  // 如果需要认证

// MQTT主题
const char* TOPIC_PUBLISH = "sensor/data";
const char* TOPIC_SUBSCRIBE = "device/command";

// 创建MQTT客户端管理器
MQTTClientManager* mqttManager = nullptr;

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

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== MQTT 客户端示例 ===");
    
    // 连接WiFi
    Serial.print("连接WiFi: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nWiFi 已连接");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    
    // 创建MQTT客户端管理器
    mqttManager = new MQTTClientManager(MQTT_CLIENT_ID, MQTT_SERVER, MQTT_PORT);
    
    // 如果需要认证，设置用户名和密码
    // mqttManager->setAuth(MQTT_USER, MQTT_PASSWORD);
    
    // 连接到MQTT服务器
    if (mqttManager->connect(3)) {
        Serial.println("MQTT 连接成功！");
        
        // 订阅主题
        mqttManager->subscribe(TOPIC_SUBSCRIBE, mqttCallback);
        
        // 发布测试消息
        mqttManager->publish(TOPIC_PUBLISH, "Hello from ESP32!");
        
        // 发布JSON消息
        StaticJsonDocument<256> doc;
        doc["device"] = "ESP32";
        doc["status"] = "online";
        doc["ip"] = WiFi.localIP().toString();
        mqttManager->publishJson(TOPIC_PUBLISH, doc);
        
    } else {
        Serial.println("MQTT 连接失败！");
    }
}

void loop() {
    // 检查WiFi连接
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi 断开，尝试重连...");
        WiFi.reconnect();
        delay(5000);
        return;
    }
    
    // 检查MQTT连接
    if (!mqttManager->isConnected()) {
        Serial.println("MQTT 断开，尝试重连...");
        mqttManager->reconnect(3);
        delay(5000);
        return;
    }
    
    // 处理MQTT消息（必须调用）
    mqttManager->loop();
    
    // 定期发布传感器数据（每10秒）
    static unsigned long lastPublish = 0;
    if (millis() - lastPublish > 10000) {
        lastPublish = millis();
        
        // 发布JSON格式的传感器数据
        StaticJsonDocument<256> sensorDoc;
        sensorDoc["temperature"] = 25.5;  // 示例温度
        sensorDoc["humidity"] = 60.0;     // 示例湿度
        sensorDoc["timestamp"] = millis();
        
        if (mqttManager->publishJson(TOPIC_PUBLISH, sensorDoc)) {
            Serial.println("传感器数据已发布");
            
            // 打印统计信息
            Serial.printf("统计: 连接=%lu, 发布=%lu, 错误=%lu\n",
                         mqttManager->getConnectCount(),
                         mqttManager->getPublishCount(),
                         mqttManager->getErrorCount());
        }
    }
    
    delay(10);
}
