char WIFI_SSID[] = "wifiname";
char WIFI_PASSWORD[] = "wifi-password";

// MQTT 配置
const char* MQTT_SERVER = "your_broker_host";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "mqtt_username";      // 如果需要认证
const char* MQTT_PASSWORD = "mqtt_pass";  // 如果需要认证
const char* MQTT_CLIENT_ID_PRE = "your id";

const char* MQTT_TOPIC = "home/livingroom/env/esp32_D777EC/state";
const char* MQTT_TOPIC_DeviceInfo = "home/livingroom/env/esp32_D777EC/metrics";

const char* kDevice_Location = "home/livingroom/env/";
const char* kDevice_Type = "esp32c3";
