# CHB-BLDC2418 电机控制配置与测试记录

**日期**: 2026-03-11  
**分支**: `feature/motor-control-config`  
**任务描述**: 完成 MQTT 心跳修复分支合并后，配置 CHB-BLDC2418 电机控制参数，更新 GPIO 引脚定义、PWM/PCNT 配置，并创建完整的测试文档和故障排查指南

---

## 1. 背景

### 历史工作回顾
根据 2026-03-05 的修复记录，MQTT 心跳问题已解决：
- 修复了心跳任务日志级别问题（ESP_LOGD → ESP_LOGI/LOGW）
- 使用 `vTaskDelayUntil()` 确保固定频率执行
- 实现 4+ 小时稳定连接

### 本次任务目标
1. 将 `fix/mqtt-heartbeat-logging` 分支合并到 `main` 并清理
2. 创建专门的电机控制配置分支
3. 配置 CHB-BLDC2418 电机硬件参数（GPIO、PWM、PCNT）
4. 创建完整的测试文档和自动化测试工具
5. 实际硬件测试并记录问题

---

## 2. 电机规格说明

### 2.1 电机型号

| 参数 | 值 |
|------|-----|
| 型号 | CHB-BLDC2418 |
| 类型 | 永磁直流无刷电机 |
| 电压 | 12V |
| 转速 | 4500 RPM |
| 最大电流 | 0.16A |
| FG 信号 | 6 pulses/cycle |
| PWM 频率 | 15KHz ~ 25KHz |

### 2.2 硬件接口

PH2.0-LI-5P 连接器定义：

| Pin | 功能 | ESP32 GPIO |
|-----|------|-----------|
| 1 | +12V | - (外部电源) |
| 2 | PWM | IO1/IO4/IO6/IO8 |
| 3 | (预留) | - (CW/CCW) |
| 4 | GND | - (外部地) |
| 5 | FG | IO2/IO5/IO7/IO9 |

---

## 3. Git 分支操作

### 3.1 合并并清理 MQTT 修复分支

```bash
# 当前在 fix/mqtt-heartbeat-logging 分支
git add -A
git commit -m "chore: 保存当前修改以备合并"

# 切换到 main 并合并
git checkout main
git checkout fix/mqtt-heartbeat-logging -- .
git add -A
git commit -m "merge: 合并 MQTT 心跳修复分支到 main"

# 删除原分支
git branch -D fix/mqtt-heartbeat-logging
```

**提交信息**:  
- Commit: `eeaae1a`  
- 合并: 14 个文件，7150 行新增

### 3.2 创建电机控制配置分支

```bash
# 创建并切换到新分支
git checkout -b feature/motor-control-config
```

**分支状态**: 
- Current: `feature/motor-control-config`
- Based on: `main`

---

## 4. 电机控制配置实现

### 4.1 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `main/main.h` | 更新 PWM/PCNT GPIO 定义，调整频率和分辨率 |
| `main/main.c` | 更新 PWM 数组大小（6→4） |
| `main/pwm.c` | 更新初始化日志，添加反相逻辑注释 |
| `main/pid.c` | 修正反相逻辑，更新 max_pcnt/max_pwm 参数 |
| `main/pcnt.c` | 修正停止时 duty 值（8192→8191） |

### 4.2 GPIO 配置变更

**原配置** → **新配置**：

| 功能 | 原 GPIO | 新 GPIO | 说明 |
|------|---------|---------|------|
| PWM_CH0 | GPIO5 | **GPIO1** | IO1 |
| PWM_CH1 | GPIO6 | **GPIO4** | IO4 |
| PWM_CH2 | GPIO7 | **GPIO6** | IO6 |
| PWM_CH3 | GPIO8 | **GPIO8** | IO8 |
| PCNT_CH0 | GPIO11 | **GPIO2** | IO2 |
| PCNT_CH1 | GPIO12 | **GPIO5** | IO5 |
| PCNT_CH2 | GPIO13 | **GPIO7** | IO7 |
| PCNT_CH3 | GPIO14 | **GPIO9** | IO9 |

### 4.3 PWM 参数配置

```c
// main/main.h
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES   LEDC_TIMER_13_BIT
#define LEDC_DUTY       (0)                 // 初始占空比（反相逻辑：0=全速）
#define LEDC_FREQ       (20000)             // 20KHz 频率

#define LEDC_GPIO_LIST  {1, 4, 6, 8}
#define LEDC_CHANNEL_LIST {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3}
```

### 4.4 PID 参数配置

```c
// main/pid.c
struct PID_params pid_params = {
    .Kp         = 8,
    .Ki         = 0.02,
    .Kd         = 0.01,
    .max_pwm    = 8191,     // 13-bit 最大值
    .min_pwm    = 0,
    .max_pcnt   = 450,      // 4500 RPM * 6 / 60 = 450 pulses/sec
    .min_pcnt   = 0
};
```

**注意**: 反相 PWM 逻辑 - Duty 8191 = 电机停止，Duty 0 = 电机全速

### 4.5 Git 提交记录

```bash
# 提交电机配置
git add main/
git commit -m "feat(motor): 配置 CHB-BLDC2418 电机控制参数

硬件变更:
- PWM GPIO: {5,6,7,8} -> {1,4,6,8}
- PCNT GPIO: {11,12,13,14} -> {2,5,7,9}
- PWM 频率: 5KHz -> 20KHz

电机规格:
- CHB-BLDC2418, 12V, 4500 RPM
- FG: 6 pulses/cycle
- PWM: 反相逻辑

软件修改:
- main.h: GPIO定义和PWM参数
- main.c: 数组大小调整
- pwm.c: 更新日志和注释
- pid.c: 修正反相逻辑和参数
- pcnt.c: 修正停止duty值"
```

**提交信息**:  
- Commit: `6ff7b01`  
- 修改: 7 个文件，3109 行新增

---

## 5. 测试文档创建

### 5.1 文档清单

| 文件 | 用途 | 说明 |
|------|------|------|
| `2026-03-11-CHB-BLDC2418-Motor-Configuration.md` | 硬件配置文档 | 电机规格、GPIO定义、接口说明 |
| `2026-03-11-Motor-Test-Guide.md` | 完整测试指南 | 5个测试阶段的详细步骤 |
| `MQTTX_TEST_GUIDE.md` | MQTTX专用指南 | 图形化MQTT客户端测试方法 |
| `QUICK_TEST_CHECKLIST.md` | 快速检查清单 | 7步快速验证流程 |
| `motor_test_client.py` | Python测试工具 | 自动化测试脚本 |

### 5.2 Python 测试工具功能

```python
# motor_test_client.py 功能
- 单电机测试: python motor_test_client.py single -m 0 -s 225 -d 5
- 全部电机测试: python motor_test_client.py all -s 225 -d 10
- 速度序列测试: python motor_test_client.py sequence -m 0
- 快速测试: python motor_test_client.py quick
```

**特性**:
- 自动计算速度误差
- 生成测试报告
- 友好的命令行界面

### 5.3 Git 提交记录

```bash
# 提交测试文档
git add 2026_03_11_moter_test/
git commit -m "docs(test): 添加 CHB-BLDC2418 电机测试文档和工具"
```

**提交信息**:  
- Commit: `6a65eaa` 和 `0d945ce`  
- 新增: 5 个文档/工具文件

---

## 6. 硬件测试与问题排查

### 6.1 测试环境

| 项目 | 配置 |
|------|------|
| 硬件 | ESP32-S3-DevKitC-1 |
| 电机 | CHB-BLDC2418 (1台) |
| 连接端口 | PH2.0-LI-5P_003 (Motor 2) |
| 工具 | MQTTX Desktop |
| Broker | EMQX @ 192.168.110.31:1883 |

### 6.2 Bug 001: MQTT JSON 格式问题

**现象**: 发送 `cmd_2_225_10` 后电机无反应

**根因**: 
- MQTTX 默认发送 JSON 格式: `{"msg": "cmd_2_225_10"}`
- ESP32 期望纯文本: `cmd_2_225_10`

**解决方案**: 将 MQTTX 格式从 JSON 改为 Plaintext

**文档**: `2026-03-11-2250-bug001-mqtt-json-format-error.md`

### 6.3 Bug 002: PWM 配置错误

**现象**: 修复 JSON 格式后，电机仍不旋转，但日志显示 PWM duty 变化

**根因**: 
```
E (10781) ledc: requested frequency 20000 and duty resolution 13 can not be achieved
```
- ESP32-S3 硬件无法同时支持 20KHz + 13位分辨率
- 计算所需除数: 80MHz / (20KHz * 8192) = 0.488 (非整数)

**影响**: PWM 外设未正确配置，电机无有效驱动信号

**解决方案选项**:
1. **(推荐)** 降低 PWM 频率到 5KHz
2. 降低分辨率到 8-bit，保持 20KHz
3. 使用外部 PWM 生成器

**文档**: `2026-03-11-2309-bug002-pwm-config-error-no-motor-rotation.md`

### 6.4 Git 提交记录

```bash
# 提交 Bug 分析报告
git add 2026_03_11_moter_test/
git commit -m "docs(bug): add bug001 and bug002 analysis reports"
```

**提交信息**:  
- Commit: `f362843` 和 `dc42160`  
- 新增: 3 个文件

---

## 7. 文件变更总结

### 7.1 代码文件修改

```
main/main.h
- LEDC_GPIO_LIST: {5,6,7,8} → {1,4,6,8}
- PCNT_GPIO: {11,12,13,14} → {2,5,7,9}
- LEDC_FREQ: 5000 → 20000
- LEDC_DUTY: 8192 → 0

main/main.c
- pwm_gpios[6] → pwm_gpios[4]
- pwm_channels[6] → pwm_channels[4]

main/pwm.c
- 更新初始化日志
- 添加反相逻辑注释

main/pid.c
- max_pwm: 8192 → 8191
- max_pcnt: 435 → 450
- 更新反相逻辑注释

main/pcnt.c
- 停止duty: 8192 → 8191
```

### 7.2 新增文档文件

```
2026_03_11_moter_test/
├── 2026-03-11-CHB-BLDC2418-Motor-Configuration.md          (硬件配置)
├── 2026-03-11-Motor-Test-Guide.md                          (测试指南)
├── MQTTX_TEST_GUIDE.md                                     (MQTTX指南)
├── QUICK_TEST_CHECKLIST.md                                 (检查清单)
├── motor_test_client.py                                    (Python工具)
├── 2026-03-11-2250-bug001-mqtt-json-format-error.md        (Bug001)
├── 2026-03-11-2309-bug002-pwm-config-error-no-motor-rotation.md (Bug002)
└── esp32_log_20260311_224107.txt                           (测试日志)
```

---

## 8. 当前分支状态

```
feature/motor-control-config
├── main/                          (修改: 5个文件)
│   ├── main.c
│   ├── main.h
│   ├── pwm.c
│   ├── pid.c
│   └── pcnt.c
├── 2026_03_11_moter_test/         (新增: 8个文件)
└── AGENTS.md                      (更新)
```

**Git Log**:
```
dc42160 docs(bug): add bug002 analysis report
f362843 docs(bug): add bug001 analysis report
0d945ce docs(test): add MQTTX test guide
6a65eaa docs(test): add motor test docs and tools
6ff7b01 feat(motor): configure CHB-BLDC2418 motor parameters
eeaae1a merge: merge MQTT heartbeat fix to main
```

---

## 9. 待解决问题

### 9.1 PWM 配置错误 (Bug 002)

**状态**: 已定位，待修复  
**方案**: 将 PWM 频率从 20KHz 改回 5KHz

```c
// main/main.h 第76行
#define LEDC_FREQ       (5000)     // 从 20000 改为 5000
```

**验证步骤**:
1. 修改配置
2. 重新编译: `idf.py build`
3. 烧录: `idf.py -p COM9 flash`
4. 检查启动日志无 `can not be achieved` 错误
5. 发送 `cmd_2_225_10` 验证电机旋转

### 9.2 长期建议

| 项目 | 建议 |
|------|------|
| PWM 频率 | 测试 5KHz 是否满足需求（可能有轻微噪音） |
| 分辨率调整 | 如需要 20KHz，改为 8-bit 分辨率 |
| 硬件验证 | 使用示波器验证 PWM 波形 |

---

## 10. 使用说明

### 10.1 编译烧录

```powershell
# 进入 ESP-IDF 环境
. $env:IDF_PATH/export.ps1

# 编译
cd e:\Platform_G2\esp32_idf
idf.py build

# 烧录
idf.py -p COM9 flash

# 监控
idf.py -p COM9 monitor
```

### 10.2 使用 MQTTX 测试

1. 连接: `192.168.110.31:1883`
2. 订阅: `esp32_1/data`
3. 发送命令:
   - 主题: `esp32_1/control`
   - 格式: **Plaintext** (非 JSON)
   - 消息: `cmd_2_225_10`

### 10.3 使用 Python 测试

```powershell
cd 2026_03_11_moter_test
python motor_test_client.py quick
```

---

## 11. 参考链接

- [CHB-BLDC2418 电机规格书](https://www.nidec.com/cn/product/motor/bldc/)
- [ESP32-S3 LEDC 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/ledc.html)
- [ESP-IDF PCNT 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/pcnt.html)
- [MQTTX 使用指南](https://mqttx.app/docs)
- Bug001 分析: `2026-03-11-2250-bug001-mqtt-json-format-error.md`
- Bug002 分析: `2026-03-11-2309-bug002-pwm-config-error-no-motor-rotation.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-03-11 23:13  
**完成时间**: 2026-03-11 23:13
