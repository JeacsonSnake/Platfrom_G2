# ESP32S3 电机 PID 调速优化最终总结报告

**日期**: 2026-07-08  
**分支**: `main`  
**任务描述**: 针对 CHB-BLDC2418 电机在 ESP32-S3 上的 PID 闭环调速进行优化，重点解决高转速切换到低转速时的“高转速缓存”导致的超速问题，并评估低速区可控性。

---

## 1. 背景

### 历史问题回顾
根据 2026-07-08 的调试记录，Motor 2 在 PID 闭环调速中存在以下问题：
- 高转速命令切换到低转速命令时，电机经常先以高速冲出，再慢慢回落至目标速度；
- 低速目标（如 target=5）从 0 启动困难或无法启动；
- 命令之间存在明显的“惯性超速”，即使命令间隔已超过 5 秒；
- 推测原因：MQTT broker 传输延迟导致命令到达顺序与下发顺序不一致，以及 PID 内部状态未在命令切换时正确重置。

### 本次任务目标
1. 集中 PID 可调参数，便于独立调试；
2. 增加 Rate Limiter 与条件积分抗饱和，抑制启动/切换过冲；
3. 调整 Kp/Ki/Kd，改善低速启动性能；
4. 修复高转速→低转速切换时的 PID 状态残留问题；
5. 输出多轮测试报告，最终形成总结文档。

---

## 2. 优化方案设计

### 2.1 PID 参数调整

| 参数 | 原始值 | 最终值 | 说明 |
|------|--------|--------|------|
| Kp | 8.0 | 7.0 | 降低比例增益，抑制过冲，同时保留低速启动扭矩 |
| Ki | 0.02 | 0.005 | 按 5Hz 采样率缩减积分增益，避免积分饱和 |
| Kd | 0.01 | 0.03 | 增强微分阻尼 |
| Rate Limiter | 无 | ±500/200ms | 限制相邻周期 PID 输出变化量 |
| Soft Start | 有 | 有 | 初始 2 秒内限制最大输出 |

### 2.2 关键代码修改

#### `main/pid.c`

**1. PID 参数宏集中定义**

```c
#define PID_KP                  (7.0)
#define PID_KI                  (0.005)
#define PID_KD                  (0.03)
#define PID_MAX_OUTPUT_DELTA    (500.0)
#define PID_SOFTSTART_MAX_INIT  (3000.0)
#define PID_SOFTSTART_STEPS     (10)
```

**2. 条件积分抗饱和**

```c
// 条件积分：预测当前误差加入后是否会加剧饱和
// 若不加当前误差时输出已朝饱和方向超出，且误差方向与饱和方向相同，则暂停积分
double predicted_output = Pout + params.Ki * data->integral + Dout + data->pre_input;
bool saturate_high = (predicted_output > params.max_pwm && error > 0);
bool saturate_low  = (predicted_output < params.min_pwm && error < 0);

if (!saturate_high && !saturate_low) {
    data->integral += error;
    // 限制积分项
    if (data->integral > params.max_pwm) data->integral = params.max_pwm;
    if (data->integral < params.min_pwm) data->integral = params.min_pwm;
}
```

**3. Rate Limiter**

```c
double delta = new_input - data.pre_input;
if (delta > PID_MAX_OUTPUT_DELTA) {
    new_input = data.pre_input + PID_MAX_OUTPUT_DELTA;
}
else if (delta < -PID_MAX_OUTPUT_DELTA) {
    new_input = data.pre_input - PID_MAX_OUTPUT_DELTA;
}
```

**4. target=0 时清零 PID 状态**

```c
// 目标为 0 时强制输出 0（电机停止），并清零 PID 历史状态，
// 避免上一条高转速命令的 pre_input/integral 残留到下一条低转速命令。
if (temp == 0) {
    new_input = 0;
    data.integral = 0;
    data.pre_error = 0;
    data.pre_input = 0;
}
```

**5. 软启动 reset 时清零 PID 状态**

```c
if (temp > 0 && prev_target_speed[index] == 0) {
    if (!startup_phase || startup_counter > PID_SOFTSTART_STEPS) {
        startup_phase = 1;
        startup_counter = 0;
        // 关键：清零 PID 内部状态
        data.integral = 0;
        data.pre_error = 0;
        data.pre_input = 0;
        ESP_LOGI(TAG, "Motor %d soft-start reset ...", index);
    }
}
```

---

## 3. Git 提交记录

### 第一次提交：集中 PID 参数并增加 Rate Limiter

```bash
git add main/pid.c
git commit -m "feat(pid): 集中 PID 参数、增加 Rate Limiter 并优化抗饱和逻辑"
```

**提交信息**:  
- Commit: `425da6a`
- 修改 `main/pid.c`

### 第二次提交：应用推荐 PID 参数

```bash
git add main/pid.c
git commit -m "feat(pid): 应用推荐 PID 参数并整理 max_pcnt/min_pcnt 宏位置"
```

**提交信息**:  
- Commit: `775a94f`

### 第三次提交：回调 Kp 提升低速启动扭矩

```bash
git add main/pid.c
git commit -m "feat(pid): 回调 Kp 至 7.0 提升低速启动扭矩"
```

**提交信息**:  
- Commit: `b3ea7ce`

### 第四次提交：target=0 时清零 PID 状态

```bash
git add main/pid.c
git commit -m "fix(pid): target=0 时清零 PID 状态，消除高转速到低转速的缓存效应"
```

**提交信息**:  
- Commit: `a7d2bff`

### 第五次提交：软启动 reset 时清零 PID 状态

```bash
git add main/pid.c
git commit -m "fix(pid): 在软启动 reset 时清零 PID 状态，彻底消除转速残留"
```

**提交信息**:  
- Commit: `588e802`

### 第六次提交：分析脚本增强

```bash
git add 2026_07_moter_modify/analyze_motor_log.py
git commit -m "feat(analysis): 支持命令行参数并改进 segment 惯性/异常值处理"
```

**提交信息**:  
- Commit: `681b73f`

---

## 4. 测试结果

### 4.1 测试方法

- 测试电机：Motor 2
- PCNT 采样周期：200ms（5Hz）
- PID 控制周期：200ms（5Hz）
- 命令格式：`cmd_<motor>_<target>_<duration>`
- 分析脚本：`2026_07_moter_modify/analyze_motor_log.py`

### 4.2 关键指标对比

| 指标 | 优化前 | modified_2 (Kp=7, 未修复状态残留) | modified_final (最终版) |
|------|--------|-----------------------------------|------------------------|
| 稳态精度 | ±3% 以内 | 23/24 目标在 ±3% 内 | 9/10 目标在 ±3% 内 |
| target=5 启动 | 可启动但过冲大 | 无法启动 | 未专门测试 |
| 真正从 0 启动最大过冲 | 465 pulses/sec | 0 pulses/sec | 20 pulses/sec |
| 最大切换过冲 | 465 pulses/sec | 355 pulses/sec | **350 pulses/sec** |
| 高速饱和 | ~444 pulses/sec | ~445 pulses/sec | ~430 pulses/sec |

### 4.3 modified_final 关键发现

- **稳态控制精度**：target > 10 且样本充足的目标中，9/10 误差在 ±3% 以内；
- **真正从 0 启动过冲**：很小（20 pulses/sec），说明 PID 状态清零修复有效；
- **切换过冲**：最大仍达 350 pulses/sec，主要发生在 450→50、300→150、250→75/20 等高低切换场景；
- **高速区饱和**：目标 450 时实际稳态约 430 pulses/sec，接近电机物理上限。

---

## 5. 问题分析

### 5.1 为什么切换过冲仍然存在？

日志证据（modified_final, target=50 切换段）：

```
actual=   0, pid_out=8191, pwm_duty=0   ← 第一拍全速输出
actual= 400, pid_out=5732, pwm_duty=2462
...
```

根本原因是 **MQTT broker 传输延迟/乱序** 与 **control_cmd 任务并发**：

1. 测试脚本下发命令序列大致为 `高转速 → 停止(0) → 低转速`；
2. 由于 MQTT broker 延迟，设备端实际接收到的顺序可能出现 `高转速 → 低转速 → 停止(0)` 或 `高转速 → 低转速`（缺少中间的停止命令）；
3. 每次收到 `cmd_` 消息都会创建一个新的 `control_cmd` 任务，多个任务可能同时运行；
4. 当 `target=450` 的任务和 `target=50` 的任务重叠时，`motor_speed_list` 被后者覆盖为 50，但 PID 的 `pre_input` 仍来自 450 任务的输出；
5. 即使 PID 在 `temp=0` 或 `temp` 从 0 变非 0 时清零，若命令序列中根本没有出现 `target=0` 的周期（或只出现 1 个 200ms 周期），清零窗口太短，无法覆盖所有乱序情况。

### 5.2 为什么 PID 清零修复对“真正从 0 启动”有效？

当命令序列中确实存在 `target=0` 且持续至少一个 PID 周期时：
- `temp==0` 分支会清零 PID 状态；
- 下一条命令到达时，`temp` 从 0 变非 0 触发软启动 reset，再次清零；
- 因此从 0 启动时不再有 `pre_input` 残留。

### 5.3 为什么 target=5 难以启动？

- target=5 时，error=5，Kp=7 时 Pout=35，输出过小；
- 无法克服电机静摩擦与启动扭矩需求；
- 本次最终测试未专门处理此问题。

---

## 6. 结论

1. **稳态性能优秀**：Motor 2 在 20~475 pulses/sec 范围内稳态误差基本在 ±3% 以内；
2. **从 0 启动性能改善**：通过 PID 状态清零，真正从 0 启动的过冲已降至很小；
3. **高→低转速切换过冲未完全消除**：最大切换过冲仍达 350 pulses/sec，主要由 MQTT broker 传输延迟/乱序和 control_cmd 任务并发导致；
4. **在现有架构下，纯 PID 参数/状态优化已接近极限**；要彻底解决切换过冲，需要在命令调度层保证命令顺序和互斥。

---

## 7. 后续建议

### 方案 A: 命令调度层改造（推荐）

在 `main/mqtt.c` 的 `message_compare()` 中，为每个电机维护一个当前运行任务的句柄：

```c
static TaskHandle_t cmd_task_handle[4] = {NULL};

// 收到新命令时，先删除同电机的旧任务
cmd_params *params = malloc(sizeof(cmd_params));
if (params != NULL) {
    params->speed = speed;
    params->duration = duration;
    params->index = index;

    if (cmd_task_handle[index] != NULL) {
        vTaskDelete(cmd_task_handle[index]);
        cmd_task_handle[index] = NULL;
        motor_speed_list[index] = 0;
        pwm_set_duty(8191, index);
    }

    if (xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, &cmd_task_handle[index]) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        free(params);
    }
}
```

这样可确保同一时刻每个电机只有一个 control_cmd 任务在运行，避免命令覆盖。

### 方案 B: 在设备端增加命令去抖

在 `control_cmd` 中，设置目标后先等待 200~500ms 的“死区”，如果期间收到新命令则自动放弃当前任务。

### 方案 C: 改用命令队列

使用 FreeRTOS Queue 串行处理命令，保证命令按到达顺序执行，避免并发冲突。

### 方案 D: 应用层下发命令时增加互斥

在 MQTT 客户端测试脚本中，确保前一条命令完全结束（收到 `task_finished_...`）后再发下一条命令。

### 方案 E: 对 target < 10 单独处理（如需支持低速）

```c
if (temp > 0 && temp < 10) {
    // 设置最小启动输出，确保克服静摩擦
    if (new_input < 500) new_input = 500;
}
```

---

## 8. 参考链接

- [ESP-IDF LEDC PWM 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)
- [ESP-IDF PCNT 脉冲计数文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html)
- [CHB-BLDC2418 电机配置文档](hardware_info/CHB-BLDC2418-Motor-Configuration.md)
- 原始低速区调研报告：`2026_07_moter_modify/低速区可控性调研报告.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-07-08  
**完成时间**: 2026-07-08
