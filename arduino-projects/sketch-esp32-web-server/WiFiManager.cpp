/**
 * WiFiManager.cpp
 * WiFi 连接管理类 - 实现文件
 */

#include "WiFiManager.h"

/**
 * 构造函数
 */
WiFiManager::WiFiManager() 
    : currentSSID(nullptr), currentPassword(nullptr) {
}

/**
 * 连接到 WiFi 网络
 * @param ssid WiFi 名称
 * @param password WiFi 密码
 * @param timeout 连接超时时间 (毫秒)
 * @param retryDelay 重试延迟 (毫秒)
 * @return 成功返回 true
 */
bool WiFiManager::connect(const char* ssid, const char* password, 
                          uint32_t timeout, uint16_t retryDelay) {
    // 保存凭据供后续重连使用
    currentSSID = ssid;
    currentPassword = password;
    
    // 断开现有连接
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        delay(100);
    }
    
    // 开始连接
    Serial.println("\n========================================");
    Serial.print("正在连接到 WiFi: ");
    Serial.println(ssid);
    Serial.println("========================================");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    int dotCount = 0;
    
    // 等待连接
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > timeout) {
            Serial.println("\n❌ WiFi 连接超时！");
            return false;
        }
        
        delay(retryDelay);
        Serial.print(".");
        dotCount++;
        
        if (dotCount >= 30) {
            Serial.println();
            dotCount = 0;
        }
    }
    
    Serial.println("\n✓ WiFi 连接成功！");
    printNetworkInfo();
    
    return true;
}

/**
 * 重新连接 WiFi (使用上次的凭据)
 * @param timeout 超时时间
 * @return 成功返回 true
 */
bool WiFiManager::reconnect(uint32_t timeout) {
    if (currentSSID == nullptr || currentPassword == nullptr) {
        logMessage("无法重连: 未保存 WiFi 凭据", true);
        return false;
    }
    
    logMessage("尝试重新连接 WiFi...", false);
    return connect(currentSSID, currentPassword, timeout);
}

/**
 * 断开 WiFi 连接
 */
void WiFiManager::disconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        logMessage("WiFi 已断开", false);
    }
}

/**
 * 检查是否已连接
 */
bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

/**
 * 获取本地 IP 地址
 */
String WiFiManager::getLocalIP() const {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "未连接";
}

/**
 * 获取信号强度 (RSSI)
 */
int WiFiManager::getRSSI() const {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}

/**
 * 获取当前连接的 SSID
 */
String WiFiManager::getSSID() const {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return "未连接";
}

/**
 * 获取 MAC 地址
 */
String WiFiManager::getMacAddress() const {
    return WiFi.macAddress();
}

/**
 * 打印网络信息
 */
void WiFiManager::printNetworkInfo() const {
    Serial.println("\n【网络信息】");
    Serial.print("  SSID: ");
    Serial.println(getSSID());
    
    Serial.print("  IP 地址: ");
    Serial.println(getLocalIP());
    
    Serial.print("  MAC 地址: ");
    Serial.println(getMacAddress());
    
    Serial.print("  信号强度: ");
    Serial.print(getRSSI());
    Serial.println(" dBm");
    Serial.println();
}

/**
 * 日志输出
 */
void WiFiManager::logMessage(const char* message, bool isError) {
    if (isError) {
        Serial.print("[错误] ");
    } else {
        Serial.print("[信息] ");
    }
    Serial.println(message);
}
