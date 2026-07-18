#include "main.h"

static const char* TAG = "PID_EVENT";

//////////////////////////////////////////////////////////////
//////////////////////// PID 可调参数 //////////////////////////
//////////////////////////////////////////////////////////////
// 以下参数集中在 pid.c 中定义，避免与 main.h 耦合，便于独立调试与快速回退。
// 当前采用：前馈（开环映射）+ 闭环 PID 修正。PID 参数用于修正环，
// 因此 Kp/Ki/Kd 数值比纯 PID 直接输出要小，修正量由 PID_CORR_MAX/MIN 限制。
// PID 控制周期为 100ms（10Hz），与高精度周期捕获读数匹配。
#define PID_PERIOD_MS           (100)   // PID 控制周期（ms）
#define PID_KP                  (3.0)   // 闭环修正比例增益（100ms 周期）
#define PID_KI                  (0.05)  // 闭环修正积分增益（100ms 周期，等效 200ms 时 0.1）
#define PID_KD                  (0.6)   // 闭环修正微分增益（100ms 周期，等效 200ms 时 0.3）
#define PID_CORR_MAX            (300.0) // 闭环修正上限（PWM），防止前馈被大幅偏离
#define PID_CORR_MIN            (-300.0)// 闭环修正下限（PWM）
#define PID_MAX_PWM             (8191)  // 13-bit 最大值
#define PID_MIN_PWM             (0)     // 输出下限（0 对应反相后 duty=8191，即停止）
#define PID_OUTPUT_MIN_LIMIT    (0)     // PID 输出最小值限制，先保持 0；调研后若需限制最高速可调整
#define PID_MAX_OUTPUT_DELTA    (250.0) // 100ms 周期最大输出增加量（等效 500/200ms）
#define PID_MAX_BRAKING_DELTA   (450.0) // 100ms 周期最大输出减少量（等效 900/200ms）
#define PID_SOFTSTART_OUTPUT_DELTA (150.0) // 100ms 软启动每周期最大增加量（等效 300/200ms）
#define PID_SOFTSTART_STEPS     (20)    // 软启动步数（20 * 100ms = 2s）
#define PID_MAX_PCNT            (4500)  // 最大转速：12V 供电下实际空载约 4500 RPM
#define PID_MIN_PCNT            (0)     // 最小 PCNT
#define PID_OPENLOOP_OFFSET     (300.0) // 死区补偿偏移量：输出低于此值电机不转
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

// PID 分项日志解耦函数（开环测试期间暂不使用）
// static void pid_log_terms(int index, double target, double actual, struct PID_terms *terms, double output, int pwm_duty, int startup_counter)
// {
//     ESP_LOGI(TAG, "Motor %d PID: target=%.0f RPM, actual=%.0f RPM (raw=%d/200ms), err=%.1f, P=%.1f, I=%.1f, D=%.1f, pid_out=%.0f, pwm_duty=%d, ss=%d",
//              index, target_rpm, actual_rpm, pcnt_count_list[index],
//              terms->error, terms->Pout, terms->Iout, terms->Dout,
//              output, pwm_duty, startup_counter);
// }

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

    // CHB-BLDC2418 转速参数
    // 采用前馈（开环映射）+ 闭环 PID 修正架构。前馈提供基准 PWM，
    // PID 修正根据 target 与 actual 的误差计算 PWM 修正量，并保留 Rate Limiter / 软启动 / 条件积分。
    // 转速单位：RPM（cmd_2_800_10 表示 800 RPM）
    // Tuned for 100ms sampling interval (10Hz)
    struct PID_params pid_params = {
        .Kp         = PID_KP,
        .Ki         = PID_KI,
        .Kd         = PID_KD,
        .max_pwm    = PID_CORR_MAX,    // PID 输出为修正量，限制在 ±300 PWM
        .min_pwm    = PID_CORR_MIN,
        .max_pcnt   = PID_MAX_PCNT,
        .min_pcnt   = PID_MIN_PCNT
    };

    // 软启动状态
    bool startup_phase = true;  // true = 处于软启动阶段
    int startup_counter = 0;
    // 跟踪上一周期目标速度，用于检测 0->非零 转换并触发状态清零与软启动
    static double prev_target_speed[4] = {0.0, 0.0, 0.0, 0.0};

    while(1){
        double temp = motor_speed_list[index];

        if (temp > 0) {
            // 使用 GPIO 中断捕获的脉冲周期计算高精度 RPM（6 PPR => RPM = 10,000,000 / period_us）
            // 保留 PCNT 原始计数作为兼容字段 raw=.../200ms 显示
            double actual_rpm = pcnt_get_rpm_highres(index);

            // 启动边沿检测：从停止转为运行时重新启用软启动（速率限制）并清零 PID 状态
            if (prev_target_speed[index] == 0) {
                startup_phase = true;
                startup_counter = 0;
                data.pre_output = 0;
                data.integral = 0;
                data.pre_error = 0;
                data.pre_measurement = 0;
                ESP_LOGI(TAG, "Motor %d closed-loop PID reset (target: 0 -> %.0f)", index, temp);
            }

            // ========== 前馈 + 闭环 PID 修正 ==========
            // 前馈：基于开环标定给出基准 PWM（解决死区与近似线性区）
            double slope = (PID_MAX_PWM - PID_OPENLOOP_OFFSET) / (double)PID_MAX_PCNT;
            double feedforward = PID_OPENLOOP_OFFSET + temp * slope;
            if (feedforward > PID_MAX_PWM) feedforward = PID_MAX_PWM;
            if (feedforward < PID_MIN_PWM) feedforward = PID_MIN_PWM;

            // 闭环 PID 修正：根据 actual 与 target 的误差微调 PWM
            // 保留微分先行（derivative on measurement）与条件积分抗饱和
            struct PID_terms terms;
            double pid_correction = PID_Calculate(pid_params, &data, temp, actual_rpm, &terms);

            double new_input = feedforward + pid_correction;

            // Rate Limiter: 限制相邻周期 PWM 输出变化量，平滑跳变
            // 软启动阶段使用更小的变化上限，防止启动过冲
            // 正常运行时允许减速比加速更快，抑制高→低目标切换时的惯性过冲
            double delta = new_input - data.pre_output;
            double max_pos_delta = startup_phase ? PID_SOFTSTART_OUTPUT_DELTA : PID_MAX_OUTPUT_DELTA;
            if (delta > max_pos_delta) {
                new_input = data.pre_output + max_pos_delta;
            }
            else if (delta < -PID_MAX_BRAKING_DELTA) {
                new_input = data.pre_output - PID_MAX_BRAKING_DELTA;
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

            // 日志格式：actual 为 0.1 RPM 精度，并输出 PID 分项与前馈/修正量，便于调参
            ESP_LOGI(TAG, "Motor %d PID: target=%.0f RPM, actual=%.1f RPM (raw=%d/200ms), err=%.1f, P=%.1f, I=%.1f, D=%.1f, ff=%.1f, corr=%.1f, pid_out=%.0f, pwm_duty=%d, ss=%d",
                     index, temp, actual_rpm, pcnt_count_list[index],
                     terms.error, terms.Pout, terms.Iout, terms.Dout,
                     feedforward, pid_correction, new_input, new_input_int, startup_counter);

            // 更新上周期目标速度
            prev_target_speed[index] = temp;
            // 更新上周期输出（用于下一周期速率限制）
            data.pre_output = new_input;
        }
        else {
            // 电机停止：确保 PWM 关闭，并在刚从运行态切换时复位状态
            if (prev_target_speed[index] != 0) {
                data.pre_output = 0;
                data.integral = 0;
                data.pre_error = 0;
                data.pre_measurement = 0;
                ESP_LOGI(TAG, "Motor %d stopped, PID state reset", index);
            }
            pwm_set_duty(8191, index);
            prev_target_speed[index] = 0;
        }

        vTaskDelay(PID_PERIOD_MS / portTICK_PERIOD_MS);
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
