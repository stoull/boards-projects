"""
MQTT 客户端封装模块
提供 MQTT 连接、发布、订阅等功能
"""

import json
from umqtt.simple import MQTTClient as UMQTTClient


class MQTTClientManager:
    """MQTT 客户端管理器"""
    
    def __init__(self, client_id, server, port=1883, user=None, password=None, logger=None):
        """
        初始化 MQTT 客户端管理器
        
        Args:
            client_id: 客户端 ID
            server: MQTT 服务器地址
            port: MQTT 服务器端口，默认 1883
            user: 用户名（可选）
            password: 密码（可选）
            logger: 日志记录器（可选）
        """
        self.client_id = client_id
        self.server = server
        self.port = port
        self.user = user
        self.password = password
        self.logger = logger
        self.client = None
        self.is_connected_flag = False
        
        # 统计信息
        self.connect_count = 0
        self.publish_count = 0
        self.error_count = 0
    
    def _log(self, message, is_error=False):
        """内部日志方法"""
        if self.logger:
            if is_error:
                self.logger.error(message)
            else:
                self.logger.info(message)
        else:
            print(message)
    
    def connect(self, retry_count=3, watchdog_feed_callback=None):
        """
        连接到 MQTT 服务器
        
        Args:
            retry_count: 失败重试次数，默认 3 次
            watchdog_feed_callback: 看门狗喂狗回调函数（可选）
            
        Returns:
            bool: 连接成功返回 True，失败返回 False
        """
        for attempt in range(retry_count):
            try:
                # 喂狗（如果提供了回调函数）
                if watchdog_feed_callback:
                    watchdog_feed_callback()
                
                # 如果已经连接，先断开
                if self.client:
                    try:
                        self.client.disconnect()
                    except:
                        pass
                
                # 创建新的客户端
                self.client = UMQTTClient(
                    client_id=self.client_id,
                    server=self.server,
                    port=self.port,
                    user=self.user,
                    password=self.password
                )
                
                # 连接到服务器
                self.client.connect()
                self.is_connected_flag = True
                self.connect_count += 1
                
                self._log(f"MQTT 连接成功: {self.server}:{self.port}")
                return True
                
            except Exception as e:
                self.error_count += 1
                error_msg = f"MQTT 连接失败 (尝试 {attempt + 1}/{retry_count}): {type(e).__name__} - {e}"
                
                if attempt == retry_count - 1:
                    self._log(error_msg, is_error=True)
                    self.is_connected_flag = False
                    return False
                else:
                    self._log(error_msg)
        
        return False
    
    def disconnect(self):
        """断开 MQTT 连接"""
        try:
            if self.client:
                self.client.disconnect()
                self._log("MQTT 已断开")
        except Exception as e:
            self._log(f"MQTT 断开异常: {e}", is_error=True)
        finally:
            self.is_connected_flag = False
            self.client = None
    
    def publish(self, topic, message, qos=0, retain=False):
        """
        发布消息到 MQTT 主题
        
        Args:
            topic: 主题
            message: 消息内容（字符串或字典）
            qos: QoS 等级，默认 0
            retain: 是否保留消息，默认 False
            
        Returns:
            bool: 发布成功返回 True，失败返回 False
        """
        try:
            if not self.is_connected_flag or not self.client:
                self._log("MQTT 未连接，无法发布消息", is_error=True)
                return False
            
            # 如果消息是字典，转换为 JSON
            if isinstance(message, dict):
                message = json.dumps(message)
            
            # 发布消息
            self.client.publish(topic, message, qos=qos, retain=retain)
            self.publish_count += 1
            
            self._log(f"MQTT 消息已发布到 {topic}: {message}")
            return True
            
        except Exception as e:
            self.error_count += 1
            self._log(f"MQTT 发布失败: {type(e).__name__} - {e}", is_error=True)
            return False
    
    def publish_json(self, topic, data, qos=0, retain=False):
        """
        发布 JSON 数据到 MQTT 主题
        
        Args:
            topic: 主题
            data: 要发布的数据（字典）
            qos: QoS 等级，默认 0
            retain: 是否保留消息，默认 False
            
        Returns:
            bool: 发布成功返回 True，失败返回 False
        """
        return self.publish(topic, data, qos=qos, retain=retain)
    
    def subscribe(self, topic, callback=None):
        """
        订阅 MQTT 主题
        
        Args:
            topic: 主题
            callback: 回调函数（可选）
            
        Returns:
            bool: 订阅成功返回 True，失败返回 False
        """
        try:
            if not self.is_connected_flag or not self.client:
                self._log("MQTT 未连接，无法订阅主题", is_error=True)
                return False
            
            if callback:
                self.client.set_callback(callback)
            
            self.client.subscribe(topic)
            self._log(f"已订阅 MQTT 主题: {topic}")
            return True
            
        except Exception as e:
            self.error_count += 1
            self._log(f"MQTT 订阅失败: {type(e).__name__} - {e}", is_error=True)
            return False
    
    def check_msg(self):
        """
        检查并处理订阅的消息
        
        Returns:
            bool: 处理成功返回 True
        """
        try:
            if self.client:
                self.client.check_msg()
                return True
        except Exception as e:
            self._log(f"MQTT 消息检查失败: {e}", is_error=True)
            return False
    
    def wait_msg(self):
        """
        等待并处理订阅的消息（阻塞）
        
        Returns:
            bool: 处理成功返回 True
        """
        try:
            if self.client:
                self.client.wait_msg()
                return True
        except Exception as e:
            self._log(f"MQTT 消息等待失败: {e}", is_error=True)
            return False
    
    def is_connected(self):
        """
        检查是否已连接
        
        Returns:
            bool: 已连接返回 True
        """
        return self.is_connected_flag and self.client is not None
    
    def get_statistics(self):
        """
        获取统计信息
        
        Returns:
            dict: 连接次数、发布次数、错误次数
        """
        return {
            'connects': self.connect_count,
            'publishes': self.publish_count,
            'errors': self.error_count
        }
    
    def reset_statistics(self):
        """重置统计信息"""
        self.connect_count = 0
        self.publish_count = 0
        self.error_count = 0
    
    def cleanup(self):
        """
        清理资源并释放内存
        """
        try:
            if self.client:
                if self.is_connected_flag:
                    self.client.disconnect()
                self.client = None
            self.is_connected_flag = False
            self.logger = None
            print("MQTT 客户端资源已清理")
        except Exception as e:
            print(f"MQTT 清理异常: {e}")
    
    def reconnect(self, retry_count=3):
        """
        重新连接到 MQTT 服务器
        
        Args:
            retry_count: 重试次数
            
        Returns:
            bool: 重连成功返回 True
        """
        self._log("尝试重新连接 MQTT...")
        self.disconnect()
        return self.connect(retry_count=retry_count)


# ==================== 便捷函数 ====================

def create_mqtt_client(client_id, server, port=1883, user=None, password=None, logger=None):
    """
    创建并连接 MQTT 客户端（便捷函数）
    
    Args:
        client_id: 客户端 ID
        server: MQTT 服务器地址
        port: MQTT 服务器端口
        user: 用户名
        password: 密码
        logger: 日志记录器
        
    Returns:
        MQTTClientManager: MQTT 客户端管理器实例，连接失败返回 None
    """
    mqtt = MQTTClientManager(client_id, server, port, user, password, logger)
    if mqtt.connect():
        return mqtt
    return None
