#pragma once

#include "esp_err.h"

#define DEVICE_SN_MAX_LEN 20

esp_err_t device_sn_init(void);
const char *device_sn_get(void);
esp_err_t device_sn_set(const char *sn);
esp_err_t device_sn_factory_reset_preserve(void);
