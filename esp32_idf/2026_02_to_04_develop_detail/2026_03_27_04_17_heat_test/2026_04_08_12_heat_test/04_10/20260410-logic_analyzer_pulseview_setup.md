# ESP32-S3 + PulseView 逻辑分析仪配置指南

**日期**: 2026-04-10  
**分支**: `feature/heating`  
**目标**: 使用 PulseView 监控 GPIO14 的 1-Wire 波形  

---

## 1. 硬件连接

### 1.1 无需额外硬件
ESP32-S3-DevKitC-1 已内置 USB-Serial/JTAG 控制器，可直接通过 USB 连接 PulseView。

```
PC (PulseView) ←→ USB Cable ←→ ESP32-S3 USB Port
                                      ↓
                                 GPIO14 (1-Wire)
                                 4.7KΩ Pull-up
                                      ↓
                              MAX31850KATB+ Sensors
```

### 1.2 GPIO 分配
| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO14 | 1-Wire 总线 | 温度传感器数据 |
| GPIO15 | PCLK (内部) | 逻辑分析仪采样时钟（悬空） |

---

## 2. 软件配置

### 2.1 项目配置
已集成 `ok-home/logic_analyzer` 组件到项目：

```
components/
└── logic_analyzer/          # SUMP 协议逻辑分析仪组件
    ├── logic_analyzer_hal/  # HAL 层
    ├── logic_analyzer_sump/ # SUMP 协议实现
    └── ...
```

### 2.2 menuconfig 配置
运行以下命令配置逻辑分析仪：

```powershell
idf.py menuconfig
```

配置路径：
```
Component config → Logic Analyzer
├── [*] Enable Logic Analyzer                      # 启用逻辑分析仪
├── [*] Enable SUMP protocol                       # 启用 SUMP 协议
├── [*] Enable WEB interface                       # 启用 Web 界面（可选）
├── [ ] Enable Hi level interrupt                  # 高级中断（可选）
│
ESP32S3 Settings
├── (15) GPIO number for PCLK transit             # PCLK GPIO（悬空引脚）
└── [*] Use LEDC timer for slow PCLK (<1MHz)      # 低速采样时使用 LEDC

Project Configuration
├── [*] Enable Logic Analyzer for 1-Wire Debugging
├── (15) PCLK GPIO (unused pin for ESP32-S3)
├── (1000000) Default Sample Rate (Hz)
└── (140000) Maximum Sample Count
```

---

## 3. 构建与烧录

### 3.1 完整构建流程

```powershell
# 1. 进入项目目录
cd E:\Platform_G2\esp32_idf

# 2. 设置 ESP-IDF 环境
. $env:IDF_PATH\export.ps1

# 3. 清理并重新配置
idf.py fullclean
idf.py set-target esp32s3

# 4. 配置项目（按需修改）
idf.py menuconfig

# 5. 构建项目
idf.py build

# 6. 烧录固件（按实际情况修改 COM 口）
idf.py -p COM9 flash

# 7. 打开串口监控（查看初始化日志）
idf.py -p COM9 monitor
```

### 3.2 烧录模式进入方法
1. 按住 **BOOT** 按钮
2. 按一下 **RESET** 按钮
3. 松开 **BOOT** 按钮
4. 端口可能变化（如 COM8 → COM9）

---

## 4. PulseView 配置

### 4.1 启动 PulseView
打开 PulseView 软件，点击 **Connect to a Device**。

### 4.2 设备连接配置

| 参数 | 值 | 说明 |
|------|-----|------|
| **Driver** | Openbench Logic Sniffer & SUMP Compatibles | SUMP 协议驱动 |
| **Interface** | Serial Port | 串口连接 |
| **Serial Port** | COMx (ESP32-S3 的 USB 端口) | 根据实际情况选择 |
| **Baud Rate** | 921600 | 默认波特率 |

**操作步骤：**
1. 选择 **Driver**: `Openbench Logic Sniffer & SUMP Compatibles`
2. 选择 **Interface**: `Serial Port`
3. 选择 **Serial Port**: ESP32-S3 对应的 COM 口
4. 设置 **Baud Rate**: `921600`
5. 点击 **Scan for Devices**
6. 设备列表中应出现 `ESP32 with 8/16 channels`
7. 选择设备并点击 **OK**

### 4.3 采样配置

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| **Sample Rate** | 1 MHz | 1-Wire 协议最大 16.3Kbps，1MHz 足够 |
| **Samples** | 140000 | ESP32-S3 RAM 模式最大采样数 |
| **Channels** | 1 (Channel 0) | 仅采样 GPIO14 |
| **Trigger** | Falling Edge | 1-Wire 起始位为下降沿 |
| **Trigger Channel** | 0 | GPIO14 映射到通道 0 |

**触发设置：**
- 启用 **Trigger**
- 选择 **Mode**: Simple
- 选择 **Edge**: Falling
- 通道 0 打勾

---

## 5. 1-Wire 协议解码器

### 5.1 添加协议解码器

1. 点击 **Add Protocol Decoder**
2. 选择 **1-Wire serial communication bus (link layer)** (`onewire_link`)
3. 配置参数：
   - **Data**: Channel 0 (GPIO14)
   - **Overdrive**: No (正常模式)

3. 再添加 **1-Wire serial communication bus (network layer)** (`onewire_network`)
4. 配置参数：
   - **Link Layer**: 选择已添加的 onewire_link

### 5.2 解码结果

成功解码后，PulseView 会显示：
- **ROM Command**: Search ROM (0xF0), Match ROM (0x55), Skip ROM (0xCC) 等
- **ROM ID**: 64-bit 设备地址
- **Function Command**: Convert T (0x44), Read Scratchpad (0xBE) 等
- **Data**: Scratchpad 数据（9字节）

---

## 6. 时序分析

### 6.1 关键时序参数

| 参数 | 标准值 | 脉冲显示 |
|------|--------|----------|
| **Reset Pulse** | 480 μs | 长低电平脉冲 |
| **Presence Pulse** | 60-240 μs | 从机响应低电平 |
| **Write 0** | 60 μs 低电平 | 中长度低电平 |
| **Write 1** | 6 μs 低电平 | 短低电平 |
| **Read Slot** | 1 μs 低电平 + 采样 | 短低电平后采样 |

### 6.2 波形特征

```
Reset:     ━━━━━━━━━━━┓                        ┏━━━━━━━━━━━━
                      ┗━━━━━━━━━━━━━━━━━━━━━━━━┛
                       └────── 480μs ──────────┘

Presence:  ━━━━━━━━━━━━━━━━━━━━┓          ┏━━━━━━━━━━━━━━━━
                               ┗━━━━━━━━━━┛
                                └─60-240μs─┘

Write 0:   ━━━━━━━━━━━━━━━━━━━━┓          ┏━━━━━━━━━━━━━━━━
                               ┗━━━━━━━━━━┛
                                └── 60μs ──┘

Write 1:   ━━━━━━━━━━━━━━━━━━━━┓ ┏━━━━━━━━━━━━━━━━━━━━━━━━━
                               ┗━┛
                                └6μs┘
```

---

## 7. 常见问题

### 7.1 PulseView 无法连接

**症状**: Scan for Devices 找不到设备

**解决**:
1. 确认 ESP32-S3 已烧录固件并运行
2. 检查串口号是否正确（设备管理器查看）
3. 尝试重启 ESP32-S3 后再次 Scan
4. 检查波特率是否为 921600

### 7.2 采样数据全为 0 或 1

**症状**: 通道显示恒高或恒低

**解决**:
1. 检查 GPIO14 连接（4.7KΩ 上拉电阻）
2. 确认 MAX31850 已上电
3. 检查逻辑分析仪通道配置（Channel 0 对应 GPIO14）

### 7.3 触发不工作

**症状**: 点击 Run 后无法触发采集

**解决**:
1. 确认 Trigger 已启用
2. 选择正确的触发边沿（1-Wire 起始为下降沿）
3. 尝试手动触发（Disable Trigger，Free Running 模式）
4. 按两次 Run 按钮（已知 PulseView bug）

### 7.4 UART0 日志干扰

**症状**: 连接后显示异常数据

**解决**:
1. 进入 menuconfig → Component config → Log output
2. 设置 **Default log verbosity** → **No output**
3. 或使用 USB-Serial/JTAG 的备用 UART

```
menuconfig path:
Component config → Log output → Default log verbosity → No output
```

---

## 8. 高级用法

### 8.1 Web 界面（无线调试）

启用 Web 界面后，可通过浏览器查看波形：

1. menuconfig 中启用 `ANALYZER_USE_WS`
2. 连接到 ESP32-S3 的 WiFi 或同一网络
3. 浏览器访问: `http://<esp32-ip>/la`

### 8.2 CLI 模式（低资源占用）

适用于 RAM 紧张的情况：

1. 启用 `ANALYZER_USE_CLI`
2. 使用 Python 脚本采集数据：
```bash
python components/logic_analyzer/logic_analyzer_cli/logic_analyzer_cli.py
```

---

## 9. 参考文档

- [ok-home/logic_analyzer GitHub](https://github.com/ok-home/logic_analyzer)
- [PulseView 官方文档](https://sigrok.org/wiki/PulseView)
- [SUMP Protocol Specification](http://www.sump.org/projects/analyzer/protocol/)
- [MAX31850 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31850-MAX31851.pdf)
- [1-Wire Protocol](https://www.analog.com/en/technical-articles/1wire-communication-through-software.html)

---

**作者**: Kimi Code Agent  
**状态**: 实现完成，待测试验证  
**下一步**: 烧录固件并连接 PulseView 验证波形采集
