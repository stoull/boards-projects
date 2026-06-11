# WiFi-provisioning-over-BLE README

**概述:**

* 使用ESP32-C3 Supermini 实现蓝牙配网与自定义功能通讯 — 基于乐鑫官方 BluFi 协议，手机端可用 EspBlufi App 完成 WiFi 配网，无需自研蓝牙协议栈。
* 使用按钮进行状态控制： — BOOT 键（GPIO 9）支持短按开关蓝牙、长按 3 秒强制配网、长按 10 秒恢复出厂，均带 50ms 防抖。
* 使用灯进行状态显示： — 板载蓝灯（GPIO 8，低电平点亮）根据蓝牙与 WiFi 状态显示熄灭、慢闪、快闪、常亮或呼吸灯。
* 实现自定义 查询设备 SN、固件版本、运行状态，下发重启、恢复出厂 控制命令等自定义功能模块。 — 通过 BluFi CUSTOM_DATA 通道以 JSON 收发，可在 EspBlufi「自定义」页测试；详细协议见 `ESP32-BLE对外接口文档.md`。
* 实现通过蓝牙写入设置的SN号。

### 手机端测试（EspBlufi）

1. 安装 [EspBlufi（Android）](https://github.com/EspressifApp/EspBlufi) 或 [EspBlufi（iOS）](https://apps.apple.com/cn/app/espblufi/id1450614082) — 为乐鑫官方配网 App，支持扫描、连接、加密、扫描 WiFi、下发 SSID/密码及查看连接状态。
2. 打开蓝牙，扫描并连接名为 `DEVICE_SN` 的设备 — SN 在 `src/device/device_config.h` 中配置（默认 `ESP0000000`），须与固件中 SN 一致。
3. 在 App 中刷新 WiFi 列表，选择 SSID 并输入密码 — 设备扫描周围 2.4GHz AP 并通过蓝牙回传列表，仅支持 STA 模式配网。
4. 点击连接，App 会显示配网状态（connecting → success / fail） — 凭证写入 NVS，重启后可自动连网。


开源工程在乐鑫 GitHub 组织下，分 Android 和 iOS 两个仓库： — App 端开发可直接参考其中 BluFi 封包/解包逻辑。

* **Android仓库**：[Android - https://github.com/EspressifApp/EspBlufi](https://github.com/EspressifApp/EspBlufi) — 含完整 BluFi GATT 通信与配网流程实现。
* **iOS仓库**：[iOS - https://github.com/EspressifApp/EspBlufiForiOS](https://github.com/EspressifApp/EspBlufiForiOS) — 协议与 Android 版一致，连接后需先完成版本交换并订阅 Notify。

## 已实现 BOOT 按键与蓝灯状态指示，并接入 BluFi 逻辑。 — 物理交互与 BluFi 配网、LED 指示在同一套状态机中协同工作。

### 硬件引脚（ESP32-C3 SuperMini）

| 功能 | GPIO | 说明 |
|------|------|------|
| 蓝灯 | GPIO 8 | 低电平点亮 |
| BOOT 键 | GPIO 9 | 按下为低，内部上拉 |

引脚定义在 `src/board/board_config.h`，可按板子修改。 — GPIO 8/9 为 strapping 引脚，正常运行中作 LED/按键使用无妨，烧录时仍用 BOOT+RST 进下载模式。

### BOOT 键（50ms 防抖）

| 操作 | 行为 |
|------|------|
| **短按** | 开关蓝牙（停止广播/断开连接，或重新开始广播） |
| **长按 3 秒** | 进入强制配网：断开 WiFi、开启 BLE、LED 慢闪 |
| **长按 10 秒** | 在 3 秒行为基础上，擦除 NVS 并重启 |

按住 10 秒时，3 秒处会先进入配网模式（LED 慢闪），10 秒处再恢复出厂并重启。 — 长按过程中 3 秒与 10 秒动作依次触发，无需松手。

### 蓝灯状态

| 蓝牙 | Wi-Fi | LED |
|------|-------|-----|
| 关 | 断 | 熄灭 |
| 开（广播） | 断 | 慢闪 1s 亮 / 1s 灭 |
| 已连接 | 断 | 快闪 0.2s 亮 / 0.2s 灭 |
| 关 | 已连 | 常亮 |
| 开 | 已连 | 呼吸灯 |

上表覆盖设备主要工作状态，便于不连手机时也能从 LED 判断当前情形。

### 上电默认逻辑

- **无已保存 WiFi**：自动开蓝牙，慢闪等待配网 — 首次上电或未写入凭证时进入该模式。
- **有已保存 WiFi**：自动连网、蓝牙默认关，LED 连上后常亮；短按 BOOT 可开蓝牙（呼吸灯） — 正常运行时关闭蓝牙以降低功耗，需要调试时再手动开启。

### 新增/修改文件

- `src/board/board_config.h` — 引脚与时间参数  
- `src/board/status_led.c/h` — LED 状态机（LEDC 呼吸）  
- `src/board/boot_button.c/h` — 按键防抖与长短按  
- `src/main.c` — 应用入口，与 BluFi 集成  
- `platformio.ini` — 新增 `esp32c3_supermini` 环境 — 默认编译目标为 SuperMini，固件使用 `partitions_singleapp_large.csv` 以容纳 >1MB 的 BluFi 镜像。

### 编译烧录

```bash
pio run -e esp32c3_supermini -t upload monitor
```

**注意**：烧录时仍需使用 BOOT+RST 进入下载模式；正常运行时 BOOT 键作为功能键使用。 — 若遇体积检查失败，先执行 `pio run -t fullclean` 再重新编译。

### 两分钟无连接自动关闭。

已实现 **蓝牙广播待连接 2 分钟无连接自动关闭**。 — 用于避免设备长期保持蓝牙广播耗电；超时后可用短按 BOOT 再次开启。

## 行为说明

| 场景 | 处理 |
|------|------|
| 蓝牙开启并开始广播 | 启动 2 分钟倒计时 |
| 手机连接成功 | 停止倒计时 |
| 手机断开后重新广播 | **重新**启动 2 分钟倒计时 |
| 2 分钟内仍无连接 | 自动关闭蓝牙、停止广播，LED 恢复对应状态 |
| 短按 BOOT 手动关蓝牙 | 取消倒计时 |

超时时间由 `board_config.h` 中 `BOARD_BLE_ADV_IDLE_TIMEOUT_MS` 定义，默认 120 秒。

## 增加一些自定义功能，即CUSTOM_DATA，使用JSON格式，实现如下功能：
* 查询设备 SN、固件版本、运行状态 — 发送 `{"cmd":"get_info"}`，返回 SN、固件版本、运行时长及 WiFi/BLE 状态。
* 下发重启、恢复出厂 控制命令 — 分别发送 `{"cmd":"reboot"}` 与 `{"cmd":"factory_reset"}`，设备先 JSON 应答约 300ms 后执行。

### 协议格式

App 发送 JSON 到自定义通道，设备以 JSON 回复。 — 走 BluFi 数据帧子类型 `0x13`（CUSTOM_DATA），须先建立蓝牙连接；若已开启加密则载荷同样加密。

#### 1. 查询设备信息

**请求：**
```json
{"cmd":"get_info"}
```

**响应示例：**
```json
{
  "ok": true,
  "cmd": "get_info",
  "sn": "HUT000001",
  "fw_version": "1.0.0",
  "uptime_ms": 123456,
  "wifi": {
    "state": "online",
    "connected": true,
    "got_ip": true,
    "connecting": false,
    "ssid": "MyWiFi"
  },
  "ble": {
    "enabled": true,
    "connected": true
  },
  "force_provisioning": false
}
```

`wifi.state` 取值：`offline` / `connecting` / `connected` / `online` — 分别表示未连接、连接中、已连 AP 未获 IP、已获 IP 可上网。

#### 2. 重启设备

**请求：**
```json
{"cmd":"reboot"}
```

**响应：**
```json
{"ok":true,"cmd":"reboot","message":"rebooting"}
```

约 300ms 后执行 `esp_restart()`。 — 仅重启设备，不擦除 NVS 中已保存的 WiFi 凭证。

#### 3. 恢复出厂

**请求：**
```json
{"cmd":"factory_reset"}
```

**响应：**
```json
{"ok":true,"cmd":"factory_reset","message":"erasing nvs and rebooting"}
```

约 300ms 后擦除 NVS 并重启（与长按 BOOT 10 秒效果相同）。 — 会清除 WiFi 凭证及 NVS 数据，重启后需重新配网。

#### 错误响应

```json
{"ok":false,"error":"unknown_cmd","message":"supported: get_info, reboot, factory_reset"}
```

未知 `cmd` 或 JSON 格式错误时返回 `ok:false`，并附带 `error` 与 `message` 字段。

---

### 新增/修改文件

| 文件 | 说明 |
|------|------|
| `src/blufi/blufi_custom_cmd.c/h` | JSON 解析与命令处理 |
| `src/device/device_config.h` | 新增 `FIRMWARE_VERSION` |
| `src/main.c` | 处理 `ESP_BLUFI_EVENT_RECV_CUSTOM_DATA` |

固件版本在 `device_config.h` 中修改 `FIRMWARE_VERSION` 即可。 — 设备 SN 同文件中的 `DEVICE_SN`，修改后需重新编译烧录。

### 测试方法

1. 蓝牙连接设备  
2. 在 EspBlufi「自定义」中发送：`{"cmd":"get_info"}`  
3. 查看返回 JSON — 重启与恢复出厂命令同样在「自定义」中发送对应 JSON 即可。

## 写入SN

已实现 **SN 写入 NVS**，统一固件烧录后可逐台写入序列号。

## 行为说明

| 情况 | 蓝牙名称 / SN |
|------|----------------|
| NVS 中未写入 SN | 默认 `ESP0000000` |
| 已通过命令写入 SN | 使用 NVS 中保存的值 |
| 恢复出厂（长按 BOOT 10s 或 `factory_reset`） | 不擦除 NVS，不会恢复为 `ESP0000000` |

SN 保存在 NVS 命名空间 `device_cfg`、键 `sn`，与 WiFi 凭证分开存储。

## 写入方式（EspBlufi 自定义）

蓝牙连接后发送：

```json
{"cmd":"set_sn","sn":"HUT000123"}
```

**成功响应：**
```json
{"ok":true,"cmd":"set_sn","sn":"HUT000123","message":"SN saved, BLE name updated"}
```

写入后会立即更新蓝牙广播名；若当前处于广播状态，会重新广播新名称。

**规则：** 1–20 字符，仅允许 `A-Z`、`a-z`、`0-9`、`-`、`_`

## 查询 SN

```json
{"cmd":"get_info"}
```

返回 JSON 中的 `sn` 字段即为当前序列号。

## 生产流程建议

1. 所有设备烧录**同一份固件**
2. 上电后蓝牙名均为 `ESP0000000`
3. 逐台连接，发送 `set_sn` 写入唯一 SN
4. 重启后仍保留，无需重新烧录
