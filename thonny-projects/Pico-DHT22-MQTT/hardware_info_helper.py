import machine
import sys
import os
import ubinascii
import time
import gc

def get_cpu_temperature():
    sensor = machine.ADC(4)
    voltage = sensor.read_u16() * (3.3 / 65535)
    temperature = 27 - (voltage - 0.706) / 0.001721
    return temperature

def get_memory_info():
    gc.collect()
    mem_free = gc.mem_free()
    mem_alloc = gc.mem_alloc()
    mem_total = mem_free + mem_alloc
    return {
        "total": mem_total,
        "used": mem_alloc,
        "free": mem_free,
        "usage_percent": round((mem_alloc / mem_total * 100) if mem_total else 0, 2)
    }

def get_device_info_all():
    all_info = {}

    all_info['unique_id'] = ubinascii.hexlify(machine.unique_id()).decode()

    all_info['platform'] = sys.platform
    all_info['os_version'] = sys.version
    all_info['cpu_frequency_mhz'] = machine.freq() // 1_000_000
    all_info['cpu_temperature'] = round(get_cpu_temperature(), 2)

    stat = os.statvfs('/')
    all_info['total_storage_bytes'] = stat[0] * stat[2]
    all_info['used_storage_bytes'] = all_info['total_storage_bytes'] - (stat[0] * stat[3])
    all_info['free_storage_bytes'] = stat[0] * stat[3]
    all_info['storage_usage_percent'] = round((all_info['used_storage_bytes'] / all_info['total_storage_bytes']) * 100, 1)

    memory_info = get_memory_info()
    all_info['total_memory_bytes'] = memory_info['total']
    all_info['used_memory_bytes'] = memory_info['used']
    all_info['free_memory_bytes'] = memory_info['free']
    all_info['memory_usage_percent'] = memory_info['usage_percent']

    all_info['uptime_seconds'] = time.ticks_ms() // 1000

    reset_cause = machine.reset_cause()
    if reset_cause == machine.PWRON_RESET:
        cause_str = 0
    elif reset_cause == machine.WDT_RESET:
        cause_str = 1
    else:
        cause_str = 9
    all_info['reset_reason'] = cause_str
    return all_info

def print_memory_info():
    """打印详细的内存信息"""
    info = get_memory_info()
    
    mem_free = info['free']
    mem_alloc = info['used']
    mem_total = info['total']
    
    print("\n【系统信息】")
    print(f"总内存:    {mem_total: >8} 字节 ({mem_total / 1024:>6.2f} KB)")
    print(f"已用内存:  {mem_alloc:>8} 字节 ({mem_alloc / 1024:>6.2f} KB)")
    print(f"剩余内存: {mem_free:>8} 字节 ({mem_free / 1024:>6.2f} KB)")
    print(f"使用率:    {(mem_alloc / mem_total * 100):>6.1f}%")
    
def print_hardware_info():
    print("=" * 50)
    print("Raspberry Pi Pico W 硬件信息")
    print("=" * 50)
    
    # 系统信息
    print("\n【系统信息】")
    print(f"平台: {sys.platform}")
    print(f"MicroPython 版本: {sys.version}")
    print(f"CPU 频率: {machine.freq() / 1_000_000:.0f} MHz")

    # 设备 ID
    print("\n【设备标识】")
    unique_id = ubinascii.hexlify(machine.unique_id()).decode()
    print(f"唯一 ID: {unique_id}")
    
    # 温度
    print("\n【传感器】")
    print(f"CPU 温度: {get_cpu_temperature():.2f}°C")
    
    # 存储信息
    print("\n【存储】")
    stat = os.statvfs('/')
    total = stat[0] * stat[2] / 1024
    free = stat[0] * stat[3] / 1024
    print(f"总空间: {total:.2f} KB")
    print(f"剩余空间: {free:.2f} KB")
    
    print("\n【电源】")
    reset_cause = machine.reset_cause()
    if reset_cause == machine.PWRON_RESET:
        cause_str = "上电复位"
    elif reset_cause == machine.WDT_RESET:
        cause_str = "看门狗复位"
    else:
        cause_str = f"其他 (代码: {reset_cause})"
    print(f"复位原因: {cause_str}")
    
    # 运行时间
    print(f"运行时间: {time.ticks_ms() / 1000:.2f} 秒")
    
    print_memory_info()

    print("=" * 50)

# 运行
# print_hardware_info()
"""
==================================================
Raspberry Pi Pico W 硬件信息
==================================================

【系统信息】
平台: rp2
MicroPython 版本: 3.4.0; MicroPython v1.23.0 on 2024-06-02
CPU 频率: 125 MHz

【设备标识】
唯一 ID: e6632c8593745230

【传感器】
CPU 温度: 28.45°C

【存储】
总空间: 848.00 KB
剩余空间: 668.00 KB

【电源】
复位原因: 上电复位
运行时间: 114.14 秒

【系统信息】
总内存:      191424 字节 (186.94 KB)
已用内存:     43808 字节 ( 42.78 KB)
剩余内存:   147616 字节 (144.16 KB)
使用率:      22.9%
==================================================
"""
