#include "main.h"

//////////////////////////////////////////////////////////////
//////////////////////// LOGIC ANALYZER INIT /////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化逻辑分析仪用于1-Wire协议调试
 * 
 * 使用SUMP协议通过USB-Serial与PulseView通信
 * 采样GPIO14（1-Wire总线）的波形用于时序分析
 * 
 * 配置说明：
 * - 通过 menuconfig 配置逻辑分析仪参数
 * - Component config -> Logic Analyzer
 * - 默认采样 GPIO14（在 menuconfig 中配置通道0为GPIO14）
 * - 使用 SUMP 协议与 PulseView 通信
 */
void logic_analyzer_init(void)
{
#if LOGIC_ANALYZER_ENABLED
    ESP_LOGI("LOGIC_ANALYZER", "========================================");
    ESP_LOGI("LOGIC_ANALYZER", "Initializing Logic Analyzer...");
    ESP_LOGI("LOGIC_ANALYZER", "========================================");
    
    // 配置信息提示
    ESP_LOGI("LOGIC_ANALYZER", "Configuration:");
    ESP_LOGI("LOGIC_ANALYZER", "  Target: ESP32-S3");
    ESP_LOGI("LOGIC_ANALYZER", "  Protocol: SUMP (Openbench Logic Sniffer)");
    ESP_LOGI("LOGIC_ANALYZER", "  Interface: USB-Serial/JTAG (UART0)");
    ESP_LOGI("LOGIC_ANALYZER", "  Baud Rate: 921600");
    ESP_LOGI("LOGIC_ANALYZER", "  Target GPIO: GPIO14 (1-Wire bus)");
    
    // 启动SUMP协议服务器
    // 注意：配置通过 menuconfig 进行
    // Component config -> Logic Analyzer
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "PulseView Connection Guide:");
    ESP_LOGI("LOGIC_ANALYZER", "  1. Open PulseView");
    ESP_LOGI("LOGIC_ANALYZER", "  2. Click 'Connect to a Device'");
    ESP_LOGI("LOGIC_ANALYZER", "  3. Driver: Openbench Logic Sniffer & SUMP Compatibles");
    ESP_LOGI("LOGIC_ANALYZER", "  4. Serial Port: Select ESP32-S3 COM port");
    ESP_LOGI("LOGIC_ANALYZER", "  5. Baud Rate: 921600");
    ESP_LOGI("LOGIC_ANALYZER", "  6. Click 'Scan for Devices'");
    ESP_LOGI("LOGIC_ANALYZER", "  7. Select 'ESP32 with 8/16 channels'");
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "Starting SUMP protocol server...");
    
    // 启动SUMP协议任务（阻塞函数，内部创建任务）
    logic_analyzer_sump();
    
    ESP_LOGI("LOGIC_ANALYZER", "Logic Analyzer initialized successfully");
    ESP_LOGI("LOGIC_ANALYZER", "========================================");
#else
    ESP_LOGI("LOGIC_ANALYZER", "Logic Analyzer disabled (LOGIC_ANALYZER_ENABLED=0)");
#endif
}

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
    
    // 初始化MAX31850温度传感器（使用GPIO14）
    // 在PID初始化之后进行，避免与其他外设初始化冲突
    esp_err_t ret = max31850_init(MAX31850_ONE_WIRE_GPIO);
    if (ret == ESP_OK) {
        // 启动温度轮询任务
        max31850_start_polling();
        // 打印传感器信息
        max31850_print_sensor_info();
    } else {
        ESP_LOGE("MAIN", "MAX31850 initialization failed: %s", esp_err_to_name(ret));
    }

    // 初始化逻辑分析仪（用于调试1-Wire协议波形）
    // 使用SUMP协议通过USB-Serial与PulseView通信
    // 注意：此函数会创建后台任务，不会阻塞
    logic_analyzer_init();

    // 防止主线程结束
    while(1)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
