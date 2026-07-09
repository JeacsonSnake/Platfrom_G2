#include "main.h"

static const char* TAG = "PID_EVENT";

//////////////////////////////////////////////////////////////
//////////////////////// PID 可调参数 //////////////////////////
//////////////////////////////////////////////////////////////
// 以下参数集中在 pid.c 中定义，避免与 main.h 耦合，便于独立调试与快速回退
#define PID_KP                  (7.0)   // 比例增益：从 8.0 降至 5.0 后 target=5 无法启动，回调到 7.0 以提升低速启动扭矩
#define PID_KI                  (0.005) // 积分增益（按 5Hz 采样率比例从 0.02 缩减）
#define PID_KD                  (0.03)  // 微分增益（增强阻尼，抑制启动超调）
#define PID_MAX_PWM             (8191)  // 13-bit 最大值
#define PID_MIN_PWM             (0)     // 输出下限（0 对应反相后 duty=8191，即停止）
#define PID_OUTPUT_MIN_LIMIT    (0)     // PID 输出最小值限制，先保持 0；调研后若需限制最高速可调整
#define PID_MAX_OUTPUT_DELTA    (500.0) // 每 200ms 周期最大输出变化，平滑 PWM 跳变
#define PID_SOFTSTART_MAX_INIT  (3000.0)// 软启动初始最大允许输出
#define PID_SOFTSTART_STEPS     (10)    // 软启动步数（10 * 200ms = 2s）
#define PID_MAX_PCNT            (450)   // 最大 PCNT：4500 RPM / 60 * 6 pulses/rotation
#define PID_MIN_PCNT            (0)     // 最小 PCNT

// 这里的PID控制针对于以下过程
// -- 转速 --> PID 控制器 --> PWM 控制输入 --> PCNT 转速测量 -->
//          ^                                     |
//          |                                     |
//          ---------------------------------------
double PID_Calculate(struct PID_params params, struct PID_data *data, double target_speed, double current_speed)
{
    // 计算Error
    double error = target_speed - current_speed;

    // 比例项
    double Pout = params.Kp * error;

    // 微分项
    double derivative = (error - data->pre_error);
    double Dout = params.Kd * derivative;

    // 条件积分：预测当前误差加入后是否会加剧饱和
    // 若不加当前误差时输出已朝饱和方向超出，且误差方向与饱和方向相同，则暂停积分
    double predicted_output = Pout + params.Ki * data->integral + Dout + data->pre_input;
    bool saturate_high = (predicted_output > params.max_pwm && error > 0);
    bool saturate_low  = (predicted_output < params.min_pwm && error < 0);

    if (!saturate_high && !saturate_low) {
        data->integral += error;
        // 限制积分项，防止积分过大
        if (data->integral > params.max_pwm) {
            data->integral = params.max_pwm;
        }
        if (data->integral < params.min_pwm) {
            data->integral = params.min_pwm;
        }
    }

    double Iout = params.Ki * data->integral;

    // 计算整体输出
    double output = Pout + Iout + Dout;
    output = data->pre_input + output;

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

    // 保存本次误差与输出到历史
    data->pre_error = error;
    data->pre_input = output;

    return output;
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
        .integral   = 0,
        .pre_error  = 0,
        .pre_input  = 0
    };

    // CHB-BLDC2418 PID Parameters
    // Max PCNT = (4500 RPM / 60) * 6 pulses/rotation = 450 pulses/sec
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

    // Soft start variables
    int startup_phase = 1;  // 1 = in startup, 0 = normal operation
    int startup_counter = 0;
    // 跟踪上一周期目标速度，用于检测 0->非零 转换并触发软启动 reset
    static double prev_target_speed[4] = {0.0, 0.0, 0.0, 0.0};

    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            // Convert 200ms PCNT count to per-second rate for PID comparison
            // pcnt_count_list is per 200ms, multiply by 5 to get per-second
            double actual_speed_per_sec = pcnt_count_list[index] * 5;
            double new_input = PID_Calculate(pid_params, &data, temp, actual_speed_per_sec);

            // 目标为 0 时强制输出 0（电机停止），并清零 PID 历史状态，
            // 避免上一条高转速命令的 pre_input/integral 残留到下一条低转速命令。
            if (temp == 0) {
                new_input = 0;
                data.integral = 0;
                data.pre_error = 0;
                data.pre_input = 0;
            }

            // Rate Limiter: 限制相邻周期 PID 输出变化量，平滑 PWM 跳变
            double delta = new_input - data.pre_input;
            if (delta > PID_MAX_OUTPUT_DELTA) {
                new_input = data.pre_input + PID_MAX_OUTPUT_DELTA;
            }
            else if (delta < -PID_MAX_OUTPUT_DELTA) {
                new_input = data.pre_input - PID_MAX_OUTPUT_DELTA;
            }

            // Soft start: limit max output during first PID_SOFTSTART_STEPS samples
            if (startup_phase) {
                startup_counter++;
                if (startup_counter <= PID_SOFTSTART_STEPS) {
                    // Gradually increase max allowed output
                    double progress = startup_counter / (double)PID_SOFTSTART_STEPS;
                    double current_max = PID_SOFTSTART_MAX_INIT + (PID_MAX_PWM - PID_SOFTSTART_MAX_INIT) * progress;
                    if (new_input > current_max) {
                        new_input = current_max;
                    }
                } else {
                    startup_phase = 0;  // End startup phase
                }
            }

            // CHB-BLDC2418: Inverted PWM logic - High=OFF, Low=ON
            // Duty 8191 = Motor OFF, Duty 0 = Motor ON
            int new_input_int = PID_MAX_PWM - (int)new_input;

            // Additional safety clamp for PWM output
            if (new_input_int < 0) new_input_int = 0;
            if (new_input_int > PID_MAX_PWM) new_input_int = PID_MAX_PWM;

            pwm_set_duty(new_input_int, index);

            ESP_LOGI(TAG, "Motor %d PID: target=%.0f/s, actual=%.0f/s (raw=%d/200ms), pid_out=%.0f, pwm_duty=%d, startup=%d",
                     index, temp, actual_speed_per_sec, pcnt_count_list[index], new_input, new_input_int, startup_counter);
            pcnt_updated_list[index] = false;

            // Reset startup phase and PID state when motor starts after being stopped
            // startup_counter > PID_SOFTSTART_STEPS means we've completed a previous soft-start cycle
            if (temp > 0 && prev_target_speed[index] == 0) {
                // Motor is starting (prev was 0, now non-zero)
                if (!startup_phase || startup_counter > PID_SOFTSTART_STEPS) {
                    // Either not in startup, or counter shows we've done a full cycle
                    startup_phase = 1;
                    startup_counter = 0;
                    // 关键：清零 PID 内部状态，避免上一条高转速命令的 pre_input/integral 残留
                    // 导致下一条低转速命令启动瞬间输出过高
                    data.integral = 0;
                    data.pre_error = 0;
                    data.pre_input = 0;
                    ESP_LOGI(TAG, "Motor %d soft-start reset (target: %.0f -> %.0f, phase=%d)",
                             index, prev_target_speed[index], temp, startup_phase);
                }
            }
            prev_target_speed[index] = temp;
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
