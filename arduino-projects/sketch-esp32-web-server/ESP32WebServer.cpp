/**
 * ESP32WebServer.cpp
 * ESP32 Web 服务器封装类 - 实现文件
 */

#include "ESP32WebServer.h"

/**
 * 构造函数
 * @param port 服务器端口 (默认 80)
 */
ESP32WebServer::ESP32WebServer(uint16_t port)
    : controlGPIO(255),
      invertedLogic(false),
      gpioState(false) {
    server = new WebServer(port);
}

/**
 * 析构函数
 */
ESP32WebServer::~ESP32WebServer() {
    stop();
    if (server != nullptr) {
        delete server;
        server = nullptr;
    }
}

/**
 * 启动 Web 服务器
 */
void ESP32WebServer::begin() {
    if (server != nullptr) {
        // 设置 404 处理器
        server->onNotFound([this]() { this->handleNotFound(); });
        
        server->begin();
        logMessage("HTTP 服务器已启动");
    }
}

/**
 * 停止 Web 服务器
 */
void ESP32WebServer::stop() {
    if (server != nullptr) {
        server->stop();
        logMessage("HTTP 服务器已停止");
    }
}

/**
 * 处理客户端请求 (需要在 loop() 中调用)
 */
void ESP32WebServer::handleClient() {
    if (server != nullptr) {
        server->handleClient();
    }
}

/**
 * 注册根路径处理器
 * @param handler 处理函数
 */
void ESP32WebServer::onRoot(RequestHandler handler) {
    if (server != nullptr) {
        server->on("/", handler);
    }
}

/**
 * 注册自定义路由处理器
 * @param uri URI 路径
 * @param handler 处理函数
 */
void ESP32WebServer::on(const char* uri, RequestHandler handler) {
    if (server != nullptr) {
        server->on(uri, handler);
    }
}

void ESP32WebServer::on(const String& uri, RequestHandler handler) {
    if (server != nullptr) {
        server->on(uri, handler);
    }
}

/**
 * 设置 GPIO 控制功能
 * @param gpioPin GPIO 引脚编号
 * @param invertedLogic 是否使用反向逻辑 (true: LOW=ON, HIGH=OFF)
 */
void ESP32WebServer::setupGPIOControl(uint8_t gpioPin, bool invertedLogic) {
    controlGPIO = gpioPin;
    this->invertedLogic = invertedLogic;
    this->gpioState = false;
    
    // 初始化 GPIO
    pinMode(controlGPIO, OUTPUT);
    digitalWrite(controlGPIO, invertedLogic ? HIGH : LOW);
    
    // 注册路由
    server->on("/", [this]() { this->handleRootDefault(); });
    server->on("/on", [this]() { this->handleGPIOOn(); });
    server->on("/off", [this]() { this->handleGPIOOff(); });
    
    char msg[50];
    snprintf(msg, sizeof(msg), "GPIO %d 控制已设置 (反向逻辑: %s)", 
             gpioPin, invertedLogic ? "是" : "否");
    logMessage(msg);
}

/**
 * 设置 GPIO 状态
 * @param state true=ON, false=OFF
 */
void ESP32WebServer::setGPIOState(bool state) {
    if (controlGPIO == 255) return;
    
    gpioState = state;
    
    if (invertedLogic) {
        digitalWrite(controlGPIO, state ? LOW : HIGH);
    } else {
        digitalWrite(controlGPIO, state ? HIGH : LOW);
    }
}

/**
 * 获取当前 GPIO 状态
 */
bool ESP32WebServer::getGPIOState() const {
    return gpioState;
}

/**
 * 发送 HTML 响应
 */
void ESP32WebServer::sendHTML(int code, const String& content) {
    if (server != nullptr) {
        server->send(code, "text/html; charset=utf-8", content);
    }
}

/**
 * 发送 JSON 响应
 */
void ESP32WebServer::sendJSON(int code, const String& content) {
    if (server != nullptr) {
        server->send(code, "application/json", content);
    }
}

/**
 * 发送纯文本响应
 */
void ESP32WebServer::sendText(int code, const String& content) {
    if (server != nullptr) {
        server->send(code, "text/plain", content);
    }
}

/**
 * 默认根路径处理器
 */
void ESP32WebServer::handleRootDefault() {
    String html = generateGPIOControlHTML();
    sendHTML(200, html);
}

/**
 * GPIO 开启处理器
 */
void ESP32WebServer::handleGPIOOn() {
    setGPIOState(true);
    logMessage("GPIO 已开启");
    handleRootDefault();
}

/**
 * GPIO 关闭处理器
 */
void ESP32WebServer::handleGPIOOff() {
    setGPIOState(false);
    logMessage("GPIO 已关闭");
    handleRootDefault();
}

/**
 * 404 处理器
 */
void ESP32WebServer::handleNotFound() {
    String message = "<!DOCTYPE html><html><head>";
    message += "<meta charset=\"UTF-8\">";
    message += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    message += "<style>";
    message += "body { font-family: Arial; text-align: center; padding: 50px; background: #f5f7fa; }";
    message += "h1 { color: #e74c3c; }";
    message += "p { color: #555; font-size: 18px; }";
    message += "a { color: #3498db; text-decoration: none; }";
    message += "</style></head><body>";
    message += "<h1>404 - 页面未找到</h1>";
    message += "<p>URI: " + server->uri() + "</p>";
    message += "<p><a href=\"/\">返回首页</a></p>";
    message += "</body></html>";
    
    sendHTML(404, message);
}

/**
 * 生成 GPIO 控制 HTML 页面
 */
String ESP32WebServer::generateGPIOControlHTML() {
    String stateText = gpioState ? "ON" : "OFF";
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<link rel=\"icon\" href=\"data:,\">";
    html += "<style>";
    html += "html { font-family: Helvetica; text-align: center; background: #f5f7fa; margin: 0; padding: 20px; }";
    html += "body { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #333; font-size: 28px; margin-bottom: 20px; }";
    html += "p { color: #555; font-size: 18px; margin: 10px 0; }";
    html += ".button { background: #4CAF50; border: none; color: white; padding: 12px 24px; text-decoration: none; font-size: 20px; border-radius: 8px; cursor: pointer; transition: background 0.2s ease; display: inline-block; width: 120px; box-sizing: border-box; }";
    html += ".button:hover { background: #45a049; }";
    html += ".button2 { background: #555555; }";
    html += ".button2:hover { background: #666666; }";
    html += "</style></head>";
    html += "<body><h1>ESP32 Web Server</h1>";
    
    // 显示 GPIO 控制
    html += "<p>GPIO " + String(controlGPIO) + " - 状态: " + stateText + "</p>";
    
    if (gpioState) {
        html += "<p><a href=\"/off\"><button class=\"button button2\">关闭</button></a></p>";
    } else {
        html += "<p><a href=\"/on\"><button class=\"button\">开启</button></a></p>";
    }
    
    html += "</body></html>";
    
    return html;
}

/**
 * 日志输出
 */
void ESP32WebServer::logMessage(const char* message) {
    Serial.print("[WebServer] ");
    Serial.println(message);
}
