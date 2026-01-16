/**
 * WiFiManager.h
 * WiFi 连接管理类 - 头文件
 * 
 * 功能:
 * - WiFi 连接和断线重连
 * - 连接状态监控
 * - 信号强度检测
 */

#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>

class WiFiManager {
public:
    // 构造函数
    WiFiManager();
    
    // WiFi 连接
    bool connect(const char* ssid, const char* password, 
                 uint32_t timeout = 30000, uint16_t retryDelay = 2000);
    
    // 断线重连
    bool reconnect(uint32_t timeout = 30000);
    
    // 断开连接
    void disconnect();
    
    // 状态检查
    bool isConnected() const;
    String getLocalIP() const;
    int getRSSI() const;
    String getSSID() const;
    String getMacAddress() const;
    
    // 网络信息
    void printNetworkInfo() const;
    
private:
    const char* currentSSID;
    const char* currentPassword;
    
    void logMessage(const char* message, bool isError = false);
};

#endif // WIFIMANAGER_H
