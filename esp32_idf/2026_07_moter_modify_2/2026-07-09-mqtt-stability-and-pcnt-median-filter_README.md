# ESP32-S3 MQTT 长时稳定性与 PCNT 噪声抑制优化记录

**日期**: 2026-07-09  
**分支**: `main`  
**任务描述**: 解决长时电机运行场景下 MQTT 频繁断连与日志中断问题，并通过 PCNT 中值滤波抑制脉冲计数噪声，提升电机控制平稳性与 MQTT 连接保持率。

---

## 1. 背景

### 1.1 历史问题回顾

在 `2026_07_moter_modify_2/modified_2/esp32_log_20260709_160008.txt` 测试中，ESP32-S3 运行 43.56 分钟出现以下问题：

- **MQTT 频繁断连**：断开 31 次，连接保持率约 74~84%。
- **断连原因集中为 `PING_OR_UNKNOWN_ERROR`**：日志底层实际为 `Writing didn't complete in specified timeout: errno=119`（TCP 写超时），被 ESP-IDF MQTT 客户端映射为 PING 或未知错误。
- **心跳发布偶发阻塞**：最长一次心跳耗时 13.65 秒。
- **PCNT 脉冲计数噪声严重**：Motor 2 启动时出现 `raw=2026/2635/10130` 等异常计数，导致 PID 误判电机超速，PWM 瞬间切到 8191（OFF），引发控制振荡。
- **高→低转速切换过冲大**：target=40/50/60 启动过冲分别达 625%、450%、558%。

### 1.2 问题关联分析

PCNT 噪声 → PID 输出剧烈抖动 → `pwm_set_duty()` 频繁发布 PWM 遥测 → MQTT 发送缓冲区被高频率 QoS 0 消息打满 → TCP 写超时 → 断连。因此，**PCNT 噪声既是控制问题，也是 MQTT 稳定性的诱因之一**。

### 1.3 本次任务目标

1. 降低长时电机运行时 MQTT 断连频率，提升连接保持率。
2. 抑制 PCNT 脉冲计数噪声，减少启动/切换过冲。
3. 保持遥测发布频率不变，避免以牺牲控制精度换取网络稳定。

---

## 2. 实现方案

### 2.1 阶段一：MQTT 层优化（commit `df88ca0`）

#### 需求分析

| 需求项 | 说明 |
|--------|------|
| 非阻塞发布 | 避免 MQTT 断连时 `esp_mqtt_client_publish()` 阻塞调用任务 |
| QoS 降级 | 周期性/生命周期遥测使用 QoS 0，减少 broker ACK 等待 |
| 参数调优 | 增大 keepalive、超时、重连间隔，降低网络抖动敏感度 |
| 任务优先级 | 避免 MQTT 内部任务饥饿 |

#### 修改文件

##### `main/mqtt.c`

新增 `mqtt_publish_safe()`：未连接时直接跳过，避免调用任务阻塞。

```c
int mqtt_publish_safe(const char *topic, const char *data, int len, int qos, int retain)
{
    if (mqtt_client == NULL || topic == NULL || data == NULL) return -1;
    if (!get_connect_flag()) return -1;  // 未连接时不入队
    return esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
}
```

调整 MQTT 配置：

```c
.session = {
    .keepalive = 120,
    .disable_clean_session = true,
},
.network = {
    .reconnect_timeout_ms = 8000,
    .timeout_ms = 15000,
},
.task = {
    .priority = 4,
    .stack_size = 8192,
}
```

##### `main/pwm.c` / `main/pcnt.c` / `main/pid.c`

所有周期性/生命周期 MQTT 发布改为 QoS 0，并调用 `mqtt_publish_safe()`：

```c
// pwm.c
mqtt_publish_safe(mqtt_telemetry_topic, buff, strlen(buff), 0, 0);

// pcnt.c
mqtt_publish_safe(mqtt_telemetry_topic, buff, strlen(buff), 0, 0);

// pid.c（control_cmd 任务生命周期通知）
mqtt_publish_safe(mqtt_task_topic, buff, strlen(buff), 0, 0);
```

##### `main/main.h`

声明 `mqtt_publish_safe()`。

##### `AGENTS.md`

更新 MQTT 参数文档。

---

### 2.2 阶段二：PCNT 中值滤波（当前未提交变更）

#### 需求分析

| 需求项 | 说明 |
|--------|------|
| 抑制单点噪声 | 对 FG 信号偶发尖峰进行滤波 |
| 不降低遥测频率 | 保持 5 Hz PCNT 采样与发布 |
| 避免误判停转 | 异常值不再直接清零，而用中值替代 |
| 启动边沿重置 | 电机从停止到运行时清空旧滤波历史 |

#### 修改文件

##### `main/pcnt.c`

新增 3 样本中值滤波器状态：

```c
// PCNT 中值滤波器状态（用于抑制启动/运行时的脉冲计数噪声，不影响遥测发布频率）
#define PCNT_FILTER_WINDOW 3
static int pcnt_filter_buf[4][PCNT_FILTER_WINDOW] = {{0}};
static int pcnt_filter_idx[4] = {0};
static bool pcnt_filter_ready[4] = {false};
```

新增中值计算与更新函数：

```c
// 三个数取中值（无分支排序）
static inline int median3(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { int t = b; b = c; c = t; }
    if (a > b) { int t = a; a = b; b = t; }
    return b;
}

// 将新采样加入中值滤波窗口并返回中值
static int pcnt_update_median(int index, int raw)
{
    pcnt_filter_buf[index][pcnt_filter_idx[index]] = raw;
    pcnt_filter_idx[index] = (pcnt_filter_idx[index] + 1) % PCNT_FILTER_WINDOW;
    if (pcnt_filter_idx[index] == 0) {
        pcnt_filter_ready[index] = true;
    }
    return median3(pcnt_filter_buf[index][0], pcnt_filter_buf[index][1], pcnt_filter_buf[index][2]);
}

// 重置中值滤波器
static void pcnt_reset_filter(int index)
{
    for (int i = 0; i < PCNT_FILTER_WINDOW; i++) {
        pcnt_filter_buf[index][i] = 0;
    }
    pcnt_filter_idx[index] = 0;
    pcnt_filter_ready[index] = false;
}
```

在采样循环中应用滤波：

```c
// 检测电机启动边沿：从停止转为运行时重置中值滤波器
if (motor_speed_list[index] != 0 && !was_running) {
    pcnt_reset_filter(index);
    was_running = true;
} else if (motor_speed_list[index] == 0) {
    was_running = false;
}

// 获取当前数字，并清除
pcnt_unit_get_count(unit, &pcnt_count_list[index]);
pcnt_unit_clear_count(unit);

// 对原始值做中值滤波
int median_raw = pcnt_update_median(index, pcnt_count_list[index]);

// 异常值检测：超限或相对突刺时记录警告
if (abnormal_check_enabled) {
    bool is_abnormal = (pcnt_count_list[index] > MAX_REASONABLE_PCNT_PER_200MS || 
                        pcnt_count_list[index] < 0);
    if (!is_abnormal && pcnt_filter_ready[index] && 
        pcnt_count_list[index] > median_raw * 5 + 30 && 
        pcnt_count_list[index] > 50) {
        is_abnormal = true;
    }
    if (is_abnormal) {
        ESP_LOGW(TAG, "Motor %d PCNT outlier rejected: raw=%d, using median=%d", 
                 index, pcnt_count_list[index], median_raw);
    }
}
// 用中值作为控制/遥测使用的计数值
pcnt_count_list[index] = median_raw;
```

##### `AGENTS.md`

更新 PCNT 模块描述，记录中值滤波与异常值替换逻辑。

---

## 3. 测试验证

### 3.1 测试日志

| 日志 | 运行时长 | 说明 |
|------|---------|------|
| `modified_2/esp32_log_20260709_160008.txt` | 43.56 min | MQTT 优化后、PCNT 滤波前 |
| `modified_3/esp32_log_20260709_171248.txt` | 26.93 min | PCNT 中值滤波后 |

### 3.2 MQTT 稳定性对比

| 指标 | modified_2 | modified_3 | 改善 |
|------|-----------:|-----------:|------|
| 总断开次数 | 31 | **2** | 减少 93.5% |
| 连接保持率 | 84.30% | **98.75%** | +14.45 pct |
| 首次稳定连接后持续时长 | 频繁断连 | **~21 min 无断连** | 显著改善 |
| 心跳 >1 s 次数 | 多次（最长 13.65 s） | 仅早期 3 次 | 基本消除 |

modified_3 的两次断连均发生在运行前 5 分钟（17:17:31、17:18:04），恢复耗时 8–9 秒；之后从 17:18:14 到日志结束一直保持连接。

### 3.3 电机控制性能对比

| 指标 | modified_2 | modified_3 | 改善 |
|------|-----------:|-----------:|------|
| PID 记录数 | 5675 | **6879** | 控制连续性更好 |
| 稳态最大误差 | 0.9% | **0.4%** | 精度提升 |
| target=40 最大过冲 | 250 (625%) | **30 (75%)** | 降低 8.3 倍 |
| target=50 最大过冲 | 225 (450%) | **25 (50%)** | 降低 8.9 倍 |
| target=60 最大过冲 | 335 (558%) | **35 (58%)** | 降低 8.5 倍 |
| 稳态标准差 | 8–13 | **3–4** | 运行更平稳 |
| PCNT 异常单点最大值 | 10130 pulses/sec | **1150 pulses/sec** | 降低近 9 倍 |

### 3.4 可视化报告

已使用 `2026_07_moter_modify/analyze_motor_log.py` 生成图表与报告：

- `modified_3/motor_2_speed_pwm_curve.png`
- `modified_3/motor_2_transient_target_40.png`
- `modified_3/低速区可控性调研报告_171248.md`

速度-PWM 曲线几乎完美贴合理想 `y=x`，瞬态响应在启动尖峰后迅速稳定。

---

## 4. Git 提交记录

### 提交 1：MQTT 稳定性优化

```bash
git add main/mqtt.c main/pwm.c main/pcnt.c main/pid.c main/main.h AGENTS.md
git commit -m "fix(mqtt): 降低长时电机运行时的 MQTT 断连与日志中断风险

- 新增 mqtt_publish_safe()，未连接时跳过发布，避免调用任务阻塞
- 周期性/生命周期 MQTT 发布改为 QoS 0
- 调整 MQTT 配置：keepalive=120s, disable_clean_session=true,
  reconnect_timeout_ms=8000, timeout_ms=15000, 任务优先级 4
- 修复 control_cmd 任务并发覆盖 motor_speed_list 的问题"
```

**提交信息**:  
- Commit: `df88ca0`

### 提交 2：PCNT 中值滤波（待提交）

```bash
git add main/pcnt.c AGENTS.md
git commit -m "fix(pcnt): 添加 3 样本中值滤波抑制脉冲计数噪声

- 为每个电机通道维护 3 样本中值滤波窗口
- 异常值用中值替换而非清零，避免 PID 误判停转
- 电机启动边沿自动重置滤波器
- 保持 5 Hz PCNT 采样与遥测频率不变"
```

---

## 5. 关键问题解析

### 5.1 `PING_OR_UNKNOWN_ERROR` 的真实原因

日志底层错误为：

```
transport_base: Poll timeout or error, errno=Connection already in progress, fd=54, timeout_ms=15000
mqtt_client: Writing didn't complete in specified timeout: errno=119
```

`errno=119` 即 `EINPROGRESS`，表示 TCP 发送缓冲区在 15 秒内无法接受数据。ESP-IDF MQTT 客户端将该传输层写超时映射为 `PING_OR_UNKNOWN_ERROR`，因此监控报告中的“PING 响应超时”实际上是 **MQTT 发送队列拥塞** 导致的写超时。

### 5.2 为什么 PCNT 滤波能改善 MQTT 稳定性

PCNT 噪声导致 PID 输出抖动 → `pwm_set_duty()` 每次变化都会发布一条 MQTT 消息 → 在 broker/网络处理不及时时，发送缓冲区快速积满 → TCP 写超时 → 断连。中值滤波减少 PWM 抖动后，遥测发送频率自然下降，MQTT 发送压力显著缓解。

---

## 6. 使用说明

### 6.1 编译烧录

```powershell
idf.py build
idf.py -p COM9 flash monitor
```

### 6.2 观察重点

1. **PCNT 滤波日志**：查找 `PCNT outlier rejected: raw=..., using median=...` 确认滤波生效。
2. **电机启动响应**：观察 `PID_EVENT` 中 target 切换后是否还有 `pwm_duty=8191` 的极端跳变。
3. **MQTT 统计报告**：标签 `MQTT_MONITOR` 每 8 分钟输出一次，关注连接保持率。
4. **心跳耗时**：标签 `ESP32S3_MQTT_EVENT` 每 30 秒输出 `心跳已发送 (elapsed=Xms)`，理想为 0 ms。

---

## 7. 后续建议

### 方案 A：扩大中值滤波窗口

将 `PCNT_FILTER_WINDOW` 从 3 扩大到 5，可进一步压制 2–3 个连续采样点的噪声簇。代价是延迟从 200 ms 增加到 400 ms，对电机机械惯性仍可接受。

### 方案 B：长时间稳定性验证

当前 modified_3 日志仅 26.93 分钟，建议跑 1 小时以上测试，确认 98.75% 的保持率是否可持续。

### 方案 C：硬件层面排查

PCNT 仍有 1150 pulses/sec 的尖峰，可能来自：
- FG 信号走线靠近电机电源线
- 共地不良
- 12V 电源纹波
建议用示波器观察 FG 信号，或缩短/屏蔽 FG 走线。

---

## 8. 参考链接

- [ESP-IDF MQTT Client 配置文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [ESP-IDF PCNT 脉冲计数驱动](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html)
- 分析脚本: `2026_07_moter_modify/analyze_motor_log.py`
- 历史报告: `2026_07_moter_modify/低速区可控性调研报告.md`
- 测试日志:
  - `2026_07_moter_modify_2/modified_2/esp32_log_20260709_160008.txt`
  - `2026_07_moter_modify_2/modified_3/esp32_log_20260709_171248.txt`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-07-09  
**完成时间**: 2026-07-09
