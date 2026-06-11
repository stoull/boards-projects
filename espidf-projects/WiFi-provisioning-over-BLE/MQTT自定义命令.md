# MQTT 自定义命令说明

设备在 WiFi 获取 IP 并成功连接 MQTT Broker 后，可通过 MQTT 下发与 **蓝牙 BluFi CUSTOM_DATA** 相同的 JSON 命令，响应发布到独立 Topic。

---

## Topic 规则

| 方向 | Topic 格式 | 示例（SN = `HUT000001`） |
|------|-----------|--------------------------|
| 下发命令 | `device/{sn}/cmd` | `device/HUT000001/cmd` |
| 接收响应 | `device/{sn}/resp` | `device/HUT000001/resp` |

- `{sn}` 为设备当前序列号（与蓝牙广播名一致，默认 `ESP0000000`）
- MQTT 连接成功后，设备**自动订阅** `device/{sn}/cmd`（QoS 1）
- 所有命令响应发布到 `device/{sn}/resp`（QoS 1）
- 修改 SN（`set_sn`）后，设备会自动切换订阅/响应 Topic

---

## 前置条件

1. **WiFi 已连接并获取 IP** — MQTT 服务仅在此时启动
2. **Broker 已配置** — 见下文「Broker 配置」
3. **Topic 中的 SN 与设备一致** — SN 不对则设备收不到命令

---

## Broker 配置

### 编译时默认（`src/device/device_config.h`）

```c
#define MQTT_BROKER_HOST_DEFAULT "broker.emqx.io"
#define MQTT_BROKER_PORT_DEFAULT 1883
#define MQTT_USERNAME_DEFAULT ""
#define MQTT_PASSWORD_DEFAULT ""
```

### 运行时配置

可通过蓝牙或 MQTT 命令写入 NVS（重启后仍有效）：

```json
{"cmd":"set_mqtt_config","host":"broker.emqx.io","port":1883,"username":"","password":""}
```

查询当前配置：

```json
{"cmd":"get_mqtt_config"}
```

---

## 消息格式

与蓝牙 CUSTOM_DATA 相同：**UTF-8 JSON**，必须包含 `"cmd"` 字段。

**成功响应示例：**

```json
{"ok": true, "cmd": "get_info", ...}
```

**失败响应示例：**

```json
{"ok": false, "cmd": "...", "error": "错误码", "message": "说明"}
```

---

## 支持的命令

| cmd | 说明 |
|-----|------|
| `get_info` | 查询 SN、固件版本、运行时长、WiFi/BLE/MQTT 状态 |
| `set_sn` | 写入设备 SN（同时更新蓝牙名） |
| `reboot` | 重启设备 |
| `factory_reset` | 恢复出厂（擦除 NVS 后重启，已写入 SN 会保留） |
| `get_mqtt_config` | 查询 MQTT Broker 配置 |
| `set_mqtt_config` | 设置 MQTT Broker 配置 |
| `mqtt_publish` | 向指定 Topic 发布消息 |
| `mqtt_subscribe` | 订阅指定 Topic |
| `mqtt_unsubscribe` | 取消订阅指定 Topic |

---

## 使用示例（mosquitto）

以下示例 Broker 为 `broker.emqx.io`，设备 SN 为 `HUT000001`。

**1. 先订阅响应 Topic（建议先启动）：**

```bash
mosquitto_sub -d -h broker.emqx.io -t "device/HUT000001/resp"
```

**2. 发送查询设备信息：**

```bash
mosquitto_pub -d -h broker.emqx.io -t "device/HUT000001/cmd" -m '{"cmd":"get_info"}'
```

**响应示例：**

```json
{
  "ok": true,
  "cmd": "get_info",
  "sn": "HUT000001",
  "fw_version": "1.1.0",
  "uptime_ms": 123456,
  "wifi": {"state": "online", "connected": true, "got_ip": true, "connecting": false, "ssid": "MyWiFi"},
  "ble": {"enabled": false, "connected": false},
  "force_provisioning": false,
  "mqtt": {"state": "connected", "configured": true, "connected": true, "host": "broker.emqx.io", "port": 1883}
}
```

**3. 设置 MQTT Broker：**

```bash
mosquitto_pub -h broker.emqx.io -t "device/HUT000001/cmd" \
  -m '{"cmd":"set_mqtt_config","host":"broker.emqx.io","port":1883}'
```

**4. 向其他 Topic 发布消息：**

```bash
mosquitto_pub -h broker.emqx.io -t "device/HUT000001/cmd" \
  -m '{"cmd":"mqtt_publish","topic":"sensor/temp","payload":"25.6","qos":0}'
```

**5. 订阅其他 Topic：**

```bash
mosquitto_pub -h broker.emqx.io -t "device/HUT000001/cmd" \
  -m '{"cmd":"mqtt_subscribe","topic":"sensor/alert","qos":1}'
```

---

## 其他说明

- **默认 SN**：未写入时为 `ESP0000000`，此时 Topic 应为 `device/ESP0000000/cmd` 和 `device/ESP0000000/resp`
- **蓝牙与 MQTT 并行**：两条通道命令协议相同；MQTT 响应只发到 `resp` Topic，不走蓝牙
- **第三方 Topic 消息**：通过 `mqtt_subscribe` 订阅的其他 Topic，若蓝牙已连接，会以 `{"event":"mqtt_message","topic":"...","payload":"..."}` 转发到蓝牙 CUSTOM_DATA

---

## 相关源码

| 文件 | 说明 |
|------|------|
| `src/mqtt/mqtt_service.c/h` | MQTT 连接、Topic 订阅、响应发布 |
| `src/mqtt/mqtt_config.c/h` | Broker 配置（NVS） |
| `src/blufi/blufi_custom_cmd.c/h` | JSON 命令解析与处理（蓝牙/MQTT 共用） |
| `src/device/device_config.h` | 默认 SN、Broker 配置 |

蓝牙侧命令详细说明见项目 `ReadMe.md` 中「CUSTOM_DATA」章节。
