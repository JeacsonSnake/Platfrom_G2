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
        // 检查积分是否推动了饱和
        double P_D_out = Pout + Dout;
        double output_without_I = data->pre_input + P_D_out;
        
        // 如果去掉积分后不会饱和，说明是积分导致的饱和
        if ((output_unlimited > params.max_pwm && error > 0) ||
            (output_unlimited < params.min_pwm && error < 0)) {
            // 回滚积分累积
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
        .Kp         = 5,        // Reduced for smoother response
        .Ki         = 0.002,    // Further reduced to prevent integral windup
        .Kd         = 0.05,     // Increased for better damping
        .max_pwm    = 6000,     // Limit max output to prevent motor running at absolute max
        .min_pwm    = 1500,     // Minimum PWM to ensure controllable speed range
        .max_pcnt   = 90,       // 450 * 0.2s = 90 pulses per 200ms interval
        .min_pcnt   = 0
    };

    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            double new_input = PID_Calculate(pid_params, &data, temp, pcnt_count_list[index]);
            // CHB-BLDC2418: Inverted PWM logic - High=OFF, Low=ON
            // Duty 8191 = Motor OFF, Duty 0 = Motor ON
            // PID output range: min_pwm(1500) to max_pwm(6000)
            // Inverted range: 8191-6000=2191 to 8191-1500=6691
            int new_input_int = 8191 - (int)new_input;
            
            // Additional safety clamp for PWM output
            if (new_input_int < 0) new_input_int = 0;
            if (new_input_int > 8191) new_input_int = 8191;
            
            pwm_set_duty(new_input_int, index);
            
            ESP_LOGI(TAG, "Motor %d PID: target=%.0f, actual=%d, pid_out=%.0f, pwm_duty=%d",
                     index, temp, pcnt_count_list[index], new_input, new_input_int);
            pcnt_updated_list[index] = false;
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



