"""
DHT22 温湿度传感器模块
提供温湿度数据读取功能
"""

import time
from machine import Pin
import dht


class DHT22Sensor:
    """DHT22 温湿度传感器类"""
    
    # 数据平滑处理常量
    MAX_CHANGE_THRESHOLD = 3.0  # 最大变化阈值（±3）
    MAX_ANOMALY_COUNT = 3       # 最大异常数据次数
    
    def __init__(self, data_pin, led_pin=None, logger=None):
        """
        初始化 DHT22 传感器
        
        Args:
            data_pin: 数据引脚编号
            led_pin: LED 引脚（可选），用于指示读取状态
            logger: 日志记录器（可选）
        """
        # 先创建带上拉的 Pin 对象
        pin_t = Pin(data_pin, Pin.IN, Pin.PULL_UP)
        self.sensor = dht.DHT22(pin_t)
        self.led = Pin(led_pin, Pin.OUT) if led_pin else None
        self.logger = logger
        
        # 统计信息
        self.read_count = 0
        self.error_count = 0
        self.last_temperature = None
        self.last_humidity = None
        self.last_read_time = None
        
        # 数据平滑处理
        self.last_valid_temperature = None  # 上次验证通过的温度
        self.last_valid_humidity = None     # 上次验证通过的湿度
        self.consecutive_anomaly_count = 0  # 连续异常数据计数
        self.anomaly_count = 0              # 异常数据总计数
    
    def _log(self, message, is_error=False):
        """内部日志方法"""
        if self.logger:
            if is_error:
                self.logger.error(message)
            else:
                self.logger.info(message)
        else:
            print(message)
    
    def _led_on(self):
        """LED 指示灯开启"""
        if self.led:
            self.led.on()
    
    def _led_off(self):
        """LED 指示灯关闭"""
        if self.led:
            self.led.off()
    
    def read(self, retry_count=3, retry_delay=2, watchdog_feed_callback=None):
        """
        读取温湿度数据
        
        Args:
            retry_count: 失败重试次数，默认 3 次
            retry_delay: 重试间隔（秒），默认 2 秒
            watchdog_feed_callback: 看门狗喂狗回调函数（可选）
            
        Returns: 
            tuple: (温度, 湿度) 或 None（失败时）
        """
        self.read_count += 1
        
        for attempt in range(retry_count):
            try:
                # 喂狗（如果提供了回调函数）
                if watchdog_feed_callback:
                    watchdog_feed_callback()
                
                # 关闭 LED 表示正在读取
                self._led_off()
                
                # 读取传感器
                self.sensor.measure()
                temperature = self.sensor.temperature()
                humidity = self.sensor.humidity()
                
                # 数据验证
                if not self._validate_data(temperature, humidity):
                    raise ValueError(f"数据超出正常范围:  温度={temperature}, 湿度={humidity}")
                
                # 数据平滑处理
                is_valid, smoothed_temp, smoothed_humidity = self._check_data_change(temperature, humidity)
                
                # 如果是有效的新数据（不是使用的旧数据），更新last_valid值
                if is_valid:
                    self.last_valid_temperature = temperature
                    self.last_valid_humidity = humidity
                
                # 保存最后读取的值（用于上传）
                self.last_temperature = smoothed_temp
                self.last_humidity = smoothed_humidity
                self.last_read_time = time.time()
                
                # 开启 LED 表示读取成功
                self._led_on()
                
                self._log(f"读取成功: 温度={smoothed_temp}°C, 湿度={smoothed_humidity}%")
                
                return (smoothed_temp, smoothed_humidity)
                
            except Exception as e: 
                self.error_count += 1
                error_msg = f"读取失败 (尝试 {attempt + 1}/{retry_count}): {type(e).__name__} - {e}"
                
                # 最后一次尝试才记录错误
                if attempt == retry_count - 1:
                    self._log(error_msg, is_error=True)
                    self._led_off()
                    return None
                else:
                    self._log(error_msg)
                    time.sleep(retry_delay)
        
        return None
    
    def _validate_data(self, temperature, humidity):
        """
        验证传感器数据是否在合理范围内
        
        Args:
            temperature: 温度值
            humidity: 湿度值
            
        Returns:
            bool: 数据有效返回 True
        """
        # DHT22 测量范围:  温度 -40~80°C, 湿度 0~100%
        if temperature < -40 or temperature > 80:
            return False
        if humidity < 0 or humidity > 100:
            return False
        return True
    
    def _check_data_change(self, temperature, humidity):
        """
        检查数据变化是否超出阈值（数据平滑处理）
        
        Args:
            temperature: 当前温度值
            humidity: 当前湿度值
            
        Returns:
            tuple: (is_valid, smoothed_temperature, smoothed_humidity)
                   is_valid: 数据是否有效
                   smoothed_temperature: 平滑处理后的温度
                   smoothed_humidity: 平滑处理后的湿度
        """
        # 如果是第一次读取，直接返回当前数据
        if self.last_valid_temperature is None or self.last_valid_humidity is None:
            return (True, temperature, humidity)
        
        # 计算温度和湿度的变化量
        temp_change = abs(temperature - self.last_valid_temperature)
        humidity_change = abs(humidity - self.last_valid_humidity)
        
        # 检查是否超出阈值
        is_anomaly = (temp_change > self.MAX_CHANGE_THRESHOLD or 
                     humidity_change > self.MAX_CHANGE_THRESHOLD)
        
        if is_anomaly:
            # 异常数据计数增加
            self.consecutive_anomaly_count += 1
            self.anomaly_count += 1
            
            self._log(
                f"检测到异常数据: 温度变化={temp_change:.1f}°C, 湿度变化={humidity_change:.1f}%, 连续异常次数={self.consecutive_anomaly_count}",
                is_error=False
            )
            
            # 如果连续异常次数超过阈值，使用当前异常数据
            if self.consecutive_anomaly_count > self.MAX_ANOMALY_COUNT:
                self._log(
                    f"连续异常数据超过{self.MAX_ANOMALY_COUNT}次，采用当前数据: 温度={temperature}°C, 湿度={humidity}%"
                )
                # 重置连续异常计数
                self.consecutive_anomaly_count = 0
                return (True, temperature, humidity)
            else:
                # 使用上次的有效数据
                self._log(
                    f"丢弃异常数据，使用上次有效数据: 温度={self.last_valid_temperature}°C, 湿度={self.last_valid_humidity}%"
                )
                return (False, self.last_valid_temperature, self.last_valid_humidity)
        else:
            # 数据正常，重置连续异常计数
            if self.consecutive_anomaly_count > 0:
                self._log(f"数据恢复正常，重置异常计数")
            self.consecutive_anomaly_count = 0
            return (True, temperature, humidity)
    
    def read_fahrenheit(self, retry_count=3, retry_delay=2):
        """
        读取温度（华氏度）和湿度
        
        Args:
            retry_count: 失败重试次数
            retry_delay: 重试间隔（秒）
            
        Returns:
            tuple: (华氏温度, 湿度) 或 None
        """
        result = self.read(retry_count, retry_delay)
        if result: 
            celsius, humidity = result
            fahrenheit = (celsius * 9 / 5) + 32
            return (fahrenheit, humidity)
        return None
    
    def get_last_reading(self):
        """
        获取上次成功读取的数据
        
        Returns: 
            dict: 包含温度、湿度、时间的字典，如果没有则返回 None
        """
        if self.last_temperature is not None:
            return {
                'temperature': self.last_temperature,
                'humidity': self.last_humidity,
                'timestamp': self.last_read_time
            }
        return None
    
    def get_statistics(self):
        """
        获取统计信息
        
        Returns:
            dict: 读取次数、错误次数、成功率、异常数据统计
        """
        success_count = self.read_count - self.error_count
        success_rate = (success_count / self.read_count * 100) if self.read_count > 0 else 0
        
        return {
            'total_reads': self.read_count,
            'errors': self.error_count,
            'success_rate': f"{success_rate:.1f}%",
            'anomaly_count': self.anomaly_count,
            'consecutive_anomaly': self.consecutive_anomaly_count
        }
    
    def reset_statistics(self):
        """重置统计信息"""
        self.read_count = 0
        self.error_count = 0
        self.anomaly_count = 0
        self.consecutive_anomaly_count = 0
    
    def cleanup(self):
        """
        清理资源并释放内存
        """
        try:
            if self.led:
                self.led.off()
                self.led = None
            self.sensor = None
            self.logger = None
            self.last_temperature = None
            self.last_humidity = None
            self.last_read_time = None
            print("DHT22 传感器资源已清理")
        except Exception as e:
            print(f"DHT22 清理异常: {e}")


# ==================== 兼容旧代码的简单函数 ====================

# 全局传感器实例（用于兼容旧代码）
_global_sensor = None


def init_sensor(data_pin=2, led_pin="LED", logger=None):
    """
    初始化全局传感器实例
    
    Args:
        data_pin: 数据引脚编号，默认 GPIO2
        led_pin: LED 引脚，默认板载 LED
        logger: 日志记录器
        
    Returns:
        DHT22Sensor:  传感器实例
    """
    global _global_sensor
    _global_sensor = DHT22Sensor(data_pin, led_pin, logger)
    return _global_sensor


def readDHT():
    """
    读取 DHT22 传感器（兼容旧代码的函数）
    
    Returns:
        tuple: (温度, 湿度) 或 None
    """
    if _global_sensor is None: 
        # 如果没有初始化，自动创建一个
        init_sensor()
    
    return _global_sensor.read()


def get_sensor():
    """
    获取全局传感器实例
    
    Returns:
        DHT22Sensor: 传感器实例
    """
    return _global_sensor

