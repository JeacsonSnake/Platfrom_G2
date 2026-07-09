#include "main.h"

static const char* TAG = "PCNT_EVENT";

// 系统启动时间戳，用于启动保护期
static uint32_t system_boot_time = 0;

// PCNT 运行统计，用于诊断
static uint32_t pcnt_zero_count[4] = {0, 0, 0, 0};
static uint32_t pcnt_total_samples[4] = {0, 0, 0, 0};

// PCNT 中值滤波器状态（用于抑制启动/运行时的脉冲计数噪声，不影响遥测发布频率）
#define PCNT_FILTER_WINDOW 3
static int pcnt_filter_buf[4][PCNT_FILTER_WINDOW] = {{0}};
static int pcnt_filter_idx[4] = {0};
static bool pcnt_filter_ready[4] = {false};

// PCNT 初始化
// 注意貌似pcnt_init()这个函数名已经被内部函数占用了，如果命名为pcnt_init()会奇妙的报错
void pcnt_func_init()
{
    // 记录系统启动时间
    system_boot_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    for(int i = 0; i <4; i++)
    {
        pcnt_unit_config_t unit_config = {
            .high_limit = 10000,
            .low_limit = -10000,
        };
        pcnt_new_unit(&unit_config, &pcnt_unit_list[i]);

        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = pcnt_gpios[i],
            .level_gpio_num = -1,
        };
        pcnt_channel_handle_t pcnt_chan_handle = NULL;
        pcnt_new_channel(pcnt_unit_list[i], &chan_config, &pcnt_chan_handle);
        pcnt_channel_set_edge_action(pcnt_chan_handle, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
        pcnt_unit_enable(pcnt_unit_list[i]);
        pcnt_unit_clear_count(pcnt_unit_list[i]);
        pcnt_unit_start(pcnt_unit_list[i]);
        ESP_LOGI(TAG, "PCNT channel %d has been initiated on pin %d.", i, pcnt_gpios[i]);
    }
}

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

// PCNT的计数器线程
void pcnt_monitor(void* params)
{
    // 获取当前计数器对应的PCNT index
    int index = *((int *) params);
    free(params);
    pcnt_unit_handle_t unit = pcnt_unit_list[index];
    
    // 空闲控制
    bool idle = false;
    // 异常值检测标志
    bool abnormal_check_enabled = false;
    // 启动保护标志：系统启动后的前几秒启用特殊保护
    bool startup_protection_active = true;
    // 记录上一周期电机是否运行，用于检测启动边沿并重置滤波器
    bool was_running = false;
    // Max theoretical PCNT per 200ms: 450 pulses/sec * 0.2s = 90
    // Allow some margin: 150 per 200ms (750/s) is max reasonable
    const int MAX_REASONABLE_PCNT_PER_200MS = 150;
    // 启动保护期：3秒（等待12V电源稳定）
    const uint32_t STARTUP_PROTECTION_MS = 3000;
    // 空闲时的噪声阈值：电机停止时，PCNT超过此值视为噪声
    const int IDLE_NOISE_THRESHOLD = 50;
    
    while(1)
    {
        // 检查启动保护期是否结束
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (startup_protection_active && (current_time - system_boot_time > STARTUP_PROTECTION_MS)) {
            startup_protection_active = false;
            ESP_LOGI(TAG, "Motor %d startup protection ended, normal PCNT monitoring active", index);
        }
        
        // 检测电机启动边沿：从停止转为运行时重置中值滤波器，避免旧噪声影响启动
        if (motor_speed_list[index] != 0 && !was_running) {
            pcnt_reset_filter(index);
            was_running = true;
        } else if (motor_speed_list[index] == 0) {
            was_running = false;
        }
        
        // 获取当前数字，并清除
        pcnt_unit_get_count(unit, &pcnt_count_list[index]);
        pcnt_unit_clear_count(unit);
        
        // 对原始值做中值滤波，抑制单点脉冲噪声（不影响遥测发布频率）
        int median_raw = pcnt_update_median(index, pcnt_count_list[index]);
        
        // 统计PCNT采样数据（用于诊断Motor 3问题）
        pcnt_total_samples[index]++;
        if (pcnt_count_list[index] == 0) {
            pcnt_zero_count[index]++;
        }
        
        // 每50个样本（约10秒）输出一次诊断信息
        if (pcnt_total_samples[index] % 50 == 0 && motor_speed_list[index] != 0) {
            uint32_t zero_rate = (pcnt_zero_count[index] * 100) / pcnt_total_samples[index];
            if (zero_rate > 80) {
                ESP_LOGW(TAG, "Motor %d PCNT诊断: %d%%采样为0，可能存在硬件连接问题", 
                         index, zero_rate);
            }
        }
        
        // 启动保护期特殊处理：强制清零异常值
        if (startup_protection_active && motor_speed_list[index] == 0) {
            // 启动保护期内且电机未运行，清零所有PCNT计数（噪声过滤）
            if (pcnt_count_list[index] > 0) {
                ESP_LOGD(TAG, "Motor %d startup protection: filtering PCNT noise %d", 
                         index, pcnt_count_list[index]);
                pcnt_count_list[index] = 0;
            }
        }
        
        // 异常值检测：电机运行时启用
        // 检测到超限或突刺时，用中值替换而不是直接清零，避免 PID 误判为停转
        if (abnormal_check_enabled) {
            bool is_abnormal = (pcnt_count_list[index] > MAX_REASONABLE_PCNT_PER_200MS || 
                                pcnt_count_list[index] < 0);
            // 额外检测相对突刺：当前值显著大于最近中值（启动期前几个点不启用）
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
        
        // 空闲状态噪声过滤：电机停止时，如果PCNT异常大，视为噪声
        if (motor_speed_list[index] == 0 && idle == false && pcnt_count_list[index] > IDLE_NOISE_THRESHOLD) {
            ESP_LOGW(TAG, "Motor %d idle noise detected: PCNT=%d, filtering", index, pcnt_count_list[index]);
            pcnt_count_list[index] = 0;
            // 重置滤波器，避免噪声被中值滤波保留
            pcnt_reset_filter(index);
        }

        // 判断是否有转动指令，是否空闲，空闲时不进行测量更新
        if(motor_speed_list[index] == 0 && idle == false)
        {
            // 如果空闲，发送PCNT转速信息并停止（QoS 0，非阻塞）
            char buff[64];
            sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
            mqtt_publish_safe(mqtt_telemetry_topic, buff, strlen(buff), 0, 0);
            ESP_LOGI(TAG, "Motor %d idle, PCNT=%d", index, pcnt_count_list[index]);
            // CHB-BLDC2418: Duty 8191 = Motor OFF (inverted logic)
            pwm_set_duty(8191, index);
            pcnt_updated_list[index] = false;
            if(pcnt_count_list[index] == 0){
                idle = true;
            }
            // 电机停止后，禁用异常值检测（下次启动前可能有噪声）
            abnormal_check_enabled = false;
        }
        else if(motor_speed_list[index] != 0)
        {
            // 电机启动后，启用异常值检测
            abnormal_check_enabled = true;
            
            // 如果不空闲则开始测量
            // 将200ms原始值转换为每秒值，保持与旧版1秒采样相同的数值范围
            int actual_per_sec = pcnt_count_list[index] * 5;
            int target_per_sec = (int)motor_speed_list[index];
            
            char buff[64];
            // MQTT发布每秒值（0-450范围），与1秒采样时格式一致（QoS 0，非阻塞）
            sprintf(buff, "pcnt_count_%d_%d", index, actual_per_sec);
            mqtt_publish_safe(mqtt_telemetry_topic, buff, strlen(buff), 0, 0);
            
            ESP_LOGI(TAG, "Motor %d running, PCNT=%d/s (raw=%d/200ms), target=%d/s", 
                     index, actual_per_sec, pcnt_count_list[index], target_per_sec);
            idle = false;
            pcnt_updated_list[index] = true;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

// PCNT 监测线程初始化
void pcnt_monitor_init()
{
    // 初始化4个PCNT监测线程
    for(int i = 0; i < 4; i++)
    {
        int* j = (int*)malloc(sizeof(int));
        if(j != NULL)
        {
            *j = i;
            if(xTaskCreate(pcnt_monitor, "PCNT_TASK", 4096, (void*) j, 1, NULL) != pdPASS)
            {
                ESP_LOGI(TAG, "PCNT monitor process %d creat failed.", *j);
                free(j);
            }
        }
    }
}