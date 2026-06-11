#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ble_enabled;
    bool ble_connected;
    bool wifi_connected;
    bool wifi_got_ip;
    bool wifi_connecting;
    bool force_provisioning;
    const char *wifi_ssid;
} blufi_custom_status_t;

typedef void (*blufi_custom_action_fn_t)(void);

void blufi_custom_cmd_handle(const uint8_t *data, uint32_t len,
                             const blufi_custom_status_t *status,
                             blufi_custom_action_fn_t reboot,
                             blufi_custom_action_fn_t factory_reset);
