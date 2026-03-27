#include "main.h"

static const char* TAG = "PID_EVENT";

// 微分滤波系数（降低高频噪声影响）
#define D_FILTER_ALPHA 0.8
// 死区阈值（避免低速抖动）
#define SPEED_DEAD_ZONE 5.0
// PID采样周期（与PCNT采样周期保持一致）
#define PID_SAMPLE_TIME_SEC 1.0
// 输出变化率限制（减少突变）
#define PWM_MAX_STEP 600.0
// 积分项限制（防止积分饱和）
#define INTEGRAL_LIMIT 5000.0

// 这里的PID控制针对于以下过程
// -- 转速 --> PID 控制器 --> PWM 控制输入 --> PCNT 转速测量 -->
//          ^                                     |
//          |                                     |
//          ---------------------------------------
double PID_Calculate(struct PID_params params, struct PID_data *data, double target_speed, double current_speed)
{
    // 目标速度限制到可测量范围内
    if (target_speed > params.max_pcnt) {
        target_speed = params.max_pcnt;
    } else if (target_speed < params.min_pcnt) {
        target_speed = params.min_pcnt;
    }

    // 低速死区：认为目标为停止，清空积分避免抖动
    if (target_speed > -SPEED_DEAD_ZONE && target_speed < SPEED_DEAD_ZONE) {
        data->integral = 0;
        data->pre_error = 0;
        data->pre_measurement = current_speed;
        data->d_filtered = 0;
        data->pre_input = params.min_pwm;
        return params.min_pwm;
    }

    // 计算Error
    double error = target_speed - current_speed;
    double dt = PID_SAMPLE_TIME_SEC;

    // 比例项
    double Pout = params.Kp * error;

    // 微分项：对测量值做微分并进行低通滤波，避免设定值突变引发D项冲击
    double raw_derivative = -(current_speed - data->pre_measurement) / dt;
    data->d_filtered = D_FILTER_ALPHA * data->d_filtered + (1.0 - D_FILTER_ALPHA) * raw_derivative;
    double Dout = params.Kd * data->d_filtered;

    // 候选积分项（带dt）
    double integral_candidate = data->integral + error * dt;
    if (integral_candidate > INTEGRAL_LIMIT) {
        integral_candidate = INTEGRAL_LIMIT;
    } else if (integral_candidate < -INTEGRAL_LIMIT) {
        integral_candidate = -INTEGRAL_LIMIT;
    }

    double Iout_candidate = params.Ki * integral_candidate;
    double unsat_output = Pout + Iout_candidate + Dout;

    // 限制输出
    double output = unsat_output;
    if(output > params.max_pwm)
    {
        output = params.max_pwm;
    }
    else if(output < params.min_pwm)
    {
        output = params.min_pwm;
    }

    // 反积分饱和：仅在输出未饱和，或误差有助于脱离饱和时更新积分
    bool saturated_high = (unsat_output > params.max_pwm);
    bool saturated_low = (unsat_output < params.min_pwm);
    bool allow_integral_update = (!saturated_high && !saturated_low) ||
                                 (saturated_high && error < 0) ||
                                 (saturated_low && error > 0);
    if (allow_integral_update) {
        data->integral = integral_candidate;
    }

    // 输出变化率限制，减少突变和机械冲击
    double delta = output - data->pre_input;
    if (delta > PWM_MAX_STEP) {
        output = data->pre_input + PWM_MAX_STEP;
    } else if (delta < -PWM_MAX_STEP) {
        output = data->pre_input - PWM_MAX_STEP;
    }

    // 再次限制条件
    if (output > params.max_pwm) {
        output = params.max_pwm;
    } else if (output < params.min_pwm) {
        output = params.min_pwm;
    }

    // 保存本次误差到上次
    data->pre_error = error;
    data->pre_measurement = current_speed;
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
        .pre_input  = 0,
        .pre_measurement = 0,
        .d_filtered = 0
    };

    struct PID_params pid_params = {
        .Kp         = 8,
        .Ki         = 0.02,
        .Kd         = 0.06,
        .max_pwm    = 8192,
        .min_pwm    = 0,
        .max_pcnt   = 435,
        .min_pcnt   = -435
    };

    while(1){
        if(pcnt_updated_list[index] == true)
        {
            double temp = motor_speed_list[index];
            double new_input = PID_Calculate(pid_params, &data, temp, pcnt_count_list[index]);
            int new_input_int = 8192 - (int)new_input;
            pwm_set_duty(new_input_int, index);
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
    int local_speed = local_params -> speed;
    int local_duration = local_params -> duration;
    int local_index = local_params -> index;

    char buff[64];
    sprintf(buff, "task_create_%d_%d_%d",local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    motor_speed_list[local_index] = local_speed;
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    pwm_set_duty(8192, local_index);
    sprintf(buff, "task_finished_%d_%d_%d",local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    vTaskDelete(NULL);
}



