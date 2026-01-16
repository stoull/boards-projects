#include "StorageUsage.h"
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "nvs.h"

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void test_SPIFFS_Usage() {
    if(!SPIFFS.begin(false)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    Serial.println("SPIFFS Info:");
    Serial.printf("Total Bytes: %u\n", SPIFFS.totalBytes());
    Serial.printf("Used Bytes: %u\n", SPIFFS.usedBytes());
    Serial.printf("Free Bytes: %u\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());

    File f = SPIFFS.open("/test.txt", FILE_WRITE);
    f.println("This is a test file");
    f.close();

    File file = SPIFFS.open("/test.txt");
    if(!file){
        Serial.println("Failed to open file for reading");
    }
    Serial.println("The content in text.txt:");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();

    listDir(SPIFFS, "/", 0);
    SPIFFS.remove("/test.txt");
}

void test_storage_usage() {
    uint32_t flash_size = ESP.getFlashChipSize();
    Serial.printf("Flash chip size: %u bytes (%u MB)\n", flash_size, flash_size / 1024 / 1024);

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    Serial.printf("NVS - Used entries: %d, Free entries: %d, Total entries: %d\n", nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    if(SPIFFS.begin()){
        uint32_t ffs_total_bytes = SPIFFS.totalBytes();
        uint32_t ffs_used_bytes = SPIFFS.usedBytes();
        Serial.printf("SPIFFS - Total: %u, Used: %u\n", ffs_total_bytes, ffs_used_bytes);

        uint32_t system_used_bytes = flash_size - ffs_total_bytes;
        float storage_usage_percent = 0.0f;
        if (flash_size > 0)
            storage_usage_percent = ((float)(ffs_used_bytes+system_used_bytes) / flash_size) * 100.0f;
        Serial.printf("Flash chip usage percent: %.2f\n", storage_usage_percent);
    }
}