/**
 * MQTT客户端管理器实现
 */

#include "MQTTClientManager.h"

// 构造函数
MQTTClientManager::MQTTClientManager(const char* clientId, const char* server, uint16_t port)
    : _port(port), _user(nullptr), _password(nullptr), _mqttClient(nullptr),
      _isConnected(false), _logEnabled(true),
      _connectCount(0), _publishCount(0), _errorCount(0) {
    
    _clientId = _strdup(clientId);
    _server = _strdup(server);
    _mqttClient = new PubSubClient(_wifiClient);
    _mqttClient->setBufferSize(1024);
    _mqttClient->setServer(_server, _port);
    _mqttClient->setSocketTimeout(15);  // 减少超时时间到15秒
    _mqttClient->setKeepAlive(60);      // 设置keepAlive为60秒
}

// 析构函数
MQTTClientManager::~MQTTClientManager() {
    cleanup();
}

// 设置认证信息
void MQTTClientManager::setAuth(const char* user, const char* password) {
    if (_user) {
        free(_user);
        _user = nullptr;
    }
    if (_password) {
        free(_password);
        _password = nullptr;
    }
    
    if (user) {
        _user = _strdup(user);
    }
    if (password) {
        _password = _strdup(password);
    }
}

// 设置日志输出开关
void MQTTClientManager::setLogEnabled(bool enabled) {
    _logEnabled = enabled;
}

// 连接到MQTT服务器
bool MQTTClientManager::connect(uint8_t retryCount, WatchdogFeedCallback watchdogCallback) {
    // 确保WiFiClient完全断开旧连接
    if (_wifiClient.connected()) {
        _wifiClient.stop();
        delay(100);
    }
    
    for (uint8_t attempt = 0; attempt < retryCount; attempt++) {
        // 喂狗（如果提供了回调函数）
        if (watchdogCallback) {
            watchdogCallback();
        }
        
        // 如果已经连接，先断开
        if (_mqttClient->connected()) {
            _mqttClient->disconnect();
            delay(100);
        }
        
        // 检查WiFi状态
        if (WiFi.status() != WL_CONNECTED) {
            _log("WiFi未连接，无法连接MQTT", true);
            return false;
        }
        
        // 尝试连接
        bool connected = false;
        if (_user && _password) {
            connected = _mqttClient->connect(_clientId, _user, _password);
        } else {
            connected = _mqttClient->connect(_clientId);
        }
        
        if (connected) {
            _isConnected = true;
            _connectCount++;
            
            char logMsg[128];
            snprintf(logMsg, sizeof(logMsg), "MQTT 连接成功: %s:%d", _server, _port);
            _log(logMsg);
            return true;
        } else {
            _errorCount++;
            int state = _mqttClient->state();
            
            char errorMsg[256];
            const char* stateStr = "未知错误";
            switch(state) {
                case -4: stateStr = "MQTT_CONNECTION_TIMEOUT"; break;
                case -3: stateStr = "MQTT_CONNECTION_LOST"; break;
                case -2: stateStr = "MQTT_CONNECT_FAILED (TCP连接失败)"; break;
                case -1: stateStr = "MQTT_DISCONNECTED"; break;
                case 1: stateStr = "MQTT_CONNECT_BAD_PROTOCOL"; break;
                case 2: stateStr = "MQTT_CONNECT_BAD_CLIENT_ID"; break;
                case 3: stateStr = "MQTT_CONNECT_UNAVAILABLE"; break;
                case 4: stateStr = "MQTT_CONNECT_BAD_CREDENTIALS"; break;
                case 5: stateStr = "MQTT_CONNECT_UNAUTHORIZED"; break;
            }
            
            snprintf(errorMsg, sizeof(errorMsg), 
                     "MQTT 连接失败 (尝试 %d/%d): 错误代码 %d (%s)", 
                     attempt + 1, retryCount, state, stateStr);
            
            if (attempt == retryCount - 1) {
                _log(errorMsg, true);
                _isConnected = false;
                return false;
            } else {
                _log(errorMsg);
            }
            
            delay(2000); // 重试前等待2秒
        }
    }
    
    return false;
}

// 断开MQTT连接
void MQTTClientManager::disconnect() {
    if (_mqttClient) {
        _mqttClient->disconnect();
        _log("MQTT 已断开");
    }
    _isConnected = false;
}

// 发布消息到MQTT主题
bool MQTTClientManager::publish(const char* topic, const char* message, uint8_t qos, bool retain) {
    if (!_isConnected || !_mqttClient || !_mqttClient->connected()) {
        _log("MQTT 未连接，无法发布消息", true);
        return false;
    }
    
    bool success = false;
    
    // PubSubClient不直接支持QoS参数，使用retain参数
    if (qos == 0) {
        success = _mqttClient->publish(topic, message, retain);
    } else {
        // 对于QoS > 0，尝试使用带长度的发布方法
        success = _mqttClient->publish(topic, (const uint8_t*)message, strlen(message), retain);
    }
    
    if (success) {
        _publishCount++;
        
        char logMsg[1024];
        snprintf(logMsg, sizeof(logMsg), "MQTT 消息已发布到 %s: %s", topic, message);
        _log(logMsg);
        return true;
    } else {
        _errorCount++;
        _log("MQTT 发布失败", true);
        return false;
    }
}

// 发布JSON数据到MQTT主题
bool MQTTClientManager::publishJson(const char* topic, JsonDocument& doc, uint8_t qos, bool retain) {
    char buffer[1024];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    
    if (len == 0) {
        _log("JSON 序列化失败", true);
        _errorCount++;
        return false;
    }
    
    return publish(topic, buffer, qos, retain);
}

// 订阅MQTT主题
bool MQTTClientManager::subscribe(const char* topic, MQTTMessageCallback callback) {
    if (!_isConnected || !_mqttClient || !_mqttClient->connected()) {
        _log("MQTT 未连接，无法订阅主题", true);
        return false;
    }
    
    if (callback) {
        _mqttClient->setCallback(callback);
    }
    
    bool success = _mqttClient->subscribe(topic);
    
    if (success) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "已订阅 MQTT 主题: %s", topic);
        _log(logMsg);
        return true;
    } else {
        _errorCount++;
        _log("MQTT 订阅失败", true);
        return false;
    }
}

// 循环处理MQTT消息
bool MQTTClientManager::loop() {
    if (_mqttClient) {
        return _mqttClient->loop();
    }
    return false;
}

// 检查是否已连接
bool MQTTClientManager::isConnected() {
    return _isConnected && _mqttClient && _mqttClient->connected();
}

// 重新连接到MQTT服务器
bool MQTTClientManager::reconnect(uint8_t retryCount) {
    _log("尝试重新连接 MQTT...");
    disconnect();
    return connect(retryCount);
}

// 获取连接次数
uint32_t MQTTClientManager::getConnectCount() const {
    return _connectCount;
}

// 获取发布次数
uint32_t MQTTClientManager::getPublishCount() const {
    return _publishCount;
}

// 获取错误次数
uint32_t MQTTClientManager::getErrorCount() const {
    return _errorCount;
}

// 重置统计信息
void MQTTClientManager::resetStatistics() {
    _connectCount = 0;
    _publishCount = 0;
    _errorCount = 0;
}

// 清理资源
void MQTTClientManager::cleanup() {
    if (_mqttClient) {
        if (_isConnected) {
            _mqttClient->disconnect();
        }
        delete _mqttClient;
        _mqttClient = nullptr;
    }
    
    // 确保WiFiClient完全断开
    if (_wifiClient.connected()) {
        _wifiClient.stop();
    }
    
    if (_clientId) {
        free(_clientId);
        _clientId = nullptr;
    }
    
    if (_server) {
        free(_server);
        _server = nullptr;
    }
    
    if (_user) {
        free(_user);
        _user = nullptr;
    }
    
    if (_password) {
        free(_password);
        _password = nullptr;
    }
    
    _isConnected = false;
    Serial.println("MQTT 客户端资源已清理");
}

// 内部日志方法
void MQTTClientManager::_log(const char* message, bool isError) {
    if (_logEnabled) {
        if (isError) {
            Serial.print("[ERROR] ");
        } else {
            Serial.print("[INFO] ");
        }
        Serial.println(message);
    }
}

// 辅助方法：复制字符串
char* MQTTClientManager::_strdup(const char* str) {
    if (!str) return nullptr;
    
    size_t len = strlen(str) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}
