#include <WiFi.h>
#include <ArduinoJson.h>
#include "NetworkUtils.h"
#include "DeviceInfoHelper.h"
#include "DHT22Sensor.h"
#include "SHT20Sensor.h"
#include "SHT3xSensor.h"
#include "MQTTClientManager.h"
#include "Secret.h"
#include "Utils.h"

/** 温湿度传感器类型：DHT22 / SHT20(0x40) / SHT3x-DIS(0x44 或 0x45) */
enum class HumiditySensorKind : uint8_t {
    DHT22 = 0,
    SHT20 = 1,
    SHT3x = 2
};

/** 启用的温湿度通道：可配置多项，每项单独读取并上传一条 state。
 *  同一 HumiditySensorKind 在表中最多出现一次（共用同一驱动实例）。 */
struct HumiditySensorSlot {
    HumiditySensorKind kind;
    int sensor_id;  // 上报 JSON 中的 sensor_id，多路时请使用不同值
};

// 按需取消注释以启用多路；I2C 上 SHT20(0x40) 与 SHT3x(0x44/0x45) 可同时存在
static constexpr HumiditySensorSlot kHumiditySensors[] = {
    {HumiditySensorKind::DHT22, 3},
    {HumiditySensorKind::SHT3x, 8},
};
static constexpr size_t kHumiditySensorCount = sizeof(kHumiditySensors) / sizeof(kHumiditySensors[0]);
static_assert(kHumiditySensorCount > 0, "kHumiditySensors must contain at least one entry");

// 引脚定义
const int DHT_PIN = 4;    // DHT22 单总线数据引脚
const int SDA_PIN = 5;    // I2C SDA（SHT20 / SHT3x）
const int SCL_PIN = 6;    // I2C SCL
/** SHT3x：ADDR 接 GND → 0x44，接 VDD → 0x45 */
const uint8_t SHT3X_I2C_ADDR = 0x44;
const int LED_PIN = 8;    // 状态 LED（与板载灯同脚时请避免与 SDA 复用）
bool ledState = false;

void setupLED();
void updateLED();
void setupHumiditySensor();
String buildMqttClientId();
// void setupWifiManager();
// bool setupMqttManager();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// 全局对象
WiFiManager* wifiManager = nullptr;
NTPTimeSync* ntpSync = nullptr;

// 创建MQTT客户端管理器
MQTTClientManager* mqttManager = nullptr;

DHT22Sensor* gDht22Sensor = nullptr;
SHT20Sensor* gSht20Sensor = nullptr;
SHT3xSensor* gSht3xSensor = nullptr;
String gUniqueId = "";
String gMqttClientId = "";

// 读取间隔 (毫秒)
const unsigned long READ_INTERVAL = 300000;  // 每 300 秒读取一次
unsigned long lastReadTime = 0;

static const char* sensorKindDisplayName(HumiditySensorKind k) {
    switch (k) {
        case HumiditySensorKind::DHT22:
            return "DHT22";
        case HumiditySensorKind::SHT20:
            return "SHT20";
        case HumiditySensorKind::SHT3x:
            return "SHT3x-DIS";
    }
    return "?";
}

static const char* sensorKindMqttName(HumiditySensorKind k) {
    switch (k) {
        case HumiditySensorKind::DHT22:
            return "dht22";
        case HumiditySensorKind::SHT20:
            return "sht20";
        case HumiditySensorKind::SHT3x:
            return "sht30";
    }
    return "unknown";
}

static bool humidityConfigUsesKind(HumiditySensorKind k) {
    for (size_t i = 0; i < kHumiditySensorCount; ++i) {
        if (kHumiditySensors[i].kind == k) {
            return true;
        }
    }
    return false;
}

static bool readHumiditySensor(HumiditySensorKind kind, float& temperature, float& humidity) {
    switch (kind) {
        case HumiditySensorKind::DHT22:
            if (!gDht22Sensor) {
                return false;
            }
            if (!gDht22Sensor->read(3, 2000)) {
                return false;
            }
            temperature = gDht22Sensor->getTemperature();
            humidity = gDht22Sensor->getHumidity();
            return true;
        case HumiditySensorKind::SHT20:
            if (!gSht20Sensor) {
                return false;
            }
            if (!gSht20Sensor->read(3, 2000)) {
                return false;
            }
            temperature = gSht20Sensor->getTemperature();
            humidity = gSht20Sensor->getHumidity();
            return true;
        case HumiditySensorKind::SHT3x:
            if (!gSht3xSensor) {
                return false;
            }
            if (!gSht3xSensor->read(3, 2000)) {
                return false;
            }
            temperature = gSht3xSensor->getTemperature();
            humidity = gSht3xSensor->getHumidity();
            return true;
    }
    return false;
}

static void printHumiditySensorStats(HumiditySensorKind kind) {
    switch (kind) {
        case HumiditySensorKind::DHT22:
            if (!gDht22Sensor) {
                return;
            }
            {
                DHT22Sensor::Reading reading = gDht22Sensor->getLastReading();
                if (reading.valid) {
                    Serial.print("  时间戳: ");
                    Serial.print(reading.timestamp);
                    Serial.println(" ms");
                }
                DHT22Sensor::Statistics stats = gDht22Sensor->getStatistics();
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
            }
            break;
        case HumiditySensorKind::SHT20:
            if (!gSht20Sensor) {
                return;
            }
            {
                SHT20Sensor::Reading reading = gSht20Sensor->getLastReading();
                if (reading.valid) {
                    Serial.print("  时间戳: ");
                    Serial.print(reading.timestamp);
                    Serial.println(" ms");
                }
                SHT20Sensor::Statistics stats = gSht20Sensor->getStatistics();
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
            }
            break;
        case HumiditySensorKind::SHT3x:
            if (!gSht3xSensor) {
                return;
            }
            {
                SHT3xSensor::Reading reading = gSht3xSensor->getLastReading();
                if (reading.valid) {
                    Serial.print("  时间戳: ");
                    Serial.print(reading.timestamp);
                    Serial.println(" ms");
                }
                SHT3xSensor::Statistics stats = gSht3xSensor->getStatistics();
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
            }
            break;
    }
}

static void printHumiditySensorFailureHints(HumiditySensorKind kind) {
    switch (kind) {
        case HumiditySensorKind::DHT22:
            Serial.println("  1. DHT22 接线与供电（数据脚 GPIO " + String(DHT_PIN) + "）");
            Serial.println("  2. 上拉电阻与线长");
            break;
        case HumiditySensorKind::SHT20:
            Serial.println("  1. SHT20 I2C：SDA=GPIO " + String(SDA_PIN) +
                           ", SCL=GPIO " + String(SCL_PIN) + "，共地，3.3V 供电");
            Serial.println("  2. 地址是否为 0x40、接线是否松动");
            break;
        case HumiditySensorKind::SHT3x:
            Serial.println("  1. SHT3x I2C：SDA=GPIO " + String(SDA_PIN) +
                           ", SCL=GPIO " + String(SCL_PIN) + "，共地，3.3V 供电");
            Serial.println("  2. 7位地址应为 0x44 或 0x45（与 ADDR 焊盘/跳线一致），当前配置 0x" +
                           String(SHT3X_I2C_ADDR, HEX));
            break;
    }
}

void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(100);
  gUniqueId = get_unique_id();
  unsigned long seed = 0;
  for (size_t i = 0; i < gUniqueId.length(); ++i) {
      seed = seed * 31 + (unsigned long)gUniqueId[i];
  }
  randomSeed(seed ^ micros());
  Serial.println();
  Serial.println("========================================");
  Serial.println("   ESP32-C3 温湿度 MQTT System Starting  ");
  Serial.println("========================================");
  Serial.print("   当前传感器: ");
  for (size_t i = 0; i < kHumiditySensorCount; ++i) {
      if (i > 0) {
          Serial.print(", ");
      }
      Serial.print(sensorKindDisplayName(kHumiditySensors[i].kind));
      Serial.print("(id=");
      Serial.print(kHumiditySensors[i].sensor_id);
      Serial.print(")");
  }
  Serial.println();
  
  // 仅初始化必要的模块
  setupLED();
  setupHumiditySensor();
  
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
        gMqttClientId = buildMqttClientId();
        Serial.println("MQTT Client ID: " + gMqttClientId);
        mqttManager = new MQTTClientManager(gMqttClientId.c_str(), MQTT_SERVER, MQTT_PORT);
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
        
        bool anyReadOk = false;
        bool slotReadOk[kHumiditySensorCount];
        float slotTemperature[kHumiditySensorCount];
        float slotHumidity[kHumiditySensorCount];
        String iso8601 = NTPTimeSync::getISO8601TimeWithTimezone(8);
        String suffix_6 = get_str_last_n(gUniqueId, 6);
        String s_topic = String(kDevice_Location) + String(kDevice_Type) + "_" + suffix_6 + "/state";

        for (size_t si = 0; si < kHumiditySensorCount; ++si) {
            HumiditySensorKind kind = kHumiditySensors[si].kind;
            int sensorId = kHumiditySensors[si].sensor_id;
            float temperature = NAN;
            float humidity = NAN;
            bool readOk = readHumiditySensor(kind, temperature, humidity);
            slotReadOk[si] = readOk;
            slotTemperature[si] = temperature;
            slotHumidity[si] = humidity;

            if (readOk) {
                anyReadOk = true;
                temperature = ((int)(temperature * 100 + 0.5f)) / 100.0f;
                humidity = ((int)(humidity * 100 + 0.5f)) / 100.0f;

                const char* sensorName = sensorKindMqttName(kind);
                StaticJsonDocument<256> state_doc;
                state_doc["sensor_type"] = sensorName;
                state_doc["sensor_id"] = sensorId;
                state_doc["temperature"] = temperature;
                state_doc["humidity"] = humidity;
                state_doc["created_at"] = iso8601;

                if (mqttManager->publishJson(s_topic.c_str(), state_doc)) {
                    Serial.print("✓ ");
                    Serial.print(sensorName);
                    Serial.print(" (id=");
                    Serial.print(sensorId);
                    Serial.println(") 数据已发布");
                } else {
                    Serial.print("✗ ");
                    Serial.print(sensorName);
                    Serial.print(" (id=");
                    Serial.print(sensorId);
                    Serial.println(") 数据发布失败");
                }
            } else {
                Serial.print("✗ ");
                Serial.print(sensorKindDisplayName(kind));
                Serial.print(" (id=");
                Serial.print(sensorId);
                Serial.println(") 读取失败");
            }
        }

        if (anyReadOk) {
            StaticJsonDocument<1024> all_dev_info = all_device_info();
            all_dev_info["created_at"] = iso8601;
            String m_topic = String(kDevice_Location) + String(kDevice_Type) + "_" + suffix_6 + "/metrics";

            if (mqttManager->publishJson(m_topic.c_str(), all_dev_info)) {
                Serial.println("✓ 设备信息已发布");
            } else {
                Serial.println("✗ 设备信息发布失败");
            }

            Serial.println("\n【当前读数】");
            for (size_t si = 0; si < kHumiditySensorCount; ++si) {
                if (!slotReadOk[si]) {
                    continue;
                }
                float temperature = ((int)(slotTemperature[si] * 100 + 0.5f)) / 100.0f;
                float humidity = ((int)(slotHumidity[si] * 100 + 0.5f)) / 100.0f;
                Serial.print("  [");
                Serial.print(sensorKindDisplayName(kHumiditySensors[si].kind));
                Serial.print(" id=");
                Serial.print(kHumiditySensors[si].sensor_id);
                Serial.print("] 温度: ");
                Serial.print(temperature, 1);
                Serial.print(" °C, 湿度: ");
                Serial.print(humidity, 1);
                Serial.println(" %");
            }

            Serial.println("\n【统计信息（各已启用传感器）】");
            for (size_t si = 0; si < kHumiditySensorCount; ++si) {
                HumiditySensorKind kind = kHumiditySensors[si].kind;
                Serial.print("--- ");
                Serial.print(sensorKindDisplayName(kind));
                Serial.println(" ---");
                printHumiditySensorStats(kind);
            }
        } else {
            Serial.println("\n❌ 全部传感器读取失败！");
            Serial.println("请检查:");
            for (size_t si = 0; si < kHumiditySensorCount; ++si) {
                Serial.print("[");
                Serial.print(sensorKindDisplayName(kHumiditySensors[si].kind));
                Serial.println("]");
                printHumiditySensorFailureHints(kHumiditySensors[si].kind);
            }
            Serial.println("  3. 传感器本体是否损坏");
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

void setupHumiditySensor() {
  if (humidityConfigUsesKind(HumiditySensorKind::DHT22)) {
    gDht22Sensor = new DHT22Sensor((uint8_t)DHT_PIN, (int8_t)LED_PIN);
    Serial.println("[传感器] 已启用 DHT22，数据脚: GPIO " + String(DHT_PIN));
  }
  if (humidityConfigUsesKind(HumiditySensorKind::SHT20)) {
    gSht20Sensor =
        new SHT20Sensor((int8_t)SDA_PIN, (int8_t)SCL_PIN, (int8_t)LED_PIN);
    Serial.println("[传感器] 已启用 SHT20，SDA=GPIO " + String(SDA_PIN) +
                   ", SCL=GPIO " + String(SCL_PIN));
  }
  if (humidityConfigUsesKind(HumiditySensorKind::SHT3x)) {
    gSht3xSensor = new SHT3xSensor((int8_t)SDA_PIN, (int8_t)SCL_PIN, (int8_t)LED_PIN,
                                   SHT3X_I2C_ADDR);
    Serial.println("[传感器] 已启用 SHT3x-DIS，SDA=GPIO " + String(SDA_PIN) +
                   ", SCL=GPIO " + String(SCL_PIN) + ", I2C 0x" +
                   String(SHT3X_I2C_ADDR, HEX));
  }
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
    gMqttClientId = buildMqttClientId();
    Serial.println("MQTT Client ID: " + gMqttClientId);
    mqttManager = new MQTTClientManager(gMqttClientId.c_str(), MQTT_SERVER, MQTT_PORT);
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

String buildMqttClientId() {
    const char* charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t charsetLen = 36;
    String result = String(MQTT_CLIENT_ID_PRE);
    result.reserve(result.length() + 12);

    for (size_t i = 0; i < 12; ++i) {
        result += charset[random(charsetLen)];
    }

    return result;
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
