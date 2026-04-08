# MAX31850 1-Wire 测试说明 v3 (方案B + 开漏优化)

**日期**: 2026-04-08  
**分支**: `feature/heating`  
**提交**: `ccb740c`  
**版本**: 方案B (极端参数) + 开漏测试优化

---

## 1. 版本历史

| 版本 | 提交 | 主要修改 | 结果 |
|------|------|----------|------|
| v1 | c50be65 | 第一次时序调整 (3/4/58) | ❌ CRC失败 |
| v2 | 4125081 | 方案2 (2/8/56) | ❓ 未完整测试 |
| **v3** | **ccb740c** | **方案B (1/12/52) + 开漏优化** | **待测试** |

---

## 2. 当前修改内容

### 2.1 方案B - 极端读时序参数

| 参数 | 数值 | 说明 |
|------|------|------|
| **ONE_WIRE_READ_LOW** | **1μs** | 最小值，尽快释放总线 |
| **ONE_WIRE_READ_SAMPLE** | **12μs** | 激进延后，给从设备最多时间 |
| **ONE_WIRE_READ_HIGH** | **52μs** | 调整恢复时间 |
| **采样点** | **13μs** | 1 + 12 = 13μs (< 15μs上限) |
| **总时隙** | **65μs** | 60-120μs范围内 |
| **安全余量** | **2μs** | 距离15μs上限的余量(偏紧但合规) |

### 2.2 开漏测试优化

**问题**: 4.7KΩ上拉电阻导致释放后无法及时拉高

**解决方案**:
```c
// 原代码: 单次100us采样
esp_rom_delay_us(100);
int level_high = gpio_get_level(gpio);

// 新代码: 10次50us采样，共500us，多数表决
for (int i = 0; i < 10; i++) {
    esp_rom_delay_us(50);
    if (gpio_get_level(gpio) == 1) high_count++;
}
bool level_high = (high_count > 5);  // 多数表决
```

**关键改进**:
- ✅ 增加等待时间: 100μs → 500μs
- ✅ 多次采样确认，避免误判
- ✅ 即使测试失败也继续初始化（发出警告）

---

## 3. 预期测试输出

### 3.1 开漏测试（优化后）

```
=== Testing Open-Drain Mode on GPIO14 ===
  Drive LOW: 0 (expected 0)
  Release (pull-up): HIGH (8/10 samples high)  <-- 多数表决
  Pull-up time: 500us (weak pull-up may need more time)
  Open-drain test: PASS
  
  或（即使弱上拉也能通过）:
  
  Release (pull-up): LOW (2/10 samples high)
  Open-drain test: FAIL
  WARNING: Pull-up may be too weak (4.7KΩ), continuing anyway...
```

### 3.2 ROM搜索（希望成功）

```
=== ROM Search Started ===
...
ROM bits: 00111011...          <-- 家族码正确 (0x3B)
CRC check: PASS                <-- CRC校验通过
MAX31850 Init: Found 4 sensors on GPIO14
```

---

## 4. 测试步骤

```powershell
# 1. 编译
idf.py build

# 2. 进入下载模式（按住BOOT，按RESET，释放BOOT）

# 3. 烧录
idf.py -p COM9 flash

# 4. 监控输出
idf.py -p COM9 monitor
```

---

## 5. 如果仍失败

### 5.1 开漏测试失败且无法继续

如果看到：
```
Drive LOW: 1 (expected 0)   <-- GPIO无法拉低
```

**原因**: GPIO14硬件损坏  
**解决**: 更换ESP32-S3或改用其他GPIO

### 5.2 开漏测试通过但CRC仍失败

如果看到：
```
Open-drain test: PASS
...
ROM bits: 11011100...       <-- 仍是0xDC
CRC check: FAIL
```

**原因**: 读时序仍不正确，或硬件有根本问题  
**解决**: 需要硬件修复

```c
// 最终尝试 - 接近极限
#define ONE_WIRE_READ_LOW           1
#define ONE_WIRE_READ_SAMPLE        13      // 13->14 (采样点14μs)
// 余量只有1μs，风险较高
```

### 5.3 硬件解决方案

如果所有软件调整都失败：

1. **更换上拉电阻**: 4.7KΩ → **2.2KΩ** 或 **1KΩ**
2. **检查焊接**: 确保R1焊接良好
3. **减少设备数量**: 只连接1个传感器测试
4. **使用外部1-Wire驱动器**: 如DS2482-100

---

## 6. 关键观察指标

| 指标 | 成功标志 | 失败标志 |
|------|----------|----------|
| 开漏测试 | PASS 或 警告后继续 | Drive LOW=1 |
| ROM家族码 | 00111011 (0x3B) | 11011100 (0xDC) |
| CRC校验 | PASS | FAIL |
| 发现传感器 | 4个 | 0个 |

---

## 7. 参考文档

- [开漏测试分析](OPEN_DRAIN_ANALYSIS.md)
- [时序验证报告](TIMING_VERIFICATION_REPORT.md)
- [数据手册](hardware_info/max31850-max31851.pdf)

---

**祝测试顺利！这是软件优化的最后一搏。**
