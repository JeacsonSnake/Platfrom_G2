# CHB-BLDC2418 电机快速测试检查清单

## 环境准备

- [ ] ESP32-S3 开发板
- [ ] 1~4 个 CHB-BLDC2418 电机
- [ ] 12V 电源 (0.5A 以上)
- [ ] PH2.0-LI-5P 连接线
- [ ] 电脑已安装 ESP-IDF 环境
- [ ] 电脑已安装 Python + paho-mqtt

```powershell
# 安装 Python MQTT 库
pip install paho-mqtt
```

---

## 第一步：编译测试 (5分钟)

```powershell
cd e:\Platform_G2\esp32_idf
. $env:IDF_PATH/export.ps1
idf.py build
```

**✓ 通过标准**: 编译无错误，显示 `Project build complete`

---

## 第二步：烧录固件 (2分钟)

```powershell
# 进入下载模式：按住 BOOT，按 RESET，松开 BOOT
idf.py -p COM9 flash
```

**✓ 通过标准**: 烧录成功，无错误

---

## 第三步：接线检查 (3分钟)

### Motor 0 (单电机测试)

```
PH2.0-LI-5P_001 接线:
┌─────┬─────────────┬──────────────────────┐
│ Pin │ 连接到       │ 说明                 │
├─────┼─────────────┼──────────────────────┤
│ 1   │ +12V        │ 外部电源正极          │
│ 2   │ ESP32 GPIO1 │ PWM 信号 (3.3V)       │
│ 3   │ 悬空        │ 预留 CW/CCW           │
│ 4   │ GND         │ 外部电源负极          │
│ 5   │ ESP32 GPIO2 │ FG 反馈 (5V输入)      │
└─────┴─────────────┴──────────────────────┘
```

**⚠️ 重要**: ESP32 GND 必须与外部电源 GND 连接在一起！

---

## 第四步：串口监控检查 (2分钟)

```powershell
idf.py -p COM9 monitor
```

**预期输出**:
```
I (xxxx) PWM_EVENT: PWM timer 0 initiated at clock speed 20000.
I (xxxx) PWM_EVENT: PWM channel 0 initiated on GPIO1 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 1 initiated on GPIO4 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 2 initiated on GPIO6 with initial duty 0 (inverted logic).
I (xxxx) PWM_EVENT: PWM channel 3 initiated on GPIO8 with initial duty 0 (inverted logic).
I (xxxx) PCNT_EVENT: PCNT channel 0 has been initiated on pin 2.
I (xxxx) PCNT_EVENT: PCNT channel 1 has been initiated on pin 5.
I (xxxx) PCNT_EVENT: PCNT channel 2 has been initiated on pin 7.
I (xxxx) PCNT_EVENT: PCNT channel 3 has been initiated on pin 9.
```

**✓ 通过标准**: 所有 PWM 和 PCNT 通道初始化成功

---

## 第五步：MQTT 连接测试 (2分钟)

等待串口显示:
```
I (xxxx) WIFI_EVENT: WiFi connected, IP: 192.168.110.xxx
I (xxxx) SNTP: 时间同步成功！
I (xxxx) MQTT_EVENT: MQTT Connected to mqtt://192.168.110.31:1883
```

**✓ 通过标准**: MQTT 连接成功

---

## 第六步：电机运行测试 (5分钟)

### 6.1 使用 Python 测试脚本

```powershell
cd 2026_03_11_moter_test
python motor_test_client.py quick
```

**预期输出**:
```
✓ Connected to MQTT Broker: 192.168.110.31:1883
✓ Subscribed to: esp32_1/data

============================================
All Motors Test - Parallel Operation
Speed: 225/450 (50.0%)
Duration: 5s
============================================

>>> Sending: cmd_0_225_5
>>> Sending: cmd_1_225_5
>>> Sending: cmd_2_225_5
>>> Sending: cmd_3_225_5
Waiting 7s for motor operation...
[18:30:15.123] DATA: pcnt_count_0_228
[18:30:15.234] DATA: pcnt_count_1_223
...

📊 Motor 0 Results:
   Expected Speed: 225
   Average PCNT:   226.5
   Error:          0.7%
   ✅ PASSED (Error < 10%)
```

### 6.2 手动 MQTT 测试

```bash
# 订阅数据主题
mosquitto_sub -h 192.168.110.31 -t "esp32_1/data"

# 在另一个终端发送命令
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_225_5"
```

**✓ 通过标准**: 
- 电机旋转平滑
- 收到 pcnt_count 数据
- 速度误差 < 10%

---

## 第七步：功能验证 (5分钟)

| 测试项 | 命令 | 预期结果 | 实际结果 |
|--------|------|----------|----------|
| 电机0 启动 | `cmd_0_225_5` | 电机旋转 | □ |
| 电机0 停止 | `cmd_0_0_1` | 电机停止 | □ |
| 50% 速度 | `cmd_0_225_5` | pcnt≈225 | □ |
| 100% 速度 | `cmd_0_450_5` | pcnt≈450 | □ |
| 电机1 启动 | `cmd_1_225_5` | 电机1旋转 | □ |
| 四电机并行 | `cmd_0_225_5` ... | 全部旋转 | □ |

---

## 故障排查速查表

| 现象 | 可能原因 | 解决方案 |
|------|----------|----------|
| 编译错误 | GPIO 冲突 | 检查 main.h 中 GPIO 定义 |
| 电机不转 | 电源未接 | 检查 +12V 和 GND |
| 电机不转 | PWM 逻辑反 | 检查 duty=0 时是否旋转 |
| 速度不准 | FG 未连接 | 检查 GPIO2/5/7/9 接线 |
| 无 MQTT | WiFi 未连 | 检查 WiFi 配置 |
| 电机抖动 | PWM 频率低 | 检查是否为 20KHz |

---

## 测试完成标准

**✓ 全部通过**:
- [ ] 编译无错误
- [ ] 固件烧录成功
- [ ] PWM 初始化日志正确
- [ ] PCNT 初始化日志正确
- [ ] MQTT 连接成功
- [ ] 单电机可启动/停止
- [ ] 速度控制误差 < 10%

**测试完成！**
