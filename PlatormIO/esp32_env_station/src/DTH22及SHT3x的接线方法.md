# 环境配置以及接线方法

### 安装依赖包

#### 板子固件

如果使用esp32-c3类型的板子:

可选如下板子的基础固件：

* `Adafruit QT Py ESP32-C3`
* `MakerGO ESP32 C3 SuperMini`

#### 软件中使用到的三方依赖包

全部 from Arduino 

* `ArduinoJson` by Benoit Blanchon

	A simple and efficient JSON library for embedded C++. ⭐ 7124 stars on GitHub! Supports serialization, deserialization, MessagePack, streams, filtering, and more. Fully tested and documented.

* `DHT sensor library` by Adafruit

	Arduino library for DHT11, DHT22, etc Temp & Humidity Sensors


* `PubSubClient` by Nick O'Leary

	A client library for MQTT messaging. MQTT is a lightweight messaging protocol ideal for small devices. This library allows you to send and receive MQTT messages. It supports the latest MQTT 3.1.1 protocol and can be configured to use the older MQTT 3.1 if needed. It supports all Arduino Ethernet Client compatible hardware, including the Intel Galileo/Edison, ESP8266 and TI CC3000.



### DTH22及SHT3x的接线方法

接线方法

### DTH22 单总线 - SuperMini


| 模块 | SuperMini | other |
| --- | --- | ---- |
| VCC | 5V | - |
| GND | GND | - |
| Out | GPIO4 -单总线数据引脚 | - |

### SHT3x I2C - SuperMini


| 模块 | SuperMini | other |
| --- | --- | ---- |
| VCC | 3.3V | - |
| GND | GND | - |
| SDA | GPIO5（推荐）或 GPIO8（若你确认无 LED/BOOT 问题） | - |
| SCL | GPIO6（推荐）或 GPIO9（不推荐，易踩 BOOT | - |

