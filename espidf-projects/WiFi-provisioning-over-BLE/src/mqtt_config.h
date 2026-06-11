#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define MQTT_HOST_MAX_LEN   64
#define MQTT_USER_MAX_LEN   32
#define MQTT_PASS_MAX_LEN   64

typedef struct {
    char host[MQTT_HOST_MAX_LEN + 1];
    uint16_t port;
    char username[MQTT_USER_MAX_LEN + 1];
    char password[MQTT_PASS_MAX_LEN + 1];
} mqtt_config_t;

esp_err_t mqtt_config_init(void);
const mqtt_config_t *mqtt_config_get(void);
bool mqtt_config_is_configured(void);
esp_err_t mqtt_config_set(const mqtt_config_t *cfg);
