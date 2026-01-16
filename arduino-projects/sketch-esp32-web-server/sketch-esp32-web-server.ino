/**
 * ESP32_WebServer_Advanced.ino
 * ESP32 Web 服务器高级示例
 * 
 * 展示如何使用 WiFiManager 和 ESP32WebServer 类创建自定义路由
 * 
 * 功能:
 * - 基础 GPIO 控制
 * - 自定义 API 路由
 * - JSON 响应
 * - 状态查询
 */

#include "WiFiManager.h"
#include "ESP32WebServer.h"
#include "Secret.h"

// GPIO 配置
const uint8_t CONTROL_GPIO = 8;

// 创建对象
WiFiManager wifiManager;
ESP32WebServer webServer(80);

// 全局变量 (用于演示)
unsigned long startTime = 0;
int requestCount = 0;

// 自定义处理函数: 状态查询 API
void handleStatus() {
  unsigned long uptime = (millis() - startTime) / 1000;
  
  String json = "{";
  json += "\"status\":\"running\",";
  json += "\"uptime\":" + String(uptime) + ",";
  json += "\"requests\":" + String(requestCount) + ",";
  json += "\"gpio\":" + String(webServer.getGPIOState() ? 1 : 0) + ",";
  json += "\"rssi\":" + String(wifiManager.getRSSI()) + ",";
  json += "\"ip\":\"" + wifiManager.getLocalIP() + "\"";
  json += "}";
  
  webServer.sendJSON(200, json);
  Serial.println("[API] 状态查询");
}

// 自定义处理函数: 关于页面
void handleAbout() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial; max-width: 600px; margin: 50px auto; padding: 20px; background: #f5f7fa; }";
  html += "h1 { color: #333; }";
  html += "p { color: #555; line-height: 1.6; }";
  html += "a { color: #3498db; text-decoration: none; }";
  html += "</style></head><body>";
  html += "<h1>关于本项目</h1>";
  html += "<p>这是一个基于 ESP32-C3 的 Web 服务器示例。</p>";
  html += "<p><strong>功能特性:</strong></p>";
  html += "<ul>";
  html += "<li>WiFi 连接管理</li>";
  html += "<li>Web 服务器</li>";
  html += "<li>GPIO 控制</li>";
  html += "<li>RESTful API</li>";
  html += "</ul>";
  html += "<p><a href=\"/\">返回首页</a> | <a href=\"/api/status\">查看状态 (JSON)</a></p>";
  html += "</body></html>";
  
  webServer.sendHTML(200, html);
  Serial.println("[Web] 关于页面访问");
}

// 自定义处理函数: 重启设备
void handleRestart() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta http-equiv=\"refresh\" content=\"10;url=/\">";
  html += "<style>";
  html += "body { font-family: Arial; text-align: center; padding: 50px; background: #f5f7fa; }";
  html += "h1 { color: #e74c3c; }";
  html += "</style></head><body>";
  html += "<h1>设备正在重启...</h1>";
  html += "<p>10 秒后自动跳转到首页</p>";
  html += "</body></html>";
  
  webServer.sendHTML(200, html);
  Serial.println("[系统] 重启请求");
  
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  startTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("  ESP32 Web 服务器 示例");
  Serial.println("========================================\n");
  
  // 1. 连接 WiFi
  if (!wifiManager.connect(ssid, password, 30000, 2000)) {
    Serial.println("❌ WiFi 连接失败，重启设备...");
    delay(3000);
    ESP.restart();
  }
  
  // 2. 设置 Web 服务器 GPIO 控制
  webServer.setupGPIOControl(CONTROL_GPIO, true);
  
  // 3. 注册自定义路由
  webServer.on("/about", handleAbout);
  webServer.on("/api/status", handleStatus);
  webServer.on("/restart", handleRestart);
  
  // 4. 启动 Web 服务器
  webServer.begin();
  
  Serial.println("\n========================================");
  Serial.println("✓ 系统就绪！");
  Serial.println("✓ 可用路由:");
  Serial.print("  - http://");
  Serial.print(wifiManager.getLocalIP());
  Serial.println("/          (首页)");
  Serial.print("  - http://");
  Serial.print(wifiManager.getLocalIP());
  Serial.println("/about     (关于)");
  Serial.print("  - http://");
  Serial.print(wifiManager.getLocalIP());
  Serial.println("/api/status (状态API)");
  Serial.print("  - http://");
  Serial.print(wifiManager.getLocalIP());
  Serial.println("/restart   (重启)");
  Serial.println("========================================\n");
}

void loop() {
  // 检查 WiFi 连接状态
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 10000) {  // 每 10 秒检查一次
    lastWiFiCheck = millis();
    
    if (!wifiManager.isConnected()) {
      Serial.println("⚠ WiFi 断开，尝试重连...");
      if (!wifiManager.reconnect(30000)) {
        Serial.println("❌ 重连失败，重启设备...");
        delay(3000);
        ESP.restart();
      }
    }
  }
  
  // 处理 Web 服务器请求
  webServer.handleClient();
  
  // 增加请求计数 (简化示例，实际应在处理函数中增加)
  static int lastState = 0;
  int currentState = webServer.getGPIOState() ? 1 : 0;
  if (currentState != lastState) {
    requestCount++;
    lastState = currentState;
  }
  
  delay(10);
}
