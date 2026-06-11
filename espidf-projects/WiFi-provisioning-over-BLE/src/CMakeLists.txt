idf_component_register(SRCS "main.c"
                            "blufi/blufi_security.c"
                            "blufi/blufi_init.c"
                            "blufi/blufi_custom_cmd.c"
                            "device/device_sn.c"
                            "mqtt/mqtt_config.c"
                            "mqtt/mqtt_service.c"
                            "board/status_led.c"
                            "board/boot_button.c"
                    PRIV_REQUIRES bt nvs_flash mbedtls esp_wifi esp_netif esp_event driver esp_timer json mqtt
                    INCLUDE_DIRS "." "blufi" "mqtt" "device" "board")
