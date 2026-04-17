# ESP32-S3 Logic Analyzer Menuconfig 配置指南

**ESP-IDF Version**: v5.5.2  
**目标**: 使用 PulseView 通过 USB-Serial 监控 GPIO14 (1-Wire) 波形  

---

## 推荐配置

### 步骤 1: PCLK GPIO 设置
```
GPIO NUM FOR PCLK
当前值: 40  →  修改为: 15
```
**说明**: 
- ESP32-S3 需要一个**悬空**的 GPIO 引脚作为内部采样时钟（PCLK）
- GPIO15 通常未使用，适合作为 PCLK
- **重要**: 确保该引脚**不连接任何外部设备**

---

### 步骤 2: 功能选项选择

| 选项 | 推荐值 | 说明 |
|------|--------|------|
| ☑️ **Use Hi level interrupt** | **勾选** | 使用高级中断（Level 5），触发延迟从 2μs 降至 0.3μs，提高触发精度 |
| ☐ Use ledc timer for PCLK < 1 mHz | **不勾选** | 仅当采样率 < 1MHz 时需要。1-Wire 使用 1MHz 采样率，无需勾选 |
| ☐ Separate mode | **不勾选** | 独立模式用于单独的逻辑分析仪设备。嵌入到现有项目时不勾选 |
| ☐ Logic analyzer output data to http websocket | **不勾选** | Web 界面模式，用于无线调试。使用 PulseView USB 连接时不勾选 |
| ☐ Command line interface with logic_analyzer_cli.py | **不勾选** | CLI 模式，用于低资源占用场景。使用 PulseView 直接连接时不勾选 |
| ☑️ **Direct connect to Sigrok PulseView** | **必须勾选** | **核心选项**：启用 SUMP 协议与 PulseView 通信 |
| ☑️ **Use GPIO assignments for channels** | **建议勾选** | 预设 GPIO 通道，将 GPIO14 绑定到通道 0 |

---

### 步骤 3: GPIO 通道分配（如果启用了 Use GPIO assignments）

如果勾选了 **"Use GPIO assignments for channels"**，需要配置通道映射：

```
Logic Analyzer Channel GPIO Configuration
├── Channel 0 GPIO: 14     (1-Wire 总线)
├── Channel 1 GPIO: -1     (未使用)
├── Channel 2 GPIO: -1     (未使用)
├── Channel 3 GPIO: -1     (未使用)
├── Channel 4 GPIO: -1     (未使用)
├── Channel 5 GPIO: -1     (未使用)
├── Channel 6 GPIO: -1     (未使用)
└── Channel 7 GPIO: -1     (未使用)
```

**注意**: 
- `-1` 表示该通道未使用
- 只配置 Channel 0 为 GPIO14，其他保持 `-1`

---

### 步骤 4: 采样率和采样数配置（可选）

如果菜单中有采样率配置选项，建议设置：

```
Logic Analyzer Default Configuration
├── Sample Rate: 1000000        (1 MHz，1-Wire 协议足够)
├── Number of Samples: 140000   (ESP32-S3 RAM 模式最大值)
├── Number of Channels: 1       (仅使用通道 0)
└── Use PSRAM: 0                (不使用 PSRAM，避免与主程序冲突)
```

---

## 最终配置总结

```
┌─────────────────────────────────────────────────────────────┐
│  Logic Analyzer Configuration                                │
├─────────────────────────────────────────────────────────────┤
│  GPIO NUM FOR PCLK: 15                                      │
│  ☑️ Use Hi level interrupt                                   │
│  ☐ Use ledc timer for PCLK < 1 mHz                          │
│  ☐ Separate mode                                             │
│  ☐ Logic analyzer output data to http websocket             │
│  ☐ Command line interface with logic_analyzer_cli.py        │
│  ☑️ Direct connect to Sigrok PulseView  ← 必须勾选            │
│  ☑️ Use GPIO assignments for channels                        │
└─────────────────────────────────────────────────────────────┘
```

---

## 配置路径速查

```
menuconfig 导航路径:

(Component config)
    └── Logic Analyzer
            ├── GPIO NUM FOR PCLK (15)
            ├── [*] Use Hi level interrupt
            ├── [ ] Use ledc timer for PCLK < 1 mHz
            ├── [ ] Separate mode
            ├── [ ] Logic analyzer output data to http websocket
            ├── [ ] Command line interface with logic_analyzer_cli.py
            ├── [*] Direct connect to Sigrok PulseView
            └── [*] Use GPIO assignments for channels
                    └── (14) Channel 0 GPIO
```

---

## 保存配置

配置完成后：
1. 按 `S` 保存配置
2. 按 `Q` 退出 menuconfig
3. 执行 `idf.py build` 构建项目

---

## 验证配置

构建时如果配置正确，应看到以下日志：
```
-- Logic Analyzer Configuration:
--   SUMP protocol: ENABLED
--   Hi-level interrupt: ENABLED
--   PCLK GPIO: 15
--   Channel 0: GPIO14
--   Sample rate: 1000000 Hz
```

---

**下一步**: 构建并烧录固件后，使用 PulseView 连接 ESP32-S3 的 USB 端口即可捕获 GPIO14 的 1-Wire 波形。
