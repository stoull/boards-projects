#include "blufi_custom_cmd.h"

#include <stdlib.h>
#include <string.h>

#include "device_config.h"
#include "device_sn.h"
#include "mqtt_config.h"
#include "mqtt_service.h"

#include "cJSON.h"
#include "esp_blufi_api.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "blufi_custom";

static const blufi_custom_channel_t *s_channel;

static void ble_send_response(void *ctx, const char *json)
{
    (void)ctx;
    if (!json) {
        return;
    }
    size_t len = strlen(json);
    if (len == 0) {
        return;
    }
    esp_err_t err = esp_blufi_send_custom_data((uint8_t *)json, (uint32_t)len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send custom data failed: %s", esp_err_to_name(err));
    }
}

static const char *mqtt_state_str(mqtt_service_state_t state)
{
    switch (state) {
    case MQTT_SERVICE_STATE_CONNECTING:
        return "connecting";
    case MQTT_SERVICE_STATE_CONNECTED:
        return "connected";
    case MQTT_SERVICE_STATE_ERROR:
        return "error";
    default:
        return "idle";
    }
}

static void send_custom_json(const char *json)
{
    if (!json || !s_channel || !s_channel->send_response) {
        return;
    }
    s_channel->send_response(s_channel->send_ctx, json);
}

static void send_error(const char *cmd, const char *error, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddBoolToObject(root, "ok", false);
    if (cmd) {
        cJSON_AddStringToObject(root, "cmd", cmd);
    }
    cJSON_AddStringToObject(root, "error", error);
    cJSON_AddStringToObject(root, "message", message);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_custom_json(out);
    cJSON_free(out);
}

static const char *wifi_state_str(const blufi_custom_status_t *st)
{
    if (st->wifi_got_ip) {
        return "online";
    }
    if (st->wifi_connecting) {
        return "connecting";
    }
    if (st->wifi_connected) {
        return "connected";
    }
    return "offline";
}

static void handle_get_info(const char *cmd, const blufi_custom_status_t *st)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddStringToObject(root, "sn", device_sn_get());
    cJSON_AddStringToObject(root, "fw_version", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000));

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "state", wifi_state_str(st));
    cJSON_AddBoolToObject(wifi, "connected", st->wifi_connected);
    cJSON_AddBoolToObject(wifi, "got_ip", st->wifi_got_ip);
    cJSON_AddBoolToObject(wifi, "connecting", st->wifi_connecting);
    if (st->wifi_ssid && st->wifi_ssid[0] != '\0') {
        cJSON_AddStringToObject(wifi, "ssid", st->wifi_ssid);
    }

    cJSON *ble = cJSON_AddObjectToObject(root, "ble");
    cJSON_AddBoolToObject(ble, "enabled", st->ble_enabled);
    cJSON_AddBoolToObject(ble, "connected", st->ble_connected);

    cJSON_AddBoolToObject(root, "force_provisioning", st->force_provisioning);

    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(mqtt, "state", mqtt_state_str(st->mqtt.state));
    cJSON_AddBoolToObject(mqtt, "configured", st->mqtt.configured);
    cJSON_AddBoolToObject(mqtt, "connected", st->mqtt.state == MQTT_SERVICE_STATE_CONNECTED);
    if (st->mqtt.last_error) {
        cJSON_AddStringToObject(mqtt, "error", st->mqtt.last_error);
    }
    if (st->mqtt.configured) {
        const mqtt_config_t *cfg = mqtt_config_get();
        cJSON_AddStringToObject(mqtt, "host", cfg->host);
        cJSON_AddNumberToObject(mqtt, "port", cfg->port);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_custom_json(out);
    cJSON_free(out);
}

static void delayed_action_task(void *arg)
{
    blufi_custom_action_fn_t action = (blufi_custom_action_fn_t)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    action();
    vTaskDelete(NULL);
}

static void schedule_action(blufi_custom_action_fn_t action)
{
    xTaskCreate(delayed_action_task, "custom_act", 2048, (void *)action, 5, NULL);
}

static void handle_reboot(const char *cmd, blufi_custom_action_fn_t reboot)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddStringToObject(root, "message", "rebooting");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_custom_json(out);
    cJSON_free(out);

    if (reboot) {
        schedule_action(reboot);
    }
}

static void handle_set_sn(const char *cmd, cJSON *root, blufi_custom_set_sn_fn_t set_sn)
{
    cJSON *sn_item = cJSON_GetObjectItem(root, "sn");
    if (!cJSON_IsString(sn_item) || !sn_item->valuestring || sn_item->valuestring[0] == '\0') {
        send_error(cmd, "missing_sn", "field 'sn' is required (1-20 chars, A-Z a-z 0-9 - _)");
        return;
    }

    if (!set_sn) {
        send_error(cmd, "not_supported", "set_sn handler not available");
        return;
    }

    esp_err_t err = set_sn(sn_item->valuestring);
    if (err == ESP_ERR_INVALID_ARG) {
        send_error(cmd, "invalid_sn", "sn must be 1-20 chars: A-Z a-z 0-9 - _");
        return;
    }
    if (err != ESP_OK) {
        send_error(cmd, "save_failed", esp_err_to_name(err));
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", cmd);
    cJSON_AddStringToObject(resp, "sn", device_sn_get());
    cJSON_AddStringToObject(resp, "message", "SN saved, BLE name updated");
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    send_custom_json(out);
    cJSON_free(out);
}

static void handle_factory_reset(const char *cmd, blufi_custom_action_fn_t factory_reset)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddStringToObject(root, "message", "erasing nvs and rebooting");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_custom_json(out);
    cJSON_free(out);

    if (factory_reset) {
        schedule_action(factory_reset);
    }
}

static void handle_get_mqtt_config(const char *cmd)
{
    const mqtt_config_t *cfg = mqtt_config_get();

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "cmd", cmd);
    cJSON_AddStringToObject(root, "host", cfg->host);
    cJSON_AddNumberToObject(root, "port", cfg->port);
    if (cfg->username[0] != '\0') {
        cJSON_AddStringToObject(root, "username", cfg->username);
    }
    cJSON_AddBoolToObject(root, "password_set", cfg->password[0] != '\0');

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    send_custom_json(out);
    cJSON_free(out);
}

static void handle_set_mqtt_config(const char *cmd, cJSON *root)
{
    cJSON *host_item = cJSON_GetObjectItem(root, "host");
    if (!cJSON_IsString(host_item) || !host_item->valuestring || host_item->valuestring[0] == '\0') {
        send_error(cmd, "missing_host", "field 'host' is required (domain or IP, max 64 chars)");
        return;
    }

    mqtt_config_t cfg = {0};
    strncpy(cfg.host, host_item->valuestring, sizeof(cfg.host) - 1);

    cJSON *port_item = cJSON_GetObjectItem(root, "port");
    if (cJSON_IsNumber(port_item) && port_item->valueint > 0) {
        cfg.port = (uint16_t)port_item->valueint;
    } else {
        cfg.port = MQTT_BROKER_PORT_DEFAULT;
    }

    cJSON *user_item = cJSON_GetObjectItem(root, "username");
    if (cJSON_IsString(user_item) && user_item->valuestring) {
        strncpy(cfg.username, user_item->valuestring, sizeof(cfg.username) - 1);
    }

    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    if (cJSON_IsString(pass_item) && pass_item->valuestring) {
        strncpy(cfg.password, pass_item->valuestring, sizeof(cfg.password) - 1);
    }

    esp_err_t err = mqtt_config_set(&cfg);
    if (err == ESP_ERR_INVALID_ARG) {
        send_error(cmd, "invalid_config", "host/port/username/password invalid");
        return;
    }
    if (err != ESP_OK) {
        send_error(cmd, "save_failed", esp_err_to_name(err));
        return;
    }

    mqtt_service_restart();

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", cmd);
    cJSON_AddStringToObject(resp, "host", cfg.host);
    cJSON_AddNumberToObject(resp, "port", cfg.port);
    cJSON_AddStringToObject(resp, "message", "MQTT config saved");
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    send_custom_json(out);
    cJSON_free(out);
}

static void handle_mqtt_publish(const char *cmd, cJSON *root)
{
    cJSON *topic_item = cJSON_GetObjectItem(root, "topic");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsString(topic_item) || !topic_item->valuestring || topic_item->valuestring[0] == '\0') {
        send_error(cmd, "missing_topic", "field 'topic' is required");
        return;
    }
    if (!cJSON_IsString(payload_item) || !payload_item->valuestring) {
        send_error(cmd, "missing_payload", "field 'payload' is required");
        return;
    }

    int qos = 0;
    cJSON *qos_item = cJSON_GetObjectItem(root, "qos");
    if (cJSON_IsNumber(qos_item)) {
        qos = qos_item->valueint;
    }

    bool retain = false;
    cJSON *retain_item = cJSON_GetObjectItem(root, "retain");
    if (cJSON_IsBool(retain_item)) {
        retain = cJSON_IsTrue(retain_item);
    }

    esp_err_t err = mqtt_service_publish(topic_item->valuestring, payload_item->valuestring, qos, retain);
    if (err == ESP_ERR_INVALID_STATE) {
        send_error(cmd, "not_connected", "MQTT not connected to broker");
        return;
    }
    if (err != ESP_OK) {
        send_error(cmd, "publish_failed", esp_err_to_name(err));
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", cmd);
    cJSON_AddStringToObject(resp, "topic", topic_item->valuestring);
    cJSON_AddStringToObject(resp, "message", "published");
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    send_custom_json(out);
    cJSON_free(out);
}

static void handle_mqtt_subscribe(const char *cmd, cJSON *root, bool subscribe)
{
    cJSON *topic_item = cJSON_GetObjectItem(root, "topic");
    if (!cJSON_IsString(topic_item) || !topic_item->valuestring || topic_item->valuestring[0] == '\0') {
        send_error(cmd, "missing_topic", "field 'topic' is required");
        return;
    }

    esp_err_t err;
    if (subscribe) {
        int qos = 0;
        cJSON *qos_item = cJSON_GetObjectItem(root, "qos");
        if (cJSON_IsNumber(qos_item)) {
            qos = qos_item->valueint;
        }
        err = mqtt_service_subscribe(topic_item->valuestring, qos);
    } else {
        err = mqtt_service_unsubscribe(topic_item->valuestring);
    }

    if (err == ESP_ERR_INVALID_STATE) {
        send_error(cmd, "not_connected", "MQTT not connected to broker");
        return;
    }
    if (err != ESP_OK) {
        send_error(cmd, subscribe ? "subscribe_failed" : "unsubscribe_failed", esp_err_to_name(err));
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return;
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", cmd);
    cJSON_AddStringToObject(resp, "topic", topic_item->valuestring);
    cJSON_AddStringToObject(resp, "message", subscribe ? "subscribed" : "unsubscribed");
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    send_custom_json(out);
    cJSON_free(out);
}

static void dispatch_command(cJSON *root, const char *cmd,
                             const blufi_custom_status_t *status,
                             blufi_custom_action_fn_t reboot,
                             blufi_custom_action_fn_t factory_reset,
                             blufi_custom_set_sn_fn_t set_sn)
{
    if (strcmp(cmd, "get_info") == 0) {
        handle_get_info(cmd, status);
    } else if (strcmp(cmd, "reboot") == 0) {
        handle_reboot(cmd, reboot);
    } else if (strcmp(cmd, "factory_reset") == 0) {
        handle_factory_reset(cmd, factory_reset);
    } else if (strcmp(cmd, "set_sn") == 0) {
        handle_set_sn(cmd, root, set_sn);
    } else if (strcmp(cmd, "get_mqtt_config") == 0) {
        handle_get_mqtt_config(cmd);
    } else if (strcmp(cmd, "set_mqtt_config") == 0) {
        handle_set_mqtt_config(cmd, root);
    } else if (strcmp(cmd, "mqtt_publish") == 0) {
        handle_mqtt_publish(cmd, root);
    } else if (strcmp(cmd, "mqtt_subscribe") == 0) {
        handle_mqtt_subscribe(cmd, root, true);
    } else if (strcmp(cmd, "mqtt_unsubscribe") == 0) {
        handle_mqtt_subscribe(cmd, root, false);
    } else {
        send_error(cmd, "unknown_cmd",
                   "supported: get_info, set_sn, reboot, factory_reset, "
                   "get_mqtt_config, set_mqtt_config, mqtt_publish, mqtt_subscribe, mqtt_unsubscribe");
    }
}

static void handle_json_str(const char *json_str,
                            const blufi_custom_status_t *status,
                            blufi_custom_action_fn_t reboot,
                            blufi_custom_action_fn_t factory_reset,
                            blufi_custom_set_sn_fn_t set_sn)
{
    if (!json_str || json_str[0] == '\0') {
        send_error(NULL, "invalid_request", "empty payload");
        return;
    }

    ESP_LOGI(TAG, "recv: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        send_error(NULL, "invalid_json", "payload is not valid JSON");
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd_item) || !cmd_item->valuestring) {
        cJSON_Delete(root);
        send_error(NULL, "missing_cmd", "field 'cmd' is required");
        return;
    }

    dispatch_command(root, cmd_item->valuestring, status, reboot, factory_reset, set_sn);
    cJSON_Delete(root);
}

void blufi_custom_cmd_handle(const uint8_t *data, uint32_t len,
                             const blufi_custom_status_t *status,
                             blufi_custom_action_fn_t reboot,
                             blufi_custom_action_fn_t factory_reset,
                             blufi_custom_set_sn_fn_t set_sn,
                             const blufi_custom_channel_t *channel)
{
    const blufi_custom_channel_t default_ble = {
        .send_response = ble_send_response,
        .send_ctx = NULL,
    };

    s_channel = channel ? channel : &default_ble;

    if (!data || len == 0) {
        send_error(NULL, "invalid_request", "empty payload");
        return;
    }

    char *json_str = malloc(len + 1);
    if (!json_str) {
        send_error(NULL, "no_memory", "out of memory");
        return;
    }
    memcpy(json_str, data, len);
    json_str[len] = '\0';

    handle_json_str(json_str, status, reboot, factory_reset, set_sn);
    free(json_str);
}
