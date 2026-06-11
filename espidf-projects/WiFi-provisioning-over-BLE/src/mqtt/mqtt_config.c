#include "mqtt_config.h"

#include "device_config.h"

#include "esp_log.h"
#include "nvs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt_config";
static const char *NVS_NAMESPACE = "device_cfg";
static const char *NVS_KEY_HOST = "mqtt_host";
static const char *NVS_KEY_PORT = "mqtt_port";
static const char *NVS_KEY_USER = "mqtt_user";
static const char *NVS_KEY_PASS = "mqtt_pass";

static mqtt_config_t s_config;

static void load_defaults(void)
{
    memset(&s_config, 0, sizeof(s_config));
    strncpy(s_config.host, MQTT_BROKER_HOST_DEFAULT, sizeof(s_config.host) - 1);
    s_config.port = MQTT_BROKER_PORT_DEFAULT;
    strncpy(s_config.username, MQTT_USERNAME_DEFAULT, sizeof(s_config.username) - 1);
    strncpy(s_config.password, MQTT_PASSWORD_DEFAULT, sizeof(s_config.password) - 1);
}

static bool is_valid_host_char(char c)
{
    return isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_';
}

static bool is_valid_host(const char *host)
{
    if (!host || host[0] == '\0') {
        return false;
    }
    size_t len = strlen(host);
    if (len > MQTT_HOST_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!is_valid_host_char(host[i])) {
            return false;
        }
    }
    return true;
}

static bool is_valid_credential(const char *value, size_t max_len)
{
    if (!value) {
        return true;
    }
    return strlen(value) <= max_len;
}

esp_err_t mqtt_config_init(void)
{
    load_defaults();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved MQTT config, using defaults");
        return ESP_OK;
    }

    size_t len = sizeof(s_config.host);
    err = nvs_get_str(handle, NVS_KEY_HOST, s_config.host, &len);
    if (err == ESP_OK) {
        uint16_t port = 0;
        if (nvs_get_u16(handle, NVS_KEY_PORT, &port) == ESP_OK && port > 0) {
            s_config.port = port;
        }

        len = sizeof(s_config.username);
        nvs_get_str(handle, NVS_KEY_USER, s_config.username, &len);

        len = sizeof(s_config.password);
        nvs_get_str(handle, NVS_KEY_PASS, s_config.password, &len);

        if (s_config.host[0] == '\0') {
            strncpy(s_config.host, MQTT_BROKER_HOST_DEFAULT, sizeof(s_config.host) - 1);
        }

        ESP_LOGI(TAG, "loaded MQTT config: %s:%u", s_config.host, s_config.port);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "MQTT config not written, using defaults");
    } else {
        ESP_LOGW(TAG, "read MQTT config failed (%s), using defaults", esp_err_to_name(err));
        load_defaults();
    }

    nvs_close(handle);
    return ESP_OK;
}

const mqtt_config_t *mqtt_config_get(void)
{
    return &s_config;
}

bool mqtt_config_is_configured(void)
{
    return s_config.host[0] != '\0';
}

esp_err_t mqtt_config_set(const mqtt_config_t *cfg)
{
    if (!cfg || !is_valid_host(cfg->host)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->port == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_valid_credential(cfg->username, MQTT_USER_MAX_LEN) ||
        !is_valid_credential(cfg->password, MQTT_PASS_MAX_LEN)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_HOST, cfg->host);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, NVS_KEY_PORT, cfg->port);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_USER, cfg->username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, cfg->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    s_config = *cfg;
    ESP_LOGI(TAG, "MQTT config saved: %s:%u", s_config.host, s_config.port);
    return ESP_OK;
}
