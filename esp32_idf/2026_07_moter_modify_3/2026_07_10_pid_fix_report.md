# 2026-07-10 PID 结构修复与 9000 RPM 量程适配报告

## 1. 诊断摘要

基于 `2026_07_moter_modify_3\prompt\2026_07_10.md` 对 `main/pid.c`、`main/pcnt.c`、`main/pwm.c` 进行审查，确认以下 6 个问题及代码位置：

| # | 问题 | 代码位置 | 现象/风险 |
|---|------|----------|-----------|
| 1 | 量程错误：`PID_MAX_PCNT` 仅 450 | `main/pid.c:18` | 按旧 4500 RPM 标定，9000 RPM 时空载会饱和 |
| 2 | 位置式与增量式混合 | `main/pid.c:59` `output = data->pre_input + output;` | 积分/历史输出被累加两次，振荡与超调根源 |
| 3 | 微分项对设定值突变敏感 | `main/pid.c:35` `derivative = (error - data->pre_error);` | 目标突变时产生 Derivative Kick |
| 4 | PCNT 异常阈值过低 | `main/pcnt.c:95` `MAX_REASONABLE_PCNT_PER_200MS = 150;` | 9000 RPM 时 200ms 正常脉冲约 180，会被误判异常 |
| 5 | 积分限幅不精确 | `main/pid.c:47-52` 用 `±max_pwm` 限幅 `integral` | Ki 很小时积分 clamp 过保守 |
| 6 | 软启动与状态重置耦合混乱 | `main/pid.c:142-155` 绝对上限软启动；`main/pid.c:173-187` 边沿检测在 PWM 输出后才清零 | `pre_input` 残留影响下一次启动 |

---

## 2. 修复策略与关键决策

| 决策项 | 用户确认结果 | 实现说明 |
|--------|--------------|----------|
| 电机规格 | 12V / 9000 RPM（空载）/ 7750 RPM（额定），供电 6V~12V | `PID_MAX_PCNT` 改为 900，文档同步更新 |
| PWM 频率 | 维持 5kHz 不变 | `pwm.c` / `main.h` 不改动 |
| `PID_data` 结构体 | 最小改动 | 保留 `pre_input`/`d_filtered`，新增 `pre_output`，复用 `pre_measurement` 作为 `pre_current` |
| PID 初始参数 | 保持当前 `Kp=7.0, Ki=0.005, Kd=0.03` | 仅做结构修复，参数作为「待整定」起点 |
| Rate Limiter | 软启动前 2s（10 周期）每周期最大增加 300；正常运行每周期最大变化 450 | 替代原有绝对上限软启动 |

---

## 3. 修改文件清单

- `main/main.h`：新增 `PID_terms` 分项输出结构体；`PID_data` 新增 `pre_output`；更新 `PID_Calculate` 声明。
- `main/pid.c`：重写 `PID_Calculate` 为纯位置式 + 微分先行 + 条件积分 + 积分限幅 `±max_pwm/Ki`；`PID_init` 改为 Rate Limiter 软启动；启动/停止边沿清零状态；新增 `pid_log_terms()` 解耦日志。
- `main/pcnt.c`：异常阈值 150 → 250；突刺检测 `5x+30/50` → `8x+50/100`。
- `main/pwm.c`：**无改动**。已在 `pid.c` 的 `PID_data.pre_output` 中维护上周期输出，避免重复逻辑。
- `hardware_info/CHB-BLDC2418-Motor-Configuration.md`：更新为 12V/9000 RPM 规格，同步 PWM 频率说明、FG 脉冲计算、PID `max_pcnt`、校验清单。
- `AGENTS.md`：同步电机规格与 Max PCNT 描述。

---

## 4. 关键代码

### 4.1 `main/pid.c`（完整）

```c
#include "main.h"

static const char* TAG = "PID_EVENT";

//////////////////////////////////////////////////////////////
//////////////////////// PID 可调参数 //////////////////////////
//////////////////////////////////////////////////////////////
// 以下参数集中在 pid.c 中定义，避免与 main.h 耦合，便于独立调试与快速回退。
// 注意：当前 Kp/Ki/Kd 为结构修复前的经验起点，在 9000 RPM 量程下建议重新整定。
#define PID_KP                  (7.0)   // 比例增益（待整定）
#define PID_KI                  (0.005) // 积分增益（待整定）
#define PID_KD                  (0.03)  // 微分增益（待整定）
#define PID_MAX_PWM             (8191)  // 13-bit 最大值
#define PID_MIN_PWM             (0)     // 输出下限（0 对应反相后 duty=8191，即停止）
#define PID_OUTPUT_MIN_LIMIT    (0)     // PID 输出最小值限制，先保持 0；调研后若需限制最高速可调整
#define PID_MAX_OUTPUT_DELTA    (450.0) // 正常运行每 200ms 最大输出变化
#define PID_SOFTSTART_OUTPUT_DELTA (300.0) // 软启动阶段每 200ms 最大输出增加量
#define PID_SOFTSTART_STEPS     (10)    // 软启动步数（10 * 200ms = 2s）
#define PID_MAX_PCNT            (900)   // 最大 PCNT：9000 RPM / 60 * 6 pulses/rotation
#define PID_MIN_PCNT            (0)     // 最小 PCNT

// 位置式 PID + 微分先行（Derivative on Measurement）+ 条件积分
// terms 为可选输出，传入非 NULL 时返回 P/I/D/error 分项，便于调参日志。
double PID_Calculate(struct PID_params params, struct PID_data *data, double target_speed, double current_speed, struct PID_terms *terms)
{
    // 计算误差
    double error = target_speed - current_speed;

    // 比例项
    double Pout = params.Kp * error;

    // 微分项：对测量值微分，避免设定值突变导致的 Derivative Kick
    double Dout = params.Kd * (data->pre_measurement - current_speed);

    // 条件积分：预测当前误差加入后是否会朝饱和方向加剧
    // 若不加当前误差时输出已朝饱和方向超出，且误差方向与饱和方向相同，则暂停积分
    double predicted_output = Pout + params.Ki * (data->integral + error) + Dout;
    bool saturate_high = (predicted_output > params.max_pwm && error > 0);
    bool saturate_low  = (predicted_output < params.min_pwm && error < 0);

    if (!saturate_high && !saturate_low) {
        data->integral += error;
        // 限制积分项：基于最大输出贡献反推，防止积分过大
        double integral_max = params.max_pwm / params.Ki;
        if (data->integral > integral_max) {
            data->integral = integral_max;
        }
        if (data->integral < -integral_max) {
            data->integral = -integral_max;
        }
    }

    double Iout = params.Ki * data->integral;

    // 总输出：纯位置式，不再与历史输出累加
    double output = Pout + Iout + Dout;

    // 输出限制
    if (output > params.max_pwm) {
        output = params.max_pwm;
    }
    else if (output < params.min_pwm) {
        output = params.min_pwm;
    }

    // 若启用输出下限限制（默认不启用），限制最高转速
    if (PID_OUTPUT_MIN_LIMIT > 0 && output > PID_OUTPUT_MIN_LIMIT) {
        output = PID_OUTPUT_MIN_LIMIT;
    }

    // 保存状态
    data->pre_error = error;
    data->pre_measurement = current_speed;

    // 返回分项（调参日志用）
    if (terms != NULL) {
        terms->Pout = Pout;
        terms->Iout = Iout;
        terms->Dout = Dout;
        terms->error = error;
    }

    return output;
}

// PID 分项日志解耦函数
static void pid_log_terms(int index, double target, double actual, struct PID_terms *terms, double output, int pwm_duty, int startup_counter)
{
    ESP_LOGI(TAG, "Motor %d PID: target=%.0f/s, actual=%.0f/s (raw=%d/200ms), err=%.1f, P=%.1f, I=%.1f, D=%.1f, pid_out=%.0f, pwm_duty=%d, ss=%d",
             index, target, actual, pcnt_count_list[index],
             terms->error, terms->Pout, terms->Iout, terms->Dout,
             output, pwm_duty, startup_counter);
}

// 初始化PID控制器
void PID_init(void* params)
{
    // 获取外部参数
    int index = *((int *) params);
    ESP_LOGI(TAG, "Index number is: %d\n", index);
    // 释放内存
    free(params);

    struct PID_data data = {
        .integral       = 0,
        .pre_error      = 0,
        .pre_input      = 0,   // 已废弃，保留字段以兼容最小改动
        .pre_measurement= 0,
        .d_filtered     = 0,   // 保留字段
        .pre_output     = 0
    };

    // CHB-BLDC2418 PID Parameters
    // Max PCNT = (9000 RPM / 60) * 6 pulses/rotation = 900 pulses/sec
    // Tuned for 200ms sampling interval (5Hz)
    struct PID_params pid_params = {
        .Kp         = PID_KP,
        .Ki         = PID_KI,
        .Kd         = PID_KD,
        .max_pwm    = PID_MAX_PWM,
        .min_pwm    = PID_MIN_PWM,
        .max_pcnt   = PID_MAX_PCNT,
        .min_pcnt   = PID_MIN_PCNT
    };

    // 软启动状态
    bool startup_phase = true;  // true = 处于软启动阶段
    int startup_counter = 0;
    // 跟踪上一周期目标速度，用于检测 0->非零 转换并触发状态清零与软启动
    static double prev_target_speed[4] = {0.0, 0.0, 0.0, 0.0};

    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            // Convert 200ms PCNT count to per-second rate for PID comparison
            // pcnt_count_list is per 200ms, multiply by 5 to get per-second
            double actual_speed_per_sec = pcnt_count_list[index] * 5;

            // 启动边沿检测：从停止转为运行时，清零 PID 历史状态并重新软启动
            // 防止上一条高转速命令的积分/历史输出残留影响下一条低转速命令
            if (temp > 0 && prev_target_speed[index] == 0) {
                startup_phase = true;
                startup_counter = 0;
                data.integral = 0;
                data.pre_error = 0;
                data.pre_measurement = 0;
                data.pre_output = 0;
                ESP_LOGI(TAG, "Motor %d soft-start reset (target: 0 -> %.0f)", index, temp);
            }

            struct PID_terms terms = {0};
            double new_input = PID_Calculate(pid_params, &data, temp, actual_speed_per_sec, &terms);

            // 目标为 0 时强制输出 0（电机停止），并清零 PID 历史状态
            if (temp == 0) {
                new_input = 0;
                data.integral = 0;
                data.pre_error = 0;
                data.pre_measurement = 0;
                data.pre_output = 0;
            }

            // Rate Limiter: 限制相邻周期 PID 输出变化量，平滑 PWM 跳变
            // 软启动阶段使用更小的变化上限，防止启动过冲
            double delta = new_input - data.pre_output;
            double max_pos_delta = startup_phase ? PID_SOFTSTART_OUTPUT_DELTA : PID_MAX_OUTPUT_DELTA;
            if (delta > max_pos_delta) {
                new_input = data.pre_output + max_pos_delta;
            }
            else if (delta < -PID_MAX_OUTPUT_DELTA) {
                new_input = data.pre_output - PID_MAX_OUTPUT_DELTA;
            }

            // 软启动计数
            if (startup_phase) {
                startup_counter++;
                if (startup_counter >= PID_SOFTSTART_STEPS) {
                    startup_phase = false;
                }
            }

            // CHB-BLDC2418: Inverted PWM logic - High=OFF, Low=ON
            // Duty 8191 = Motor OFF, Duty 0 = Motor ON
            int new_input_int = PID_MAX_PWM - (int)new_input;

            // Additional safety clamp for PWM output
            if (new_input_int < 0) new_input_int = 0;
            if (new_input_int > PID_MAX_PWM) new_input_int = PID_MAX_PWM;

            pwm_set_duty(new_input_int, index);

            pid_log_terms(index, temp, actual_speed_per_sec, &terms, new_input, new_input_int, startup_counter);
            pcnt_updated_list[index] = false;

            // 更新上周期目标速度
            prev_target_speed[index] = temp;
            // 更新上周期输出（用于下一周期速率限制）
            data.pre_output = new_input;
        }
        else{
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}

void pid_process_init()
{
    for(int i = 0; i < 4; i++)
    {
        // 动态分配所需的内存空间
        int *j = (int *)malloc(sizeof(int));
        if(j != NULL)
        {
            *j = i;
            // 创建线程
            if(xTaskCreate(PID_init, "PID_TASK", 4096, (void*) j, 1, NULL) != pdPASS)
            {
                // 如果失败，释放内存
                ESP_LOGI(TAG, "PID process %d creation failed.", *j);
                free(j);
            }
        }
    }
}


// 创建一个控制任务
void control_cmd(void *params)
{
    cmd_params* local_params = (cmd_params*)params;
    int local_speed = local_params->speed;
    int local_duration = local_params->duration;
    int local_index = local_params->index;

    // Free the allocated memory after copying to local variables
    free(local_params);

    TaskHandle_t my_handle = xTaskGetCurrentTaskHandle();

    char buff[64];
    sprintf(buff, "task_create_%d_%d_%d", local_index, local_speed, local_duration);
    // QoS 0 非阻塞发布，避免任务被删除时卡在 MQTT 握手
    mqtt_publish_safe(mqtt_task_topic, buff, strlen(buff), 0, 0);
    motor_speed_list[local_index] = local_speed;
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    // CHB-BLDC2418: Duty 8191 = Motor OFF (inverted logic)
    pwm_set_duty(8191, local_index);
    sprintf(buff, "task_finished_%d_%d_%d", local_index, local_speed, local_duration);
    // QoS 0 非阻塞发布，避免任务被删除时卡在 MQTT 握手
    mqtt_publish_safe(mqtt_task_topic, buff, strlen(buff), 0, 0);

    // 任务正常结束时清空句柄；仅当本任务仍被记录为当前任务时才清空，
    // 避免在新命令已创建新任务后误把新句柄覆盖为 NULL。
    if (cmd_task_handle[local_index] == my_handle) {
        cmd_task_handle[local_index] = NULL;
    }
    vTaskDelete(NULL);
}
```

### 4.2 `main/pcnt.c` 修改片段

```c
    // Max theoretical PCNT per 200ms: 900 pulses/sec * 0.2s = 180
    // Allow some margin: 250 per 200ms (1250/s) is max reasonable
    const int MAX_REASONABLE_PCNT_PER_200MS = 250;
```

```c
            // 额外检测相对突刺：当前值显著大于最近中值（启动期前几个点不启用）
            if (!is_abnormal && pcnt_filter_ready[index] && 
                pcnt_count_list[index] > median_raw * 8 + 50 && 
                pcnt_count_list[index] > 100) {
                is_abnormal = true;
            }
```

### 4.3 `main/pwm.c`

无改动。速率限制所需的上周期输出已在 `pid.c` 的 `PID_data.pre_output` 中维护，未在 `pwm.c` 中重复维护 `pwm_current_duty[4]`。

---

## 5. 调参与验证指南

### 5.1 关键公式

- 转速换算：`RPM = pulses/sec × 60 / 6 = pulses/sec × 10`
- 目标值范围：`0 ~ 900 pulses/sec`，对应 `0 ~ 9000 RPM`（空载）
- 每 200ms 原始计数：`raw = pulses/sec / 5`

### 5.2 当前代码中的起点参数

```c
#define PID_KP  (7.0)   // 待整定
#define PID_KI  (0.005) // 待整定
#define PID_KD  (0.03)  // 待整定
```

> **注意**：这些参数是结构修复前在 4500 RPM 量程下的经验值。由于算法结构已改为纯位置式 + 微分先行，相同数值在 9000 RPM 量程下的表现会不同，**务必重新整定**。

### 5.3 推荐手动整定流程

1. **开环阶跃测试**
   - 固定输出 50% PWM（反相后 `duty = 4096`）。
   - 记录 200ms 内 `pcnt_count` 稳定值，换算为 `pulses/sec`。
   - 计算电机增益 `K`（pulses/sec per %PWM）和时间常数 `τ`（达到 63% 稳态值的时间）。

2. **纯 P 测试**
   - 设 `Ki=0, Kd=0`，从 `Kp=1.0` 开始逐步提高。
   - 直到出现轻微振荡，记录临界增益 `Ku` 和振荡周期 `Tu`。

3. **加入 I**
   - 初始尝试 `Ki = Kp / (2 * τ)`。
   - 或按 Ziegler-Nichols：`Ki = 1.2 * Kp / Tu`。
   - 逐步消除静差，观察是否引入振荡。

4. **加入 D**
   - 初始尝试 `Kd = Kp * Tu / 8` 或从 `0.5` 开始。
   - 用于抑制超调。若 FG 信号噪声大导致 D 项抖动，降低 `Kd` 或增加 PCNT 中值滤波窗口。

5. **低速测试**
   - 目标设为 `90 pulses/sec`（约 900 RPM）。
   - 观察是否平稳。若抖动，降低 `Kd` 或增大 `PID_MAX_OUTPUT_DELTA`。

### 5.4 Rate Limiter 调整建议

| 阶段 | 当前值 | 说明 |
|------|--------|------|
| 软启动最大增加 | 300 / 200ms | 前 2 秒限制加速，减小启动冲击 |
| 正常运行最大变化 | 450 / 200ms | 限制相邻周期输出跳变，抑制振荡 |

若电机响应迟缓，可逐步提高软启动增加量；若高速切换仍有超调，可降低正常变化上限。

---

## 6. 编译指引

在已安装 ESP-IDF 5.5.2 的环境中执行：

```powershell
# 进入项目目录
cd e:\Platform_G2\esp32_idf

# 加载 ESP-IDF 环境（PowerShell）
. $env:IDF_PATH/export.ps1

# 清理并编译
idf.py fullclean
idf.py build

# 烧录（按实际 COM 口修改）
idf.py -p COM9 flash
idf.py -p COM9 monitor
```

> 本次修改未涉及 sdkconfig、分区表、任务结构、MQTT 逻辑或 LEDC/PCNT 初始化流程，因此 `menuconfig` 通常无需调整。

---

## 7. 验证 checklist

- [ ] `idf.py build` 通过，无编译警告/错误。
- [ ] 串口日志中 `Motor X PID: ...` 出现 `P=... I=... D=... err=...` 分项。
- [ ] 发送 `cmd_0_900_10`（目标 9000 RPM）后电机能接近空载上限。
- [ ] 发送 `cmd_0_90_10`（目标 900 RPM）后低速运行平稳，无抖动。
- [ ] 高→低目标切换时无 residual 超速。
- [ ] PCNT 日志无 `outlier rejected` 误报。

---

*报告生成时间：2026-07-09*
