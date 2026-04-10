# ESP32-S3 MAX31850 温度传感器逻辑分析仪调试方案

**日期**: 2026-04-10  
**分支**: `feature/heating`  
**任务描述**: 为 MAX31850KATB+ 1-Wire 温度传感器驱动配置逻辑分析仪，用于捕获 GPIO14 波形进行时序分析，解决 CRC 校验失败和 MQTT 连接受影响问题

---

## 1. 问题背景

### 1.1 历史调试记录
根据 `2026_04_08_12_heat_test/04_10/20260410-heat_detect_001_README.md` 的分析：

| 阶段 | 日期 | 状态 |
|------|------|------|
| 初始调试 | 2026-04-08 | ROM 搜索发现设备，但 CRC 校验失败 |
| 时序调试 | 2026-04-09 | 发现 20μs 采样点可正常读取 |
| 回退验证 | 2026-04-10 | 代码卷回原状态，等待硬件改进 |

### 1.2 核心问题
MAX31850 驱动使用 1-Wire 协议，存在以下问题：
- **硬件层面**: 4.7KΩ 上拉电阻导致信号上升沿过慢
- **软件层面**: 1-Wire 协议需要精确时序，关键区保护与 FreeRTOS 调度冲突
- **系统层面**: MQTT 协议对延迟敏感，长时间禁用中断导致 TCP/IP 栈超时

### 1.3 本次任务目标
配置逻辑分析仪捕获 GPIO14（1-Wire 总线）波形，用于：
- 分析 1-Wire 时序（Reset、Presence、Read/Write slots）
- 验证采样点（当前 13μs）是否合适
- 为硬件改进（更换 1KΩ 上拉电阻）提供数据支撑

---

## 2. 逻辑分析仪方案设计

### 2.1 需求分析

| 需求项 | 说明 |
|--------|------|
| **目标 GPIO** | GPIO14（1-Wire 总线，MAX31850） |
| **PCLK GPIO** | GPIO15（悬空，ESP32-S3 内部使用） |
| **采样率** | 1 MHz（1-Wire 协议最大 16.3Kbps，足够） |
| **采样数** | 140000（ESP32-S3 RAM 模式） |
| **连接方式** | 待定（Web/CLI/SUMP 三种模式评估） |
| **协议解码** | PulseView 1-Wire 解码器（onewire_link + onewire_network） |

### 2.2 硬件连接

```
PC (PulseView/Web) ←→ WiFi/USB ←→ ESP32-S3
                                           ↓
                                      GPIO14 (1-Wire)
                                      4.7KΩ Pull-up
                                           ↓
                                   MAX31850KATB+ (4x)
```

---

## 3. 实现方案与评估

### 3.1 方案一：Web 模式（首选，但存在兼容性问题）

**设计思路**：
- ESP32-S3 启动 WebSocket HTTP 服务器
- PC 通过浏览器访问 `http://<esp32-ip>/la`
- 实时采集并显示波形
- 支持导出 `.bin` 文件到 PulseView

**Menuconfig 配置**：
```
Component config → Logic Analyzer
├── GPIO NUM FOR PCLK: 15
├── ☑️ Use Hi level interrupt
├── ☑️ Logic analyzer output data to http websocket
│   └── ☑️ Start WS server
│       └── ☑️ Connect wifi
├── ☐ Direct connect to Sigrok PulseView
└── ☑️ Use GPIO assignments for channels
    └── GPIO for channel 0: 14
```

**遇到的问题**：
```
E:/Platform_G2/esp32_idf/components/logic_analyzer/logic_analyzer_ws/logic_analyzer_ws.c:104:5: 
error: unknown type name 'httpd_ws_frame_t'
     httpd_ws_frame_t ws_pkt;
     ^~~~~~~~~~~~~~~~
```

**原因分析**：
- `logic_analyzer` 组件与 ESP-IDF v5.5.2 的 WebSocket API 不兼容
- `httpd_ws_frame_t` 类型在 ESP-IDF v5.5.2 中定义变更

**结论**：**暂不可用**，需等待组件更新或降级 ESP-IDF

---

### 3.2 方案二：CLI 模式（推荐，完全兼容）

**设计思路**：
- ESP32-S3 通过 USB-JTAG 与 PC 通信
- Python 脚本 `logic_analyzer_cli.py` 发送采集配置
- 接收原始数据并保存为 `RowBin.bin`
- 导入 PulseView 进行协议解码

**Menuconfig 配置**：
```
Component config → Logic Analyzer
├── GPIO NUM FOR PCLK: 15
├── ☑️ Use Hi level interrupt
├── ☑️ Command line interface with logic_analyzer_cli.py
│   └── UART or USB_SERIAL_JTAG → USB_SERIAL_JTAG
│   └── uart port number → 0
│   └── uart port baud rate → 921600
├── ☐ Direct connect to Sigrok PulseView
└── ☑️ Use GPIO assignments for channels
    └── GPIO for channel 0: 14
```

**优势**：
| 特性 | 说明 |
|------|------|
| **单 USB 线** | 使用板载 USB-JTAG，无需额外硬件 |
| **完全兼容** | 与 ESP-IDF v5.5.2 无兼容性问题 |
| **简单采集** | `python logic_analyzer_cli.py` 一键采集 |
| **导出分析** | `RowBin.bin` 直接导入 PulseView |

**使用流程**：
```powershell
# 1. 安装依赖
pip install pyserial

# 2. 运行采集脚本
cd components/logic_analyzer/logic_analyzer_cli
python logic_analyzer_cli.py

# 3. 导入 PulseView
# File → Import → Raw binary logic data → RowBin.bin
# Channels: 1, Sample Rate: 1000000 Hz
```

---

### 3.3 方案三：SUMP 模式（实时，需要额外硬件）

**设计思路**：
- ESP32-S3 通过 UART 与 PC 通信
- PulseView 使用 SUMP 协议直接连接
- 实时采集和分析

**Menuconfig 配置**：
```
Component config → Logic Analyzer
├── GPIO NUM FOR PCLK: 15
├── ☑️ Use Hi level interrupt
├── ☑️ Direct connect to Sigrok PulseView
│   └── UART or USB_SERIAL_JTAG → UART
│   └── uart port number → 0
│   └── uart port baud rate → 921600
│   └── uart port rx pin → 21
│   └── uart port tx pin → 22
└── ☑️ Use GPIO assignments for channels
    └── GPIO for channel 0: 14
```

**硬件需求**：
- USB-TTL 转换器（CH340/CP2102）
- 杜邦线 3 根

**接线**：
```
USB-TTL RX  →  ESP32 GPIO22 (TX)
USB-TTL TX  →  ESP32 GPIO21 (RX)
USB-TTL GND →  ESP32 GND
```

**缺点**：需要额外 USB-TTL 硬件

---

## 4. GPIO 可用性分析

### 4.1 已使用 GPIO

| GPIO | 功能 | 状态 |
|------|------|------|
| GPIO 1 | Motor 0 PWM | 已用 |
| GPIO 2 | Motor 0 FG | 已用 |
| GPIO 4 | Motor 1 PWM | 已用 |
| GPIO 5 | Motor 1 FG | 已用 |
| GPIO 6 | Motor 2 PWM | 已用 |
| GPIO 7 | Motor 2 FG | 已用 |
| GPIO 8 | Motor 3 PWM | 已用 |
| GPIO 9 | Motor 3 FG | 已用 |
| GPIO 14 | 1-Wire (MAX31850) | 已用（目标） |
| GPIO 15 | PCLK | 可用（悬空） |
| GPIO 48 | RGB LED | 已用 |

### 4.2 推荐的高编号 GPIO（用于 UART 模式）

| GPIO | 可用性 | 说明 |
|------|--------|------|
| **GPIO 21** | ✅ 推荐 | 通用 GPIO，未使用 |
| **GPIO 22** | ✅ 推荐 | 通用 GPIO，未使用 |
| GPIO 10-13 | ❌ 不可用 | 用户确认后续占用 |

---

## 5. 文档创建记录

本次方案设计过程中创建了以下文档：

| 文档 | 路径 | 内容 |
|------|------|------|
| **GPIO15 分析** | `04_10/20260410-gpio15-analysis.md` | GPIO15 可用性验证 |
| **GPIO > 15 分析** | `04_10/20260410-gpio-analysis-high.md` | 高编号 GPIO 备选 |
| **Menuconfig 指南** | `04_10/20260410-menuconfig-guide.md` | 配置参数说明 |
| **PulseView 连接** | `04_10/20260410-logic_analyzer_pulseview_setup.md` | 各种模式连接指南 |
| **Web 模式指南** | `04_10/20260410-logic-analyzer-web-mode-guide.md` | Web 模式完整指南 |
| **CLI 模式指南** | `04_10/20260410-logic-analyzer-cli-mode-guide.md` | CLI 模式完整指南 |
| **快速参考** | `04_10/20260410-logic-analyzer-quick-reference.md` | 日常使用速查 |

---

## 6. Git 提交记录

### 第一次提交：添加逻辑分析仪组件

```bash
# 添加子模块
git submodule add https://github.com/ok-home/logic_analyzer.git components/logic_analyzer

# 修改 CMakeLists.txt 包含组件
git add CMakeLists.txt main/CMakeLists.txt

# 修改 main.c 和 main.h 添加初始化代码
git add main/main.c main/main.h

# 添加项目配置
git add main/Kconfig.projbuild

# 提交
git commit -m "feat: Add ESP32-S3 Logic Analyzer for 1-Wire debugging with PulseView"
```

**提交信息**：
- Commit: `0dc08f7`
- 新增：logic_analyzer 子模块、CMakeLists 配置、初始化代码

---

### 第二次提交：修复组件依赖

```bash
# 修复 main/CMakeLists.txt 中的组件依赖
git add main/CMakeLists.txt
git commit -m "fix: Correct component dependency in main/CMakeLists.txt"
```

**提交信息**：
- Commit: `677e051`
- 修改：修正组件名从 `logic_analyzer_hal` 到 `logic_analyzer`

---

### 第三次提交：更新为多模式支持

```bash
# 更新 main.c 支持多模式检测
git add main/main.c

# 添加 CLI 模式文档
git add 2026_04_08_12_heat_test/04_10/20260410-logic-analyzer-cli-mode-guide.md

git commit -m "feat: Update logic analyzer to support multi-mode (Web/SUMP/CLI)"
```

**提交信息**：
- Commit: `7f591f2`
- 新增：CLI 模式指南、模式切换说明
- 修改：main.c 添加模式检测和日志提示

---

### 第四次提交：切换到 CLI 模式

```bash
# 更新代码和文档，标记 Web 模式不兼容
git add main/main.c

git commit -m "fix: Switch to CLI mode due to ESP-IDF v5.5.2 WebSocket compatibility"
```

**提交信息**：
- Commit: `40231ca`
- 修改：推荐 CLI 模式，添加 ESP-IDF v5.5.2 兼容性说明

---

### 第五次提交：回滚代码更改

```bash
# 回滚所有代码修改，保留文档
git checkout HEAD~5 -- CMakeLists.txt main/CMakeLists.txt main/main.c main/main.h
git rm main/Kconfig.projbuild
git submodule deinit -f components/logic_analyzer
git rm -f components/logic_analyzer

git commit -m "revert: Rollback logic analyzer code changes"
```

**提交信息**：
- Commit: `fae7eac`
- 删除：logic_analyzer 子模块、Kconfig.projbuild
- 回滚：CMakeLists.txt、main.c、main.h 到原始状态

---

## 7. 当前状态

### 7.1 代码状态
- ✅ **代码已回滚**到添加逻辑分析仪之前的状态
- ✅ **所有文档保留**在 `2026_04_08_12_heat_test/04_10/` 目录
- ✅ **无功能影响**，项目可正常编译运行

### 7.2 文档状态
- ✅ 7 份技术文档已保存
- ✅ 包含三种模式的完整配置指南
- ✅ 包含 GPIO 可用性分析
- ✅ 包含故障排查指南

---

## 8. 后续建议

### 8.1 方案 A：使用 CLI 模式（推荐，立即可用）

**适用条件**：
- 有 ESP32-S3-DevKitC-1 开发板
- 有 USB 线连接 PC
- 已安装 Python 和 PySerial

**实施步骤**：
1. 恢复代码：`git revert fae7eac`
2. 配置 menuconfig：CLI 模式 + USB_SERIAL_JTAG
3. 构建烧录：`idf.py build && idf.py flash`
4. 运行采集：`python logic_analyzer_cli.py`
5. 导入 PulseView 分析

---

### 8.2 方案 B：等待硬件逻辑分析仪

**适用条件**：
- 计划购买 Saleae Logic Pro/DSLogic 等硬件逻辑分析仪
- 不需要立即调试 1-Wire 时序

**优势**：
- 不占用 ESP32 GPIO
- 专业级采样率和通道数
- 独立的分析软件

---

### 8.3 方案 C：修复 Web 模式（长期方案）

**适用条件**：
- 希望使用无线调试
- 有 ESP-IDF 开发经验

**可能方案**：
1. 降级 ESP-IDF 到 v5.2/v5.3
2. 修改 `logic_analyzer` 组件适配 v5.5.2
3. 等待上游组件更新

---

## 9. 参考链接

- [ok-home/logic_analyzer GitHub](https://github.com/ok-home/logic_analyzer)
- [PulseView 官方文档](https://sigrok.org/wiki/PulseView)
- [ESP32-S3-DevKitC-1 用户指南](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [MAX31850 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31850-MAX31851.pdf)
- 历史调试记录：`2026_04_08_12_heat_test/04_10/20260410-heat_detect_001_README.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-04-10 17:45  
**状态**: 代码已回滚，文档已保存，等待硬件或后续实施
