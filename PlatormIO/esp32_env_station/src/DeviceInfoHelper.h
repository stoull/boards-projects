#ifndef DEVICE_INFO_HELPER_H
#define DEVICE_INFO_HELPER_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_spi_flash.h>
#include <FS.h>
#include <SPIFFS.h>

struct NetInfo {
  String ip;
  String subnet;
  String gateway;
  String dns;
  String rssi;
  String mac;
};

struct DeviceInfo {
  String unique_id;
  String platform;
  String os_version;
  int cpu_frequency_mhz;
  String cpu_temperature;
  uint64_t total_storage_bytes;
  uint64_t used_storage_bytes;
  uint64_t free_storage_bytes;
  float storage_usage_percent;
  uint64_t total_memory_bytes;
  uint64_t used_memory_bytes;
  uint64_t free_memory_bytes;
  float memory_usage_percent;
  uint64_t uptime_seconds;
  int reset_reason;
};

struct MemoryInfo {
  uint64_t total;
  uint64_t used;
  uint64_t free;
  float usage_percent;
};

// Function declarations
NetInfo get_network_info();
String get_unique_id();
String get_platform();
String get_os_version();
int get_cpu_frequency_mhz();
String get_cpu_temperature();
void get_storage_info(DeviceInfo &info);
MemoryInfo get_memory_info();
DeviceInfo get_device_info_all();
StaticJsonDocument<1024> all_device_info();

#endif // DEVICE_INFO_HELPER_H
