#include "main.h"

static const char* TAG = "PCNT_EVENT";

// 系统启动时间戳，用于启动保护期
static uint32_t system_boot_time = 0;

// PCNT 运行统计，用于诊断
static uint32_t pcnt_zero_count[4] = {0, 0, 0, 0};
static uint32_t pcnt_total_samples[4] = {0, 0, 0, 0};

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
        
        // 获取当前数字，并清除
        pcnt_unit_get_count(unit, &pcnt_count_list[index]);
        pcnt_unit_clear_count(unit);
        
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
        if (abnormal_check_enabled) {
            if (pcnt_count_list[index] > MAX_REASONABLE_PCNT_PER_200MS || pcnt_count_list[index] < 0) {
                ESP_LOGW(TAG, "Motor %d PCNT abnormal value detected: %d, resetting to 0", 
                         index, pcnt_count_list[index]);
                pcnt_count_list[index] = 0;
            }
        }
        
        // 空闲状态噪声过滤：电机停止时，如果PCNT异常大，视为噪声
        if (motor_speed_list[index] == 0 && idle == false && pcnt_count_list[index] > IDLE_NOISE_THRESHOLD) {
            ESP_LOGW(TAG, "Motor %d idle noise detected: PCNT=%d, filtering", index, pcnt_count_list[index]);
            pcnt_count_list[index] = 0;
        }

        // 判断是否有转动指令，是否空闲，空闲时不进行测量更新
        if(motor_speed_list[index] == 0 && idle == false)
        {
            // 如果空闲，发送PCNT转速信息并停止
            char buff[64];
            sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
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
            // MQTT发布每秒值（0-450范围），与1秒采样时格式一致
            sprintf(buff, "pcnt_count_%d_%d", index, actual_per_sec);
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
            
            ESP_LOGI(TAG, "Motor %d running, PCNT=%d/s (raw=%d/200ms), target=%d/s", 
                     index, actual_per_sec, pcnt_count_list[index], target_per_sec);
            idle = false;
            pcnt_updated_list[index] = true;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

// P9接口（Motor 3）专项诊断任务
// 用于测试PH2.0-LI-5P_004接口的FG信号线（GPIO 9）是否正常
void p9_interface_diagnostic_task(void* params)
{
    int index = 3; // Motor 3 / P9接口
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "P9接口（Motor 3）专项诊断启动");
    ESP_LOGI(TAG, "GPIO: PWM=GPIO%d, FG=GPIO%d", pwm_gpios[index], pcnt_gpios[index]);
    ESP_LOGI(TAG, "物理接口: PH2.0-LI-5P_004");
    ESP_LOGI(TAG, "========================================");
    
    // 配置GPIO 9为输入模式，用于直接读取电平
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pcnt_gpios[index]),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // 检测GPIO 9电平变化
    int level_change_count = 0;
    int last_level = gpio_get_level(pcnt_gpios[index]);
    int stable_count = 0;
    
    ESP_LOGI(TAG, "P9诊断: GPIO %d 初始电平=%d", pcnt_gpios[index], last_level);
    
    // 持续监测5秒
    for (int i = 0; i < 50; i++) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        int current_level = gpio_get_level(pcnt_gpios[index]);
        
        if (current_level != last_level) {
            level_change_count++;
            ESP_LOGI(TAG, "P9诊断: GPIO %d 电平变化 %d->%d (变化次数:%d)", 
                     pcnt_gpios[index], last_level, current_level, level_change_count);
            last_level = current_level;
            stable_count = 0;
        } else {
            stable_count++;
        }
    }
    
    // 输出诊断结果
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "P9接口诊断结果:");
    ESP_LOGI(TAG, "  - GPIO %d 电平变化次数: %d", pcnt_gpios[index], level_change_count);
    if (level_change_count == 0) {
        ESP_LOGW(TAG, "  ⚠️ 警告: GPIO %d 无信号变化，可能原因:", pcnt_gpios[index]);
        ESP_LOGW(TAG, "     1. P9接口未连接电机");
        ESP_LOGW(TAG, "     2. FG信号线（IO9）断开或接触不良");
        ESP_LOGW(TAG, "     3. 电机未上电（12V未接通）");
        ESP_LOGW(TAG, "     4. 编码器硬件故障");
    } else if (level_change_count < 10) {
        ESP_LOGW(TAG, "  ⚠️ 信号较弱，可能接触不良");
    } else {
        ESP_LOGI(TAG, "  ✅ GPIO %d 信号正常", pcnt_gpios[index]);
    }
    ESP_LOGI(TAG, "========================================");
    
    vTaskDelete(NULL);
}

// PCNT 监测线程初始化
void pcnt_monitor_init()
{
    // 创建P9接口专项诊断任务（仅运行一次，自动删除）
    xTaskCreate(p9_interface_diagnostic_task, "P9_DIAG_TASK", 4096, NULL, 2, NULL);
    
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