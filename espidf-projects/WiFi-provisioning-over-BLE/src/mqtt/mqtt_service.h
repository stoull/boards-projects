#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    MQTT_SERVICE_STATE_IDLE = 0,
    MQTT_SERVICE_STATE_CONNECTING,
    MQTT_SERVICE_STATE_CONNECTED,
    MQTT_SERVICE_STATE_ERROR,
} mqtt_service_state_t;

typedef struct {
    mqtt_service_state_t state;
    bool configured;
    const char *last_error;
} mqtt_service_status_t;

typedef void (*mqtt_service_message_cb_t)(const char *topic, const char *payload, int payload_len);

esp_err_t mqtt_service_init(mqtt_service_message_cb_t message_cb);
void mqtt_service_start(void);
void mqtt_service_stop(void);
void mqtt_service_restart(void);

esp_err_t mqtt_service_publish(const char *topic, const char *payload, int qos, bool retain);
esp_err_t mqtt_service_subscribe(const char *topic, int qos);
esp_err_t mqtt_service_unsubscribe(const char *topic);

void mqtt_service_get_status(mqtt_service_status_t *status);

void mqtt_service_build_cmd_topic(char *buf, size_t len);
void mqtt_service_build_resp_topic(char *buf, size_t len);
bool mqtt_service_is_device_cmd_topic(const char *topic);
esp_err_t mqtt_service_publish_response(const char *json);
void mqtt_service_resubscribe_device_cmd(void);
