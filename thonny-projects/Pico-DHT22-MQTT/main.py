"""
DHT22 温湿度传感器数据采集并发送到 MQTT 服务器
运行环境:  Raspberry Pi Pico W (MicroPython)
功能: 每 5 分钟读取一次温湿度数据并通过 MQTT 发布
"""

import json
import time
import gc
from machine import Pin, WDT
from network_utils import WiFiManager, NTPTimeSync
from logger import init_logger, log_info, log_error, log_warning, get_logger, update_logger_filename
from dht_sensor import DHT22Sensor
from mqtt_client import MQTTClientManager
from system_monitor import SystemMonitor

from hardware_info_helper import get_device_info_all

from config import (
    WIFI_SSID,
    WIFI_PASSWORD,
    MQTT_HOST,
    MQTT_PORT,
    MQTT_TOPIC,
    MQTT_TOPIC_DeviceInfo,
    MQTT_USER,
    MQTT_PASSWORD,
    MQTT_CLIENT_ID,
)


# ==================== 配置常量 ====================
# 硬件配置
DHT22_PIN = 2           # DHT22 数据引脚
LED_PIN = "LED"         # 板载 LED

# 时区配置
TIMEZONE_OFFSET = 8  # UTC+8 (北京时间)

# 数据采集间隔（秒）
SAMPLE_INTERVAL = 300

# 日志配置
LOG_FILE = "_log_temp.txt"
LOG_MAX_SIZE = 20480  # 20KB

# 看门狗配置
WATCHDOG_TIMEOUT = 8000  # 看门狗超时时间（毫秒），8秒 最大8388ms
WATCHDOG_ENABLED = False   # 是否启用看门狗
MAX_CONSECUTIVE_ERRORS = 5  # 最大连续错误次数，超过后触发硬重启


# ==================== 全局变量 ====================
led_pin = Pin(LED_PIN, Pin.OUT)
wifi_manager = None
time_sync = None
sensor = None
mqtt_client = None
watchdog = None
system_monitor = None  # 系统状态监控器
consecutive_errors = 0  # 连续错误计数


# ==================== 初始化模块 ====================
def initialize_logger():
    """初始化日志系统"""
    logger = init_logger(LOG_FILE, LOG_MAX_SIZE)
    log_info("=== 程序启动 ===")
    return logger


def initialize_network():
    """初始化网络连接和时间同步"""
    # global 是为了告诉 Python：我要修改全局的这两个变量，而不是创建新的局部变量
    global wifi_manager, time_sync
    
    # 创建 WiFi 管理器
    wifi_manager = WiFiManager(WIFI_SSID, WIFI_PASSWORD)
    
    # 连接 WiFi（传入喂狗回调）
    if not wifi_manager.connect(watchdog_feed_callback=feed_watchdog):
        log_error("WiFi 连接失败")
        return False
    
    # 创建时间同步器
    time_sync = NTPTimeSync(TIMEZONE_OFFSET)
    
    # 同步时间
    if not time_sync.sync():
        log_warning("时间同步失败，将使用系统时间")
    else:
        log_info(f"当前时间: {time_sync.get_iso8601_time_with_timezone()}")
    
    return True

def initialize_sensor():
    """初始化传感器"""
    global sensor
    
    logger = get_logger()
    sensor = DHT22Sensor(
        data_pin=DHT22_PIN,
        led_pin=LED_PIN,
        logger=logger
    )
    
    log_info("传感器初始化完成")
    return sensor


def initialize_mqtt():
    """初始化MQTT客户端"""
    global mqtt_client
    
    logger = get_logger()
    mqtt_client = MQTTClientManager(
        client_id=MQTT_CLIENT_ID,
        server=MQTT_HOST,
        port=MQTT_PORT,
        user=MQTT_USER,
        password=MQTT_PASSWORD,
        logger=logger
    )
    
    log_info("MQTT客户端初始化完成")
    return mqtt_client


def initialize_system_monitor():
    """初始化系统监控器"""
    global system_monitor
    
    logger = get_logger()
    system_monitor = SystemMonitor(logger=logger)
    
    log_info("系统监控器初始化完成")
    return system_monitor


def initialize_watchdog():
    """初始化看门狗"""
    global watchdog
    
    if not WATCHDOG_ENABLED:
        log_info("看门狗功能已禁用")
        return None
    
    try:
        # 创建看门狗实例，设置超时时间
        watchdog = WDT(timeout=WATCHDOG_TIMEOUT)
        log_info(f"看门狗已启动，超时时间: {WATCHDOG_TIMEOUT}ms")
        return watchdog
    except Exception as e:
        log_error(f"看门狗初始化失败: {e}")
        return None


def feed_watchdog():
    """喂狗 - 重置看门狗计时器"""
    global watchdog, system_monitor
    
    if not WATCHDOG_ENABLED:
        return None

    if watchdog:
        try:
            watchdog.feed()
            if system_monitor:
                system_monitor.record_watchdog_feed()
        except Exception as e:
            log_error(f"喂狗失败: {e}")

# 清理资源
def cleanup_all_resources():
    """清理所有资源：WiFi、传感器、MQTT等"""
    global wifi_manager, time_sync, sensor, mqtt_client, system_monitor
    
    log_info("开始清理资源...")
    
    # 清理MQTT客户端
    if mqtt_client:
        try:
            mqtt_client.cleanup()
            mqtt_client = None
        except Exception as e:
            log_error(f"MQTT清理失败: {e}")
    
    # 清理传感器
    if sensor:
        try:
            sensor.cleanup()
            sensor = None
        except Exception as e:
            log_error(f"传感器清理失败: {e}")
    
    # 清理网络
    if wifi_manager:
        try:
            wifi_manager.cleanup()
            wifi_manager = None
        except Exception as e:
            log_error(f"WiFi清理失败: {e}")
    
    # 清理系统监控器
    if system_monitor:
        try:
            system_monitor.cleanup()
            system_monitor = None
        except Exception as e:
            log_error(f"系统监控器清理失败: {e}")
    
    # 清理时间同步器
    time_sync = None
    
    # 触发垃圾回收
    gc.collect()
    log_info("资源清理完成")

# ==================== MQTT 数据发布 ====================
def publish_sensor_data():
    """读取传感器数据并发布到 MQTT"""
    # 记录传感器读取
    if system_monitor:
        system_monitor.record_sensor_read()
    
    # 读取传感器数据（自动重试 3 次，并在重试时喂狗）
    result = sensor.read(retry_count=3, retry_delay=2, watchdog_feed_callback=feed_watchdog)
    
    if result is None:
        log_error("传感器读取失败")
        if system_monitor:
            system_monitor.record_sensor_error()
        return False
    
    try:
        temperature, humidity = result
        
        # 构造数据包
        data = {
            "created_at": time_sync.get_iso8601_time_with_timezone(),
            "temperature": temperature,
            "humidity": humidity,
        }
        
        # 在发布前喂狗
        feed_watchdog()
        
        # 使用封装的MQTT客户端发布JSON数据
        success = mqtt_client.publish_json(MQTT_TOPIC, data)
        
        # 记录MQTT统计
        if system_monitor:
            if success:
                system_monitor.record_mqtt_publish()
            else:
                system_monitor.record_mqtt_failure()
        
        # 发布后喂狗
        feed_watchdog()
        
        return success
        
    except Exception as e:
        log_error(f"发布数据失败: {e}")
        if system_monitor:
            system_monitor.record_mqtt_failure()
        return False

# ==================== MQTT 数据发布 ====================
def publish_device_info_data():
    """读取设备信息并发布到 MQTT"""

    # 读取设备信息
    d_info = get_device_info_all()
    
    # 读取网络信息
    net_info = wifi_manager.get_network_info()
    if net_info:
        d_info.update(net_info)
    
    if d_info is None:
        log_error("设备信息读取失败")
        return False
    
    try:
        # 构造数据包
        d_info["created_at"] = time_sync.get_iso8601_time_with_timezone()
        # 在发布前喂狗
        feed_watchdog()
        
        # 使用封装的MQTT客户端发布JSON数据
        success = mqtt_client.publish_json(MQTT_TOPIC_DeviceInfo, d_info)
        
        # 发布后喂狗
        feed_watchdog()
        
        return success
        
    except Exception as e:
        log_error(f"发布数据失败: {e}")
        if system_monitor:
            system_monitor.record_mqtt_failure()
        return False

# ==================== 主循环 ====================
def start_main_loop():
    """主循环:  连接 MQTT 并定期发布传感器数据"""
    global consecutive_errors
    
    log_info("启动主循环")
    
    try:
        # 主循环
        loop_count = 0
        while True:
            loop_count += 1
            loop_success = True
            
            # 记录循环开始
            if system_monitor:
                system_monitor.record_loop_start()
            
            # 喂狗 - 主循环开始
            feed_watchdog()
            
            try:
                # 检查 WiFi 连接
                is_wifi_connected = True
                if not wifi_manager.is_connected():
                    log_warning("WiFi 断开，尝试重连...")
                    if system_monitor:
                        system_monitor.record_wifi_failure()
                    feed_watchdog()  # 重连前喂狗
                    is_wifi_connected = False
                    if not wifi_manager.connect(watchdog_feed_callback=feed_watchdog):
                        if WATCHDOG_ENABLED:
                            raise Exception("WiFi 重连失败")
                        else:
                            loop_success = False
                            is_wifi_connected = False
                    else:
                        is_wifi_connected = True
                        if system_monitor:
                            system_monitor.record_wifi_connect(is_reconnect=True)
                        feed_watchdog()  # 重连后喂狗
                
                if is_wifi_connected:
                    # 连接到 MQTT 服务器（并在重试时喂狗）
                    if not mqtt_client.connect(retry_count=3, watchdog_feed_callback=feed_watchdog):
                        log_error("MQTT 连接失败，跳过本次发布")
                        if system_monitor:
                            system_monitor.record_mqtt_failure()
                        loop_success = False
                    else:
                        if system_monitor:
                            system_monitor.record_mqtt_connect()
                        
                        # 发布传感器数据
                        if not publish_sensor_data():
                            loop_success = False

                        # 发布设备信息数据
                        if not publish_device_info_data():
                            log_error("设备信息发布失败")
                        
                        # 断开 MQTT 连接
                        mqtt_client.disconnect()
                
                # 记录循环结果
                if loop_success:
                    consecutive_errors = 0
                    if system_monitor:
                        system_monitor.record_loop_success()
                else:
                    consecutive_errors += 1
                    if system_monitor:
                        system_monitor.record_loop_failure()
                    log_warning(f"本次循环失败，连续错误次数: {consecutive_errors}/{MAX_CONSECUTIVE_ERRORS}")
                
                # 检查是否达到最大连续错误次数
                if consecutive_errors >= MAX_CONSECUTIVE_ERRORS:
                    log_error(f"连续错误次数达到 {MAX_CONSECUTIVE_ERRORS} 次，触发设备重启")
                    time.sleep(2)  # 等待日志写入
                    # 停止喂狗，让看门狗触发重启
                    if WATCHDOG_ENABLED:
                        log_info("停止喂狗，等待看门狗重启设备...")
                        while True:
                            time.sleep(1)  # 等待看门狗触发
                    else:
                        machine.reset()
                
                # 每 10 次循环显示一次统计信息
                if loop_count % 10 == 0:
                    sensor_stats = sensor.get_statistics()
                    mqtt_stats = mqtt_client.get_statistics()
                    log_info(f"传感器统计: {sensor_stats}")
                    log_info(f"MQTT统计: {mqtt_stats}")
                    log_info(f"连续错误计数: {consecutive_errors}")
                    
                    # 显示系统监控状态
                    if system_monitor:
                        system_monitor.log_status(detailed=True)
                        # 检查系统健康状态
                        is_healthy, issues = system_monitor.check_health()
                        if not is_healthy:
                            log_warning(f"系统健康检查发现问题: {', '.join(issues)}")
                
            except Exception as loop_error:
                consecutive_errors += 1
                log_error(f"循环内异常: {type(loop_error).__name__} - {loop_error}")
                log_warning(f"连续错误次数: {consecutive_errors}/{MAX_CONSECUTIVE_ERRORS}")
                
                # 检查是否需要硬重启
                if consecutive_errors >= MAX_CONSECUTIVE_ERRORS:
                    log_error(f"连续错误次数达到 {MAX_CONSECUTIVE_ERRORS} 次，触发设备重启")
                    time.sleep(2)
                    # 停止喂狗，让看门狗触发重启
                    if WATCHDOG_ENABLED:
                        log_info("停止喂狗，等待看门狗重启设备...")
                        while True:
                            time.sleep(1)
            
            # 喂狗 - 循环结束前
            feed_watchdog()
            
            # 等待下次采集（分段睡眠以便定期喂狗）
            if WATCHDOG_ENABLED:
                log_info(f"等待 {SAMPLE_INTERVAL} 秒...")
                sleep_interval = 7  # 每7秒喂一次狗
                remaining_time = SAMPLE_INTERVAL
                while remaining_time > 0:
                    sleep_time = min(sleep_interval, remaining_time)
                    time.sleep(sleep_time)
                    feed_watchdog()  # 在等待期间定期喂狗
                    remaining_time -= sleep_time
            else:
                if is_wifi_connected:
                    log_info(f"等待 {SAMPLE_INTERVAL} 秒...")
                    time.sleep(SAMPLE_INTERVAL)
                else:
                    log_info(f"等待 4 秒...")
                    time.sleep(4)
            
    except KeyboardInterrupt:
        log_info("程序被用户中断")
        print("程序已停止")
        # 用户中断时，停止喂狗，让设备重启
        if WATCHDOG_ENABLED:
            log_info("等待看门狗重启设备...")
            while True:
                time.sleep(1)
        
    except Exception as e:
        log_error(f"主循环严重异常: {type(e).__name__} - {e}")
        log_error("停止喂狗，等待看门狗重启设备...")
        time.sleep(2)
        # 停止喂狗，让看门狗触发重启
        if WATCHDOG_ENABLED:
            while True:
                time.sleep(1)
        
    finally:
        # 显示最终统计
        if sensor: 
            stats = sensor.get_statistics()
            log_info(f"传感器最终统计: {stats}")
        
        if mqtt_client:
            stats = mqtt_client.get_statistics()
            log_info(f"MQTT最终统计: {stats}")
        
        if system_monitor:
            log_info("=== 系统运行最终统计 ===")
            system_monitor.log_status(detailed=True)
        
        # 清理所有资源（网络、传感器等）
        cleanup_all_resources()
        
        # 等待看门狗重启设备
        if WATCHDOG_ENABLED:
            log_warning("等待看门狗重启设备...")
            time.sleep(2)
            while True:
                time.sleep(1)

def update_logger_filename_with_date():
    # 重命名日志文件为带日期的文件名
    import os
    try:
        date_str = time_sync.get_iso8601_time_with_timezone()[:10]  # 获取日期部分 YYYY-MM-DD
        new_log_file = f"_log_{date_str}.txt"
        update_logger_filename(new_log_file)
    except Exception as e:
        log_warning(f"日志文件重命名失败: {e}")

# ==================== 程序入口 ====================
def main():
    """程序主入口"""
    global consecutive_errors
    
    # 1. 初始化日志
    initialize_logger()
    
    # 2. 初始化看门狗（尽早启动以监控整个启动过程）
    initialize_watchdog()
    
    # 3. 初始化网络连接
    if not initialize_network():
        log_error("网络初始化失败，停止喂狗等待重启")
        if WATCHDOG_ENABLED:
            while True:
                time.sleep(1)  # 等待看门狗触发重启

    # 3.1 更新日志文件名
    update_logger_filename_with_date()
     
    # 4. 初始化传感器
    initialize_sensor()
    
    # 5. 初始化MQTT客户端
    initialize_mqtt()
    
    # 6. 初始化系统监控器
    initialize_system_monitor()
    
    # 7. 重置错误计数
    consecutive_errors = 0
    
    # 8. 点亮 LED 表示就绪
    led_pin.on()
    log_info("系统就绪")
    
    # 9. 喂狗后启动主循环
    feed_watchdog()
    
    # 10. 启动主循环
    start_main_loop()


# 启动程序
if __name__ == "__main__":
    main()
