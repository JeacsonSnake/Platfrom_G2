# MAX31850 有效数据窗口检测指南

**文档版本**: 1.0  
**日期**: 2026-04-08  
**作者**: ESP32-S3 Motor Control IoT Project

---

## 1. 什么是有效数据窗口？

### 1.1 1-Wire 读时隙时序

```
时间轴 (μs):  0    tINIT   tSAMPLE          tSLOT
               |      |        |               |
主机操作:      [LOW]        [释放]              [等待]
               |<-5μs->|<-9μs->|<----56μs----->|
从机响应:              [====有效数据========]
                              ↑
                          采样点
```

### 1.2 数据手册要求

根据 MAX31850 数据手册 (Page 20):

> "Output data from the slave is **valid for 15μs after the falling edge**"

这意味着：
- **理论有效窗口**: 0μs ~ 15μs（从下降沿开始）
- **实际有效窗口**: 可能因硬件条件而不同

### 1.3 影响实际数据窗口的因素

| 因素 | 影响 |
|------|------|
| **上拉电阻值** | 4.7KΩ → 上升沿慢，延长有效窗口 |
| **总线电容** | 4个设备并联 → 更大的电容，更慢的上升沿 |
| **GPIO驱动能力** | 开漏模式下完全依赖上拉电阻 |
| **从设备响应时间** | MAX31850 需要时间来驱动总线 |

---

## 2. 软件检测方法

### 2.1 二分法扫描（已实现）

代码中已经实现了 `max31850_scan_data_window()` 函数：

```c
// 在调试模式下调用
#if MAX31850_DEBUG_ENABLE
    if (MAX31850_DEBUG_TIMING_SCAN) {
        max31850_timing_diagnostic();
    }
#endif
```

**输出示例**:
```
=== Data Window Scan ===
Testing different sample points...
  Sample point  5μs: 0/5 correct [FAIL]
  Sample point  7μs: 1/5 correct [FAIL]
  Sample point  9μs: 3/5 correct [OK]
  Sample point 11μs: 5/5 correct [OK]
  Sample point 13μs: 5/5 correct [OK]
  Sample point 15μs: 4/5 correct [OK]
  Sample point 17μs: 2/5 correct [FAIL]
  Sample point 19μs: 0/5 correct [FAIL]
Data window: 9μs to 15μs
Recommended sample point: 12μs
```

### 2.2 手动测试不同采样点

如果你需要手动测试，可以临时修改时序参数：

```c
// 在 heating_detect.c 中修改
#define ONE_WIRE_READ_LOW       5   // 保持5μs初始化
#define ONE_WIRE_READ_SAMPLE    X   // 修改这个值测试: 5, 7, 9, 11, 13, 15, 17
```

**测试矩阵**:

| READ_SAMPLE | 采样点 | 测试结果 | 备注 |
|-------------|--------|----------|------|
| 4μs | 9μs | ? | 可能太早 |
| 6μs | 11μs | ? | 安全范围 |
| 8μs | 13μs | ? | 推荐值 |
| 10μs | 15μs | ? | 临界值 |
| 12μs | 17μs | ? | 可能超出 |

### 2.3 使用逻辑分析仪软件分析

如果没有示波器，可以用软件方法估算：

```c
/**
 * @brief 测量总线上升时间
 * 
 * 通过多次采样测量信号从低到高的上升时间
 */
static uint32_t measure_bus_rise_time(void)
{
    const int samples = 100;
    uint32_t rise_time_us = 0;
    
    gpio_set_level(g_driver.gpio_num, 0);  // 拉低
    esp_rom_delay_us(10);
    
    uint32_t start = esp_cpu_get_cycle_count();
    gpio_set_level(g_driver.gpio_num, 1);  // 释放
    
    // 等待上升沿
    for (int i = 0; i < samples; i++) {
        if (gpio_get_level(g_driver.gpio_num) == 1) {
            uint32_t end = esp_cpu_get_cycle_count();
            // ESP32-S3 @ 240MHz: 240 cycles = 1μs
            rise_time_us = (end - start) / 240;
            break;
        }
        esp_rom_delay_us(1);
    }
    
    return rise_time_us;
}
```

---

## 3. 硬件检测方法

### 3.1 示波器测量（推荐）

**所需设备**:
- 数字示波器（100MHz带宽足够）
- 探针（10x衰减）

**测量步骤**:

1. **连接示波器**
   - 探头接 GPIO14
   - 地线接开发板GND
   - 设置触发为下降沿触发

2. **捕捉读时隙波形**

```
示波器设置:
- 时基: 10μs/div
- 垂直: 2V/div
- 触发: 下降沿，触发电平 1.5V
- 单次触发模式
```

3. **分析波形**

```
     3.3V ─┐
           │      ┌──────────────────┐
           │      │                  │
     1.5V ─┤──────┤─────采样窗口─────┤─────────
           │      │                  │
       0V ─┘      └──────────────────┘
           │<-5μs->|<------?μs------->|
           │INIT   │   有效数据窗口    │
           │       │                  │
           下降沿   从机开始驱动        从机停止驱动
```

**测量要点**:
- 测量下降沿到信号稳定在高电平的时间
- 确定从机何时开始驱动总线（信号从上升转为稳定）
- 确定从机何时停止驱动总线（信号开始被上拉电阻拉高）

### 3.2 逻辑分析仪测量

**所需设备**:
- 逻辑分析仪（24MHz采样率足够）
- 如 Saleae Logic、DSLogic 等

**测量步骤**:

1. **连接逻辑分析仪**
   - CH0 接 GPIO14
   - GND 接开发板GND

2. **软件设置**
   - 采样率: 24MHz
   - 触发: 下降沿

3. **分析**
   - 测量低电平持续时间
   - 观察信号跳变点

---

## 4. 推荐的采样点选择策略

### 4.1 保守策略（稳定性优先）

```c
// 选择窗口中间偏左的位置
#define ONE_WIRE_READ_LOW       5
#define ONE_WIRE_READ_SAMPLE    6   // 采样点 = 11μs
// 距离15μs上限有4μs余量，非常安全
```

### 4.2 平衡策略（推荐）

```c
// 选择窗口中间位置
#define ONE_WIRE_READ_LOW       5
#define ONE_WIRE_READ_SAMPLE    9   // 采样点 = 14μs
// 距离15μs上限有1μs余量，同时给从机足够时间
```

### 4.3 激进策略（速度优先）

```c
// 尽快采样，假设从机响应快
#define ONE_WIRE_READ_LOW       3
#define ONE_WIRE_READ_SAMPLE    5   // 采样点 = 8μs
// 从机必须有足够时间驱动总线
```

---

## 5. 不同硬件条件下的推荐参数

### 5.1 4.7KΩ上拉 + 4设备（当前硬件）

```c
#define ONE_WIRE_READ_LOW       5   // 给从机充足准备时间
#define ONE_WIRE_READ_SAMPLE    9   // 采样点14μs
// 原因：弱上拉导致上升沿慢，需要更长的初始化时间
```

### 5.2 2.2KΩ上拉 + 4设备

```c
#define ONE_WIRE_READ_LOW       3   // 可以缩短初始化时间
#define ONE_WIRE_READ_SAMPLE    10  // 采样点13μs
// 原因：更强的上拉让信号上升更快
```

### 5.3 1KΩ上拉 + 1设备（最佳条件）

```c
#define ONE_WIRE_READ_LOW       2   // 最小时序
#define ONE_WIRE_READ_SAMPLE    11  // 采样点13μs
// 原因：理想条件，可以用更激进的参数
```

---

## 6. 调试技巧

### 6.1 启用详细日志

```c
// 在 heating_detect.h 中
#define MAX31850_DEBUG_ENABLE       1
#define MAX31850_DEBUG_ROM_SEARCH   1
#define MAX31850_DEBUG_TIMING_SCAN  1
```

### 6.2 检查家族码

```
正确家族码: 00111011 (0x3B)
错误家族码: 11011100 (0xDC) - 位反转，说明采样太早
```

### 6.3 CRC校验失败分析

| CRC错误模式 | 可能原因 | 解决方案 |
|-------------|----------|----------|
| 总是失败 | 采样点完全错误 | 大幅调整时序 |
| 偶尔失败 | 采样点在边界 | 向中间移动 |
| 特定设备失败 | 该设备响应慢 | 增加初始化时间 |

---

## 7. 快速诊断流程

```
开始
  │
  ▼
启用 DEBUG_TIMING_SCAN
  │
  ▼
运行数据窗口扫描
  │
  ├─► 找到有效窗口？ ──Yes──► 选择中间采样点 ──► 测试ROM搜索
  │                            │
  │                           Success
  │                            │
  │                            ▼
  │                        使用新参数
  │
  No
  │
  ▼
检查硬件连接
  │
  ├─► 上拉电阻焊接良好？
  │
  ├─► GPIO14配置正确？
  │
  └─► 传感器供电正常？
```

---

## 8. 参考文档

- [MAX31850/MAX31851 Datasheet](hardware_info/max31850-max31851.pdf) Page 20
- [1-Wire Protocol Specification](https://www.analog.com/en/technical-articles/1wire-communication-through-software.html)
- [旧代码分析](ANALYSIS_OLD_VS_NEW_CODE.md)

---

**总结**: 有效数据窗口的检测需要结合软件扫描和硬件测量。在4.7KΩ弱上拉条件下，推荐使用 `5μs初始化 + 9μs采样延迟 = 14μs采样点` 的配置，这既符合数据手册，又能适应缓慢的信号上升沿。
