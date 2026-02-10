#!/usr/bin/env python3
"""ESP32-S3 批量分析脚本"""

import subprocess
import sys
import time

PORT = "COM9"
CHIP = "esp32s3"

def run_cmd(cmd, timeout=60):
    """运行命令并返回输出"""
    try:
        result = subprocess.run(
            cmd, 
            capture_output=True, 
            text=True, 
            timeout=timeout,
            encoding='utf-8',
            errors='ignore'
        )
        return result.stdout + result.stderr
    except Exception as e:
        return f"Error: {e}"

# 1. 芯片基本信息
print("=" * 60)
print("1. 芯片基本信息")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "--port", PORT, "--chip", CHIP, "chip-id"]))

# 2. Flash 信息
print("=" * 60)
print("2. Flash 信息")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "--port", PORT, "--chip", CHIP, "flash-id"]))

# 3. 读取分区表
print("=" * 60)
print("3. 读取分区表")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "--port", PORT, "--chip", CHIP, 
               "read-flash", "0x8000", "0x1000", "partition_table_dump.bin"]))
print(run_cmd(["python", "-m", "esptool", "partition-table", "partition_table_dump.bin"]))

# 4. 读取应用程序信息 (从 0x10000 开始读取 64KB)
print("=" * 60)
print("4. 读取应用程序头部信息")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "--port", PORT, "--chip", CHIP, 
               "read-flash", "0x10000", "0x10000", "app_header.bin"]))

# 5. 使用 image_info 分析
print("=" * 60)
print("5. 分析应用程序镜像")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "image-info", "app_header.bin"]))

# 6. 安全信息
print("=" * 60)
print("6. 安全信息")
print("=" * 60)
print(run_cmd(["python", "-m", "esptool", "--port", PORT, "--chip", CHIP, "get-security-info"]))

print("=" * 60)
print("分析完成!")
print("=" * 60)
