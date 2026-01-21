"""
轻量级日志模块
适用于 Raspberry Pi Pico 等资源受限设备
"""

import time


class SimpleLogger:
    """简单日志记录器"""
    
    # 日志级别
    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3
    
    LEVEL_NAMES = {
        DEBUG: "DEBUG",
        INFO:  "INFO",
        WARNING: "WARN",
        ERROR: "ERROR"
    }
    
    def __init__(self, filename, max_size=10240, level=INFO, use_timestamp=True, keep_ratio=0.5):
        """
        初始化日志记录器
        
        Args:
            filename: 日志文件名
            max_size: 最大文件大小（字节），默认 10KB
            level: 日志级别，默认 INFO
            use_timestamp: 是否添加时间戳，默认 True
            keep_ratio: 文件过大时保留的比例（0-1），默认 0.5（保留后50%的日志）
        """
        self.filename = filename
        self.max_size = max_size
        self.level = level
        self.use_timestamp = use_timestamp
        self.keep_ratio = max(0.1, min(0.9, keep_ratio))  # 限制在 0.1-0.9 之间
        self.file = None
        self._open_file()
    
    def _open_file(self):
        """打开日志文件"""
        try:
            # 检查文件大小
            try:
                import os
                file_size = os.stat(self.filename)[6]
                
                # 如果文件过大，删除最旧的日志
                if file_size > self.max_size:
                    self._trim_old_logs()
                    self.file = open(self.filename, "a")
                else:
                    self.file = open(self.filename, "a")
            except OSError:
                # 文件不存在，创建新文件
                self.file = open(self.filename, "w")
                
        except Exception as e:
            print(f"无法打开日志文件:  {e}")
            self.file = None
    
    def _trim_old_logs(self):
        """删除最旧的日志，保留较新的日志"""
        try:
            # 读取所有日志行
            with open(self.filename, "r") as f:
                lines = f.readlines()
            
            # 计算要保留的行数
            total_lines = len(lines)
            keep_lines = int(total_lines * self.keep_ratio)
            
            if keep_lines < total_lines:
                # 只保留较新的日志
                kept_lines = lines[-keep_lines:] if keep_lines > 0 else []
                
                # 重写文件
                with open(self.filename, "w") as f:
                    f.write("=== 日志文件已修剪（删除最旧的日志） ===\n")
                    f.write(kept_lines)
                
                print(f"日志已修剪：保留 {len(kept_lines)}/{total_lines} 行")
        except Exception as e:
            print(f"修剪日志失败: {e}")
            # 如果修剪失败，清空文件
            try:
                with open(self.filename, "w") as f:
                    f.write("=== 日志文件已重置（修剪失败） ===\n")
            except:
                pass
    
    def _write_raw(self, message):
        """直接写入消息到文件"""
        if self.file:
            try:
                self.file.write(message)
                self.file.flush()
            except Exception as e:
                print(f"写入日志失败: {e}")
    
    def _get_timestamp(self):
        """获取简单的时间戳"""
        if not self.use_timestamp:
            return ""
        
        try:
            t = time.localtime()
            return "{:02d}-{:02d} {:02d}:{:02d}:{:02d}".format(
                t[1], t[2], t[3], t[4], t[5]  # MM-DD HH:MM:SS
            )
        except:
            return ""
    
    def _check_and_trim(self):
        """检查文件大小，如果超过限制则修剪"""
        if not self.file:
            return
        
        try:
            import os
            file_size = os.stat(self.filename)[6]
            
            if file_size > self.max_size:
                # 关闭当前文件
                self.file.close()
                # 修剪日志
                self._trim_old_logs()
                # 重新打开文件
                self.file = open(self.filename, "a")
        except Exception as e:
            print(f"检查文件大小失败: {e}")
    
    def _log(self, level, message):
        """内部日志方法"""
        if level < self.level:
            return
        
        # 检查文件大小，如果需要则修剪
        self._check_and_trim()
        
        # 构建日志消息
        parts = []
        
        # 添加时间戳
        timestamp = self._get_timestamp()
        if timestamp:
            parts. append(f"[{timestamp}]")
        
        # 添加级别
        parts.append(f"[{self.LEVEL_NAMES[level]}]")
        
        # 添加消息
        parts.append(str(message))
        
        log_line = " ".join(parts) + "\n"
        
        # 写入文件
        self._write_raw(log_line)
        
        # 同时输出到控制台（可选）
        print(log_line. rstrip())
    
    def debug(self, message):
        """记录 DEBUG 级别日志"""
        self._log(self.DEBUG, message)
    
    def info(self, message):
        """记录 INFO 级别日志"""
        self._log(self.INFO, message)
    
    def warning(self, message):
        """记录 WARNING 级别日志"""
        self._log(self.WARNING, message)
    
    def error(self, message):
        """记录 ERROR 级别日志"""
        self._log(self.ERROR, message)
    
    def set_filename(self, new_filename):
        """
        更新日志文件名
        
        Args:
            new_filename: 新的日志文件名
            
        Returns:
            bool: 是否成功更新
        """
        try:
            # 关闭当前文件
            if self.file:
                self.file.close()
                self.file = None
            
            # 更新文件名
            old_filename = self.filename
            self.filename = new_filename
            
            # 打开新文件
            self._open_file()
            
            if self.file:
                self._write_raw(f"=== 日志文件从 {old_filename} 切换 ===\n")
                print(f"日志文件已更新: {old_filename} -> {new_filename}")
                return True
            else:
                # 如果打开失败，恢复旧文件名
                self.filename = old_filename
                self._open_file()
                return False
        except Exception as e:
            print(f"更新日志文件名失败: {e}")
            # 尝试恢复
            self.filename = old_filename
            self._open_file()
            return False
    
    def get_filename(self):
        """
        获取当前日志文件名
        
        Returns:
            str: 日志文件名
        """
        return self.filename
    
    def close(self):
        """关闭日志文件"""
        if self.file:
            try:
                self.file.close()
            except:
                pass
            self.file = None
    
    def __del__(self):
        """析构时关闭文件"""
        self.close()


class MemoryLogger: 
    """内存日志记录器（不写文件，仅保存在内存中）"""
    
    def __init__(self, max_lines=50):
        """
        初始化内存日志记录器
        
        Args:
            max_lines: 最大保存行数，默认 50 行
        """
        self.max_lines = max_lines
        self.logs = []
    
    def log(self, message):
        """记录日志到内存"""
        # 添加时间戳
        try:
            t = time.localtime()
            timestamp = "{:02d}-{:02d} {:02d}:{:02d}:{:02d}".format(
                t[1], t[2], t[3], t[4], t[5]
            )
            log_line = f"[{timestamp}] {message}"
        except:
            log_line = str(message)
        
        self.logs.append(log_line)
        
        # 保持在最大行数限制内
        if len(self.logs) > self.max_lines:
            self.logs.pop(0)
        
        print(log_line)
    
    def get_logs(self, last_n=None):
        """
        获取日志
        
        Args:
            last_n: 获取最后 N 条，默认全部
            
        Returns:
            list: 日志列表
        """
        if last_n: 
            return self.logs[-last_n:]
        return self. logs. copy()
    
    def clear(self):
        """清空日志"""
        self.logs.clear()
    
    def save_to_file(self, filename):
        """
        保存日志到文件
        
        Args:
            filename: 文件名
        """
        try:
            with open(filename, "w") as f:
                for log in self.logs:
                    f.write(log + "\n")
            print(f"日志已保存到 {filename}")
            return True
        except Exception as e: 
            print(f"保存日志失败: {e}")
            return False


# ==================== 全局日志实例 ====================
_global_logger = None


def init_logger(filename="_log.txt", max_size=10240, level=SimpleLogger.INFO):
    """
    初始化全局日志记录器
    
    Args: 
        filename: 日志文件名
        max_size:  最大文件大小（字节）
        level: 日志级别
        
    Returns:
        SimpleLogger: 日志记录器实例
    """
    global _global_logger
    _global_logger = SimpleLogger(filename, max_size, level)
    return _global_logger

def update_logger_filename(new_filename=None):
    """更新全局日志记录器的文件名，添加当前日期"""
    global _global_logger
    if not _global_logger:
        return

    if not new_filename:
        return
     
    try:
        success = _global_logger.set_filename(new_filename)
        if success:
            _global_logger.info(f"日志文件已更新为: {new_filename}")
        else:
            _global_logger.error("更新日志文件名失败")
    except Exception as e:
        print(f"更新日志文件名异常: {e}")

def get_logger():
    """
    获取全局日志记录器
    
    Returns: 
        SimpleLogger: 日志记录器实例，如果未初始化则返回 None
    """
    return _global_logger


def log_info(message):
    """记录 INFO 日志（便捷函数）"""
    if _global_logger:
        _global_logger.info(message)
    else:
        print(f"[INFO] {message}")


def log_error(message):
    """记录 ERROR 日志（便捷函数）"""
    if _global_logger:
        _global_logger.error(message)
    else:
        print(f"[ERROR] {message}")


def log_warning(message):
    """记录 WARNING 日志（便捷函数）"""
    if _global_logger:
        _global_logger.warning(message)
    else:
        print(f"[WARNING] {message}")


def log_debug(message):
    """记录 DEBUG 日志（便捷函数）"""
    if _global_logger: 
        _global_logger.debug(message)
    else:
        print(f"[DEBUG] {message}")
