#include "main.h"
#include "heating_detect.h"

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

    // 初始化MAX31850温度传感器（GPIO14）
    ESP_LOGI("MAIN", "Initializing MAX31850 temperature sensors...");
    esp_err_t temp_err = max31850_init(MAX31850_ONEWIRE_GPIO);
    if (temp_err != ESP_OK) {
        ESP_LOGW("MAIN", "MAX31850 init returned %d, will retry in background", temp_err);
    }
    
    // 创建温度打印任务（每2秒打印4个通道温度及状态）
    xTaskCreate(heating_print_task, "HEATING_PRINT", 4096, NULL, 1, NULL);

    // 防止主线程结束
    while(1)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 温度打印任务 - 每2秒打印4个通道温度及状态
 * 
 * PCB mapping based on schematic:
 * - Sensor 0: U1 (P1) - AD0=GND,  AD1=GND  -> HW_ADDR 00
 * - Sensor 1: U2 (P2) - AD0=3.3V, AD1=GND  -> HW_ADDR 01
 * - Sensor 2: U3 (P3) - AD0=GND,  AD1=3.3V -> HW_ADDR 10
 * - Sensor 3: U4 (P4) - AD0=3.3V, AD1=3.3V -> HW_ADDR 11
 */
void heating_print_task(void *pvParameters)
{
    float temp;
    max31850_err_t err;
    // PCB label mapping: U1(P1), U2(P2), U3(P3), U4(P4)
    const char* pcb_label[] = {"U1(P1)", "U2(P2)", "U3(P3)", "U4(P4)"};
    
    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI("HEATING", "Temperature print task started (GPIO14, 4.7K pull-up)");
    
    while (1) {
        ESP_LOGI("HEATING", "========== Temperature Report ==========");
        
        for (uint8_t i = 0; i < MAX31850_SENSOR_COUNT; i++) {
            err = max31850_get_temperature(i, &temp);
            
            if (err == MAX31850_OK) {
                ESP_LOGI("HEATING", "[%s]: %.2f°C  [OK]", pcb_label[i], temp);
            } else {
                const char *err_str = max31850_err_to_string(err);
                bool online = max31850_is_online(i);
                ESP_LOGW("HEATING", "[%s]: %s  [%s]",
                         pcb_label[i], err_str, online ? "ONLINE" : "OFFLINE");
            }
        }
        
        ESP_LOGI("HEATING", "=======================================");
        
        // 每2秒打印一次
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
