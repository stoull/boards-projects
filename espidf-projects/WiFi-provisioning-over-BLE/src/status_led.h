#pragma once

#include <stdbool.h>

void status_led_init(void);
void status_led_update(bool ble_enabled, bool ble_connected, bool wifi_connected);
