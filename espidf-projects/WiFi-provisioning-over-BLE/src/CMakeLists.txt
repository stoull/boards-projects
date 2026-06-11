idf_component_register(SRCS "main.c"
                            "blufi_security.c"
                            "blufi_init.c"
                            "blufi_custom_cmd.c"
                            "status_led.c"
                            "boot_button.c"
                    PRIV_REQUIRES bt nvs_flash mbedtls esp_wifi esp_netif esp_event driver esp_timer json
                    INCLUDE_DIRS ".")
