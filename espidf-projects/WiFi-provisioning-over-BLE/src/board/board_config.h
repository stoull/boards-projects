#pragma once

/* ESP32-C3 SuperMini onboard peripherals */
#define BOARD_LED_GPIO        8   /* Blue LED, active LOW */
#define BOARD_BOOT_BTN_GPIO   9   /* BOOT button, active LOW, internal pull-up */

#define BOARD_BTN_DEBOUNCE_MS       50
#define BOARD_BTN_LONG_PRESS_3S_MS  3000
#define BOARD_BTN_LONG_PRESS_10S_MS 10000

#define BOARD_LED_SLOW_BLINK_MS     1000
#define BOARD_LED_FAST_BLINK_MS     200
#define BOARD_LED_BREATH_STEP_MS    20

#define BOARD_BLE_ADV_IDLE_TIMEOUT_MS  (2 * 60 * 1000)  /* 广播待连接超时：2 分钟 */
