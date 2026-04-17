# CHB-BLDC2418 Motor Test Guide

**Date**: 2026-03-11  
**Branch**: `feature/motor-control-config`  
**Target**: ESP32-S3 + 4× CHB-BLDC2418 Motors  

---

## 测试阶段概览

```
阶段1: 软件编译测试 (无需硬件)
    ↓
阶段2: PWM 信号测试 (需示波器/逻辑分析仪)
    ↓
阶段3: 基础硬件测试 (1个电机)
    ↓
阶段4: 全功能测试 (4个电机)
    ↓
阶段5: 长期稳定性测试
```

---

## 阶段1: 软件编译测试 (Software Build Test)

### 1.1 编译检查
```powershell
# 进入 ESP-IDF 环境
. $env:IDF_PATH/export.ps1

# 清理并重新编译
cd e:\Platform_G2\esp32_idf
idf.py fullclean
idf.py build
```

**预期结果**: 编译无错误，生成 `build/test.bin`

### 1.2 静态代码检查
检查重点：
- [ ] GPIO 1,4,6,8 未被其他功能占用
- [ ] GPIO 2,5,7,9 未被其他功能占用
- [ ] PWM 通道 0-3 配置正确
- [ ] PCNT 单元 0-3 配置正确

```powershell
# 检查 GPIO 冲突
grep -n "GPIO_NUM_1\|GPIO_NUM_4\|GPIO_NUM_6\|GPIO_NUM_8" main/*.c main/*.h
grep -n "GPIO_NUM_2\|GPIO_NUM_5\|GPIO_NUM_7\|GPIO_NUM_9" main/*.c main/*.h
```

---

## 阶段2: PWM 信号验证 (Signal Verification)

### 2.1 测试准备
- **设备**: ESP32-S3-DevKitC-1
- **工具**: 示波器或逻辑分析仪
- **接线**: 无需连接电机，仅测量 GPIO 信号

### 2.2 测试步骤

#### 测试 2.2.1: PWM 频率验证
测量点: GPIO1, GPIO4, GPIO6, GPIO8

| 测试项 | 预期值 | 容差 | 方法 |
|--------|--------|------|------|
| PWM 频率 | 20KHz | ±5% | 示波器频率计 |
| 占空比范围 | 0%~100% | - | 逻辑分析仪 |
| 高电平电压 | 3.3V | ±0.1V | 示波器 |

#### 测试 2.2.2: 反相逻辑验证
```c
// 测试代码片段 (在 mqtt.c 中添加临时测试命令)
if (strstr(data, "test_pwm_duty_") != NULL) {
    int duty;
    sscanf(data, "test_pwm_duty_%d", &duty);
    pwm_set_duty(duty, 0);  // 设置电机0的占空比
    ESP_LOGI(TAG, "Test PWM: duty=%d", duty);
}
```

**验证逻辑**:
- 发送 `test_pwm_duty_0` → 电机应该**全速** (Low = ON)
- 发送 `test_pwm_duty_8191` → 电机应该**停止** (High = OFF)

### 2.3 串口日志检查
```
# 正常启动日志应包含:
I (xxxx) PWM_EVENT: PWM timer 0 initiated at clock speed 20000.
I (xxxx) PWM_EVENT: PWM channel 0 initiated on GPIO1 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 1 initiated on GPIO4 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 2 initiated on GPIO6 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 3 initiated on GPIO8 with initial duty 0 (inverted logic).
```

---

## 阶段3: 基础硬件测试 (Basic Hardware Test)

### 3.1 单电机测试接线

**使用 PH2.0-LI-5P_001 接口 (Motor 0)**:
```
PH2.0-LI-5P_001 引脚定义:
Pin 1: +12V  ----->  外部电源 +12V
Pin 2: IO1   ----->  ESP32 GPIO1 (PWM)
Pin 3: (空)  ----->  悬空
Pin 4: GND   ----->  外部电源 GND
Pin 5: IO2   ----->  ESP32 GPIO2 (FG)
```

**注意事项**:
- ⚠️ ESP32 GND 必须与外部电源 GND 共地
- ⚠️ 先接好线再给 ESP32 和电机供电
- ⚠️ FG 信号为 5V，确认 ESP32 GPIO 可承受

### 3.2 手动测试命令

通过 MQTT 发送测试命令：

```bash
# 连接 MQTT Broker
mosquitto_sub -h 192.168.110.31 -t "esp32_1/data" &

# 测试1: 电机0 以50%速度运行5秒
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_225_5"

# 测试2: 电机0 以100%速度运行3秒
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_450_3"

# 测试3: 电机0 停止
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_0_1"
```

**命令格式**: `cmd_<motor_index>_<speed>_<duration_seconds>`
- motor_index: 0~3
- speed: 0~450 (对应 0~4500 RPM)
- duration: 运行时间(秒)

### 3.3 观察指标

| 检查项 | 正常表现 | 异常表现 |
|--------|----------|----------|
| 电机旋转 | 平滑旋转，无异响 | 抖动、不转、异响 |
| 速度响应 | 与设定值接近 | 速度偏差大 |
| FG 反馈 | MQTT 返回 pcnt_count_0_XXX | 无返回或返回0 |
| PWM 波形 | 20KHz 方波 | 无波形或频率不对 |

---

## 阶段4: 全功能测试 (Full Function Test)

### 4.1 四电机同时测试

```bash
# 同时启动4个电机
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_225_10"
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_1_225_10"
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_2_225_10"
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_3_225_10"
```

### 4.2 速度梯度测试

| 测试编号 | 速度设定 | 预期 PCNT | 容差 | 结果 |
|----------|----------|-----------|------|------|
| T1 | 0 | 0 | ±0 | □ |
| T2 | 100 | ~100 | ±10% | □ |
| T3 | 225 | ~225 | ±10% | □ |
| T4 | 450 | ~450 | ±10% | □ |

### 4.3 PID 响应测试

```bash
# 快速改变速度，观察 PID 调节
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_100_5"
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_300_5"
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_100_5"
```

**评估标准**:
- 超调量 < 20%
- 稳定时间 < 2秒
- 稳态误差 < 5%

---

## 阶段5: 长期稳定性测试

### 5.1 连续运行测试

```bash
# 循环测试脚本
for i in {1..100}; do
    mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_225_5"
    sleep 6
done
```

### 5.2 热稳定性测试

运行 30 分钟后检查:
- [ ] 电机温度 < 60°C
- [ ] ESP32 温度正常
- [ ] 无丢步或失速现象
- [ ] MQTT 连接保持

---

## 故障排查指南

### F1: 电机不转
| 检查项 | 排查方法 |
|--------|----------|
| 电源 | 测量 +12V 和 GND 之间电压 |
| PWM 信号 | 示波器测量 GPIO1 是否有波形 |
| 接线 | 检查 PH2.0 插头是否插紧 |
| PWM 逻辑 | 确认反相逻辑正确 (duty=0 时电机应转) |

### F2: 速度不准确
| 检查项 | 排查方法 |
|--------|----------|
| FG 接线 | 检查 GPIO2 是否接收到信号 |
| PCNT 计数 | 查看串口日志 pcnt_count_0_XXX |
| PID 参数 | 尝试调整 Kp, Ki, Kd |

### F3: 电机抖动
| 检查项 | 排查方法 |
|--------|----------|
| PWM 频率 | 确认 20KHz，过低会导致抖动 |
| 电源纹波 | 检查 +12V 是否稳定 |
| 机械问题 | 检查电机轴承和负载 |

---

## 推荐测试工具

### 软件工具
| 工具 | 用途 | 获取方式 |
|------|------|----------|
| mosquitto | MQTT 测试 | 官网下载 |
| ESP-IDF Monitor | 串口监控 | idf.py monitor |
| PulseView | 逻辑分析 | 开源软件 |

### 硬件工具
| 工具 | 用途 | 推荐型号 |
|------|------|----------|
| 示波器 | PWM 测量 | 100MHz 以上 |
| 逻辑分析仪 | 信号时序 | Saleae Logic |
| 万用表 | 电压测量 | 任意数字万用表 |
| 可调电源 | 电源供应 | 0-30V/5A |

---

## 测试记录表

| 日期 | 测试项 | 结果 | 备注 |
|------|--------|------|------|
| | 编译测试 | □通过 □失败 | |
| | PWM 频率 | _______ Hz | 目标 20KHz |
| | 单电机启动 | □正常 □异常 | |
| | 速度准确性 | _______ % | 误差范围 |
| | 四电机并行 | □正常 □异常 | |
| | 长期稳定性 | _______ 分钟 | 无故障运行时间 |

---

**Document Version**: 1.0  
**Author**: Kimi Code CLI  
**Last Updated**: 2026-03-11
