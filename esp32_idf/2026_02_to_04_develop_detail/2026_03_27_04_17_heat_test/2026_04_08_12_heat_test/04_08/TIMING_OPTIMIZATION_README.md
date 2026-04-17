# MAX31850 1-Wire 读时序优化说明

**日期**: 2026-04-08  
**分支**: `feature/heating`  
**提交**: `c50be65`  

---

## 1. 问题背景

根据日志 `esp32_log_20260408_173208.txt` 分析：

### 1.1 现象
- **Presence 检测**: ✅ 成功（`Presence detected: YES`）
- **开漏测试**: ❌ 失败（`Release (pull-up): 0 (expected 1)`）
- **ROM CRC 校验**: ❌ 失败（`CRC check: FAIL`）
- **发现的传感器**: 0个

### 1.2 根本原因
- **4.7KΩ 上拉电阻** 对于 4 设备总线来说上拉能力太弱
- 信号上升沿过慢，导致读取时采样到错误电平
- 读取的家族码为 `0xDC` (11011100)，而非预期的 `0x3B` (00111011)
- 两者呈**位反转**关系，说明采样时刻信号电平不稳定

---

## 2. 时序优化方案

### 2.1 修改内容

| 参数 | 原值 | 新值 | 说明 |
|------|------|------|------|
| `ONE_WIRE_READ_LOW` | 6μs | **3μs** | 缩短拉低时间，尽快释放总线让从设备驱动 |
| `ONE_WIRE_READ_SAMPLE` | 10μs | **4μs** | 提前采样点，在信号还稳定时读取 |
| `ONE_WIRE_READ_HIGH` | 55μs | **58μs** | 增加恢复时间，保证总时隙合规 |
| **总时隙** | 71μs | **65μs** | 仍大于 60μs，符合 1-Wire 标准 |

### 2.2 原理说明

```
原时序 (问题):
  主机拉低 6μs -> 释放 -> 等待 10μs 采样 -> 恢复 55μs
                               ↑
                          此时信号还在上升沿，读取错误

新时序 (优化):
  主机拉低 3μs -> 释放 -> 等待 4μs 采样 -> 恢复 58μs
                          ↑
                     此时从设备已稳定驱动总线，读取正确
```

---

## 3. 如何测试

### 3.1 编译和烧录

```powershell
# 设置 ESP-IDF 环境
. $env:IDF_PATH/export.ps1

# 编译项目
idf.py build

# 烧录到新的 ESP32-S3（进入下载模式：按住 BOOT，按 RESET，释放 BOOT）
idf.py -p COM9 flash

# 查看串口输出
idf.py -p COM9 monitor
```

### 3.2 预期结果

#### 成功标志
```
[日志输出]
I (XXXX) MAX31850: ROM search complete. Found 4 device(s)
I (XXXX) MAX31850: Sensor [0]: ROM=0x3B..., HW_ADDR=0
I (XXXX) MAX31850: Sensor [1]: ROM=0x3B..., HW_ADDR=1
...
I (XXXX) MAX31850: CRC check: PASS
```

#### 仍失败的标志
如果仍然出现以下输出，说明时序调整不够：
```
I (XXXX) MAX31850: ROM bits: 1101110000000000...
I (XXXX) MAX31850: CRC check: FAIL
```

---

## 4. 如果仍失败

### 4.1 进一步调整时序

可以尝试更激进的参数：

```c
#define ONE_WIRE_READ_LOW           2       // 缩短到 2μs
#define ONE_WIRE_READ_SAMPLE        3       // 提前到 3μs 采样
#define ONE_WIRE_READ_HIGH          60      // 增加恢复时间
```

### 4.2 硬件解决方案（推荐）

如果软件调整无法解决，需要硬件修改：
- 将 4.7KΩ 上拉电阻更换为 **2.2KΩ** 或 **1KΩ**
- 这将显著改善信号上升沿

---

## 5. Git 提交信息

```
commit c50be65
Author: Kimi Code CLI
Date:   2026-04-08

    fix(heating): optimize 1-Wire read timing for weak pull-up resistor
    
    Adjust 1-Wire read slot timing to fix bit inversion issue caused by
    slow signal rise time with 4.7KΩ pull-up resistor.
    
    Problem:
    - ROM search CRC fail: family code 0x3B read as 0xDC (bit reversed)
    - Weak 4.7KΩ pull-up causes slow rising edge on bus
    - Original sampling at 10us reads unstable signal level
    
    Solution:
    - ONE_WIRE_READ_LOW: 6us -> 3us (release bus faster)
    - ONE_WIRE_READ_SAMPLE: 10us -> 4us (sample earlier when stable)
    - ONE_WIRE_READ_HIGH: 55us -> 58us (maintain >60us slot)
    - Total slot: 65us (still compliant with 1-Wire spec)
```

---

## 6. 参考文档

- [MAX31850 Datasheet](hardware_info/max31850-max31851.pdf)
- [1-Wire Protocol Specification](https://www.analog.com/en/technical-articles/1wire-communication-through-software.html)
- 历史记录: `2026_04_01_02_heat_test/20260402-heat_detect_FIN_README.md`

---

**修改文件**: `main/heating_detect.c`  
**修改行数**: 9 行 (6 行新增注释，3 行修改参数)
