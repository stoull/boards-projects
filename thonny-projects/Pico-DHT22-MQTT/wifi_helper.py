import network
import ubinascii

class WiFiHelper:
    def __init__(self):
        self.wlan = network.WLAN(network.STA_IF)
        self.wlan.active(True)
    
    def connect(self, ssid, password, timeout=10):
        """连接到 WiFi"""
        self.wlan.connect(ssid, password)
        while timeout > 0:
            if self.wlan.status() >= 3:
                return True
            timeout -= 1
            time.sleep(1)
        return False
    
    def get_ip(self):
        """获取 IP 地址"""
        return self.wlan.ifconfig()[0]
    
    def get_mac(self):
        """获取 MAC 地址"""
        return ubinascii.hexlify(self.wlan.config('mac'), ':').decode()
        """获取 WiFi 信号强度"""
        return self.wlan.status('rssi')
    
    def get_rssi(self):
        """获取 WiFi 信号强度"""
        return self.wlan.status('rssi')
    
    def get_all_info(self):
        """获取所有网络信息"""
        info = {
            'ip': self.get_ip(),
            'mac': self.get_mac(),
            'rssi': self.get_rssi(),
            'subnet':  self.wlan.ifconfig()[1],
            'gateway': self.wlan.ifconfig()[2],
            'dns': self.wlan.ifconfig()[3]
        }
        return info

# 使用示例
wifi = WiFiHelper()
if wifi.connect('your_ssid', 'your_password'):
    info = wifi.get_all_info()
    for key, value in info.items():
        print(f'{key}: {value}')