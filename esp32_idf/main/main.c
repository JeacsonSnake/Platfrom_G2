#include "main.h"

//////////////////////////////////////////////////////////////
//////////////////////// LOGIC ANALYZER INIT /////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化逻辑分析仪用于1-Wire协议调试
 * 
 * 支持多种模式（通过 menuconfig 配置）：
 * 
 * 【推荐】CLI 模式 (ANALYZER_USE_CLI):
 *   - USB 连接，Python 脚本采集
 *   - 单线连接，无需额外硬件
 *   - 完全兼容 ESP-IDF v5.5.2
 *   - 使用: python logic_analyzer_cli.py
 * 
 * SUMP 模式 (ANALYZER_USE_SUMP):
 *   - USB + UART 连接 PulseView
 *   - 实时分析
 *   - 需要 USB-TTL 转换器
 * 
 * Web 模式 (ANALYZER_USE_WS):
 *   - WiFi 无线连接
 *   - 浏览器访问 http://<ip>/la
 *   - ⚠️ ESP-IDF v5.5.2 存在兼容性问题
 * 
 * 默认配置：
 * - 采样 GPIO14（1-Wire 总线）
 * - PCLK 使用 GPIO15（悬空引脚）
 * 
 * 使用指南详见:
 * 2026_04_08_12_heat_test/04_10/20260410-logic-analyzer-cli-mode-guide.md
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
    ESP_LOGI("LOGIC_ANALYZER", "  PCLK GPIO: 15 (internal use)");
    ESP_LOGI("LOGIC_ANALYZER", "  Channel 0: GPIO14 (1-Wire bus)");
    
    // 模式检测提示
#if defined(CONFIG_ANALYZER_USE_WS)
    ESP_LOGI("LOGIC_ANALYZER", "  Mode: WebSocket (HTTP)");
    ESP_LOGI("LOGIC_ANALYZER", "  Interface: WiFi");
    ESP_LOGI("LOGIC_ANALYZER", "  Access: http://<esp32-ip>/la");
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "NOTE: Web mode has ESP-IDF v5.5.2 compatibility issues");
    ESP_LOGI("LOGIC_ANALYZER", "Recommended to use CLI mode instead.");
#elif defined(CONFIG_ANALYZER_USE_CLI)
    ESP_LOGI("LOGIC_ANALYZER", "  Mode: CLI (Command Line Interface)");
    ESP_LOGI("LOGIC_ANALYZER", "  Interface: USB-Serial/JTAG");
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "CLI Usage:");
    ESP_LOGI("LOGIC_ANALYZER", "  1. pip install pyserial");
    ESP_LOGI("LOGIC_ANALYZER", "  2. cd components/logic_analyzer/logic_analyzer_cli");
    ESP_LOGI("LOGIC_ANALYZER", "  3. Edit la_cfg.json: port=COMx, gpio=14");
    ESP_LOGI("LOGIC_ANALYZER", "  4. python logic_analyzer_cli.py");
    ESP_LOGI("LOGIC_ANALYZER", "  5. Import RowBin.bin to PulseView");
#elif defined(CONFIG_ANALYZER_USE_SUMP)
    ESP_LOGI("LOGIC_ANALYZER", "  Mode: SUMP Protocol");
    ESP_LOGI("LOGIC_ANALYZER", "  Interface: UART");
    ESP_LOGI("LOGIC_ANALYZER", "  Baud Rate: 921600");
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "PulseView Connection Guide:");
    ESP_LOGI("LOGIC_ANALYZER", "  1. Open PulseView");
    ESP_LOGI("LOGIC_ANALYZER", "  2. Driver: Openbench Logic Sniffer & SUMP");
    ESP_LOGI("LOGIC_ANALYZER", "  3. Serial Port: ESP32 UART port");
    ESP_LOGI("LOGIC_ANALYZER", "  4. Baud Rate: 921600");
    ESP_LOGI("LOGIC_ANALYZER", "  5. Click 'Scan for Devices'");
#elif defined(CONFIG_ANALYZER_USE_CLI)
    ESP_LOGI("LOGIC_ANALYZER", "  Mode: CLI (Command Line Interface)");
    ESP_LOGI("LOGIC_ANALYZER", "  Interface: USB-Serial/JTAG");
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "CLI Usage:");
    ESP_LOGI("LOGIC_ANALYZER", "  python logic_analyzer_cli.py");
    ESP_LOGI("LOGIC_ANALYZER", "  Import RowBin.bin to PulseView");
#else
    ESP_LOGW("LOGIC_ANALYZER", "  Mode: No output mode selected!");
    ESP_LOGW("LOGIC_ANALYZER", "  Please enable one mode in menuconfig:");
    ESP_LOGW("LOGIC_ANALYZER", "    - WebSocket (ANALYZER_USE_WS)");
    ESP_LOGW("LOGIC_ANALYZER", "    - SUMP (ANALYZER_USE_SUMP)");
    ESP_LOGW("LOGIC_ANALYZER", "    - CLI (ANALYZER_USE_CLI)");
#endif
    
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "1-Wire Debug Info:");
    ESP_LOGI("LOGIC_ANALYZER", "  Target: MAX31850 on GPIO14");
    ESP_LOGI("LOGIC_ANALYZER", "  Sample Rate: 1MHz recommended");
    ESP_LOGI("LOGIC_ANALYZER", "  Trigger: Falling edge");
    ESP_LOGI("LOGIC_ANALYZER", "");
    
    // 模式自动初始化
    // 根据 menuconfig 自动选择对应模式
#if defined(CONFIG_ANALYZER_USE_SUMP)
    ESP_LOGI("LOGIC_ANALYZER", "Starting SUMP protocol server...");
    logic_analyzer_sump();
    ESP_LOGI("LOGIC_ANALYZER", "SUMP server started");
#elif defined(CONFIG_ANALYZER_USE_CLI)
    ESP_LOGI("LOGIC_ANALYZER", "CLI mode selected.");
    ESP_LOGI("LOGIC_ANALYZER", "Connect USB and run logic_analyzer_cli.py");
    // CLI 模式无需额外初始化，组件自动处理
#elif defined(CONFIG_ANALYZER_USE_WS)
    ESP_LOGI("LOGIC_ANALYZER", "WebSocket mode selected.");
    ESP_LOGI("LOGIC_ANALYZER", "WARNING: May have compatibility issues with ESP-IDF v5.5.2");
    ESP_LOGI("LOGIC_ANALYZER", "Web server will start after WiFi connection.");
    // Web 服务器由 logic_analyzer 组件自动启动
#endif
    
    ESP_LOGI("LOGIC_ANALYZER", "");
    ESP_LOGI("LOGIC_ANALYZER", "Logic Analyzer initialized");
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
    // 支持多种模式：Web、SUMP、CLI（通过menuconfig配置）
    // 注意：此函数不会阻塞，根据配置自动初始化对应模式
    logic_analyzer_init();

    // 防止主线程结束
    while(1)
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
