# ESP32-S3 逻辑分析仪 Web 模式配置与使用指南

**日期**: 2026-04-10  
**分支**: `feature/heating`  
**模式**: Web 界面模式（无线调试）  
**硬件**: ESP32-S3-DevKitC-1 + MAX31850 温度传感器  

---

## 1. 方案概述

### 1.1 Web 模式特点

| 特性 | 说明 |
|------|------|
| **连接方式** | WiFi 无线连接，无需 USB-TTL 转换器 |
| **访问地址** | `http://<esp32-ip>/la` |
| **实时性** | 实时采集，浏览器直接查看波形 |
| **导出功能** | 支持导出 `.bin` 文件到 PulseView 分析 |
| **协议解码** | Web 界面暂无，需导出到 PulseView 解码 |

### 1.2 与其他模式对比

| 模式 | 连接方式 | 实时性 | 协议解码 | 适用场景 |
|------|---------|--------|---------|---------|
| **Web 模式** | WiFi | 实时查看 | 导出后解码 | 无线调试，快速查看波形 |
| SUMP 模式 | USB+UART | 实时 | 支持 | 需要实时协议分析 |
| CLI 模式 | USB | 非实时 | 导出后解码 | 资源受限，低功耗 |

---

## 2. Menuconfig 配置

### 2.1 完整配置路径

```
Component config → Logic Analyzer
```

### 2.2 推荐配置

```
┌─────────────────────────────────────────────────────────────┐
│  Logic Analyzer Configuration                                │
├─────────────────────────────────────────────────────────────┤
│  GPIO NUM FOR PCLK: 15                                      │
│  ☑️ Use Hi level interrupt                                   │
│  ☐ Use ledc timer for PCLK < 1 mHz                          │
│  ☐ Separate mode                                             │
│  ☑️ Logic analyzer output data to http websocket            │
│     └── ☑️ Start WS server                                   │
│         └── ☑️ Connect wifi                                  │
│             └── WiFi SSID: "WeShare-6148"                   │
│             └── WiFi Password: "1234567890"                 │
│  ☐ Command line interface with logic_analyzer_cli.py        │
│  ☐ Direct connect to Sigrok PulseView                       │
│  ☑️ Use GPIO assignments for channels                        │
│     └── GPIO for channel 0: 14                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 配置详解

#### 2.3.1 基础配置

| 选项 | 值 | 说明 |
|------|-----|------|
| **GPIO NUM FOR PCLK** | 15 | ESP32-S3 内部采样时钟，悬空引脚 |
| **Use Hi level interrupt** | ☑️ | 高级中断，触发延迟 0.3μs |

#### 2.3.2 Web 模式配置

| 选项 | 值 | 说明 |
|------|-----|------|
| **Logic analyzer output data to http websocket** | ☑️ | 启用 Web 界面 |
| **Start WS server** | ☑️ | 启动独立 WebSocket 服务器 |
| **Connect wifi** | ☑️ | 连接 WiFi（使用现有连接或新建）|
| **WiFi SSID** | WeShare-6148 | 与项目 WiFi 一致 |
| **WiFi Password** | 1234567890 | 与项目 WiFi 一致 |

#### 2.3.3 通道配置

| 选项 | 值 | 说明 |
|------|-----|------|
| **Use GPIO assignments for channels** | ☑️ | 预设 GPIO 通道 |
| **GPIO for channel 0** | 14 | 1-Wire 总线 |
| **GPIO for channel 1-7** | -1 | 未使用 |

---

## 3. 使用流程

### 3.1 构建与烧录

```powershell
# 1. 加载 ESP-IDF 环境
. $env:IDF_PATH\export.ps1

# 2. 配置项目
idf.py menuconfig
# 按上述配置修改

# 3. 构建
idf.py build

# 4. 烧录
idf.py -p COM9 flash

# 5. 查看日志（获取 IP 地址）
idf.py -p COM9 monitor
```

### 3.2 查看日志确认启动

成功启动后，串口日志应显示：

```
I (1234) LOGIC_ANALYZER: Initializing Logic Analyzer...
I (1234) LOGIC_ANALYZER: Configuration:
I (1234) LOGIC_ANALYZER:   Target: ESP32-S3
I (1234) LOGIC_ANALYZER:   Mode: WebSocket (HTTP)
I (1234) LOGIC_ANALYZER:   PCLK GPIO: 15
I (1234) LOGIC_ANALYZER:   Channel 0: GPIO14 (1-Wire)
I (3456) wifi: connected to WeShare-6148
I (4567) LOGIC_ANALYZER: Web server started
I (4567) LOGIC_ANALYZER: Access: http://192.168.110.xx/la
```

**记录 IP 地址**，后续访问需要。

### 3.3 访问 Web 界面

1. **确保 PC 与 ESP32 在同一网络**
   - PC 连接到 "WeShare-6148" WiFi

2. **浏览器访问**
   ```
   http://192.168.110.xx/la
   ```
   （替换为实际 IP 地址）

3. **Web 界面功能**
   - 设置采样率（Sample Rate）
   - 设置采样数（Samples）
   - 选择通道（Channels）
   - 设置触发条件（Trigger）
   - 启动采集（Start Capture）
   - 导出数据（Export to Bin）

### 3.4 采集 1-Wire 波形

#### 步骤 1: 配置参数

```
Sample Rate: 1000000 (1 MHz)
Samples: 140000
Channels: 1
Channel 0: GPIO14
Trigger: Falling Edge
```

#### 步骤 2: 启动采集

点击 **"Start Capture"** 按钮

#### 步骤 3: 触发采集

Web 界面会等待触发条件。此时可以：
- 等待温度传感器自动轮询（每秒一次）
- 或重启 ESP32 触发 1-Wire 初始化

#### 步骤 4: 查看波形

采集完成后，Web 界面会显示波形图。

#### 步骤 5: 导出数据

点击 **"Export to Bin"** 按钮，下载 `raw_data.bin` 文件。

---

## 4. PulseView 分析导出数据

### 4.1 导入数据

1. 打开 **PulseView**
2. 点击 **"Import"** → **"Raw binary logic data"**
3. 选择下载的 `raw_data.bin` 文件
4. 配置参数：
   ```
   Format: Raw binary logic data
   Channels: 1
   Sample Rate: 1000000 (与采集时一致)
   ```

### 4.2 添加 1-Wire 协议解码器

1. 点击 **"Add Protocol Decoder"**
2. 搜索 **"onewire_link"**
3. 选择 **"1-Wire serial communication bus (link layer)"**
4. 配置：
   ```
   Data: Channel 0
   Overdrive: No
   ```
5. 再次添加 **"onewire_network"**（网络层解码）
6. 配置：
   ```
   Link Layer: 选择已添加的 onewire_link
   ```

### 4.3 分析波形

成功解码后，PulseView 会显示：
- **Reset 脉冲**: 480μs 低电平
- **Presence 脉冲**: 从机响应
- **ROM 命令**: Search ROM (0xF0), Match ROM (0x55) 等
- **数据字节**: Scratchpad 数据

---

## 5. 预留其他模式配置

为了之后方便切换到其他模式，记录以下配置：

### 5.1 预留 UART + SUMP 模式配置

**适用场景**: 需要实时协议分析，有 USB-TTL 转换器

```
☑️ Direct connect to Sigrok PulseView
    UART or USB_SERIAL_JTAG → UART
    uart port number → 0
    uart port baud rate → 921600
    uart port rx pin → 21
    uart port tx pin → 22
☐ Logic analyzer output data to http websocket
☐ Command line interface
```

**硬件需求**:
- USB-TTL 转换器（CH340/CP2102）
- 杜邦线 3 根

**接线**:
```
USB-TTL RX  →  ESP32 GPIO22 (TX)
USB-TTL TX  →  ESP32 GPIO21 (RX)
USB-TTL GND →  ESP32 GND
```

### 5.2 预留 CLI 模式配置

**适用场景**: 资源受限，只需要导出数据

```
☑️ Command line interface with logic_analyzer_cli.py
    UART or USB_SERIAL_JTAG → USB_SERIAL_JTAG
    uart port number → 0
    uart port baud rate → 921600
☐ Direct connect to Sigrok PulseView
☐ Logic analyzer output data to http websocket
```

**使用方式**:
```bash
# 1. 安装 PySerial
pip install pyserial

# 2. 运行采集脚本
python components/logic_analyzer/logic_analyzer_cli/logic_analyzer_cli.py

# 3. 生成 RowBin.bin，导入 PulseView
```

### 5.3 模式切换速查表

| 模式 | 启用选项 | 禁用选项 | 额外硬件 |
|------|---------|---------|---------|
| **Web 模式** | `ANALYZER_USE_WS` | `ANALYZER_USE_SUMP`, `ANALYZER_USE_CLI` | 无 |
| **SUMP 模式** | `ANALYZER_USE_SUMP` + UART | `ANALYZER_USE_WS`, `ANALYZER_USE_CLI` | USB-TTL 转换器 |
| **CLI 模式** | `ANALYZER_USE_CLI` + USB_SERIAL_JTAG | `ANALYZER_USE_WS`, `ANALYZER_USE_SUMP` | 无（单 USB）|

---

## 6. 常见问题

### 6.1 Web 界面无法访问

**症状**: 浏览器显示 "无法访问此网站"

**排查步骤**:
1. 确认 ESP32 已连接 WiFi（查看串口日志）
2. 确认 PC 与 ESP32 在同一网络
   ```bash
   ping 192.168.110.xx
   ```
3. 检查防火墙是否阻断端口 80
4. 尝试使用 IP 地址而非主机名

### 6.2 波形采集无数据

**症状**: 采集完成后波形全为 0 或 1

**排查步骤**:
1. 检查 GPIO14 接线（4.7KΩ 上拉电阻）
2. 确认 MAX31850 已上电
3. 检查 Web 界面通道配置（Channel 0 = GPIO14）
4. 尝试手动触发（断开/连接 1-Wire 总线）

### 6.3 导出的 bin 文件 PulseView 无法识别

**症状**: PulseView 导入失败或显示乱码

**解决**:
1. 确认导入时设置的通道数与采集时一致
2. 确认采样率与采集时一致
3. 尝试使用 "Raw binary logic data" 格式

### 6.4 内存不足

**症状**: Web 界面提示 "Out of memory"

**解决**:
1. 减少采样数（Samples）
2. 使用 PSRAM（如果有）
3. 降低采样率（1-Wire 协议 100KHz 足够）

---

## 7. 参考文档

- [ok-home/logic_analyzer GitHub](https://github.com/ok-home/logic_analyzer)
- [PulseView 官方文档](https://sigrok.org/wiki/PulseView)
- [ESP32-S3-DevKitC-1 用户指南](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)

---

**文档版本**: 1.0  
**作者**: Kimi Code Agent  
**状态**: Web 模式配置完成
