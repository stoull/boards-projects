#include "blufi_custom_cmd.h"

#include <stdlib.h>
#include <string.h>

#include "device_config.h"

#include "cJSON.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "blufi_custom";

static void send_custom_json(const char *json)
{
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
    cJSON_AddStringToObject(root, "sn", DEVICE_SN);
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

void blufi_custom_cmd_handle(const uint8_t *data, uint32_t len,
                             const blufi_custom_status_t *status,
                             blufi_custom_action_fn_t reboot,
                             blufi_custom_action_fn_t factory_reset)
{
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

    ESP_LOGI(TAG, "recv: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
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

    const char *cmd = cmd_item->valuestring;

    if (strcmp(cmd, "get_info") == 0) {
        handle_get_info(cmd, status);
    } else if (strcmp(cmd, "reboot") == 0) {
        handle_reboot(cmd, reboot);
    } else if (strcmp(cmd, "factory_reset") == 0) {
        handle_factory_reset(cmd, factory_reset);
    } else {
        send_error(cmd, "unknown_cmd", "supported: get_info, reboot, factory_reset");
    }

    cJSON_Delete(root);
}
