"""
系统状态监控模块
用于记录和跟踪 Raspberry Pi Pico W 的系统状态
"""

import time
import gc
import sys
import os


class SystemMonitor:
    """系统状态监控类"""
    
    def __init__(self, logger=None):
        """
        初始化系统监控器
        
        Args:
            logger: 日志记录器（可选）
        """
        self.logger = logger
        
        # 系统启动时间
        self.start_time = time.time()
        
        # 统计信息
        self.total_loops = 0          # 总循环次数
        self.successful_loops = 0     # 成功的循环次数
        self.failed_loops = 0         # 失败的循环次数
        self.consecutive_failures = 0 # 连续失败次数
        self.max_consecutive_failures = 0  # 最大连续失败次数
        
        # WiFi 统计
        self.wifi_connects = 0        # WiFi 连接次数
        self.wifi_reconnects = 0      # WiFi 重连次数
        self.wifi_failures = 0        # WiFi 失败次数
        
        # MQTT 统计
        self.mqtt_connects = 0        # MQTT 连接次数
        self.mqtt_publishes = 0       # MQTT 发布次数
        self.mqtt_failures = 0        # MQTT 失败次数
        
        # 传感器统计
        self.sensor_reads = 0         # 传感器读取次数
        self.sensor_errors = 0        # 传感器错误次数
        
        # 内存统计
        self.min_free_memory = None   # 最小可用内存
        self.max_free_memory = None   # 最大可用内存
        
        # 看门狗统计
        self.watchdog_feeds = 0       # 喂狗次数
        self.watchdog_resets = 0      # 看门狗重置次数
        
        # 最后一次状态检查时间
        self.last_check_time = time.time()
    
    def _log(self, message, is_error=False):
        """内部日志方法"""
        if self.logger:
            if is_error:
                self.logger.error(message)
            else:
                self.logger.info(message)
        else:
            print(message)
    
    def get_uptime(self):
        """
        获取系统运行时间
        
        Returns:
            int: 运行时间（秒）
        """
        return int(time.time() - self.start_time)
    
    def get_uptime_formatted(self):
        """
        获取格式化的运行时间
        
        Returns:
            str: 格式化的运行时间字符串 (例如: "2天3小时45分钟")
        """
        uptime_seconds = self.get_uptime()
        
        days = uptime_seconds // 86400
        hours = (uptime_seconds % 86400) // 3600
        minutes = (uptime_seconds % 3600) // 60
        seconds = uptime_seconds % 60
        
        parts = []
        if days > 0:
            parts.append(f"{days}天")
        if hours > 0:
            parts.append(f"{hours}小时")
        if minutes > 0:
            parts.append(f"{minutes}分钟")
        if seconds > 0 or not parts:
            parts.append(f"{seconds}秒")
        
        return "".join(parts)
    
    def get_memory_info(self):
        """
        获取内存信息
        
        Returns:
            dict: 包含内存使用情况的字典
        """
        gc.collect()  # 先触发垃圾回收
        free_memory = gc.mem_free()
        allocated_memory = gc.mem_alloc()
        total_memory = free_memory + allocated_memory
        
        # 更新最小/最大可用内存
        if self.min_free_memory is None or free_memory < self.min_free_memory:
            self.min_free_memory = free_memory
        if self.max_free_memory is None or free_memory > self.max_free_memory:
            self.max_free_memory = free_memory
        
        return {
            'free': free_memory,
            'allocated': allocated_memory,
            'total': total_memory,
            'usage_percent': (allocated_memory / total_memory * 100) if total_memory > 0 else 0,
            'min_free': self.min_free_memory,
            'max_free': self.max_free_memory
        }
    
    def get_flash_info(self):
        """
        获取 Flash 存储信息
        
        Returns:
            dict: 包含存储使用情况的字典
        """
        try:
            # os.statvfs('/') 返回文件系统统计信息
            # 返回值: (f_bsize, f_frsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail, f_flag, f_namemax)
            stat = os.statvfs('/')
            
            # f_bsize: 文件系统块大小
            # f_blocks: 文件系统总块数
            # f_bfree: 可用块数
            block_size = stat[0]
            total_blocks = stat[2]
            free_blocks = stat[3]
            
            total_size = block_size * total_blocks
            free_size = block_size * free_blocks
            used_size = total_size - free_size
            
            return {
                'total': total_size,
                'used': used_size,
                'free': free_size,
                'total_kb': total_size // 1024,
                'used_kb': used_size // 1024,
                'free_kb': free_size // 1024,
                'usage_percent': (used_size / total_size * 100) if total_size > 0 else 0
            }
        except Exception as e:
            return {
                'total': 0,
                'used': 0,
                'free': 0,
                'total_kb': 0,
                'used_kb': 0,
                'free_kb': 0,
                'usage_percent': 0,
                'error': str(e)
            }
    
    def record_loop_start(self):
        """记录循环开始"""
        self.total_loops += 1
        self.last_check_time = time.time()
    
    def record_loop_success(self):
        """记录循环成功"""
        self.successful_loops += 1
        self.consecutive_failures = 0
    
    def record_loop_failure(self):
        """记录循环失败"""
        self.failed_loops += 1
        self.consecutive_failures += 1
        
        # 更新最大连续失败次数
        if self.consecutive_failures > self.max_consecutive_failures:
            self.max_consecutive_failures = self.consecutive_failures
    
    def record_wifi_connect(self, is_reconnect=False):
        """记录 WiFi 连接"""
        self.wifi_connects += 1
        if is_reconnect:
            self.wifi_reconnects += 1
    
    def record_wifi_failure(self):
        """记录 WiFi 失败"""
        self.wifi_failures += 1
    
    def record_mqtt_connect(self):
        """记录 MQTT 连接"""
        self.mqtt_connects += 1
    
    def record_mqtt_publish(self):
        """记录 MQTT 发布"""
        self.mqtt_publishes += 1
    
    def record_mqtt_failure(self):
        """记录 MQTT 失败"""
        self.mqtt_failures += 1
    
    def record_sensor_read(self):
        """记录传感器读取"""
        self.sensor_reads += 1
    
    def record_sensor_error(self):
        """记录传感器错误"""
        self.sensor_errors += 1
    
    def record_watchdog_feed(self):
        """记录喂狗操作"""
        self.watchdog_feeds += 1
    
    def record_watchdog_reset(self):
        """记录看门狗重置"""
        self.watchdog_resets += 1
    
    def get_success_rate(self):
        """
        获取成功率
        
        Returns:
            float: 成功率百分比
        """
        if self.total_loops == 0:
            return 0.0
        return (self.successful_loops / self.total_loops) * 100
    
    def get_wifi_success_rate(self):
        """
        获取 WiFi 成功率
        
        Returns:
            float: WiFi 成功率百分比
        """
        total = self.wifi_connects + self.wifi_failures
        if total == 0:
            return 0.0
        return (self.wifi_connects / total) * 100
    
    def get_mqtt_success_rate(self):
        """
        获取 MQTT 成功率
        
        Returns:
            float: MQTT 成功率百分比
        """
        total = self.mqtt_publishes + self.mqtt_failures
        if total == 0:
            return 0.0
        return (self.mqtt_publishes / total) * 100
    
    def get_sensor_success_rate(self):
        """
        获取传感器成功率
        
        Returns:
            float: 传感器成功率百分比
        """
        if self.sensor_reads == 0:
            return 0.0
        successful_reads = self.sensor_reads - self.sensor_errors
        return (successful_reads / self.sensor_reads) * 100
    
    def get_statistics(self):
        """
        获取完整的统计信息
        
        Returns:
            dict: 包含所有统计信息的字典
        """
        memory_info = self.get_memory_info()
        flash_info = self.get_flash_info()
        
        return {
            'uptime': self.get_uptime_formatted(),
            'uptime_seconds': self.get_uptime(),
            'total_loops': self.total_loops,
            'successful_loops': self.successful_loops,
            'failed_loops': self.failed_loops,
            'success_rate': f"{self.get_success_rate():.1f}%",
            'consecutive_failures': self.consecutive_failures,
            'max_consecutive_failures': self.max_consecutive_failures,
            'wifi': {
                'connects': self.wifi_connects,
                'reconnects': self.wifi_reconnects,
                'failures': self.wifi_failures,
                'success_rate': f"{self.get_wifi_success_rate():.1f}%"
            },
            'mqtt': {
                'connects': self.mqtt_connects,
                'publishes': self.mqtt_publishes,
                'failures': self.mqtt_failures,
                'success_rate': f"{self.get_mqtt_success_rate():.1f}%"
            },
            'sensor': {
                'reads': self.sensor_reads,
                'errors': self.sensor_errors,
                'success_rate': f"{self.get_sensor_success_rate():.1f}%"
            },
            'memory': {
                'free_kb': memory_info['free'] // 1024,
                'allocated_kb': memory_info['allocated'] // 1024,
                'usage_percent': f"{memory_info['usage_percent']:.1f}%",
                'min_free_kb': memory_info['min_free'] // 1024 if memory_info['min_free'] else 0,
                'max_free_kb': memory_info['max_free'] // 1024 if memory_info['max_free'] else 0
            },
            'flash': {
                'total_kb': flash_info['total_kb'],
                'used_kb': flash_info['used_kb'],
                'free_kb': flash_info['free_kb'],
                'usage_percent': f"{flash_info['usage_percent']:.1f}%"
            },
            'watchdog': {
                'feeds': self.watchdog_feeds,
                'resets': self.watchdog_resets
            }
        }
    
    def get_status_summary(self):
        """
        获取状态摘要（简洁版本）
        
        Returns:
            str: 状态摘要字符串
        """
        stats = self.get_statistics()
        memory = self.get_memory_info()
        
        summary = "运行时间: {} | 循环: {}次 (成功率: {}) | 内存: {}KB可用/{}KB总计 | 连续失败: {}次".format(
            stats['uptime'],
            stats['total_loops'],
            stats['success_rate'],
            memory['free']//1024,
            memory['total']//1024,
            self.consecutive_failures
        )
        
        return summary
    
    def log_status(self, detailed=False):
        """
        记录当前状态到日志
        
        Args:
            detailed: 是否输出详细信息
        """
        if detailed:
            stats = self.get_statistics()
            self._log("=" * 50)
            self._log("系统状态详细报告")
            self._log("=" * 50)
            self._log(f"运行时间: {stats['uptime']} ({stats['uptime_seconds']}秒)")
            self._log(f"总循环次数: {stats['total_loops']}")
            self._log(f"  - 成功: {stats['successful_loops']}")
            self._log(f"  - 失败: {stats['failed_loops']}")
            self._log(f"  - 成功率: {stats['success_rate']}")
            self._log(f"  - 当前连续失败: {stats['consecutive_failures']}")
            self._log(f"  - 最大连续失败: {stats['max_consecutive_failures']}")
            self._log(f"WiFi统计: 连接{stats['wifi']['connects']}次, 重连{stats['wifi']['reconnects']}次, 失败{stats['wifi']['failures']}次 (成功率: {stats['wifi']['success_rate']})")
            self._log(f"MQTT统计: 连接{stats['mqtt']['connects']}次, 发布{stats['mqtt']['publishes']}次, 失败{stats['mqtt']['failures']}次 (成功率: {stats['mqtt']['success_rate']})")
            self._log(f"传感器统计: 读取{stats['sensor']['reads']}次, 错误{stats['sensor']['errors']}次 (成功率: {stats['sensor']['success_rate']})")
            self._log(f"内存统计: 可用{stats['memory']['free_kb']}KB, 已用{stats['memory']['allocated_kb']}KB, 使用率{stats['memory']['usage_percent']}")
            self._log(f"  - 最小可用: {stats['memory']['min_free_kb']}KB")
            self._log(f"  - 最大可用: {stats['memory']['max_free_kb']}KB")
            self._log(f"Flash存储: 总计{stats['flash']['total_kb']}KB, 已用{stats['flash']['used_kb']}KB, 可用{stats['flash']['free_kb']}KB, 使用率{stats['flash']['usage_percent']}")
            self._log(f"看门狗统计: 喂狗{stats['watchdog']['feeds']}次, 重置{stats['watchdog']['resets']}次")
            self._log("=" * 50)
        else:
            self._log(self.get_status_summary())
    
    def check_health(self):
        """
        检查系统健康状态
        
        Returns:
            tuple: (is_healthy, issues)
                   is_healthy: 系统是否健康
                   issues: 问题列表
        """
        issues = []
        
        # 检查连续失败次数
        if self.consecutive_failures >= 3:
            issues.append(f"连续失败次数过高: {self.consecutive_failures}")
        
        # 检查成功率
        success_rate = self.get_success_rate()
        if self.total_loops > 10 and success_rate < 80:
            issues.append(f"循环成功率过低: {success_rate:.1f}%")
        
        # 检查内存
        memory = self.get_memory_info()
        if memory['free'] < 10240:  # 小于 10KB
            issues.append(f"可用内存过低: {memory['free']//1024}KB")
        
        # 检查 WiFi 成功率
        wifi_rate = self.get_wifi_success_rate()
        if self.wifi_connects > 5 and wifi_rate < 90:
            issues.append(f"WiFi成功率过低: {wifi_rate:.1f}%")
        
        # 检查 MQTT 成功率
        mqtt_rate = self.get_mqtt_success_rate()
        if self.mqtt_publishes > 5 and mqtt_rate < 90:
            issues.append(f"MQTT成功率过低: {mqtt_rate:.1f}%")
        
        is_healthy = len(issues) == 0
        return (is_healthy, issues)
    
    def reset_statistics(self):
        """重置所有统计信息"""
        self.total_loops = 0
        self.successful_loops = 0
        self.failed_loops = 0
        self.consecutive_failures = 0
        self.max_consecutive_failures = 0
        
        self.wifi_connects = 0
        self.wifi_reconnects = 0
        self.wifi_failures = 0
        
        self.mqtt_connects = 0
        self.mqtt_publishes = 0
        self.mqtt_failures = 0
        
        self.sensor_reads = 0
        self.sensor_errors = 0
        
        self.min_free_memory = None
        self.max_free_memory = None
        
        self.watchdog_feeds = 0
        self.watchdog_resets = 0
        
        self._log("统计信息已重置")
    
    def cleanup(self):
        """清理资源"""
        try:
            self.logger = None
            print("系统监控器资源已清理")
        except Exception as e:
            print(f"系统监控器清理异常: {e}")
