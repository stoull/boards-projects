#pragma once

typedef void (*boot_button_cb_t)(void);

typedef struct {
    boot_button_cb_t on_short_press;
    boot_button_cb_t on_long_press_3s;
    boot_button_cb_t on_long_press_10s;
} boot_button_handlers_t;

void boot_button_init(const boot_button_handlers_t *handlers);
