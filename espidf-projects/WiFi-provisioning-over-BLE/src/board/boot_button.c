#include "boot_button.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boot_button";

static boot_button_handlers_t s_handlers;
static TaskHandle_t s_task;

static bool is_pressed(void)
{
    return gpio_get_level(BOARD_BOOT_BTN_GPIO) == 0;
}

static void boot_button_task(void *arg)
{
    bool stable_pressed = false;
    bool last_raw = false;
    int64_t press_start_us = 0;
    bool long_3s_fired = false;
    bool long_10s_fired = false;
    int64_t last_change_us = 0;

    while (true) {
        bool raw = is_pressed();
        int64_t now = esp_timer_get_time();

        if (raw != last_raw) {
            last_change_us = now;
            last_raw = raw;
        }

        if ((now - last_change_us) >= (int64_t)BOARD_BTN_DEBOUNCE_MS * 1000) {
            if (raw && !stable_pressed) {
                stable_pressed = true;
                press_start_us = now;
                long_3s_fired = false;
                long_10s_fired = false;
            } else if (!raw && stable_pressed) {
                int64_t held_ms = (now - press_start_us) / 1000;
                if (!long_3s_fired && !long_10s_fired && held_ms >= BOARD_BTN_DEBOUNCE_MS &&
                    held_ms < BOARD_BTN_LONG_PRESS_3S_MS && s_handlers.on_short_press) {
                    ESP_LOGI(TAG, "Short press");
                    s_handlers.on_short_press();
                }
                stable_pressed = false;
            } else if (raw && stable_pressed) {
                int64_t held_ms = (now - press_start_us) / 1000;
                if (!long_3s_fired && held_ms >= BOARD_BTN_LONG_PRESS_3S_MS) {
                    long_3s_fired = true;
                    if (s_handlers.on_long_press_3s) {
                        ESP_LOGI(TAG, "Long press 3s");
                        s_handlers.on_long_press_3s();
                    }
                }
                if (!long_10s_fired && held_ms >= BOARD_BTN_LONG_PRESS_10S_MS) {
                    long_10s_fired = true;
                    if (s_handlers.on_long_press_10s) {
                        ESP_LOGI(TAG, "Long press 10s");
                        s_handlers.on_long_press_10s();
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void boot_button_init(const boot_button_handlers_t *handlers)
{
    if (handlers) {
        s_handlers = *handlers;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    xTaskCreate(boot_button_task, "boot_button", 3072, NULL, 5, &s_task);
    ESP_LOGI(TAG, "BOOT button init on GPIO%d", BOARD_BOOT_BTN_GPIO);
}
