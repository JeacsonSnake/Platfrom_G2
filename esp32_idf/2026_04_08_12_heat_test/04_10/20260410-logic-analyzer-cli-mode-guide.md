# ESP32-S3 逻辑分析仪 CLI 模式配置与使用指南

**日期**: 2026-04-10  
**分支**: `feature/heating`  
**模式**: CLI 模式（命令行接口）  
**硬件**: ESP32-S3-DevKitC-1 + MAX31850 温度传感器  

---

## 1. 方案概述

### 1.1 为什么使用 CLI 模式

原计划的 **Web 模式** 在 ESP-IDF v5.5.2 下存在兼容性问题：
- `httpd_ws_frame_t` 类型未定义
- WebSocket API 与 ESP-IDF v5.5.2 不兼容

**CLI 模式的优势**:
| 特性 | 说明 |
|------|------|
| **单 USB 线连接** | 无需额外 USB-TTL 转换器 |
| **兼容性** | 完全支持 ESP-IDF v5.5.2 |
| **简单采集** | Python 脚本一键采集 |
| **导出 PulseView** | 生成的 `.bin` 文件可直接导入分析 |

### 1.2 工作流程

```
┌─────────────────┐     USB Cable      ┌─────────────────┐
│   PC (Python)   │ ←──────────────→   │   ESP32-S3      │
│                 │   Serial Port      │                 │
│ logic_analyzer  │                    │  CLI Interface  │
│    _cli.py      │                    │                 │
└────────┬────────┘                    └────────┬────────┘
         │                                      │
         │  1. Send config (GPIO, rate, etc)    │
         │ ←────────────────────────────────────┤
         │                                      │
         │  2. Trigger capture                  │
         │ ←────────────────────────────────────┤
         │                                      │
         │  3. Receive raw data                 │
         │ ────────────────────────────────────→│
         │                                      │
         ▼                                      ▼
┌─────────────────┐                    ┌─────────────────┐
│   RowBin.bin    │ ─────────────────→ │    PulseView    │
│   (raw data)    │    Import for      │  (with 1-Wire   │
│                 │    analysis        │   decoder)      │
└─────────────────┘                    └─────────────────┘
```

---

## 2. Menuconfig 配置

### 2.1 完整配置

```
Component config → Logic Analyzer
┌─────────────────────────────────────────────────────────────┐
│  Logic Analyzer Configuration                                │
├─────────────────────────────────────────────────────────────┤
│  GPIO NUM FOR PCLK: 15                                      │
│  ☑️ Use Hi level interrupt                                   │
│  ☐ Use ledc timer for PCLK < 1 mHz                          │
│  ☐ Separate mode                                             │
│  ☐ Logic analyzer output data to http websocket             │
│  ☑️ Command line interface with logic_analyzer_cli.py       │
│     UART or USB_SERIAL_JTAG → USB_SERIAL_JTAG               │
│     uart port number 0-2 → 0                                │
│     uart port baud rate → 921600                            │
│  ☐ Direct connect to Sigrok PulseView                       │
│  ☑️ Use GPIO assignments for channels                        │
│     └── GPIO for channel 0: 14                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 关键配置项

| 选项 | 值 | 说明 |
|------|-----|------|
| **Command line interface** | ☑️ | 启用 CLI 模式 |
| **UART or USB_SERIAL_JTAG** | USB_SERIAL_JTAG | 使用板载 USB |
| **uart port number** | 0 | UART0 |
| **uart port baud rate** | 921600 | 默认波特率 |
| **GPIO for channel 0** | 14 | 1-Wire 总线 |

---

## 3. Python 环境配置

### 3.1 安装依赖

```powershell
# 1. 确保 Python 已安装 (3.8+)
python --version

# 2. 安装 PySerial
pip install pyserial
```

### 3.2 配置文件

首次运行脚本会自动创建 `la_cfg.json`：

```json
{
    "port": "COM9",
    "baudrate": 921600,
    "gpio": 14,
    "samples": 140000,
    "sample_rate": 1000000,
    "channels": 1,
    "trigger": 0,
    "trigger_edge": 0,
    "psram": 0
}
```

**参数说明**:
- `port`: ESP32-S3 的 COM 端口（设备管理器查看）
- `gpio`: 通道 0 的 GPIO 号（14 = 1-Wire）
- `samples`: 采样点数
- `sample_rate`: 采样率（1MHz = 1000000）
- `trigger`: 触发通道（-1 = 无触发）
- `trigger_edge`: 触发边沿（0 = 下降沿, 1 = 上升沿）

---

## 4. 使用流程

### 4.1 构建与烧录

```powershell
# 1. 加载 ESP-IDF 环境
. $env:IDF_PATH\export.ps1

# 2. 配置项目
idf.py menuconfig
# 按上述 CLI 模式配置

# 3. 构建
idf.py build

# 4. 烧录
idf.py -p COM9 flash

# 5. 关闭串口监控（确保端口释放）
# Ctrl + ] 退出 monitor
```

### 4.2 运行采集脚本

```powershell
# 1. 进入脚本目录
cd E:\Platform_G2\esp32_idf\components\logic_analyzer\logic_analyzer_cli

# 2. 修改配置文件（首次运行后生成）
notepad la_cfg.json
# 确保 port 设置为正确的 COM 口

# 3. 运行采集
python logic_analyzer_cli.py
```

### 4.3 采集过程

```
Logic Analyzer CLI
Connecting to COM9 @ 921600 baud...
Connected to ESP32-S3 Logic Analyzer

Configuration:
  GPIO: 14
  Sample Rate: 1000000 Hz
  Samples: 140000
  Channels: 1

Waiting for trigger...
[============================>] 100%
Capture complete!

Saving to RowBin.bin...
Done! File size: 140000 bytes
```

### 4.4 导入 PulseView

1. **打开 PulseView**
2. **导入数据**:
   ```
   File → Import → Raw binary logic data
   ```
3. **配置参数**:
   ```
   File: RowBin.bin
   Channels: 1
   Sample Rate: 1000000 Hz
   ```
4. **添加 1-Wire 解码器**:
   ```
   Add Protocol Decoder → onewire_link
   Data: Channel 0
   
   Add Protocol Decoder → onewire_network
   Link Layer: onewire_link
   ```

---

## 5. 自动化采集脚本

创建 PowerShell 脚本 `capture_and_analyze.ps1`：

```powershell
# capture_and_analyze.ps1
# 一键采集并打开 PulseView

$ESP_PORT = "COM9"
$SCRIPT_DIR = "E:\Platform_G2\esp32_idf\components\logic_analyzer\logic_analyzer_cli"
$PULSEVIEW_PATH = "D:\Software_Toolbox\PulseView\pulseview.exe"

Write-Host "=== ESP32-S3 Logic Analyzer Capture ===" -ForegroundColor Green

# 1. Run capture
Write-Host "Starting capture..." -ForegroundColor Yellow
cd $SCRIPT_DIR
python logic_analyzer_cli.py

if ($LASTEXITCODE -ne 0) {
    Write-Host "Capture failed!" -ForegroundColor Red
    exit 1
}

# 2. Check file
if (-not (Test-Path "RowBin.bin")) {
    Write-Host "Output file not found!" -ForegroundColor Red
    exit 1
}

Write-Host "Capture complete: RowBin.bin" -ForegroundColor Green

# 3. Open PulseView
Write-Host "Opening PulseView..." -ForegroundColor Yellow
& $PULSEVIEW_PATH RowBin.bin

Write-Host "Done!" -ForegroundColor Green
```

使用方式：
```powershell
.\capture_and_analyze.ps1
```

---

## 6. 预留其他模式

### 6.1 切换回 Web 模式（待修复）

当 ESP-IDF 更新或组件修复后，可切换回 Web 模式：

```
☑️ Logic analyzer output data to http websocket
☐ Command line interface
```

### 6.2 切换到 SUMP 模式（需要 USB-TTL）

```
☑️ Direct connect to Sigrok PulseView
    UART or USB_SERIAL_JTAG → UART
    uart port rx pin → 21
    uart port tx pin → 22
☐ Command line interface
```

---

## 7. 常见问题

### 7.1 端口被占用

**症状**: `Could not open port COM9`

**解决**:
```powershell
# 关闭所有串口监控软件
# 检查设备管理器，确认 COM 口号
# 修改 la_cfg.json 中的 port 字段
```

### 7.2 采集数据全为 0

**症状**: PulseView 显示全低电平

**排查**:
1. 检查 GPIO14 接线（4.7KΩ 上拉）
2. 确认 MAX31850 已上电
3. 检查 la_cfg.json 中的 gpio 是否为 14

### 7.3 Python 脚本报错

**症状**: `ModuleNotFoundError: No module named 'serial'`

**解决**:
```powershell
pip install pyserial
```

### 7.4 PulseView 无法识别格式

**症状**: 导入后显示乱码

**解决**:
1. 确认导入格式: **Raw binary logic data**
2. 确认通道数: **1**
3. 确认采样率与采集时一致

---

## 8. 参考文档

- [ok-home/logic_analyzer CLI README](https://github.com/ok-home/logic_analyzer/blob/master/logic_analyzer_cli/README.md)
- [PulseView Import Documentation](https://sigrok.org/wiki/PulseView#Importing_data)

---

**文档版本**: 1.0  
**更新日期**: 2026-04-10  
**状态**: CLI 模式可用
