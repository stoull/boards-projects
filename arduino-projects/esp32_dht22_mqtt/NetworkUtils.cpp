/**
 * 网络工具模块 - ESP32-C3版本实现
 * 提供WiFi连接和NTP时间同步功能
 * 作者: Arduino版本转换
 * 日期: 2026年1月
 */

#include "NetworkUtils.h"
#include <time.h>
#include <sys/time.h>

// ==================== WiFiManager 实现 ====================

// 构造函数
WiFiManager::WiFiManager(const String& ssid, const String& password)
    : ssid(ssid), password(password), connected(false), logCallback(nullptr) {
}

// 析构函数
WiFiManager::~WiFiManager() {
    cleanup();
}

// 设置日志回调函数
void WiFiManager::setLogCallback(LogCallback callback) {
    logCallback = callback;
}

// 内部日志方法
void WiFiManager::log(const String& message) {
    if (logCallback) {
        logCallback(message);
    } else {
        Serial.println(message);
    }
}

// 获取WiFi状态字符串
String WiFiManager::getStatusString(int status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "空闲状态";
        case WL_NO_SSID_AVAIL:
            return "无可用网络";
        case WL_SCAN_COMPLETED:
            return "扫描完成";
        case WL_CONNECTED:
            return "已连接";
        case WL_CONNECT_FAILED:
            return "连接失败";
        case WL_CONNECTION_LOST:
            return "连接丢失";
        case WL_DISCONNECTED:
            return "已断开";
        case WL_NO_SHIELD:
            return "无WiFi模块";
        default:
            return "未知状态";
    }
}

// 连接到WiFi网络
bool WiFiManager::connect(int timeout, int maxRetries, int retryDelay, WatchdogCallback watchdogCallback) {
    try {
        // 配置WiFi模式
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);  // 禁用WiFi省电模式

        // 启用Wi-Fi自动重连机制
        // WiFi.setAutoConnect(true);
        // 启用DHCP以确保IP地址动态分配
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        
        // 如果已经连接，直接返回
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            log("已连接到 WiFi: " + WiFi.localIP().toString());
            return true;
        }
        
        log("开始WiFi连接，最大重试次数: " + String(maxRetries));
        
        // 重试连接循环
        for (int attempt = 1; attempt <= maxRetries; attempt++) {
            log("=== 连接尝试 " + String(attempt) + "/" + String(maxRetries) + " ===");
            
            // 清理之前的连接状态
            if (attempt > 1) {
                log("清理WiFi状态...");
                WiFi.disconnect(true);
                delay(1000);
                WiFi.mode(WIFI_STA);
                delay(500);
            }
            
            // 先扫描并验证目标SSID是否存在（第一次尝试或网络扫描失败时）
            if (attempt == 1 || attempt % 2 == 0) {
                log("扫描网络并验证SSID...");
                if (!this->scanAndVerifySSID()) {
                    if (attempt < maxRetries) {
                        log("网络扫描失败，等待 " + String(retryDelay/1000) + " 秒后重试...");
                        delay(retryDelay);
                        continue;
                    } else {
                        log("所有重试均失败：未找到目标网络");
                        connected = false;
                        return false;
                    }
                }
            }
            
            // 开始连接
            log("正在连接到 WiFi: " + ssid + " (尝试 " + String(attempt) + ")");
            WiFi.begin(ssid.c_str(), password.c_str());
            
            // 等待连接（每秒检查状态并喂狗）
            unsigned long startTime = millis();
            bool connectionSuccess = false;
            
            while (WiFi.status() != WL_CONNECTED) {
                if (millis() - startTime > timeout * 1000UL) {
                    log("连接超时（" + String(timeout) + "秒）");
                    break;
                }
                
                // 喂狗（如果提供了回调函数）
                if (watchdogCallback) {
                    watchdogCallback();
                }
                
                int status = WiFi.status();
                log("等待连接... 状态: " + String(status) + " (" + getStatusString(status) + ")");
                
                // 检查特定的错误状态
                if (status == WL_CONNECT_FAILED) {
                    log("连接失败，可能是密码错误");
                    break;
                } else if (status == WL_NO_SSID_AVAIL) {
                    log("网络不可用");
                    break;
                }
                
                delay(1000);
            }
            
            // 检查连接结果
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                String ipAddress = WiFi.localIP().toString();
                int rssi = WiFi.RSSI();
                log("✓ WiFi 连接成功！");
                log("  IP 地址: " + ipAddress);
                log("  信号强度: " + String(rssi) + " dBm");
                log("  尝试次数: " + String(attempt));
                return true;
            } else {
                // 连接失败
                int status = WiFi.status();
                log("❌ 连接失败，状态: " + String(status) + " (" + getStatusString(status) + ")");
                
                if (attempt < maxRetries) {
                    log("等待 " + String(retryDelay/1000) + " 秒后重试...");
                    delay(retryDelay);
                } else {
                    log("所有重试均失败，放弃连接");
                }
            }
        }
        
        // 所有重试都失败了
        connected = false;
        log("WiFi 连接失败：已达到最大重试次数");
        return false;
        
    } catch (...) {
        log("WiFi 连接失败: 未知异常");
        connected = false;
        return false;
    }
}

// 断开WiFi连接
void WiFiManager::disconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        connected = false;
        log("WiFi 已断开");
    }
}

// 检查WiFi是否已连接
bool WiFiManager::isConnected() {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    connected = wifiConnected;
    return connected;
}

// 获取IP地址
String WiFiManager::getIpAddress() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

// 获取网络信息
NetworkInfo WiFiManager::getNetworkInfo() {
    NetworkInfo info;
    
    if (isConnected()) {
        info.ip = WiFi.localIP().toString();
        info.subnet = WiFi.subnetMask().toString();
        info.gateway = WiFi.gatewayIP().toString();
        info.dns1 = WiFi.dnsIP().toString();
        info.dns2 = WiFi.dnsIP(1).toString();
        info.rssi = WiFi.RSSI();
        info.mac = WiFi.macAddress();
        info.hostname = WiFi.getHostname();
    }
    
    return info;
}

// 获取WiFi信号强度
int WiFiManager::getRSSI() {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}

// 获取MAC地址
String WiFiManager::getMacAddress() {
    return WiFi.macAddress();
}

// 设置主机名
bool WiFiManager::setHostname(const String& hostname) {
    return WiFi.setHostname(hostname.c_str());
}

// 扫描可用WiFi网络
int WiFiManager::scanNetworks() {
    log("开始扫描WiFi网络...");
    int networks = WiFi.scanNetworks();
    log("发现 " + String(networks) + " 个网络");
    return networks;
}

// 扫描网络并验证目标SSID是否存在
bool WiFiManager::scanAndVerifySSID() {
    try {
        log("扫描可用的WiFi网络...");
        int networkCount = WiFi.scanNetworks();
        log("发现 " + String(networkCount) + " 个网络:");
        
        bool ssidFound = false;
        for (int i = 0; i < networkCount; i++) {
            String scannedSSID = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            log("  [" + String(i) + "] " + scannedSSID + " (信号强度: " + String(rssi) + " dBm)");
            
            if (scannedSSID == ssid) {
                ssidFound = true;
                log("  ✓ 找到目标网络: " + ssid + " (信号强度: " + String(rssi) + " dBm)");
            }
        }
        
        if (!ssidFound) {
            log("❌ 错误: 未找到指定的网络 '" + ssid + "'");
            log("请检查:");
            log("  1. SSID名称是否正确（区分大小写）");
            log("  2. 设备是否在WiFi覆盖范围内");
            log("  3. 网络是否为2.4GHz频段");
            log("  4. 路由器是否启用SSID广播");
            return false;
        }
        
        return true;
        
    } catch (...) {
        log("网络扫描失败: 未知异常");
        return false;
    }
}

// 获取扫描到的网络信息
String WiFiManager::getScannedSSID(int index) {
    if (index >= 0 && index < WiFi.scanComplete()) {
        return WiFi.SSID(index);
    }
    return "";
}

// 清理资源并释放内存
void WiFiManager::cleanup() {
    try {
        if (WiFi.status() == WL_CONNECTED) {
            WiFi.disconnect(true);
        }
        WiFi.mode(WIFI_OFF);
        connected = false;
        logCallback = nullptr;
        log("WiFi 资源已清理");
    } catch (...) {
        Serial.println("WiFi 清理异常");
    }
}

// ==================== NTPTimeSync 实现 ====================

// 常用NTP服务器列表
const char* NTPTimeSync::NTP_SERVERS[] = {
    "ntp.aliyun.com",       // 阿里云
    "ntp.ntsc.ac.cn",       // 中国国家授时中心
    "ntp1.aliyun.com",      // 阿里云备用
    "pool.ntp.org",         // 国际NTP池
    "time.nist.gov",         // NIST时间服务器
    "time.asia.apple.com",
    "time.apple.com"
};

const int NTPTimeSync::NTP_SERVER_COUNT = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);

// 构造函数
NTPTimeSync::NTPTimeSync(int timezoneOffsetHours)
    : timezoneOffset(timezoneOffsetHours * 3600), logCallback(nullptr) {
}

// 析构函数
NTPTimeSync::~NTPTimeSync() {
    // ESP32 Arduino框架会自动管理SNTP资源
    // 无需手动停止
}

// 设置日志回调函数
void NTPTimeSync::setLogCallback(LogCallback callback) {
    logCallback = callback;
}

// 内部日志方法
void NTPTimeSync::log(const String& message) {
    if (logCallback) {
        logCallback(message);
    } else {
        Serial.println(message);
    }
}

// 从NTP服务器同步时间
bool NTPTimeSync::sync(const char* ntpServer, int retryCount) {
    try {
        // 确定要使用的NTP服务器列表
        const char* servers[NTP_SERVER_COUNT + 1];
        int serverCount = 0;
        
        if (ntpServer) {
            servers[serverCount++] = ntpServer;
        }
        
        // 添加默认服务器
        for (int i = 0; i < NTP_SERVER_COUNT; i++) {
            servers[serverCount++] = NTP_SERVERS[i];
        }
        
        // 尝试每个服务器
        for (int s = 0; s < serverCount; s++) {
            for (int attempt = 0; attempt < retryCount; attempt++) {
                try {
                    log("正在从 " + String(servers[s]) + " 同步时间... (尝试 " + 
                        String(attempt + 1) + "/" + String(retryCount) + ")");
                    
                    // 配置时区和NTP服务器
                    configTime(timezoneOffset, 0, servers[s]);
                    
                    // 等待时间同步
                    int syncTimeout = 20000;  // 20秒超时
                    unsigned long startTime = millis();
                    
                    while (!isTimeSynced() && (millis() - startTime < syncTimeout)) {
                        delay(100);
                    }
                    
                    if (isTimeSynced()) {
                        // 同步成功
                        String currentTime = getISO8601Time();
                        log("时间同步成功: " + currentTime);
                        return true;
                    } else {
                        log("同步超时");
                    }
                    
                } catch (...) {
                    log("同步异常");
                    if (attempt < retryCount - 1) {
                        delay(2000);  // 重试前等待
                    }
                    continue;
                }
            }
        }
        
        log("所有 NTP 服务器同步失败");
        return false;
        
    } catch (...) {
        log("时间同步异常");
        return false;
    }
}

// 设置时区偏移量
void NTPTimeSync::setTimezoneOffset(int hours) {
    timezoneOffset = hours * 3600;
}

// 获取当前时间戳
time_t NTPTimeSync::getTimestamp() {
    time_t now;
    time(&now);
    return now;
}

// 获取ISO 8601格式的当前时间字符串
String NTPTimeSync::getISO8601Time() {
    time_t now = getTimestamp();
    struct tm* timeinfo = localtime(&now);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    
    return String(buffer);
}

// 获取带时区偏移的ISO 8601格式时间
String NTPTimeSync::getISO8601TimeWithTimezone(int timezoneOffsetHours) {
    time_t now = getTimestamp();
    struct tm* timeinfo = localtime(&now);
    
    // 计算时区偏移的小时和分钟
    int offsetHours = timezoneOffsetHours;
    int offsetMinutes = 0;
    char offsetSign = (offsetHours >= 0) ? '+' : '-';
    offsetHours = abs(offsetHours);
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec,
             offsetSign,
             offsetHours,
             offsetMinutes);
    
    return String(buffer);
}

// 获取UTC时间的ISO 8601格式
String NTPTimeSync::getISO8601TimeUTC() {
    time_t now = getTimestamp();
    struct tm* timeinfo = gmtime(&now);
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    
    return String(buffer);
}

// 格式化当前时间
String NTPTimeSync::formatTime(const String& formatStr) {
    time_t now = getTimestamp();
    struct tm* timeinfo = localtime(&now);
    
    char buffer[128];
    strftime(buffer, sizeof(buffer), formatStr.c_str(), timeinfo);
    
    return String(buffer);
}

// 检查时间是否已同步
bool NTPTimeSync::isTimeSynced() {
    time_t now = getTimestamp();
    return (now > 1000000000);  // 时间戳大于2001年表示已同步
}

// 获取系统运行时间（毫秒）
unsigned long NTPTimeSync::getUptime() {
    return millis();
}

// 延迟到指定时间（非阻塞检查）
bool NTPTimeSync::delayUntil(time_t targetTime) {
    return getTimestamp() >= targetTime;
}

// ==================== 便捷函数实现 ====================

// 快速连接WiFi（便捷函数）
WiFiManager* quickConnectWifi(const String& ssid, const String& password, int timeout) {
    WiFiManager* wifi = new WiFiManager(ssid, password);
    if (wifi->connect(timeout)) {
        return wifi;
    }
    delete wifi;
    return nullptr;
}

// 快速同步时间（便捷函数）
bool quickSyncTime(int timezoneOffsetHours, const char* ntpServer) {
    NTPTimeSync ntp(timezoneOffsetHours);
    return ntp.sync(ntpServer);
}

// 检查网络连接状态
bool isNetworkConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

// 获取网络状态字符串
String getNetworkStatus() {
    switch (WiFi.status()) {
        case WL_IDLE_STATUS:
            return "空闲状态";
        case WL_NO_SSID_AVAIL:
            return "无可用网络";
        case WL_SCAN_COMPLETED:
            return "扫描完成";
        case WL_CONNECTED:
            return "已连接 - IP: " + WiFi.localIP().toString();
        case WL_CONNECT_FAILED:
            return "连接失败";
        case WL_CONNECTION_LOST:
            return "连接丢失";
        case WL_DISCONNECTED:
            return "已断开";
        case WL_NO_SHIELD:
            return "无WiFi模块";
        default:
            return "未知状态: " + String(WiFi.status());
    }
}

// 等待网络连接
bool waitForNetwork(int timeout) {
    unsigned long startTime = millis();
    
    while (!isNetworkConnected() && (millis() - startTime < timeout * 1000UL)) {
        delay(100);
    }
    
    return isNetworkConnected();
}