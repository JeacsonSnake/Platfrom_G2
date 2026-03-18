#include "main.h"

static const char* TAG = "PID_EVENT";

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
    
    // 积分项 - 带抗饱和处理
    // 条件积分：当输出饱和且误差与输出同方向时，暂停积分
    double pre_integral = data->integral;
    data->integral += error;
    
    // 限制积分项，防止积分饱和
    if(data->integral > params.max_pwm)
    {
        data->integral = params.max_pwm;
    }
    if(data->integral < params.min_pwm)
    {
        data->integral = params.min_pwm;
    }
    
    double Iout = params.Ki * data->integral;

    // 微分项
    double derivative = (error - data->pre_error);
    double Dout = params.Kd * derivative;

    // 计算整体输出
    double output = Pout + Iout + Dout;
    output = data->pre_input + output;

    // 输出限制前的值（用于抗饱和判断）
    double output_unlimited = output;

    // 限制条件
    bool is_saturated = false;
    if(output > params.max_pwm)
    {
        output = params.max_pwm;
        is_saturated = true;
    }
    else if(output < params.min_pwm)
    {
        output = params.min_pwm;
        is_saturated = true;
    }

    // 抗饱和处理：如果输出饱和且积分推动了饱和，则回滚积分
    if (is_saturated) {
        // 如果当前误差与饱和方向相同，说明积分推动了饱和
        // 回滚积分累积以防止积分 windup
        if ((output_unlimited > params.max_pwm && error > 0) ||
            (output_unlimited < params.min_pwm && error < 0)) {
            data->integral = pre_integral;
        }
    }

    // 保存本次误差到上次
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
        .pre_input = 0
    };

    // CHB-BLDC2418 PID Parameters
    // Max PCNT = (4500 RPM / 60) * 6 pulses/rotation = 450 pulses/sec
    // Tuned for 200ms sampling interval (5Hz)
    struct PID_params pid_params = {
        .Kp         = 8,        // Proportional gain
        .Ki         = 0.02,     // Integral gain (scaled for 5Hz)
        .Kd         = 0.01,     // Derivative gain
        .max_pwm    = 8191,     // 13-bit max (full range)
        .min_pwm    = 0,        // Allow full stop
        .max_pcnt   = 450,      // 450 pulses/sec max
        .min_pcnt   = 0
    };

    // Soft start variables
    int startup_phase = 1;  // 1 = in startup, 0 = normal operation
    int startup_counter = 0;
    double max_pwm_during_startup = 3000;  // Limit initial PWM to prevent overshoot
    
    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            // Convert 200ms PCNT count to per-second rate for PID comparison
            // pcnt_count_list is per 200ms, multiply by 5 to get per-second
            double actual_speed_per_sec = pcnt_count_list[index] * 5;
            double new_input = PID_Calculate(pid_params, &data, temp, actual_speed_per_sec);
            
            // Soft start: limit max output during first 10 samples (2 seconds)
            if (startup_phase) {
                startup_counter++;
                if (startup_counter <= 10) {
                    // Gradually increase max allowed output
                    double progress = startup_counter / 10.0;
                    double current_max = max_pwm_during_startup + (8191 - max_pwm_during_startup) * progress;
                    if (new_input > current_max) {
                        new_input = current_max;
                    }
                } else {
                    startup_phase = 0;  // End startup phase
                }
            }
            
            // CHB-BLDC2418: Inverted PWM logic - High=OFF, Low=ON
            // Duty 8191 = Motor OFF, Duty 0 = Motor ON
            int new_input_int = 8191 - (int)new_input;
            
            // Additional safety clamp for PWM output
            if (new_input_int < 0) new_input_int = 0;
            if (new_input_int > 8191) new_input_int = 8191;
            
            pwm_set_duty(new_input_int, index);
            
            ESP_LOGI(TAG, "Motor %d PID: target=%.0f/s, actual=%.0f/s (raw=%d/200ms), pid_out=%.0f, pwm_duty=%d, startup=%d",
                     index, temp, actual_speed_per_sec, pcnt_count_list[index], new_input, new_input_int, startup_counter);
            pcnt_updated_list[index] = false;
            
            // Reset startup phase when motor starts (transition from 0 to non-zero)
            // Track previous target speed to detect startup transition
            static double prev_target_speed[4] = {0, 0, 0, 0};
            if (temp > 0 && prev_target_speed[index] == 0 && !startup_phase) {
                startup_phase = 1;
                startup_counter = 0;
                ESP_LOGI(TAG, "Motor %d soft-start reset detected (target: %.0f -> %.0f)", 
                         index, prev_target_speed[index], temp);
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

    char buff[64];
    sprintf(buff, "task_create_%d_%d_%d", local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    motor_speed_list[local_index] = local_speed;
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    // CHB-BLDC2418: Duty 8191 = Motor OFF (inverted logic)
    pwm_set_duty(8191, local_index);
    sprintf(buff, "task_finished_%d_%d_%d", local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    vTaskDelete(NULL);
}



