# MAX31850 1-Wire 时序优化测试说明 v2

**日期**: 2026-04-08  
**分支**: `feature/heating`  
**提交**: `4125081`  
**优化版本**: 方案2 (安全方案)

---

## 1. 优化历程

### 1.1 第一次尝试 (commit c50be65)
| 参数 | 数值 | 结果 |
|------|------|------|
| READ_LOW | 3μs | ❌ CRC仍失败 |
| READ_SAMPLE | 4μs | |
| 采样点 | 7μs | |

### 1.2 第二次尝试 (commit 4125081 - 当前)
| 参数 | 数值 | 数据手册验证 |
|------|------|--------------|
| READ_LOW | **2μs** | ≥ 1μs ✅ (1μs余量) |
| READ_SAMPLE | **8μs** | N/A |
| READ_HIGH | **56μs** | N/A |
| 采样点 | **10μs** | < 15μs ✅ (5μs余量) |
| 总时隙 | **66μs** | 60-120μs ✅ |

---

## 2. 时序图

```
时间轴 (μs):  0       2      10              66
               |       |       |               |
主机拉低:      [  LOW  ]       
释放总线:              [       HIGH          ]
采样点:                        ↑
                              10μs
                               
安全余量: 距离15μs上限还有5μs
```

---

## 3. 如何测试

### 3.1 编译和烧录

```powershell
# 设置 ESP-IDF 环境
. $env:IDF_PATH/export.ps1

# 编译项目
idf.py build

# 进入下载模式：
# 1. 按住 BOOT 按钮
# 2. 按 RESET 按钮
# 3. 释放 BOOT 按钮

# 烧录到 ESP32-S3
idf.py -p COM9 flash

# 查看串口输出
idf.py -p COM9 monitor
```

### 3.2 关键观察点

#### ✅ 成功标志
```
[LOG] Open-drain test: PASS
[LOG] ROM bits: 00111011...          <-- 家族码正确 (0x3B)
[LOG] CRC check: PASS                 <-- CRC校验通过
[LOG] Found 4 sensors on GPIO14       <-- 发现4个传感器
```

#### ❌ 仍需调整的标志
```
[LOG] ROM bits: 11011100...          <-- 家族码仍被读反 (0xDC)
[LOG] CRC check: FAIL
[LOG] Found 0 sensors on GPIO14
```

---

## 4. 如果仍失败

### 4.1 进一步调整方向

如果方案2仍无法解决问题，可尝试以下渐进式调整：

#### 尝试A: 延后采样点
```c
#define ONE_WIRE_READ_LOW           2
#define ONE_WIRE_READ_SAMPLE        10      // 8->10
// 采样点：12μs，余量3μs
```

#### 尝试B: 更接近上限 (激进但合规)
```c
#define ONE_WIRE_READ_LOW           1       // 最小值
#define ONE_WIRE_READ_SAMPLE        12      // 激进
// 采样点：13μs，余量2μs (偏紧但仍合规)
```

### 4.2 硬件解决方案

如果软件调整都无法解决问题，需要硬件修改：
- **更换上拉电阻**: 4.7KΩ → **2.2KΩ** 或 **1KΩ**
- 这将从根本上解决信号上升沿慢的问题

---

## 5. 测试记录表

| 测试次数 | 提交版本 | READ_LOW | READ_SAMPLE | 采样点 | CRC结果 | 发现传感器 | 备注 |
|----------|----------|----------|-------------|--------|---------|------------|------|
| 1 | c50be65 | 3μs | 4μs | 7μs | ❌ FAIL | 0 | |
| 2 | 4125081 | 2μs | 8μs | 10μs | ? | ? | 当前测试 |
| 3 | TBD | ? | ? | ? | ? | ? | |

---

## 6. 参考文档

- [数据手册验证报告](SCHEME1_VERIFICATION.md)
- [时序验证报告](TIMING_VERIFICATION_REPORT.md)
- [数据手册](hardware_info/max31850-max31851.pdf) Page 20
- [上一次测试日志](esp32_log_20260408_175431.txt)

---

**祝测试顺利！**
