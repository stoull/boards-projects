/**
 * MQTT客户端管理器
 * 提供MQTT连接、发布、订阅等功能
 * 适用于Arduino/ESP32平台
 */

#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// 回调函数类型定义
typedef void (*WatchdogFeedCallback)();
typedef void (*MQTTMessageCallback)(char* topic, byte* payload, unsigned int length);

class MQTTClientManager {
public:
    /**
     * 构造函数
     * @param clientId 客户端ID
     * @param server MQTT服务器地址
     * @param port MQTT服务器端口，默认1883
     */
    MQTTClientManager(const char* clientId, const char* server, uint16_t port = 1883);
    
    /**
     * 析构函数
     */
    ~MQTTClientManager();
    
    /**
     * 设置认证信息
     * @param user 用户名
     * @param password 密码
     */
    void setAuth(const char* user, const char* password);
    
    /**
     * 设置日志输出开关
     * @param enabled true启用日志，false禁用
     */
    void setLogEnabled(bool enabled);
    
    /**
     * 连接到MQTT服务器
     * @param retryCount 失败重试次数，默认3次
     * @param watchdogCallback 看门狗喂狗回调函数（可选）
     * @return 连接成功返回true，失败返回false
     */
    bool connect(uint8_t retryCount = 3, WatchdogFeedCallback watchdogCallback = nullptr);
    
    /**
     * 断开MQTT连接
     */
    void disconnect();
    
    /**
     * 发布消息到MQTT主题
     * @param topic 主题
     * @param message 消息内容
     * @param qos QoS等级，默认0
     * @param retain 是否保留消息，默认false
     * @return 发布成功返回true，失败返回false
     */
    bool publish(const char* topic, const char* message, uint8_t qos = 0, bool retain = false);
    
    /**
     * 发布JSON数据到MQTT主题
     * @param topic 主题
     * @param doc JsonDocument对象
     * @param qos QoS等级，默认0
     * @param retain 是否保留消息，默认false
     * @return 发布成功返回true，失败返回false
     */
    bool publishJson(const char* topic, JsonDocument& doc, uint8_t qos = 0, bool retain = false);
    
    /**
     * 订阅MQTT主题
     * @param topic 主题
     * @param callback 回调函数（可选）
     * @return 订阅成功返回true，失败返回false
     */
    bool subscribe(const char* topic, MQTTMessageCallback callback = nullptr);
    
    /**
     * 循环处理MQTT消息（需在loop()中调用）
     * @return 处理成功返回true
     */
    bool loop();
    
    /**
     * 检查是否已连接
     * @return 已连接返回true
     */
    bool isConnected();
    
    /**
     * 重新连接到MQTT服务器
     * @param retryCount 重试次数
     * @return 重连成功返回true
     */
    bool reconnect(uint8_t retryCount = 3);
    
    /**
     * 获取连接次数
     * @return 连接次数
     */
    uint32_t getConnectCount() const;
    
    /**
     * 获取发布次数
     * @return 发布次数
     */
    uint32_t getPublishCount() const;
    
    /**
     * 获取错误次数
     * @return 错误次数
     */
    uint32_t getErrorCount() const;
    
    /**
     * 重置统计信息
     */
    void resetStatistics();
    
    /**
     * 清理资源
     */
    void cleanup();

private:
    // MQTT连接参数
    char* _clientId;
    char* _server;
    uint16_t _port;
    char* _user;
    char* _password;
    
    // MQTT客户端对象
    WiFiClient _wifiClient;
    PubSubClient* _mqttClient;
    
    // 状态标志
    bool _isConnected;
    bool _logEnabled;
    
    // 统计信息
    uint32_t _connectCount;
    uint32_t _publishCount;
    uint32_t _errorCount;
    
    // 内部日志方法
    void _log(const char* message, bool isError = false);
    
    // 辅助方法：复制字符串
    char* _strdup(const char* str);
};

#endif // MQTT_CLIENT_MANAGER_H
