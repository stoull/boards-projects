/**
 * 网络工具模块 - ESP32-C3版本
 * 提供WiFi连接和NTP时间同步功能
 * 作者: Arduino版本转换
 * 日期: 2026年1月
 */

#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <functional>

// 回调函数类型定义
typedef std::function<void()> WatchdogCallback;
typedef std::function<void(const String& message)> LogCallback;

// 网络信息结构体
struct NetworkInfo {
    String ip;
    String subnet;
    String gateway;
    String dns1;
    String dns2;
    int rssi;
    String mac;
    String hostname;
};

class WiFiManager {
private:
    String ssid;
    String password;
    bool connected;
    LogCallback logCallback;
    
    // 内部方法
    void log(const String& message);
    String getStatusString(int status);

public:
    /**
     * 构造函数
     * @param ssid WiFi网络名称
     * @param password WiFi密码
     */
    WiFiManager(const String& ssid, const String& password);
    
    /**
     * 析构函数
     */
    ~WiFiManager();
    
    /**
     * 设置日志回调函数
     * @param callback 日志回调函数
     */
    void setLogCallback(LogCallback callback);
    
    /**
     * 连接到WiFi网络
     * @param timeout 单次连接超时时间（秒），默认30秒
     * @param maxRetries 最大重试次数，默认3次
     * @param retryDelay 重试间隔（毫秒），默认5秒
     * @param watchdogCallback 看门狗喂狗回调函数（可选）
     * @return 连接成功返回true，失败返回false
     */
    bool connect(int timeout = 30, int maxRetries = 3, int retryDelay = 5000, WatchdogCallback watchdogCallback = nullptr);
    
    /**
     * 断开WiFi连接
     */
    void disconnect();
    
    /**
     * 检查WiFi是否已连接
     * @return 已连接返回true，否则返回false
     */
    bool isConnected();
    
    /**
     * 获取IP地址
     * @return IP地址，未连接返回空字符串
     */
    String getIpAddress();
    
    /**
     * 获取网络信息
     * @return 网络信息结构体
     */
    NetworkInfo getNetworkInfo();
    
    /**
     * 获取WiFi信号强度
     * @return RSSI值
     */
    int getRSSI();
    
    /**
     * 获取MAC地址
     * @return MAC地址字符串
     */
    String getMacAddress();
    
    /**
     * 设置主机名
     * @param hostname 主机名
     * @return 设置成功返回true
     */
    bool setHostname(const String& hostname);
    
    /**
     * 扫描可用WiFi网络
     * @return 可用网络数量
     */
    int scanNetworks();
    
    /**
     * 获取扫描到的网络信息
     * @param index 网络索引
     * @return 网络SSID，索引无效返回空字符串
     */
    String getScannedSSID(int index);
    
    /**
     * 扫描网络并验证目标SSID是否存在
     * @return 找到目标SSID返回true，否则返回false
     */
    bool scanAndVerifySSID();
    
    /**
     * 清理资源并释放内存
     */
    void cleanup();
};

class NTPTimeSync {
private:
    long timezoneOffset;  // 时区偏移量（秒）
    LogCallback logCallback;
    
    // 常用NTP服务器列表
    static const char* NTP_SERVERS[];
    static const int NTP_SERVER_COUNT;
    
    // 内部方法
    void log(const String& message);
    void adjustTimezone();

public:
    /**
     * 构造函数
     * @param timezoneOffsetHours 时区偏移量（小时），默认8（北京时间UTC+8）
     */
    NTPTimeSync(int timezoneOffsetHours = 8);
    
    /**
     * 析构函数
     */
    ~NTPTimeSync();
    
    /**
     * 设置日志回调函数
     * @param callback 日志回调函数
     */
    void setLogCallback(LogCallback callback);
    
    /**
     * 从NTP服务器同步时间
     * @param ntpServer NTP服务器地址，默认使用服务器列表
     * @param retryCount 重试次数，默认3次
     * @return 同步成功返回true，失败返回false
     */
    bool sync(const char* ntpServer = nullptr, int retryCount = 3);
    
    /**
     * 设置时区偏移量
     * @param hours 时区偏移小时数
     */
    void setTimezoneOffset(int hours);
    
    /**
     * 获取当前时间戳
     * @return Unix时间戳
     */
    static time_t getTimestamp();
    
    /**
     * 获取ISO 8601格式的当前时间字符串
     * @return ISO 8601格式时间 (YYYY-MM-DDTHH:MM:SS)
     */
    static String getISO8601Time();
    
    /**
     * 获取带时区偏移的ISO 8601格式时间
     * @param timezoneOffsetHours 时区偏移小时数
     * @return 格式如 2026-01-04T09:49:41+08:00
     */
    static String getISO8601TimeWithTimezone(int timezoneOffsetHours = 8);
    
    /**
     * 获取UTC时间的ISO 8601格式
     * @return UTC时间格式如 2026-01-04T01:49:41Z
     */
    static String getISO8601TimeUTC();
    
    /**
     * 格式化当前时间
     * @param formatStr 时间格式字符串，支持strftime格式
     * @return 格式化的时间字符串
     */
    static String formatTime(const String& formatStr = "%Y-%m-%d %H:%M:%S");
    
    /**
     * 检查时间是否已同步
     * @return 时间已同步返回true
     */
    static bool isTimeSynced();
    
    /**
     * 获取系统运行时间（毫秒）
     * @return 系统运行时间
     */
    static unsigned long getUptime();
    
    /**
     * 延迟到指定时间（非阻塞检查）
     * @param targetTime 目标时间戳
     * @return 已到达目标时间返回true
     */
    static bool delayUntil(time_t targetTime);
};

// ==================== 便捷函数声明 ====================

/**
 * 快速连接WiFi（便捷函数）
 * @param ssid WiFi网络名称
 * @param password WiFi密码
 * @param timeout 连接超时时间（秒）
 * @return WiFi管理器指针，连接失败返回nullptr
 */
WiFiManager* quickConnectWifi(const String& ssid, const String& password, int timeout = 30);

/**
 * 快速同步时间（便捷函数）
 * @param timezoneOffsetHours 时区偏移量（小时）
 * @param ntpServer NTP服务器地址
 * @return 同步成功返回true，失败返回false
 */
bool quickSyncTime(int timezoneOffsetHours = 8, const char* ntpServer = nullptr);

/**
 * 检查网络连接状态
 * @return 网络已连接返回true
 */
bool isNetworkConnected();

/**
 * 获取网络状态字符串
 * @return 网络状态描述
 */
String getNetworkStatus();

/**
 * 等待网络连接
 * @param timeout 超时时间（秒）
 * @return 连接成功返回true
 */
bool waitForNetwork(int timeout = 30);

#endif // NETWORK_UTILS_H