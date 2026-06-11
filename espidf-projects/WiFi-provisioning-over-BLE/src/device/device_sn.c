#include "device_sn.h"

#include "device_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <string.h>

static const char *TAG = "device_sn";
static const char *NVS_NAMESPACE = "device_cfg";
static const char *NVS_KEY_SN = "sn";

static char s_device_sn[DEVICE_SN_MAX_LEN + 1];

static bool is_valid_sn_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

static bool is_valid_sn(const char *sn)
{
    if (!sn) {
        return false;
    }
    size_t len = strlen(sn);
    if (len == 0 || len > DEVICE_SN_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!is_valid_sn_char(sn[i])) {
            return false;
        }
    }
    return true;
}

esp_err_t device_sn_init(void)
{
    strncpy(s_device_sn, DEVICE_SN_DEFAULT, sizeof(s_device_sn) - 1);
    s_device_sn[sizeof(s_device_sn) - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved SN, using default '%s'", s_device_sn);
        return ESP_OK;
    }

    size_t len = sizeof(s_device_sn);
    err = nvs_get_str(handle, NVS_KEY_SN, s_device_sn, &len);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "SN not written, using default '%s'", s_device_sn);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read SN failed (%s), using default", esp_err_to_name(err));
        strncpy(s_device_sn, DEVICE_SN_DEFAULT, sizeof(s_device_sn) - 1);
        return ESP_OK;
    }

    if (!is_valid_sn(s_device_sn)) {
        ESP_LOGW(TAG, "invalid SN in NVS, using default");
        strncpy(s_device_sn, DEVICE_SN_DEFAULT, sizeof(s_device_sn) - 1);
    } else {
        ESP_LOGI(TAG, "loaded SN '%s' from NVS", s_device_sn);
    }
    return ESP_OK;
}

const char *device_sn_get(void)
{
    return s_device_sn;
}

esp_err_t device_sn_set(const char *sn)
{
    if (!is_valid_sn(sn)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SN, sn);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    strncpy(s_device_sn, sn, sizeof(s_device_sn) - 1);
    s_device_sn[sizeof(s_device_sn) - 1] = '\0';
    ESP_LOGI(TAG, "SN saved to NVS: '%s'", s_device_sn);
    return ESP_OK;
}

esp_err_t device_sn_factory_reset_preserve(void)
{
    char backup[DEVICE_SN_MAX_LEN + 1] = {0};
    bool preserve = false;

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(backup);
        if (nvs_get_str(handle, NVS_KEY_SN, backup, &len) == ESP_OK && is_valid_sn(backup)) {
            preserve = true;
        }
        nvs_close(handle);
    }

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    if (preserve) {
        ESP_LOGI(TAG, "factory reset: preserving SN '%s'", backup);
        return device_sn_set(backup);
    }

    strncpy(s_device_sn, DEVICE_SN_DEFAULT, sizeof(s_device_sn) - 1);
    s_device_sn[sizeof(s_device_sn) - 1] = '\0';
    ESP_LOGI(TAG, "factory reset: no saved SN, default '%s' after reboot", DEVICE_SN_DEFAULT);
    return ESP_OK;
}
