#include "DeviceInfoHelper.h"
// extern "C" {
//   #include "driver/temp_sensor.h"
// }

NetInfo get_network_info() {
  NetInfo info;
  if (WiFi.isConnected()) {
    info.ip = WiFi.localIP().toString();
    info.subnet = WiFi.subnetMask().toString();
    info.gateway = WiFi.gatewayIP().toString();
    info.dns = WiFi.dnsIP().toString();
    info.rssi = String(WiFi.RSSI());

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    info.mac = String(macStr);
  } else {
    info.ip = "";
    info.subnet = "";
    info.gateway = "";
    info.dns = "";
    info.rssi = "";
    info.mac = "";
  }
  return info;
}

String get_unique_id() {
  uint8_t chipid[6];
  esp_efuse_mac_get_default(chipid);
  char id[13];
  snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
    chipid[0], chipid[1], chipid[2], chipid[3], chipid[4], chipid[5]);
  return String(id);
}

String get_platform() {
  return "esp32c3";
}

String get_os_version() {
  return String(ESP.getSdkVersion());
}

int get_cpu_frequency_mhz() {
  return ESP.getCpuFreqMHz();
}

String get_cpu_temperature() {
  // ESP32C3 does not have built-in temperature sensor, return "" as placeholder
  return "";
  
  // ESP-IDF框架
  // #include "driver/temp_sensor.h"
  // float temperature;
  // temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  // temp_sensor_set_config(temp_sensor);
  // temp_sensor_start();
  // temp_sensor_read_celsius(&temperature);
  // temp_sensor_stop();
  // return String(temperature, 2);
}

void get_storage_info(DeviceInfo &info) {
  if (!SPIFFS.begin(true)) {
    info.total_storage_bytes = 0;
    info.used_storage_bytes = 0;
    info.free_storage_bytes = 0;
    info.storage_usage_percent = 0.0f;
    return;
  }

  // info.total_storage_bytes = SPIFFS.totalBytes();
  info.total_storage_bytes = SPIFFS.totalBytes();
  info.used_storage_bytes  = SPIFFS.usedBytes();
  info.free_storage_bytes = info.total_storage_bytes - info.used_storage_bytes;

  // Flash总容量
  uint32_t flash_size = ESP.getFlashChipSize();
  uint32_t system_used_bytes = flash_size - info.total_storage_bytes;
  info.total_storage_bytes = flash_size;
  
  if (info.total_storage_bytes > 0)
    info.storage_usage_percent = ((float)(system_used_bytes + info.used_storage_bytes) / info.total_storage_bytes) * 100.0f;
  else
    info.storage_usage_percent = 0.0f;
}

MemoryInfo get_memory_info() {
  MemoryInfo m;
  m.total = ESP.getHeapSize();
  m.free = ESP.getFreeHeap();
  m.used = m.total - m.free;
  if (m.total > 0)
    m.usage_percent = ((float)m.used / m.total) * 100.0f;
  else
    m.usage_percent = 0.0f;
  return m;
}

DeviceInfo get_device_info_all() {
  DeviceInfo info;
  info.unique_id = get_unique_id();
  info.platform = get_platform();
  info.os_version = get_os_version();
  info.cpu_frequency_mhz = get_cpu_frequency_mhz();
  info.cpu_temperature = get_cpu_temperature();

  get_storage_info(info);

  MemoryInfo mem = get_memory_info();
  info.total_memory_bytes = mem.total;
  info.used_memory_bytes  = mem.used;
  info.free_memory_bytes  = mem.free;
  info.memory_usage_percent = mem.usage_percent;

  info.uptime_seconds = millis() / 1000;

  // 获得芯片复位原因
  esp_reset_reason_t reason = esp_reset_reason();

  // 按照你的 mircopython 代码分档（0=power on，1=WDT，其它9）
  if (reason == ESP_RST_POWERON)
    info.reset_reason = 0;
  else if (reason == ESP_RST_WDT)
    info.reset_reason = 1;
  else
    info.reset_reason = 9;

  return info;
}

StaticJsonDocument<1024> all_device_info() {
  DeviceInfo d_info = get_device_info_all();
  NetInfo net_info = get_network_info();

  StaticJsonDocument<1024> doc;
  doc["unique_id"] = d_info.unique_id;
  doc["platform"] = d_info.platform;
  doc["os_version"] = d_info.os_version;
  doc["cpu_frequency_mhz"] = d_info.cpu_frequency_mhz;
  doc["cpu_temperature"] = d_info.cpu_temperature;
  doc["total_storage_bytes"] = d_info.total_storage_bytes;
  doc["used_storage_bytes"] = d_info.used_storage_bytes;
  doc["free_storage_bytes"] = d_info.free_storage_bytes;
  doc["storage_usage_percent"] = d_info.storage_usage_percent;
  doc["total_memory_bytes"] = d_info.total_memory_bytes;
  doc["used_memory_bytes"] = d_info.used_memory_bytes;
  doc["free_memory_bytes"] = d_info.free_memory_bytes;
  doc["memory_usage_percent"] = d_info.memory_usage_percent;
  doc["uptime_seconds"] = d_info.uptime_seconds;
  doc["reset_reason"] = d_info.reset_reason;
  
  doc["ip"] = net_info.ip;
  doc["subnet"] = net_info.subnet;
  doc["gateway"] = net_info.gateway;
  doc["dns"] = net_info.dns;
  doc["rssi"] = net_info.rssi;
  doc["mac"] = net_info.mac;

  // Publish the JSON to MQTT (fill in your MQTT publish logic here)
  // String jsonStr;
  // serializeJson(doc, jsonStr);
  // mqttClient.publish("device/info", jsonStr.c_str()); // example
  return doc;
}
