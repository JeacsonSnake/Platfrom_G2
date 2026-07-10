#include "main.h"

static const char* TAG = "PID_EVENT";

//////////////////////////////////////////////////////////////
//////////////////////// PID 可调参数 //////////////////////////
//////////////////////////////////////////////////////////////
// 以下参数集中在 pid.c 中定义，避免与 main.h 耦合，便于独立调试与快速回退。
// 注意：当前 Kp/Ki/Kd 在开环测试期间暂不使用；4500 RPM 量程下建议重新整定。
#define PID_KP                  (50.0)  // 比例增益（待整定；2026-07-10 基于实测数据上调）
#define PID_KI                  (0.50)  // 积分增益（待整定；2026-07-10 基于实测数据大幅上调）
#define PID_KD                  (0.30)  // 微分增益（待整定；2026-07-10 基于 200ms 采样周期上调）
#define PID_MAX_PWM             (8191)  // 13-bit 最大值
#define PID_MIN_PWM             (0)     // 输出下限（0 对应反相后 duty=8191，即停止）
#define PID_OUTPUT_MIN_LIMIT    (0)     // PID 输出最小值限制，先保持 0；调研后若需限制最高速可调整
#define PID_MAX_OUTPUT_DELTA    (500.0) // 正常运行每 200ms 最大输出增加量（加速限制）
#define PID_MAX_BRAKING_DELTA   (900.0) // 正常运行每 200ms 最大输出减少量（减速/制动限制，允许更快刹车）
#define PID_SOFTSTART_OUTPUT_DELTA (300.0) // 软启动阶段每 200ms 最大输出增加量
#define PID_SOFTSTART_STEPS     (10)    // 软启动步数（10 * 200ms = 2s）
#define PID_MAX_PCNT            (4500)  // 最大转速：12V 供电下实际空载约 4500 RPM
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

    // CHB-BLDC2418 转速参数（开环测试期间暂不使用 PID 参数）
    // 电机参数表可能标注 24V/9000 RPM，但当前 12V 供电下实际空载转速约 4500 RPM。
    // 转速单位：RPM（cmd_2_800_10 表示 800 RPM）
    // Tuned for 200ms sampling interval (5Hz)
    // struct PID_params pid_params = {
    //     .Kp         = PID_KP,
    //     .Ki         = PID_KI,
    //     .Kd         = PID_KD,
    //     .max_pwm    = PID_MAX_PWM,
    //     .min_pwm    = PID_MIN_PWM,
    //     .max_pcnt   = PID_MAX_PCNT,
    //     .min_pcnt   = PID_MIN_PCNT
    // };

    // 软启动状态
    bool startup_phase = true;  // true = 处于软启动阶段
    int startup_counter = 0;
    // 跟踪上一周期目标速度，用于检测 0->非零 转换并触发状态清零与软启动
    static double prev_target_speed[4] = {0.0, 0.0, 0.0, 0.0};

    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            // Convert 200ms PCNT raw count to RPM
            // 6 PPR => RPM = pulses/sec * 60/6 = pulses/sec * 10
            // pcnt_count_list is raw pulses per 200ms; RPM = pulses/200ms * 5 * 10 = pulses/200ms * 50
            double actual_rpm = pcnt_count_list[index] * 50.0;

            // 启动边沿检测：从停止转为运行时重新启用软启动（速率限制）
            if (temp > 0 && prev_target_speed[index] == 0) {
                startup_phase = true;
                startup_counter = 0;
                data.pre_output = 0;
                ESP_LOGI(TAG, "Motor %d open-loop soft-start reset (target: 0 -> %.0f)", index, temp);
            }

            // ========== 开环控制 + 死区补偿 ==========
            //  电机驱动板内部疑似已有速度闭环，ESP32 侧采用开环映射即可。
            //  实测发现电机在输出 < ~300 时不转动（静摩擦死区），
            //  因此当 target > 0 时加入一个最小输出偏移量，低速区目标才能真实对应转速。
            //  target=0 -> output=0 (duty=8191, OFF)
            //  target=PID_MAX_PCNT -> output=PID_MAX_PWM (duty=0, ON)
            #define PID_OPENLOOP_OFFSET     (300.0)  // 死区补偿偏移量（待整定）
            double new_input = 0.0;
            if (temp > 0) {
                double slope = (PID_MAX_PWM - PID_OPENLOOP_OFFSET) / (double)PID_MAX_PCNT;
                new_input = PID_OPENLOOP_OFFSET + temp * slope;
                if (new_input > PID_MAX_PWM) {
                    new_input = PID_MAX_PWM;
                }
                if (new_input < PID_MIN_PWM) {
                    new_input = PID_MIN_PWM;
                }
            }

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

            // 保持与 analyze_motor_log.py 兼容的日志格式
            ESP_LOGI(TAG, "Motor %d PID: target=%.0f RPM, actual=%.0f RPM (raw=%d/200ms), pid_out=%.0f, pwm_duty=%d, ss=%d",
                     index, temp, actual_rpm, pcnt_count_list[index], new_input, new_input_int, startup_counter);
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
