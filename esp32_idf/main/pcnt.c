#include "main.h"

static const char* TAG = "PCNT_EVENT";

// PCNT 初始化
// 注意貌似pcnt_init()这个函数名已经被内部函数占用了，如果命名为pcnt_init()会奇妙的报错
void pcnt_func_init()
{
    for(int i = 0; i <4; i++)
    {
        // Configure GPIO with pull-up to prevent floating input noise
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << pcnt_gpios[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,  // Enable internal pull-up
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        
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
        // Only count on rising edge (one pulse per rotation cycle)
        pcnt_channel_set_edge_action(pcnt_chan_handle, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_unit_enable(pcnt_unit_list[i]);
        pcnt_unit_clear_count(pcnt_unit_list[i]);
        pcnt_unit_start(pcnt_unit_list[i]);
        ESP_LOGI(TAG, "PCNT channel %d has been initiated on pin %d with pull-up.", i, pcnt_gpios[i]);
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
    // Max theoretical PCNT per 200ms: 450 pulses/sec * 0.2s = 90
    // Allow some margin: 150 per 200ms (750/s) is max reasonable
    const int MAX_REASONABLE_PCNT_PER_200MS = 150;
    
    while(1)
    {
        // 获取当前数字，并清除
        pcnt_unit_get_count(unit, &pcnt_count_list[index]);
        pcnt_unit_clear_count(unit);
        
        // Filter abnormal values (noise or overflow)
        if (pcnt_count_list[index] > MAX_REASONABLE_PCNT_PER_200MS || pcnt_count_list[index] < 0) {
            ESP_LOGW(TAG, "Motor %d PCNT abnormal value detected: %d, resetting to 0", 
                     index, pcnt_count_list[index]);
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
        }
        else if(motor_speed_list[index] != 0)
        {
            // 如果不空闲则开始测量
            char buff[64];
            sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
            int actual_per_sec = pcnt_count_list[index] * 5;
            int target_per_sec = (int)motor_speed_list[index];
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