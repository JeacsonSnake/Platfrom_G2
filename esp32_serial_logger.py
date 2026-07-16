#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 串口日志记录与分析工具
用于长时间监控和记录 MQTT 连接状态及错误日志

功能:
- 实时读取 ESP32 串口输出 (COM9, 115200 baud)
- 自动保存日志到 network_connect_log/ 目录
- 统计错误类型和连接状态
- 支持 Ctrl+C 安全退出

用法:
    python esp32_serial_logger.py
    python esp32_serial_logger.py --port COM9 --baud 115200
"""

import serial
import serial.tools.list_ports
import sys
import os
import time
import signal
import argparse
from datetime import datetime
from collections import defaultdict
import threading

# 配置参数
DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 115200
# 以脚本所在目录为基准，避免依赖运行时的 CWD
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "network_connect_log")

# 错误模式定义 (用于实时统计)
ERROR_PATTERNS = {
    "TCP_TRANSPORT_ERROR": "TCP传输层错误 (select() timeout)",
    "PING_OR_UNKNOWN_ERROR": "PING响应超时或其他错误",
    "TLS_CANNOT_CONNECT": "TLS无法连接",
    "WIFI_NOT_CONNECTED": "WiFi未连接",
    "TCP_CONNECTION_REFUSED": "TCP连接被拒绝",
    "TCP_CONNECT_TIMEOUT": "TCP连接超时",
}

# 连接事件模式
EVENT_PATTERNS = {
    "[连接事件]": "MQTT连接成功",
    "[断开事件]": "MQTT断开",
    "WiFi connection timeout": "WiFi连接超时",
    "Connected to ap": "WiFi连接成功",
    "Got ip": "获取IP地址",
}


class ESP32SerialLogger:
    """ESP32 串口日志记录器"""
    
    def __init__(self, port=DEFAULT_PORT, baud=DEFAULT_BAUD, dtr=False, rts=False):
        self.port = port
        self.baud = baud
        # 显式控制 DTR/RTS，防止 CP210x 驱动在 open() 后默认拉高，导致 ESP32 被复位或按住 BOOT
        self.dtr = dtr
        self.rts = rts
        self.serial_conn = None
        self.running = False
        self.log_file = None
        self.start_time = None
        
        # 统计数据
        self.stats = {
            "total_lines": 0,
            "error_counts": defaultdict(int),
            "event_counts": defaultdict(int),
            "connect_count": 0,
            "disconnect_count": 0,
            "last_connect_time": None,
            "last_disconnect_time": None,
        }
        
        # 确保日志目录存在
        self._ensure_log_dir()
        
        # 设置信号处理
        signal.signal(signal.SIGINT, self._signal_handler)
        
    def _ensure_log_dir(self):
        """确保日志目录存在"""
        if not os.path.exists(LOG_DIR):
            os.makedirs(LOG_DIR)
            print(f"[INFO] 创建日志目录: {LOG_DIR}")
    
    def _signal_handler(self, signum, frame):
        """处理 Ctrl+C 信号"""
        print("\n[INFO] 收到中断信号，正在安全退出...")
        self.running = False
    
    def _create_log_file(self):
        """创建日志文件"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"esp32_log_{timestamp}.txt"
        filepath = os.path.join(LOG_DIR, filename)
        
        self.log_file = open(filepath, 'w', encoding='utf-8', buffering=1)
        self.start_time = datetime.now()
        
        # 写入文件头
        header = f"""============================================================
ESP32-S3 串口日志记录
开始时间: {self.start_time.strftime("%Y-%m-%d %H:%M:%S")}
串口: {self.port}, 波特率: {self.baud}
============================================================

"""
        self.log_file.write(header)
        self.log_file.flush()
        
        print(f"[INFO] 日志文件已创建: {filepath}")
        return filepath
    
    def _analyze_line(self, line):
        """分析单行日志内容"""
        # 统计错误
        for error_key, description in ERROR_PATTERNS.items():
            if error_key in line:
                self.stats["error_counts"][error_key] += 1
        
        # 统计事件
        for event_key, description in EVENT_PATTERNS.items():
            if event_key in line:
                self.stats["event_counts"][event_key] += 1
        
        # 统计连接/断开次数
        if "[连接事件]" in line:
            self.stats["connect_count"] += 1
            self.stats["last_connect_time"] = datetime.now()
        elif "[断开事件]" in line:
            self.stats["disconnect_count"] += 1
            self.stats["last_disconnect_time"] = datetime.now()
    
    def _print_status(self):
        """打印当前状态统计"""
        if self.stats["total_lines"] % 100 == 0:  # 每100行打印一次状态
            elapsed = (datetime.now() - self.start_time).total_seconds()
            print(f"\n[STATUS] 运行时间: {elapsed/60:.1f}分钟, "
                  f"总行数: {self.stats['total_lines']}, "
                  f"连接: {self.stats['connect_count']}, "
                  f"断开: {self.stats['disconnect_count']}")
            
            if self.stats["error_counts"]:
                print(f"[STATUS] 错误统计:")
                for error, count in sorted(self.stats["error_counts"].items(), 
                                          key=lambda x: x[1], reverse=True)[:5]:
                    print(f"         - {error}: {count}")
    
    def _write_summary(self):
        """写入统计摘要到日志文件"""
        if not self.log_file:
            return
            
        end_time = datetime.now()
        duration = (end_time - self.start_time).total_seconds()
        
        summary = f"""

============================================================
日志记录结束
结束时间: {end_time.strftime("%Y-%m-%d %H:%M:%S")}
总运行时间: {duration/60:.2f} 分钟
总行数: {self.stats['total_lines']}
============================================================
连接统计:
  - MQTT连接次数: {self.stats['connect_count']}
  - MQTT断开次数: {self.stats['disconnect_count']}
  - 连接保持率: {(self.stats['connect_count'] / max(self.stats['disconnect_count'], 1) * 100):.2f}%

错误统计:
"""
        
        if self.stats["error_counts"]:
            for error, count in sorted(self.stats["error_counts"].items(), 
                                      key=lambda x: x[1], reverse=True):
                summary += f"  - {error}: {count}\n"
        else:
            summary += "  - 无错误记录\n"
        
        summary += "============================================================\n"
        
        self.log_file.write(summary)
        self.log_file.flush()
        
        # 同时打印到控制台
        print(summary)
    
    def connect(self):
        """连接串口"""
        try:
            print(f"[INFO] 正在连接串口 {self.port} (波特率: {self.baud})...")
            
            # 列出可用串口
            available_ports = list(serial.tools.list_ports.comports())
            if available_ports:
                print("[INFO] 可用串口:")
                for p in available_ports:
                    print(f"       - {p.device}: {p.description}")
            else:
                print("[WARN] 未检测到可用串口")
            
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1,  # 1秒超时，便于响应Ctrl+C
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )

            # 显式设置 DTR/RTS，避免 CP210x 驱动重装后默认电平变化导致 ESP32 无输出
            self.serial_conn.dtr = self.dtr
            self.serial_conn.rts = self.rts
            # 等待 ESP32 从可能的复位状态稳定启动
            time.sleep(0.2)

            print(f"[INFO] 串口连接成功: {self.port} (DTR={self.dtr}, RTS={self.rts})")
            return True
            
        except serial.SerialException as e:
            print(f"[ERROR] 串口连接失败: {e}")
            print(f"[HINT] 请检查:")
            print(f"       1. ESP32 是否已连接到 {self.port}")
            print(f"       2. 是否有其他程序占用了该串口 (如 idf.py monitor、另一个 Python 实例、串口调试助手)")
            print(f"       3. 驱动程序是否正确安装 (当前设备管理器显示: Silicon Labs CP210x USB to UART Bridge)")
            print(f"       4. 是否有足够的权限访问 {self.port}")
            return False
        except Exception as e:
            print(f"[ERROR] 未知错误: {e}")
            return False
    
    def run(self):
        """主循环：读取和记录日志"""
        if not self.connect():
            return False
        
        log_path = self._create_log_file()
        self.running = True
        
        print("[INFO] 开始记录日志，按 Ctrl+C 停止...")
        print("-" * 60)
        
        try:
            while self.running:
                if self.serial_conn.in_waiting > 0:
                    # 读取一行数据
                    try:
                        raw_data = self.serial_conn.readline()

                        if not raw_data:
                            continue

                        # 尝试多种编码解码，使用 replace 策略避免异常中断
                        line = None
                        for enc in ('utf-8', 'gbk', 'latin-1'):
                            try:
                                line = raw_data.decode(enc, errors='replace').rstrip()
                                break
                            except UnicodeDecodeError:
                                continue

                        if line is None:
                            # 兜底：以 repr 形式记录原始字节，便于排查
                            line = f"<RAW_BYTES: {repr(raw_data)}>"
                            print(f"[WARN] 无法解码的串口数据: {line}")
                        
                        # 添加时间戳
                        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                        log_line = f"[{timestamp}] {line}"
                        
                        # 写入文件
                        self.log_file.write(log_line + '\n')
                        self.log_file.flush()
                        
                        # 分析内容
                        self._analyze_line(line)
                        self.stats["total_lines"] += 1

                        # 实时打印到控制台（可选，可以注释掉减少刷屏）
                        print(log_line)
                        
                        # 定期打印状态
                        self._print_status()
                        
                    except UnicodeDecodeError as e:
                        print(f"[ERROR] 解码异常: {e}, raw_data={repr(raw_data)}")
                    except Exception as e:
                        print(f"[ERROR] 读取数据时出错: {e}")
                
                # 短暂休眠，避免CPU占用过高
                time.sleep(0.001)
                
        except Exception as e:
            print(f"[ERROR] 运行时错误: {e}")
        
        finally:
            self._cleanup()
        
        return True
    
    def _cleanup(self):
        """清理资源"""
        print("\n[INFO] 正在清理资源...")
        
        # 写入统计摘要
        if self.log_file:
            self._write_summary()
            self.log_file.close()
            print("[INFO] 日志文件已关闭")
        
        # 关闭串口
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("[INFO] 串口已关闭")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='ESP32-S3 串口日志记录与分析工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
    python esp32_serial_logger.py                    # 使用默认设置 (COM9, 115200)
    python esp32_serial_logger.py --port COM3        # 指定串口
    python esp32_serial_logger.py --baud 921600      # 指定波特率
        """
    )
    
    parser.add_argument('--port', '-p', 
                        default=DEFAULT_PORT,
                        help=f'串口号 (默认: {DEFAULT_PORT})')
    
    parser.add_argument('--baud', '-b',
                        type=int,
                        default=DEFAULT_BAUD,
                        help=f'波特率 (默认: {DEFAULT_BAUD})')

    parser.add_argument('--dtr',
                        action='store_true',
                        default=False,
                        help='打开串口后置位 DTR (默认: False，防止 ESP32 被复位)')

    parser.add_argument('--rts',
                        action='store_true',
                        default=False,
                        help='打开串口后置位 RTS (默认: False，防止 ESP32 进入 BOOT 模式)')

    args = parser.parse_args()
    
    print("=" * 60)
    print("ESP32-S3 串口日志记录工具")
    print("=" * 60)
    print(f"日志目录: {os.path.abspath(LOG_DIR)}")
    print("-" * 60)
    
    # 创建并运行记录器
    logger = ESP32SerialLogger(port=args.port, baud=args.baud, dtr=args.dtr, rts=args.rts)
    success = logger.run()
    
    if success:
        print("\n[INFO] 程序正常结束")
        return 0
    else:
        print("\n[ERROR] 程序异常结束")
        return 1


if __name__ == "__main__":
    sys.exit(main())
