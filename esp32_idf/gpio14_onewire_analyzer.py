#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 GPIO14 1-Wire 波形分析工具
基于串口日志重建和分析1-Wire总线波形

功能:
- 实时读取 ESP32 串口输出，解析1-Wire通信日志
- 重建1-Wire波形时序图
- 分析ROM搜索过程和CRC错误模式
- 验证时序参数是否符合MAX31850规范

用法:
    python gpio14_onewire_analyzer.py
    python gpio14_onewire_analyzer.py --port COM9 --log-file 2026_04_08_12_heat_test/04_10/esp32_log_20260410_130529.txt
"""

import serial
import serial.tools.list_ports
import sys
import os
import re
import time
import signal
import argparse
from datetime import datetime
from collections import defaultdict, deque
from dataclasses import dataclass
from typing import List, Tuple, Optional

# 配置参数
DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 115200
ANALYSIS_DIR = "onewire_analysis"

# 1-Wire时序规范 (MAX31850数据手册)
ONEWIRE_TIMING_SPEC = {
    "reset_low_min": 480,      # us
    "reset_low_max": 960,      # us (注意: >960us可能触发POR)
    "presence_wait": 70,       # us
    "presence_low_min": 60,    # us
    "presence_low_max": 240,   # us
    "write0_low_min": 60,      # us
    "write0_low_max": 120,     # us
    "write1_low_min": 1,       # us
    "write1_low_max": 15,      # us
    "read_slot_min": 60,       # us
    "read_slot_max": 120,      # us
    "read_sample_max": 15,     # us (关键参数)
}

# MAX31850家族码
MAX31850_FAMILY_CODE = 0x3B


@dataclass
class ROMDevice:
    """1-Wire设备ROM信息"""
    family_code: int
    serial: bytes
    crc: int
    crc_valid: bool
    raw_bits: str = ""
    
    @property
    def rom_id(self) -> str:
        return f"{self.family_code:02X}{self.serial.hex().upper()}{self.crc:02X}"


@dataclass  
class WaveformEvent:
    """波形事件"""
    timestamp: datetime
    event_type: str  # RESET, PRESENCE, WRITE0, WRITE1, READ
    duration_us: int
    level: int  # 0 or 1
    data: Optional[str] = None


class OneWireLogAnalyzer:
    """1-Wire日志分析器"""
    
    def __init__(self, port: Optional[str] = None, baud: int = 115200, 
                 log_file: Optional[str] = None):
        self.port = port
        self.baud = baud
        self.log_file_path = log_file
        self.serial_conn = None
        self.running = False
        self.analysis_file = None
        
        # 解析状态
        self.current_rom_search = None
        self.bit_buffer = []
        self.devices_found = []
        self.waveform_events = deque(maxlen=1000)
        
        # 统计
        self.stats = {
            "reset_count": 0,
            "presence_detected": 0,
            "rom_searches": 0,
            "crc_failures": 0,
            "crc_success": 0,
            "devices_found": 0,
        }
        
        # 时序分析
        self.timing_violations = []
        
        self._ensure_dir()
        signal.signal(signal.SIGINT, self._signal_handler)
    
    def _ensure_dir(self):
        """确保分析目录存在"""
        if not os.path.exists(ANALYSIS_DIR):
            os.makedirs(ANALYSIS_DIR)
    
    def _signal_handler(self, signum, frame):
        print("\n[INFO] 收到中断信号，正在保存分析结果...")
        self.running = False
    
    def _create_analysis_file(self):
        """创建分析输出文件"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"onewire_analysis_{timestamp}.txt"
        filepath = os.path.join(ANALYSIS_DIR, filename)
        self.analysis_file = open(filepath, 'w', encoding='utf-8', buffering=1)
        
        header = f"""============================================================
ESP32-S3 GPIO14 1-Wire 波形分析报告
分析时间: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}
数据来源: {self.log_file_path or self.port}
============================================================

1-Wire 时序规范 (MAX31850):
  - Reset Low: {ONEWIRE_TIMING_SPEC['reset_low_min']}-{ONEWIRE_TIMING_SPEC['reset_low_max']} us
  - Presence Wait: {ONEWIRE_TIMING_SPEC['presence_wait']} us
  - Presence Low: {ONEWIRE_TIMING_SPEC['presence_low_min']}-{ONEWIRE_TIMING_SPEC['presence_low_max']} us
  - Write 0 Low: {ONEWIRE_TIMING_SPEC['write0_low_min']}-{ONEWIRE_TIMING_SPEC['write0_low_max']} us
  - Write 1 Low: {ONEWIRE_TIMING_SPEC['write1_low_min']}-{ONEWIRE_TIMING_SPEC['write1_low_max']} us
  - Read Sample: <{ONEWIRE_TIMING_SPEC['read_sample_max']} us (关键!)

============================================================

"""
        self.analysis_file.write(header)
        self.analysis_file.flush()
        return filepath
    
    def _parse_rom_bits(self, line: str) -> Optional[str]:
        """解析ROM位字符串"""
        match = re.search(r'ROM bits: ([01]{64})', line)
        if match:
            return match.group(1)
        return None
    
    def _parse_crc_result(self, line: str) -> Optional[bool]:
        """解析CRC检查结果"""
        if "CRC check: FAIL" in line:
            return False
        elif "CRC check: PASS" in line:
            return True
        return None
    
    def _parse_family_code(self, line: str) -> Optional[int]:
        """解析Family Code"""
        match = re.search(r'Family=0x([0-9A-Fa-f]{2})', line)
        if match:
            return int(match.group(1), 16)
        return None
    
    def _parse_timing(self, line: str) -> dict:
        """解析时序参数"""
        timing = {}
        
        # 解析Reset时序
        match = re.search(r'reset=(\d+)us', line)
        if match:
            timing['reset'] = int(match.group(1))
        
        # 解析wait时序
        match = re.search(r'wait=(\d+)us', line)
        if match:
            timing['wait'] = int(match.group(1))
        
        return timing
    
    def _analyze_rom_bits(self, bits: str) -> ROMDevice:
        """分析64位ROM数据"""
        if len(bits) != 64:
            return None
        
        # 1-Wire是LSB first，需要反转每个字节
        bytes_data = []
        for i in range(8):
            byte_bits = bits[i*8:(i+1)*8]
            # 反转位顺序
            byte_val = int(byte_bits[::-1], 2)
            bytes_data.append(byte_val)
        
        family_code = bytes_data[0]
        serial = bytes(bytes_data[1:7])
        crc = bytes_data[7]
        
        # 计算CRC8 (X8 + X5 + X4 + 1 = 0x31)
        crc_calc = self._calc_crc8(bytes_data[:7])
        crc_valid = (crc_calc == crc)
        
        return ROMDevice(
            family_code=family_code,
            serial=serial,
            crc=crc,
            crc_valid=crc_valid,
            raw_bits=bits
        )
    
    def _calc_crc8(self, data: List[int]) -> int:
        """计算CRC8-MAXIM"""
        crc = 0
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x80:
                    crc = ((crc << 1) ^ 0x31) & 0xFF
                else:
                    crc = (crc << 1) & 0xFF
        return crc
    
    def _generate_waveform(self, events: List[WaveformEvent]) -> str:
        """生成ASCII波形图"""
        if not events:
            return ""
        
        lines = []
        lines.append("\n--- 1-Wire 波形时序图 ---")
        lines.append("时间(us) | 波形")
        lines.append("-" * 80)
        
        current_time = 0
        waveform = ""
        
        for event in events:
            if event.event_type == "RESET":
                lines.append(f"{current_time:8} | {'_' * 10}{'=' * 48}{'_' * 10} RESET")
                current_time += event.duration_us
            elif event.event_type == "PRESENCE":
                lines.append(f"{current_time:8} | {'_' * 10}{'=' * 12}{'_' * 10} PRESENCE")
                current_time += event.duration_us
            elif event.event_type == "WRITE0":
                lines.append(f"{current_time:8} | {'_' * 10}{'=' * 6}{'_' * 4} 0")
                current_time += event.duration_us
            elif event.event_type == "WRITE1":
                lines.append(f"{current_time:8} | {'_' * 10}{'=' * 1}{'_' * 9} 1")
                current_time += event.duration_us
        
        return "\n".join(lines)
    
    def _check_timing_violation(self, event: WaveformEvent) -> Optional[str]:
        """检查时序违规"""
        spec = ONEWIRE_TIMING_SPEC
        
        if event.event_type == "RESET":
            if event.duration_us < spec['reset_low_min']:
                return f"Reset太短: {event.duration_us}us (最小{spec['reset_low_min']}us)"
            if event.duration_us > spec['reset_low_max']:
                return f"Reset太长: {event.duration_us}us (最大{spec['reset_low_max']}us, 可能触发POR)"
        
        return None
    
    def _analyze_line(self, line: str, timestamp: datetime):
        """分析单行日志"""
        
        # 检测Reset波形
        if "1-Wire Reset Waveform" in line:
            self.stats["reset_count"] += 1
            self._write(f"\n[{timestamp.strftime('%H:%M:%S.%f')[:-3]}] 检测到Reset脉冲")
        
        # 检测Presence
        if "Presence detected: YES" in line:
            self.stats["presence_detected"] += 1
            self._write("  -> Presence检测成功")
        elif "Presence detected: NO" in line:
            self._write("  -> Presence检测失败!")
        
        # 解析时序参数
        timing = self._parse_timing(line)
        if timing:
            timing_str = ", ".join([f"{k}={v}us" for k, v in timing.items()])
            self._write(f"  时序参数: {timing_str}")
        
        # 解析ROM位
        rom_bits = self._parse_rom_bits(line)
        if rom_bits:
            self._write(f"\n  ROM位流: {rom_bits}")
            device = self._analyze_rom_bits(rom_bits)
            if device:
                self.devices_found.append(device)
                self._write(f"  Family Code: 0x{device.family_code:02X} " + 
                           f"({'MAX31850' if device.family_code == MAX31850_FAMILY_CODE else '未知'})")
                self._write(f"  Serial: {device.serial.hex().upper()}")
                self._write(f"  CRC: 0x{device.crc:02X} (计算值: 0x{self._calc_crc8([device.family_code] + list(device.serial)):02X})")
                self._write(f"  CRC验证: {'通过' if device.crc_valid else '失败'}")
        
        # 解析CRC结果
        crc_result = self._parse_crc_result(line)
        if crc_result is not None:
            if crc_result:
                self.stats["crc_success"] += 1
            else:
                self.stats["crc_failures"] += 1
        
        # 解析Family Code接受
        if "Family Code valid" in line:
            family = self._parse_family_code(line)
            if family == MAX31850_FAMILY_CODE:
                self._write(f"  ✓ Family Code 0x{family:02X} 验证通过，接受设备")
        
        # 检测ROM搜索完成
        if "Total devices found" in line:
            match = re.search(r'Total devices found: (\d+)', line)
            if match:
                self.stats["devices_found"] = int(match.group(1))
                self.stats["rom_searches"] += 1
    
    def _write(self, text: str):
        """写入分析文件"""
        if self.analysis_file:
            self.analysis_file.write(text + "\n")
            self.analysis_file.flush()
        print(text)
    
    def _print_summary(self):
        """打印分析摘要"""
        summary = f"""
============================================================
分析摘要
============================================================
1-Wire通信统计:
  - Reset次数: {self.stats['reset_count']}
  - Presence检测成功: {self.stats['presence_detected']}
  - ROM搜索次数: {self.stats['rom_searches']}
  - 发现设备数: {self.stats['devices_found']}

CRC校验统计:
  - 成功: {self.stats['crc_success']}
  - 失败: {self.stats['crc_failures']}
  - 成功率: {(self.stats['crc_success'] / max(self.stats['crc_success'] + self.stats['crc_failures'], 1) * 100):.1f}%

发现的设备:
"""
        for i, dev in enumerate(self.devices_found[-4:], 1):
            summary += f"  [{i}] ROM={dev.rom_id}, CRC={'OK' if dev.crc_valid else 'FAIL'}\n"
        
        summary += """
关键发现:
"""
        if self.stats['crc_failures'] > self.stats['crc_success']:
            summary += """  ⚠️ CRC失败率过高！可能原因:
    1. 上拉电阻过弱(4.7KΩ)，建议更换为1-2.2KΩ
    2. 采样点过早，建议延后到15-20μs
    3. 总线电容过大，建议缩短走线
"""
        
        summary += "============================================================\n"
        self._write(summary)
    
    def analyze_log_file(self, filepath: str):
        """分析日志文件"""
        print(f"[INFO] 正在分析日志文件: {filepath}")
        
        self._create_analysis_file()
        
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    line = line.strip()
                    
                    # 提取时间戳
                    timestamp_match = re.match(r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]', line)
                    if timestamp_match:
                        timestamp = datetime.strptime(timestamp_match.group(1), "%Y-%m-%d %H:%M:%S.%f")
                        content = line[timestamp_match.end():].strip()
                    else:
                        timestamp = datetime.now()
                        content = line
                    
                    self._analyze_line(content, timestamp)
            
            self._print_summary()
            
        except Exception as e:
            print(f"[ERROR] 分析日志文件时出错: {e}")
    
    def monitor_serial(self):
        """实时监控串口"""
        try:
            print(f"[INFO] 正在连接串口 {self.port}...")
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                timeout=1
            )
            
            self._create_analysis_file()
            self.running = True
            
            print("[INFO] 开始监控1-Wire通信，按 Ctrl+C 停止...")
            
            while self.running:
                if self.serial_conn.in_waiting > 0:
                    try:
                        line = self.serial_conn.readline().decode('utf-8').strip()
                        timestamp = datetime.now()
                        self._analyze_line(line, timestamp)
                    except Exception as e:
                        print(f"[ERROR] 读取串口时出错: {e}")
                
                time.sleep(0.001)
            
            self._print_summary()
            
        except serial.SerialException as e:
            print(f"[ERROR] 串口连接失败: {e}")
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
            if self.analysis_file:
                self.analysis_file.close()
    
    def run(self):
        """主入口"""
        if self.log_file_path:
            self.analyze_log_file(self.log_file_path)
        elif self.port:
            self.monitor_serial()
        else:
            print("[ERROR] 请指定日志文件路径(--log-file)或串口号(--port)")


def main():
    parser = argparse.ArgumentParser(
        description='ESP32-S3 GPIO14 1-Wire 波形分析工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
    python gpio14_onewire_analyzer.py --log-file 2026_04_08_12_heat_test/04_10/esp32_log_20260410_130529.txt
    python gpio14_onewire_analyzer.py --port COM9
        """
    )
    
    parser.add_argument('--port', '-p', default=DEFAULT_PORT,
                       help=f'串口号 (默认: {DEFAULT_PORT})')
    parser.add_argument('--baud', '-b', type=int, default=DEFAULT_BAUD,
                       help=f'波特率 (默认: {DEFAULT_BAUD})')
    parser.add_argument('--log-file', '-f', help='日志文件路径')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("ESP32-S3 GPIO14 1-Wire 波形分析工具")
    print("=" * 60)
    
    analyzer = OneWireLogAnalyzer(
        port=args.port,
        baud=args.baud,
        log_file=args.log_file
    )
    analyzer.run()


if __name__ == "__main__":
    main()
