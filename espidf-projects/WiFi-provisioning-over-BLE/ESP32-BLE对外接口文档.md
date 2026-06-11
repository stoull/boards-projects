# ESP32-C3 BluFi 对外接口文档

本文档面向 **手机 App 开发人员**，说明 EspBlufi App 中各功能与 ESP32 固件之间的通信接口。  
固件基于乐鑫官方 **BluFi 协议**（ESP-IDF），设备型号：**ESP32-C3 SuperMini**。

> 官方协议详解：[BluFi - ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/blufi.html)  
> 开源参考 App：[EspBlufi Android](https://github.com/EspressifApp/EspBlufi) / [EspBlufi iOS](https://github.com/EspressifApp/EspBlufiForiOS)

---

## 1. 设备发现与连接（BLE 层）

### 1.1 扫描设备

| 项目 | 说明 |
|------|------|
| 协议 | 标准 BLE 扫描 |
| 设备名称 | 固件中 `DEVICE_SN`（当前示例：`HUT000001`） |
| 广播 Service UUID | `0xFFFF` |
| 可发现条件 | 固件蓝牙已开启且处于广播状态 |

**固件行为：**

- 首次无 WiFi 凭证：上电自动开启蓝牙并广播
- 已有 WiFi 凭证：默认关闭蓝牙；用户短按 BOOT 键可重新开启
- 手机连接后：停止广播；断开后若蓝牙仍开启则恢复广播

### 1.2 建立连接（App「连接」）

| 项目 | 说明 |
|------|------|
| 连接方式 | BLE GATT Client 连接 GATT Server |
| 连接后 App 必须操作 | 发现服务 → 订阅 **E2P** 特征 Notify |

**GATT 服务定义：**

| 名称 | UUID | 属性 | 方向 |
|------|------|------|------|
| BluFi Service | `0xFFFF` | Primary Service | — |
| P2E（Phone → ESP） | `0xFF01` | Write | App 写入控制/数据帧 |
| E2P（ESP → Phone） | `0xFF02` | Read + Notify | App 订阅接收应答/上报 |

**固件行为（连接成功）：**

1. 停止 BLE 广播
2. 初始化 BluFi 安全上下文（DH 密钥协商准备）
3. 蓝灯切换为「蓝牙已连接、WiFi 未连」快闪（0.2s）

> **App 开发要点：** 连接后必须先完成 GATT 服务发现并开启 E2P Notify，否则收不到版本应答、状态上报，界面功能会全部不可用。

### 1.3 断开连接（App「断开」）

App 可通过两种方式断开：

| 方式 | BluFi 控制帧 | 固件处理 |
|------|-------------|----------|
| App 直接断开 BLE | 无需 BluFi 帧 | 固件收到 GATT 断开事件 |
| App 发送 BluFi 断开指令 | 控制帧 `0x08` DISCONNECT_BLE | 固件主动断开 GATT |

**固件行为（断开后）：**

1. 清理 BluFi 安全上下文
2. 若蓝牙仍开启：重新开始广播
3. 蓝灯恢复对应状态（如配网慢闪 / 熄灭）

---

## 2. BluFi 协议帧格式（App 必读）

App 与 ESP32 之间所有业务数据均通过 **P2E 写入**、**E2P Notify 接收**。

### 2.1 帧结构（无分包）

```
+------+-------------+------+------------+------+----------+
| Type | Frame Ctrl  | Seq  | Data Len   | Data | CheckSum |
| 1B   | 1B          | 1B   | 1B         | nB   | 2B(可选) |
+------+-------------+-------------+------+----------+
```

- **Type 低 2 位**：`0x0` = 控制帧，`0x1` = 数据帧  
- **Type 高 6 位**：子类型（见下文各功能表）  
- **Frame Ctrl**：加密位、校验位、方向位、分包位（见官方文档）  
- **Seq**：序号，需与 ACK 对应

### 2.2 控制帧子类型（App → ESP32）

| 子类型值 | 名称 | EspBlufi 功能 |
|---------|------|---------------|
| `0x00` | ACK | 应答确认 |
| `0x01` | SET_SEC_MODE | **加密**（设置安全模式） |
| `0x02` | SET_WIFI_OPMODE | **配网**（设置 WiFi 模式） |
| `0x03` | CONN_TO_AP | **配网**（请求连接 WiFi） |
| `0x04` | DISCONN_FROM_AP | 断开 WiFi |
| `0x05` | GET_WIFI_STATUS | **状态**（查询 WiFi 状态） |
| `0x07` | GET_VERSION | **版本**（获取协议版本） |
| `0x08` | DISCONNECT_BLE | **断开**（断开 BLE） |
| `0x09` | GET_WIFI_LIST | **扫描**（请求 WiFi 列表） |

### 2.3 数据帧子类型

| 子类型值 | 名称 | 方向 | EspBlufi 功能 |
|---------|------|------|---------------|
| `0x00` | NEG | 双向 | **加密**（DH 密钥协商数据） |
| `0x02` | STA_SSID | App→ESP | **配网**（下发 SSID） |
| `0x03` | STA_PASSWD | App→ESP | **配网**（下发密码） |
| `0x0F` | WIFI_REP | ESP→App | **状态**（WiFi 连接状态上报） |
| `0x10` | REPLY_VERSION | ESP→App | **版本**（版本应答） |
| `0x11` | WIFI_LIST | ESP→App | **扫描**（WiFi 列表上报） |
| `0x12` | ERROR_INFO | ESP→App | 错误上报 |

---

## 3. EspBlufi 功能与接口对照

### 3.1 版本（Version）

**App 操作：** 点击「版本」或连接后自动查询

| 项目 | 内容 |
|------|------|
| App 发送 | 控制帧 `GET_VERSION`（Type = `0x07`） |
| ESP32 应答 | 数据帧 `REPLY_VERSION`（Type = `0x10`） |
| 应答数据 | `data[0]` = 主版本，`data[1]` = 子版本 |
| 当前固件版本 | **V1.3**（`0x01`, `0x03`） |

**App 解析示例：**

```
收到 data[0]=0x01, data[1]=0x03 → 显示 "V1.3"
```

> 版本交换是 App 解锁后续功能的前提，应作为连接后第一步。

---

### 3.2 加密（Encrypt）

**App 操作：** 点击「加密」开启通信加密

加密流程（DH + AES-128-CFB + CRC16）：

```
App                          ESP32
 |  数据帧 NEG (DH参数)  -->  |
 |  <--  数据帧 NEG (DH公钥)  |
 |  (双方计算共享密钥 PSK)     |
 |  控制帧 SET_SEC_MODE  -->  |  设置校验/加密模式
 |  后续数据帧/控制帧加密传输   |
```

| 步骤 | App 发送 | ESP32 处理 |
|------|---------|-----------|
| 1. 密钥协商 | 数据帧 `NEG`（DH 参数/公钥） | `blufi_dh_negotiate_data_handler()` 计算并回传公钥 |
| 2. 设置安全模式 | 控制帧 `SET_SEC_MODE`，data[0] 低/高 4 位分别指定数据帧/控制帧安全模式 | 保存 `sec_mode`，后续帧按模式加解密 |
| 3. 加密通信 | 加密后的配网数据 | AES 解密后处理 |

**安全模式 data[0] 位定义：**

| 位 | 含义 |
|----|------|
| 低 4 位 | 数据帧：0=无校验无加密，1=仅校验，2=仅加密，3=校验+加密 |
| 高 4 位 | 控制帧：同上 |

**ESP32 错误码（ERROR_INFO）：**

| 值 | 含义 |
|----|------|
| 0x04 | 安全初始化失败 |
| 0x05 | DH 内存分配失败 |
| 0x06 | DH 参数错误 |
| 0x02 | 解密错误 |
| 0x03 | 加密错误 |

---

### 3.3 扫描（Scan WiFi 列表）

**App 操作：** 点击「扫描」获取周围 WiFi

| 项目 | 内容 |
|------|------|
| App 发送 | 控制帧 `GET_WIFI_LIST`（Type = `0x09`） |
| ESP32 处理 | 启动 `esp_wifi_scan_start()` 扫描周围 AP |
| ESP32 应答 | 数据帧 `WIFI_LIST`（Type = `0x11`），可能分包 |

**WiFi 列表单条 AP 格式：**

```
Length(1B) + RSSI(1B, int8) + SSID(Length-1 字节)
```

**App 解析示例：**

```
data: [0x08, 0xCE, 'M','y','W','i','F','i']
→ SSID="MyWiFi", RSSI=-50 dBm
```

**错误情况：**

- 扫描失败 → ESP32 上报 `ERROR_INFO`，错误码 `0x0B`（WiFi scan fail）

---

### 3.4 配网（Provision）

**App 操作：** 选择 SSID、输入密码后点击「配网 / Confirm」

标准 STA 配网顺序：

| 步骤 | App 发送 | ESP32 处理 | 持久化 |
|------|---------|-----------|--------|
| 1 | 控制帧 `SET_WIFI_OPMODE`，data=`0x01`(STA) | `esp_wifi_set_mode(WIFI_MODE_STA)` | — |
| 2 | 数据帧 `STA_SSID` | 保存 SSID 到 WiFi 配置 | NVS |
| 3 | 数据帧 `STA_PASSWD` | 保存密码到 WiFi 配置 | NVS |
| 4 | 控制帧 `CONN_TO_AP` | `esp_wifi_disconnect()` → `esp_wifi_connect()` | — |

**本固件配网参数：**

| 参数 | 值 |
|------|-----|
| WiFi 模式 | 仅 **STA 模式** |
| 认证阈值 | WPA2-PSK |
| 最大重试次数 | 5 次 |
| 凭证存储 | NVS（`WIFI_STORAGE_FLASH`） |

**配网成功后：**

- ESP32 获取 IP 后主动上报 WiFi 状态
- 退出强制配网模式
- 蓝灯：蓝牙开+WiFi 连 → 呼吸灯；蓝牙关+WiFi 连 → 常亮

> **注意：** 本固件 **不支持** SoftAP 配网相关接口（`SOFTAP_SSID/PASSWD` 等），App 请固定使用 **Station 模式**。

---

### 3.5 状态（Status）

**App 操作：** 点击「状态」查询当前 WiFi 连接情况

| 项目 | 内容 |
|------|------|
| App 发送 | 控制帧 `GET_WIFI_STATUS`（Type = `0x05`） |
| ESP32 应答 | 数据帧 `WIFI_REP`（Type = `0x0F`） |

**WIFI_REP 数据格式：**

| 偏移 | 字段 | 说明 |
|------|------|------|
| data[0] | opmode | `0x01` = STA |
| data[1] | STA 状态 | 见下表 |
| data[2] | SoftAP 连接数 | 本固件固定为 0 |
| data[3…] | 附加信息 | SSID / BSSID / 重试次数 / 失败原因 / RSSI |

**STA 连接状态（data[1]）：**

| 值 | 常量 | 含义 | App 显示建议 |
|----|------|------|-------------|
| `0x00` | STA_CONN_SUCCESS | 已连接且已获取 IP | 连接成功 |
| `0x01` | STA_CONN_FAIL | 连接失败 | 连接失败 |
| `0x02` | STA_CONNECTING | 正在连接 | 连接中… |
| `0x03` | STA_NO_IP | 已连 AP 但未获取 IP | 已连接无 IP |

**附加信息（连接失败时）：**

| 字段 | 子类型 | 说明 |
|------|--------|------|
| 最大重试次数 | `0x14` | 正在重连时附带 |
| 失败原因 | `0x15` | `wifi_err_reason_t` |
| 信号强度 | `0x16` | RSSI，无效时为 `-128` |

**ESP32 主动上报时机（无需 App 查询）：**

- 手机请求连接 WiFi 后状态变化
- 获取 IP 成功
- 连接失败且重试耗尽

---

### 3.6 连接 / 断开（汇总）

| EspBlufi 功能 | 接口层 | App 关键操作 | ESP32 响应 |
|--------------|--------|-------------|-----------|
| **连接** | BLE GATT | `connect()` + 发现服务 + 订阅 E2P Notify | 停广播、初始化安全、LED 快闪 |
| **断开** | BLE GATT | `disconnect()` | 清安全、恢复广播、更新 LED |
| **断开** | BluFi 控制帧 `0x08` | 写入 P2E | 固件主动 `esp_blufi_disconnect()` |
| **断开 WiFi** | BluFi 控制帧 `0x04` | 写入 P2E | `esp_wifi_disconnect()` |

---

## 4. 典型 App 开发流程

```
┌─────────────┐
│ 1. BLE 扫描  │  按 DEVICE_SN 过滤设备
└──────┬──────┘
       ▼
┌─────────────┐
│ 2. GATT 连接 │  连接 0xFFFF 服务
└──────┬──────┘
       ▼
┌─────────────┐
│ 3. 订阅 Notify│  E2P (0xFF02) 必须开启
└──────┬──────┘
       ▼
┌─────────────┐
│ 4. 获取版本  │  GET_VERSION → REPLY_VERSION
└──────┬──────┘
       ▼
┌─────────────┐
│ 5. 加密(可选)│  NEG 协商 + SET_SEC_MODE
└──────┬──────┘
       ▼
┌─────────────┐
│ 6. 扫描 WiFi │  GET_WIFI_LIST → WIFI_LIST
└──────┬──────┘
       ▼
┌─────────────┐
│ 7. 下发配网  │  SET_OPMODE → SSID → PASSWD → CONN_TO_AP
└──────┬──────┘
       ▼
┌─────────────┐
│ 8. 查询状态  │  GET_WIFI_STATUS → WIFI_REP
└─────────────┘
```

---

## 5. 本固件支持情况一览

| 功能 | 支持 | 备注 |
|------|------|------|
| BLE 扫描/连接/断开 | ✅ | 标准 GATT |
| 版本查询 | ✅ | V1.3 |
| DH 加密协商 | ✅ | AES-128-CFB + CRC16 |
| WiFi 扫描 | ✅ | 2.4GHz AP |
| STA 配网 | ✅ | SSID + 密码写入 NVS |
| WiFi 状态查询/上报 | ✅ | 含失败原因、RSSI |
| BLE 主动断开 | ✅ | 控制帧 0x08 |
| WiFi 断开 | ✅ | 控制帧 0x04 |
| SoftAP 配网 | ❌ | 未实现 |
| 企业级认证（证书/用户名） | ❌ | 未实现 |
| 自定义数据 | ❌ | 未实现 |

---

## 6. 设备侧物理交互（App 需知晓）

以下由 **BOOT 按键** 触发，App 无需调用，但会影响蓝牙可见性和 LED 状态：

| 操作 | 效果 |
|------|------|
| 短按 BOOT | 开关蓝牙广播 |
| 长按 3s | 强制配网：断开 WiFi、开启 BLE、LED 慢闪 |
| 长按 10s | 强制配网 + 擦除 NVS + 重启 |

**LED 状态与 App 可展示的设备状态：**

| 蓝牙 | WiFi | LED | 建议 App 展示 |
|------|------|-----|--------------|
| 关 | 断 | 熄灭 | 离线 |
| 广播中 | 断 | 慢闪 1s | 等待配网 |
| 已连接 | 断 | 快闪 0.2s | 配网中 |
| 关 | 连 | 常亮 | 正常运行 |
| 开 | 连 | 呼吸灯 | 正常 + 可调试 |

---

## 7. App 开发参考资源

| 资源 | 链接 |
|------|------|
| BluFi 协议官方文档 | https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ble/blufi.html |
| EspBlufi Android 源码 | https://github.com/EspressifApp/EspBlufi |
| EspBlufi iOS 源码 | https://github.com/EspressifApp/EspBlufiForiOS |
| EspBlufi iOS App Store | https://apps.apple.com/cn/app/espblufi/id1450614082 |

建议 App 开发时 **直接复用 EspBlufi 开源工程中的 BluFi 封包/解包逻辑**，仅替换 UI 和设备发现（按 `DEVICE_SN` 过滤），可最大程度保证与 ESP32 固件兼容。

---

## 8. 常见问题

**Q：连接后 App 按钮全部灰色，无法扫描/加密？**  
A：检查是否已订阅 E2P Notify，以及是否完成版本交换（GET_VERSION）。

**Q：扫描 WiFi 无结果？**  
A：确认 ESP32 周围有 2.4GHz AP；扫描失败会收到错误码 `0x0B`。

**Q：配网后重启能否自动连 WiFi？**  
A：可以。SSID/密码已存 NVS，上电自动连接；蓝牙默认关闭，需短按 BOOT 或重新配网开启。

**Q：iOS 与 Android 协议是否一致？**  
A：一致，均使用相同 BluFi 协议和 GATT UUID，差异仅在 BLE 系统层实现。
