# ESP32-S3 逻辑分析仪快速参考卡片

**当前模式**: Web 模式 (WiFi + HTTP WebSocket)  
**目标 GPIO**: GPIO14 (1-Wire 总线 / MAX31850)  
**PCLK GPIO**: GPIO15 (悬空，内部使用)

---

## 🚀 快速开始

### 1. 构建与烧录
```powershell
. $env:IDF_PATH\export.ps1
idf.py build
idf.py -p COM9 flash
idf.py -p COM9 monitor
```

### 2. 获取 IP 地址
查看串口日志，找到：
```
I (4567) LOGIC_ANALYZER: Access: http://192.168.110.xx/la
```

### 3. 访问 Web 界面
浏览器打开：`http://192.168.110.xx/la`

### 4. 采集配置
```
Sample Rate: 1000000 (1 MHz)
Samples: 140000
Channels: 1
Channel 0: GPIO14
Trigger: Falling Edge
```

### 5. 导出到 PulseView
- Web 界面点击 **"Export to Bin"**
- PulseView → Import → Raw binary logic data
- 添加解码器: `onewire_link` + `onewire_network`

---

## 📋 Menuconfig 路径

```
Component config → Logic Analyzer
├── GPIO NUM FOR PCLK: 15
├── ☑️ Use Hi level interrupt
├── ☑️ Logic analyzer output data to http websocket
│   └── ☑️ Start WS server
│       └── ☑️ Connect wifi
│           ├── WiFi SSID: "WeShare-6148"
│           └── WiFi Password: "1234567890"
├── ☑️ Use GPIO assignments for channels
│   └── GPIO for channel 0: 14
└── ☐ Direct connect to Sigrok PulseView (禁用)
```

---

## 🔄 模式切换指南

### 切换到 SUMP 模式（USB + PulseView 实时）
```
☐ Logic analyzer output data to http websocket
☑️ Direct connect to Sigrok PulseView
    UART or USB_SERIAL_JTAG → UART
    uart port number → 0
    uart port baud rate → 921600
    uart port rx pin → 21
    uart port tx pin → 22
```
**需要**: USB-TTL 转换器

### 切换到 CLI 模式（USB + 脚本采集）
```
☐ Logic analyzer output data to http websocket
☑️ Command line interface with logic_analyzer_cli.py
    UART or USB_SERIAL_JTAG → USB_SERIAL_JTAG
    uart port number → 0
    uart port baud rate → 921600
```
**使用**: `python logic_analyzer_cli.py`

---

## 📁 相关文档

| 文档 | 内容 |
|------|------|
| `20260410-logic-analyzer-web-mode-guide.md` | Web 模式完整指南 |
| `20260410-logic_analyzer_pulseview_setup.md` | PulseView 连接指南 |
| `20260410-gpio15-analysis.md` | GPIO15 可用性分析 |
| `20260410-gpio-analysis-high.md` | GPIO > 15 分析 |
| `20260410-menuconfig-guide.md` | Menuconfig 配置指南 |

---

## ⚠️ 注意事项

1. **GPIO15 必须悬空**，不要连接任何外部设备
2. **Web 模式需要 WiFi**，确保 ESP32 和 PC 在同一网络
3. **1-Wire 协议采样率 1MHz 足够**，无需更高
4. **导出文件格式**: Raw binary logic data

---

**Version**: 1.0 | **Date**: 2026-04-10
