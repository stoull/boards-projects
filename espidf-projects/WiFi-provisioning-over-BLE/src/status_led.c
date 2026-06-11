#include "status_led.h"

#include "board_config.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "status_led";

typedef enum {
    LED_PATTERN_OFF = 0,
    LED_PATTERN_SOLID_ON,
    LED_PATTERN_SLOW_BLINK,
    LED_PATTERN_FAST_BLINK,
    LED_PATTERN_BREATHING,
} led_pattern_t;

#define LEDC_DUTY_MAX 255

static SemaphoreHandle_t s_lock;
static led_pattern_t s_pattern = LED_PATTERN_OFF;
static TaskHandle_t s_task;

static void led_apply_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void led_set_on(void)
{
    led_apply_duty(0);
}

static void led_set_off(void)
{
    led_apply_duty(LEDC_DUTY_MAX);
}

static led_pattern_t pattern_from_state(bool ble_enabled, bool ble_connected, bool wifi_connected)
{
    if (!ble_enabled && !wifi_connected) {
        return LED_PATTERN_OFF;
    }
    if (ble_enabled && !wifi_connected) {
        return ble_connected ? LED_PATTERN_FAST_BLINK : LED_PATTERN_SLOW_BLINK;
    }
    if (!ble_enabled && wifi_connected) {
        return LED_PATTERN_SOLID_ON;
    }
    return LED_PATTERN_BREATHING;
}

static void status_led_task(void *arg)
{
    bool blink_on = false;
    int breath = 0;
    int breath_dir = 1;

    while (true) {
        led_pattern_t pattern;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            pattern = s_pattern;
            xSemaphoreGive(s_lock);
        } else {
            pattern = LED_PATTERN_OFF;
        }

        switch (pattern) {
        case LED_PATTERN_OFF:
            led_set_off();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_PATTERN_SOLID_ON:
            led_set_on();
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_PATTERN_SLOW_BLINK:
            blink_on = !blink_on;
            if (blink_on) {
                led_set_on();
            } else {
                led_set_off();
            }
            vTaskDelay(pdMS_TO_TICKS(BOARD_LED_SLOW_BLINK_MS));
            break;

        case LED_PATTERN_FAST_BLINK:
            blink_on = !blink_on;
            if (blink_on) {
                led_set_on();
            } else {
                led_set_off();
            }
            vTaskDelay(pdMS_TO_TICKS(BOARD_LED_FAST_BLINK_MS));
            break;

        case LED_PATTERN_BREATHING:
            breath += breath_dir * 4;
            if (breath >= (int)LEDC_DUTY_MAX) {
                breath = LEDC_DUTY_MAX;
                breath_dir = -1;
            } else if (breath <= 0) {
                breath = 0;
                breath_dir = 1;
            }
            led_apply_duty((uint32_t)breath);
            vTaskDelay(pdMS_TO_TICKS(BOARD_LED_BREATH_STEP_MS));
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

void status_led_init(void)
{
    s_lock = xSemaphoreCreateMutex();

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = BOARD_LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = LEDC_DUTY_MAX,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    led_set_off();
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 5, &s_task);
    ESP_LOGI(TAG, "LED init on GPIO%d (active low)", BOARD_LED_GPIO);
}

void status_led_update(bool ble_enabled, bool ble_connected, bool wifi_connected)
{
    led_pattern_t pattern = pattern_from_state(ble_enabled, ble_connected, wifi_connected);

    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_pattern = pattern;
        xSemaphoreGive(s_lock);
    }
}
