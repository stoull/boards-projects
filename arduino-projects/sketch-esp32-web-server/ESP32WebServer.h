/**
 * ESP32WebServer.h
 * ESP32 Web 服务器封装类 - 头文件
 * 
 * 功能:
 * - HTTP 服务器管理
 * - 路由处理
 * - GPIO 控制页面
 * - 自定义处理器支持
 */

#ifndef ESP32WEBSERVER_H
#define ESP32WEBSERVER_H

#include <Arduino.h>
#include <WebServer.h>

class ESP32WebServer {
public:
    // 回调函数类型定义
    typedef void (*RequestHandler)();
    
    // 构造函数
    ESP32WebServer(uint16_t port = 80);
    ~ESP32WebServer();
    
    // 服务器控制
    void begin();
    void stop();
    void handleClient();
    
    // 路由注册
    void onRoot(RequestHandler handler);
    void on(const char* uri, RequestHandler handler);
    void on(const String& uri, RequestHandler handler);
    
    // GPIO 控制功能
    void setupGPIOControl(uint8_t gpioPin, bool invertedLogic = false);
    void setGPIOState(bool state);
    bool getGPIOState() const;
    
    // 发送响应
    void sendHTML(int code, const String& content);
    void sendJSON(int code, const String& content);
    void sendText(int code, const String& content);
    
    // 获取服务器对象
    WebServer* getServer() { return server; }
    
private:
    WebServer* server;
    uint8_t controlGPIO;
    bool invertedLogic;
    bool gpioState;
    
    // 默认处理器
    void handleRootDefault();
    void handleGPIOOn();
    void handleGPIOOff();
    void handleNotFound();
    
    // HTML 生成
    String generateGPIOControlHTML();
    
    void logMessage(const char* message);
};

#endif // ESP32WEBSERVER_H
