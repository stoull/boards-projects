#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#endif

#include "esp_blufi_api.h"
#include "blufi_example.h"
#include "device_config.h"
#include "esp_blufi.h"
#include "status_led.h"
#include "boot_button.h"
#include "board_config.h"
#include "blufi_custom_cmd.h"
#include "device_sn.h"

#define INVALID_REASON 255
#define INVALID_RSSI   -128
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

static wifi_config_t sta_config;

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static uint8_t wifi_retry = 0;

static bool sta_connected = false;
static bool sta_got_ip = false;
static bool ble_is_connected = false;
static bool ble_enabled = false;
static bool blufi_host_ready = false;
static bool force_provisioning_mode = false;
static uint8_t sta_bssid[6];
static uint8_t sta_ssid[32];
static int sta_ssid_len;
static wifi_sta_list_t sta_list;
static bool sta_is_connecting = false;
static esp_blufi_extra_info_t sta_conn_info;
static esp_timer_handle_t ble_adv_idle_timer;

static void ble_stop_all(void);
static void app_update_led(void);

static void ble_adv_idle_timer_stop(void)
{
    if (ble_adv_idle_timer) {
        esp_timer_stop(ble_adv_idle_timer);
    }
}

static void ble_adv_idle_timeout_cb(void *arg)
{
    if (ble_enabled && !ble_is_connected) {
        ble_enabled = false;
        ble_stop_all();
        app_update_led();
        BLUFI_INFO("BLE auto-off: no connection within %d s",
                   BOARD_BLE_ADV_IDLE_TIMEOUT_MS / 1000);
    }
}

static void ble_adv_idle_timer_start(void)
{
    if (!ble_adv_idle_timer || !ble_enabled || ble_is_connected) {
        return;
    }
    ble_adv_idle_timer_stop();
    esp_timer_start_once(ble_adv_idle_timer,
                         (uint64_t)BOARD_BLE_ADV_IDLE_TIMEOUT_MS * 1000);
}

static void app_update_led(void)
{
    status_led_update(ble_enabled, ble_is_connected, sta_got_ip);
}

static void ble_start_advertising(void)
{
    if (blufi_host_ready && ble_enabled && !ble_is_connected) {
        esp_blufi_adv_start();
        ble_adv_idle_timer_start();
        BLUFI_INFO("BLE advertising started (%d s idle timeout)",
                   BOARD_BLE_ADV_IDLE_TIMEOUT_MS / 1000);
    }
}

static void ble_stop_all(void)
{
    ble_adv_idle_timer_stop();
    if (ble_is_connected) {
        esp_blufi_disconnect();
    } else if (blufi_host_ready) {
        esp_blufi_adv_stop();
    }
}

static void record_wifi_conn_info(int rssi, uint8_t reason)
{
    memset(&sta_conn_info, 0, sizeof(esp_blufi_extra_info_t));
    if (sta_is_connecting) {
        sta_conn_info.sta_max_conn_retry_set = true;
        sta_conn_info.sta_max_conn_retry = WIFI_CONNECTION_MAXIMUM_RETRY;
    } else {
        sta_conn_info.sta_conn_rssi_set = true;
        sta_conn_info.sta_conn_rssi = rssi;
        sta_conn_info.sta_conn_end_reason_set = true;
        sta_conn_info.sta_conn_end_reason = reason;
    }
}

static void wifi_connect(void)
{
    wifi_retry = 0;
    sta_is_connecting = (esp_wifi_connect() == ESP_OK);
    record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
}

static bool wifi_reconnect(void)
{
    if (force_provisioning_mode) {
        return false;
    }
    if (sta_is_connecting && wifi_retry++ < WIFI_CONNECTION_MAXIMUM_RETRY) {
        BLUFI_INFO("WiFi reconnect attempt %d/%d", wifi_retry, WIFI_CONNECTION_MAXIMUM_RETRY);
        sta_is_connecting = (esp_wifi_connect() == ESP_OK);
        record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
        return true;
    }
    return false;
}

static int softap_get_current_connection_number(void)
{
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        return sta_list.num;
    }
    return 0;
}

static void send_wifi_status_report(void)
{
    wifi_mode_t mode;
    esp_blufi_extra_info_t info;

    if (!ble_is_connected) {
        return;
    }

    esp_wifi_get_mode(&mode);

    if (sta_connected) {
        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
        memcpy(info.sta_bssid, sta_bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = sta_ssid;
        info.sta_ssid_len = sta_ssid_len;
        esp_blufi_send_wifi_conn_report(mode, sta_got_ip ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP,
                                        softap_get_current_connection_number(), &info);
    } else if (sta_is_connecting) {
        esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(),
                                        &sta_conn_info);
    } else {
        esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(),
                                        &sta_conn_info);
    }
}

static bool wifi_has_saved_credentials(void)
{
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        return false;
    }
    return cfg.sta.ssid[0] != '\0';
}

static void enter_force_provisioning_mode(void)
{
    force_provisioning_mode = true;
    ble_enabled = true;

    sta_is_connecting = false;
    esp_wifi_disconnect();

    ble_start_advertising();
    app_update_led();
    BLUFI_INFO("Force provisioning mode: WiFi disconnected, BLE enabled");
}

static esp_err_t apply_device_sn(const char *sn)
{
    esp_err_t err = device_sn_set(sn);
    if (err != ESP_OK) {
        return err;
    }

    if (blufi_host_ready) {
        esp_blufi_update_ble_device_name(device_sn_get());
        if (ble_enabled && !ble_is_connected) {
            esp_blufi_adv_stop();
            esp_blufi_adv_start();
            ble_adv_idle_timer_start();
        }
    }
    return ESP_OK;
}

static void device_reboot(void)
{
    BLUFI_INFO("Reboot requested via custom command");
    esp_restart();
}

static void factory_reset_and_reboot(void)
{
    BLUFI_INFO("Factory reset: erasing NVS (SN preserved if written) and rebooting");
    esp_wifi_disconnect();
    ble_stop_all();

    esp_err_t err = device_sn_factory_reset_preserve();
    if (err != ESP_OK) {
        BLUFI_ERROR("Factory reset failed: %s", esp_err_to_name(err));
    }
    esp_restart();
}

static void handle_custom_data(esp_blufi_cb_param_t *param)
{
    char ssid_buf[33] = {0};
    if (sta_ssid_len > 0) {
        memcpy(ssid_buf, sta_ssid, sta_ssid_len < 32 ? sta_ssid_len : 32);
    }

    blufi_custom_status_t status = {
        .ble_enabled = ble_enabled,
        .ble_connected = ble_is_connected,
        .wifi_connected = sta_connected,
        .wifi_got_ip = sta_got_ip,
        .wifi_connecting = sta_is_connecting,
        .force_provisioning = force_provisioning_mode,
        .wifi_ssid = ssid_buf[0] ? ssid_buf : NULL,
    };

    blufi_custom_cmd_handle(param->custom_data.data, param->custom_data.data_len,
                            &status, device_reboot, factory_reset_and_reboot, apply_device_sn);
}

static void on_boot_button_short_press(void)
{
    ble_enabled = !ble_enabled;
    if (ble_enabled) {
        ble_start_advertising();
        BLUFI_INFO("BLE enabled by button");
    } else {
        ble_stop_all();
        BLUFI_INFO("BLE disabled by button");
    }
    app_update_led();
}

static void on_boot_button_long_press_3s(void)
{
    enter_force_provisioning_mode();
}

static void on_boot_button_long_press_10s(void)
{
    enter_force_provisioning_mode();
    factory_reset_and_reboot();
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    sta_got_ip = true;
    force_provisioning_mode = false;
    send_wifi_status_report();
    app_update_led();
    BLUFI_INFO("Got IP, WiFi connected to %.*s", sta_ssid_len, sta_ssid);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        if (!force_provisioning_mode && wifi_has_saved_credentials()) {
            BLUFI_INFO("Found saved WiFi credentials, connecting...");
            wifi_connect();
        } else {
            BLUFI_INFO("Waiting for BluFi provisioning");
        }
        break;

    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        sta_connected = true;
        sta_is_connecting = false;
        memcpy(sta_bssid, event->bssid, 6);
        memcpy(sta_ssid, event->ssid, event->ssid_len);
        sta_ssid_len = event->ssid_len;
        app_update_led();
        BLUFI_INFO("Connected to AP %.*s", event->ssid_len, event->ssid);
        break;
    }

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;

        if (!sta_connected && !wifi_reconnect()) {
            sta_is_connecting = false;
            record_wifi_conn_info(disconnected->rssi, disconnected->reason);
            send_wifi_status_report();
            BLUFI_INFO("WiFi connect failed, reason=%d", disconnected->reason);
        }

        sta_connected = false;
        sta_got_ip = false;
        memset(sta_ssid, 0, sizeof(sta_ssid));
        memset(sta_bssid, 0, sizeof(sta_bssid));
        sta_ssid_len = 0;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        app_update_led();
        break;
    }

    case WIFI_EVENT_SCAN_DONE: {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count == 0) {
            BLUFI_INFO("No AP found");
            break;
        }

        wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (!ap_list) {
            esp_wifi_clear_ap_list();
            break;
        }

        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

        esp_blufi_ap_record_t *blufi_ap_list = malloc(ap_count * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            free(ap_list);
            break;
        }

        for (int i = 0; i < ap_count; ++i) {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }

        if (ble_is_connected) {
            esp_blufi_send_wifi_list(ap_count, blufi_ap_list);
            BLUFI_INFO("Sent %d WiFi APs to phone", ap_count);
        }

        esp_wifi_scan_stop();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }

    default:
        break;
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_blufi_callbacks_t blufi_callbacks = {
    .event_cb = blufi_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

static void blufi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        blufi_host_ready = true;
        BLUFI_INFO("BluFi init done, advertising as '%s'", device_sn_get());
        ble_start_advertising();
        app_update_led();
        break;

    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        blufi_host_ready = false;
        BLUFI_INFO("BluFi deinit done");
        break;

    case ESP_BLUFI_EVENT_BLE_CONNECT:
        BLUFI_INFO("Phone connected via BLE");
        ble_is_connected = true;
        ble_adv_idle_timer_stop();
        esp_blufi_adv_stop();
        blufi_security_init();
        app_update_led();
        break;

    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("Phone disconnected from BLE");
        ble_is_connected = false;
        blufi_security_deinit();
        ble_start_advertising();
        app_update_led();
        break;

    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        ESP_ERROR_CHECK(esp_wifi_set_mode(param->wifi_mode.op_mode));
        break;

    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("Phone requested WiFi connect");
        force_provisioning_mode = false;
        esp_wifi_disconnect();
        wifi_connect();
        break;

    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        esp_wifi_disconnect();
        break;

    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("BluFi error: %d", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_STATUS:
        send_wifi_status_report();
        break;

    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        esp_blufi_disconnect();
        break;

    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        break;

    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param->sta_ssid.ssid_len >= sizeof(sta_config.sta.ssid)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(sta_config.sta.ssid, 0, sizeof(sta_config.sta.ssid));
        memcpy(sta_config.sta.ssid, param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Received SSID: %s", sta_config.sta.ssid);
        break;

    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param->sta_passwd.passwd_len >= sizeof(sta_config.sta.password)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(sta_config.sta.password, 0, sizeof(sta_config.sta.password));
        memcpy(sta_config.sta.password, param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.threshold.authmode = WIFI_SCAN_AUTH_MODE_THRESHOLD;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Received password, saved to NVS");
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        wifi_scan_config_t scan_conf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false,
        };
        esp_err_t ret = esp_wifi_scan_start(&scan_conf, true);
        if (ret != ESP_OK) {
            esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        } else {
            BLUFI_INFO("WiFi scan started");
        }
        break;
    }

    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        handle_custom_data(param);
        break;

    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(device_sn_init());

    status_led_init();

    BLUFI_INFO("Device SN (BLE name): %s", device_sn_get());

    initialise_wifi();

    ble_enabled = !wifi_has_saved_credentials();
    if (ble_enabled) {
        BLUFI_INFO("No saved WiFi, BLE enabled for provisioning");
    } else {
        BLUFI_INFO("Saved WiFi found, BLE off (short-press BOOT to enable)");
    }
    app_update_led();

    boot_button_init(&(boot_button_handlers_t){
        .on_short_press = on_boot_button_short_press,
        .on_long_press_3s = on_boot_button_long_press_3s,
        .on_long_press_10s = on_boot_button_long_press_10s,
    });

    ESP_ERROR_CHECK(esp_timer_create(&(esp_timer_create_args_t){
        .callback = ble_adv_idle_timeout_cb,
        .name = "ble_adv_idle",
    }, &ble_adv_idle_timer));

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    ret = esp_blufi_controller_init();
    if (ret != ESP_OK) {
        BLUFI_ERROR("BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }
#endif

    ret = esp_blufi_host_and_cb_init(&blufi_callbacks);
    if (ret != ESP_OK) {
        BLUFI_ERROR("BluFi init failed: %s", esp_err_to_name(ret));
        return;
    }

    BLUFI_INFO("BluFi version 0x%04x, ready for EspBlufi app", esp_blufi_get_version());
}
