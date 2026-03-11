#include "main.h"

//////////////////////////////////////////////////////////////
//////////////////////// Data Init ///////////////////////////
//////////////////////////////////////////////////////////////
// MQTT 客户端初始化
esp_mqtt_client_handle_t mqtt_client = MQTT_CLIENT_INIT;

// Motor 控制数组初始化
double motor_speed_list[4] = MOTOR_SPEED;

// PWM 参数组初始化 (CHB-BLDC2418: IO1, IO4, IO6, IO8)
// PWM GPIO 信道初始化
const int pwm_gpios[4] = LEDC_GPIO_LIST;
// PWM 频道初始化
const int pwm_channels[4] = LEDC_CHANNEL_LIST;

// PCNT参数组初始化
// PCNT GPIO 信道初始化
const int pcnt_gpios[4] = PCNT_GPIO;
// PCNT 单元回调函数初始化
pcnt_unit_handle_t pcnt_unit_list[4] = PCNT_UNIT;
// PCNT 计数数组初始化
int pcnt_count_list[4] = PCNT_COUNT;
// PCNT 更新数组初始化
bool pcnt_updated_list[4] = PCNT_UPDATE;

// 主函数
void app_main(void){
    // 创建LED状态指示任务（优先级2，低于WiFi初始化，避免影响WiFi连接）
    xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 2, NULL);
    
    // 初始化WiFi，并等待WiFi连接
    // 注意：监控任务需要在WiFi连接后创建，确保NTP同步能正常进行
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    // 创建MQTT连接监控任务（在WiFi连接后创建，确保NTP同步能正常进行）
    xTaskCreate(monitor_task, "MONITOR_TASK", 4096, NULL, 3, NULL);
    // 创建MQTT初始化任务（初始化完成后会自删除）
    xTaskCreate(mqtt_init, "MQTT_INIT", 4096, NULL, 2, NULL);
    // 创建MQTT心跳发送任务
    xTaskCreate(mqtt_heartbeat_task, "MQTT_HB", 4096, NULL, 1, NULL);
    // 创建MQTT连接健康检查任务
    xTaskCreate(mqtt_health_check_task, "MQTT_CHK", 4096, NULL, 1, NULL);
    // 创建MQTT错误统计报告任务（栈大小4096防止栈溢出）
    xTaskCreate(mqtt_error_report_task, "MQTT_ERR", 4096, NULL, 1, NULL);
    // 初始化PWM
    pwm_init();
    // 初始化PCNT
    pcnt_func_init();
    // 创建PCNT计数线程
    pcnt_monitor_init();
    // 初始化pid线程
    pid_process_init();

    // 防止主线程结束
    while(1)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
