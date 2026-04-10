# ESP32-S3 逻辑分析仪快速参考卡片

**推荐模式**: CLI 模式（USB + Python 脚本）  
**备选模式**: SUMP 模式（需要 USB-TTL）  
**目标 GPIO**: GPIO14 (1-Wire 总线 / MAX31850)  
**PCLK GPIO**: GPIO15 (悬空，内部使用)

---

## 🚀 CLI 模式快速开始

### 1. Menuconfig 配置

```
Component config → Logic Analyzer
┌─────────────────────────────────────────────────────────────┐
│  GPIO NUM FOR PCLK: 15                                      │
│  ☑️ Use Hi level interrupt                                   │
│  ☑️ Command line interface with logic_analyzer_cli.py       │
│     UART or USB_SERIAL_JTAG → USB_SERIAL_JTAG  ← 关键设置   │
│     uart port number 0-2 → 0                                │
│     uart port baud rate → 921600                            │
│  ☐ Direct connect to Sigrok PulseView                       │
│  ☑️ Use GPIO assignments for channels                        │
│     └── GPIO for channel 0: 14                              │
└─────────────────────────────────────────────────────────────┘
```

### 2. 构建与烧录

```powershell
. $env:IDF_PATH\export.ps1
idf.py build
idf.py -p COM9 flash
```

### 3. Python 环境准备

```powershell
pip install pyserial
```

### 4. 运行采集脚本

```powershell
cd E:\Platform_G2\esp32_idf\components\logic_analyzer\logic_analyzer_cli

# 首次运行会生成 la_cfg.json，修改 port 为实际 COM 口
python logic_analyzer_cli.py

# 输出: RowBin.bin
```

### 5. 导入 PulseView

```
1. PulseView → File → Import → Raw binary logic data
2. Select: RowBin.bin
3. Channels: 1
4. Sample Rate: 1000000 Hz
5. Add Protocol Decoder → onewire_link → onewire_network
```

---

## 📋 CLI 模式配置文件

`la_cfg.json`:
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

---

## 🔄 模式切换指南

### 切换到 SUMP 模式（实时 PulseView）

**需要**: USB-TTL 转换器（CH340/CP2102）

```
☐ Command line interface
☑️ Direct connect to Sigrok PulseView
    UART or USB_SERIAL_JTAG → UART
    uart port rx pin → 21
    uart port tx pin → 22
```

**接线**:
```
USB-TTL RX → ESP32 GPIO22 (TX)
USB-TTL TX → ESP32 GPIO21 (RX)
USB-TTL GND → ESP32 GND
```

### Web 模式（暂不推荐）

⚠️ ESP-IDF v5.5.2 存在 WebSocket 兼容性问题

```
☑️ Logic analyzer output data to http websocket
☐ Command line interface
```

---

## 📁 相关文档

| 文档 | 内容 |
|------|------|
| `20260410-logic-analyzer-cli-mode-guide.md` | **CLI 模式完整指南** |
| `20260410-logic_analyzer_pulseview_setup.md` | PulseView 连接指南 |
| `20260410-gpio15-analysis.md` | GPIO15 可用性验证 |
| `20260410-gpio-analysis-high.md` | GPIO > 15 分析 |

---

## ⚠️ 重要提示

1. **CLI 模式是 ESP-IDF v5.5.2 下的最佳选择**
   - 单 USB 线连接
   - 无需额外硬件
   - 完全兼容

2. **GPIO15 必须悬空**

3. **关闭串口监控后再运行 Python 脚本**
   - 避免端口占用

4. **1-Wire 协议采样率 1MHz 足够**

---

**Version**: 1.1 | **Date**: 2026-04-10 | **Mode**: CLI (Recommended)
