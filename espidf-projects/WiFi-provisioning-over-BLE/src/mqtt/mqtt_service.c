#include "mqtt_service.h"

#include "device_sn.h"
#include "mqtt_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "mqtt_service";

#define MQTT_CMD_TOPIC_BUF_LEN 64
#define MQTT_DATA_QUEUE_LEN 4
#define MQTT_DATA_TASK_STACK 4096

typedef struct {
    char topic[128];
    char *payload;
    int payload_len;
} mqtt_data_msg_t;

static esp_mqtt_client_handle_t s_client;
static mqtt_service_state_t s_state = MQTT_SERVICE_STATE_IDLE;
static mqtt_service_message_cb_t s_message_cb;
static char s_last_error[64];
static bool s_wifi_ready;
static char s_cmd_topic[MQTT_CMD_TOPIC_BUF_LEN];
static char s_resp_topic[MQTT_CMD_TOPIC_BUF_LEN];
static QueueHandle_t s_data_queue;
static TaskHandle_t s_data_task;

static void mqtt_data_task(void *arg)
{
    mqtt_data_msg_t msg;

    while (1) {
        if (xQueueReceive(s_data_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_message_cb && msg.payload) {
            s_message_cb(msg.topic, msg.payload, msg.payload_len);
        }
        free(msg.payload);
    }
}

static bool enqueue_mqtt_data(esp_mqtt_event_handle_t event)
{
    if (!s_data_queue || event->topic_len <= 0 || event->data_len < 0) {
        return false;
    }

    mqtt_data_msg_t msg = {0};
    int topic_len = event->topic_len < (int)sizeof(msg.topic) - 1 ? event->topic_len : (int)sizeof(msg.topic) - 1;
    memcpy(msg.topic, event->topic, topic_len);
    msg.topic[topic_len] = '\0';
    msg.payload_len = event->data_len;

    msg.payload = malloc((size_t)event->data_len + 1);
    if (!msg.payload) {
        ESP_LOGE(TAG, "drop mqtt data: no memory");
        return false;
    }
    if (event->data_len > 0) {
        memcpy(msg.payload, event->data, event->data_len);
    }
    msg.payload[event->data_len] = '\0';

    if (xQueueSend(s_data_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "drop mqtt data: queue full");
        free(msg.payload);
        return false;
    }
    return true;
}

void mqtt_service_build_cmd_topic(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return;
    }
    snprintf(buf, len, "device/%s/cmd", device_sn_get());
}

void mqtt_service_build_resp_topic(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return;
    }
    snprintf(buf, len, "device/%s/resp", device_sn_get());
}

bool mqtt_service_is_device_cmd_topic(const char *topic)
{
    return topic && s_cmd_topic[0] != '\0' && strcmp(topic, s_cmd_topic) == 0;
}

static void refresh_device_topics(void)
{
    mqtt_service_build_cmd_topic(s_cmd_topic, sizeof(s_cmd_topic));
    mqtt_service_build_resp_topic(s_resp_topic, sizeof(s_resp_topic));
}

static void subscribe_device_cmd(void)
{
    if (!s_client || s_state != MQTT_SERVICE_STATE_CONNECTED || s_cmd_topic[0] == '\0') {
        return;
    }
    int msg_id = esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "subscribed to %s", s_cmd_topic);
    } else {
        ESP_LOGE(TAG, "subscribe %s failed", s_cmd_topic);
    }
}

void mqtt_service_resubscribe_device_cmd(void)
{
    char old_cmd[MQTT_CMD_TOPIC_BUF_LEN] = {0};
    if (s_cmd_topic[0] != '\0') {
        strncpy(old_cmd, s_cmd_topic, sizeof(old_cmd) - 1);
    }

    refresh_device_topics();

    if (!s_client || s_state != MQTT_SERVICE_STATE_CONNECTED) {
        return;
    }

    if (old_cmd[0] != '\0' && strcmp(old_cmd, s_cmd_topic) != 0) {
        esp_mqtt_client_unsubscribe(s_client, old_cmd);
    }
    subscribe_device_cmd();
}

esp_err_t mqtt_service_publish_response(const char *json)
{
    if (!json || s_resp_topic[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "publish resp -> %s (%d bytes)", s_resp_topic, (int)strlen(json));
    return mqtt_service_publish(s_resp_topic, json, 1, false);
}

static void set_state(mqtt_service_state_t state)
{
    s_state = state;
}

static void set_error(const char *msg)
{
    if (msg) {
        strncpy(s_last_error, msg, sizeof(s_last_error) - 1);
        s_last_error[sizeof(s_last_error) - 1] = '\0';
    } else {
        s_last_error[0] = '\0';
    }
}

static esp_mqtt_client_handle_t create_client(void)
{
    const mqtt_config_t *cfg = mqtt_config_get();
    char uri[128];

    snprintf(uri, sizeof(uri), "mqtt://%s:%u", cfg->host, cfg->port);

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp_%s", device_sn_get());

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = client_id,
    };

    if (cfg->username[0] != '\0') {
        mqtt_cfg.credentials.username = cfg->username;
    }
    if (cfg->password[0] != '\0') {
        mqtt_cfg.credentials.authentication.password = cfg->password;
    }

    ESP_LOGI(TAG, "connecting to %s", uri);
    return esp_mqtt_client_init(&mqtt_cfg);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to broker");
        set_state(MQTT_SERVICE_STATE_CONNECTED);
        set_error(NULL);
        refresh_device_topics();
        subscribe_device_cmd();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        if (s_wifi_ready && mqtt_config_is_configured()) {
            set_state(MQTT_SERVICE_STATE_CONNECTING);
        } else {
            set_state(MQTT_SERVICE_STATE_IDLE);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "unsubscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        if (event->topic_len > 0) {
            char topic_preview[128];
            int topic_len = event->topic_len < (int)sizeof(topic_preview) - 1 ?
                            event->topic_len : (int)sizeof(topic_preview) - 1;
            memcpy(topic_preview, event->topic, topic_len);
            topic_preview[topic_len] = '\0';
            ESP_LOGI(TAG, "recv data topic=%s len=%d", topic_preview, event->data_len);
        }
        enqueue_mqtt_data(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        set_state(MQTT_SERVICE_STATE_ERROR);
        set_error("connection error");
        break;

    default:
        break;
    }
}

esp_err_t mqtt_service_init(mqtt_service_message_cb_t message_cb)
{
    s_message_cb = message_cb;
    s_wifi_ready = false;
    set_state(MQTT_SERVICE_STATE_IDLE);
    set_error(NULL);
    refresh_device_topics();

    if (!s_data_queue) {
        s_data_queue = xQueueCreate(MQTT_DATA_QUEUE_LEN, sizeof(mqtt_data_msg_t));
        if (!s_data_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_data_task) {
        if (xTaskCreate(mqtt_data_task, "mqtt_data", MQTT_DATA_TASK_STACK, NULL, 5, &s_data_task) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "device cmd topic: %s", s_cmd_topic);
    ESP_LOGI(TAG, "device resp topic: %s", s_resp_topic);
    return ESP_OK;
}

void mqtt_service_start(void)
{
    s_wifi_ready = true;

    if (!mqtt_config_is_configured()) {
        ESP_LOGI(TAG, "MQTT broker not configured, skip start");
        set_state(MQTT_SERVICE_STATE_IDLE);
        return;
    }

    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    refresh_device_topics();

    s_client = create_client();
    if (!s_client) {
        set_state(MQTT_SERVICE_STATE_ERROR);
        set_error("init failed");
        return;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    set_state(MQTT_SERVICE_STATE_CONNECTING);
    esp_mqtt_client_start(s_client);
}

void mqtt_service_stop(void)
{
    s_wifi_ready = false;
    s_cmd_topic[0] = '\0';
    s_resp_topic[0] = '\0';

    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    set_state(MQTT_SERVICE_STATE_IDLE);
    set_error(NULL);
}

void mqtt_service_restart(void)
{
    if (!s_wifi_ready) {
        return;
    }
    mqtt_service_stop();
    s_wifi_ready = true;
    mqtt_service_start();
}

esp_err_t mqtt_service_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_client || s_state != MQTT_SERVICE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (qos < 0 || qos > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, (int)strlen(payload), qos, retain ? 1 : 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_service_subscribe(const char *topic, int qos)
{
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_client || s_state != MQTT_SERVICE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (qos < 0 || qos > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_service_unsubscribe(const char *topic)
{
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_client || s_state != MQTT_SERVICE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_unsubscribe(s_client, topic);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

void mqtt_service_get_status(mqtt_service_status_t *status)
{
    if (!status) {
        return;
    }
    status->state = s_state;
    status->configured = mqtt_config_is_configured();
    status->last_error = s_last_error[0] ? s_last_error : NULL;
}
