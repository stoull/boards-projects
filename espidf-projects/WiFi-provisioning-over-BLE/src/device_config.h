#pragma once

/* 未写入 NVS 时使用的默认序列号（蓝牙广播名） */
#define DEVICE_SN_DEFAULT "ESP0000000"

#define FIRMWARE_VERSION "1.1.0"

#define WIFI_CONNECTION_MAXIMUM_RETRY 5

/* MQTT Broker 默认配置（可通过 NVS 或蓝牙 set_mqtt_config 覆盖） */
#define MQTT_BROKER_HOST_DEFAULT "broker.emqx.io"
#define MQTT_BROKER_PORT_DEFAULT 1883
#define MQTT_USERNAME_DEFAULT ""
#define MQTT_PASSWORD_DEFAULT ""
