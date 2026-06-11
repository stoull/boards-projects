#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mqtt_service.h"

typedef struct {
    bool ble_enabled;
    bool ble_connected;
    bool wifi_connected;
    bool wifi_got_ip;
    bool wifi_connecting;
    bool force_provisioning;
    const char *wifi_ssid;
    mqtt_service_status_t mqtt;
} blufi_custom_status_t;

typedef void (*blufi_custom_action_fn_t)(void);

typedef esp_err_t (*blufi_custom_set_sn_fn_t)(const char *sn);

typedef void (*blufi_custom_send_fn_t)(void *ctx, const char *json);

typedef struct {
    blufi_custom_send_fn_t send_response;
    void *send_ctx;
} blufi_custom_channel_t;

void blufi_custom_cmd_handle(const uint8_t *data, uint32_t len,
                             const blufi_custom_status_t *status,
                             blufi_custom_action_fn_t reboot,
                             blufi_custom_action_fn_t factory_reset,
                             blufi_custom_set_sn_fn_t set_sn,
                             const blufi_custom_channel_t *channel);
