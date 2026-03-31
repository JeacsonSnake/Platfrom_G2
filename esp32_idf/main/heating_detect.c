/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ Temperature Sensor Driver (GPIO Bit-bang 1-Wire Implementation)
 * 
 * 使用ESP32-S3 GPIO bit-bang配合临界区保护实现精确的1-Wire时序控制
 * 详细调试版本，用于问题定位
 */

#include "heating_detect.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

static const char *TAG = "MAX31850";

/* 启用详细调试日志 */
#define MAX31850_DEBUG_LEVEL  3   /* 0=关闭, 1=错误, 2=警告, 3=信息, 4=详细调试 */

//////////////////////////////////////////////////////////////
/////////////////////// 1-Wire时序配置 ///////////////////////
//////////////////////////////////////////////////////////////

/* 1-Wire时序参数 (微秒) - 基于1-Wire标准协议，使用保守时序 */
#define ONEWIRE_RESET_PULSE_US      480         /**< Reset脉冲宽度 */
#define ONEWIRE_RESET_WAIT_US       70          /**< 等待Presence响应 */
#define ONEWIRE_RESET_RECOVERY_US   410         /**< Reset恢复时间 */

#define ONEWIRE_WRITE1_LOW_US       10          /**< 写1低电平时间 (保守值) */
#define ONEWIRE_WRITE1_HIGH_US      60          /**< 写1高电平时间(总时隙70μs) */
#define ONEWIRE_WRITE0_LOW_US       65          /**< 写0低电平时间 (保守值) */
#define ONEWIRE_WRITE0_HIGH_US      10          /**< 写0恢复时间 */

#define ONEWIRE_READ_INIT_US        3           /**< 读初始化低电平 (标准1-5μs) */
#define ONEWIRE_READ_SAMPLE_US      12          /**< 读到采样点的延迟 (总15μs) */
#define ONEWIRE_READ_RECOVERY_US    60          /**< 读恢复时间 (更保守) */

#define ONEWIRE_INTER_BYTE_DELAY_US 5           /**< 字节间延迟 */

/* CRC8多项式: X8 + X5 + X4 + 1 */
#define CRC8_POLYNOMIAL             0x31

//////////////////////////////////////////////////////////////
/////////////////////// 全局变量 /////////////////////////////
//////////////////////////////////////////////////////////////

static gpio_num_t s_onewire_pin = GPIO_NUM_NC;

static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT];
static uint8_t s_found_devices = 0;
static SemaphoreHandle_t s_onewire_mutex = NULL;
static TaskHandle_t s_poll_task_handle = NULL;
static bool s_initialized = false;

//////////////////////////////////////////////////////////////
/////////////////////// CRC8计算 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 计算CRC8校验值 (X8+X5+X4+1)
 */
static uint8_t crc8_calculate(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

//////////////////////////////////////////////////////////////
/////////////////////// GPIO底层操作 /////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 配置GPIO为开漏输出模式
 */
static inline void onewire_set_opendrain(void)
{
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
}

/**
 * @brief 配置GPIO为输入模式
 */
static inline void onewire_set_input(void)
{
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
}

//////////////////////////////////////////////////////////////
/////////////////////// 调试诊断函数 /////////////////////////
//////////////////////////////////////////////////////////////

#if MAX31850_DEBUG_LEVEL >= 4
/**
 * @brief 打印二进制数据（调试用）
 */
static void debug_print_hex(const char *label, const uint8_t *data, uint8_t len)
{
    char buf[128];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s: [", label);
    for (int i = 0; i < len && pos < sizeof(buf) - 4; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", data[i]);
    }
    if (pos > 0 && buf[pos-1] == ' ') pos--;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
    ESP_LOGI(TAG, "%s", buf);
}

/**
 * @brief 检查GPIO电平状态
 */
static void debug_check_bus_level(const char *context)
{
    int level = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "[DEBUG] Bus level at %s: %d (%s)", 
             context, level, level ? "HIGH" : "LOW");
}
#endif

/**
 * @brief GPIO诊断测试
 * 
 * 测试GPIO14的开漏模式是否工作正常
 */
static esp_err_t onewire_diagnose_gpio(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GPIO Diagnostic Test on GPIO%d", s_onewire_pin);
    ESP_LOGI(TAG, "========================================");
    
    int test_results[5] = {0};
    
    // Test 1: 检查默认状态（上拉电阻）
    onewire_set_input();
    vTaskDelay(pdMS_TO_TICKS(10));
    test_results[0] = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "Test 1 - Floating input (pull-up): %d %s", 
             test_results[0], test_results[0] ? "✓" : "✗");
    
    // Test 2: 强制拉低（开漏模式）
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_onewire_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    test_results[1] = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "Test 2 - Forced low (open-drain): %d %s", 
             test_results[1], test_results[1] == 0 ? "✓" : "✗");
    
    // Test 3: 释放（应该被上拉拉高）
    gpio_set_level(s_onewire_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    test_results[2] = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "Test 3 - Released (should be high): %d %s", 
             test_results[2], test_results[2] ? "✓" : "✗");
    
    // Test 4: 配置为输入模式
    onewire_set_input();
    vTaskDelay(pdMS_TO_TICKS(10));
    test_results[3] = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "Test 4 - Input mode: %d %s", 
             test_results[3], test_results[3] ? "✓" : "✗");
    
    // Test 5: 最终状态检查
    test_results[4] = gpio_get_level(s_onewire_pin);
    ESP_LOGI(TAG, "Test 5 - Final state: %d %s", 
             test_results[4], test_results[4] ? "✓" : "✗");
    
    // 分析结果
    ESP_LOGI(TAG, "----------------------------------------");
    if (test_results[0] == 0) {
        ESP_LOGW(TAG, "WARNING: Bus is LOW in idle state!");
        ESP_LOGW(TAG, "  - Check pull-up resistor (should be 4.7K to 3.3V)");
        ESP_LOGW(TAG, "  - Check for short to GND");
    }
    if (test_results[1] != 0) {
        ESP_LOGW(TAG, "WARNING: Cannot pull bus LOW!");
        ESP_LOGW(TAG, "  - Check GPIO configuration");
        ESP_LOGW(TAG, "  - Check for short to VCC");
    }
    if (test_results[2] == 0 || test_results[3] == 0 || test_results[4] == 0) {
        ESP_LOGW(TAG, "WARNING: Bus not returning HIGH!");
        ESP_LOGW(TAG, "  - Check pull-up resistor");
        ESP_LOGW(TAG, "  - Check for bus contention");
    }
    ESP_LOGI(TAG, "========================================");
    
    // 返回总线状态是否正常
    return (test_results[0] && test_results[2] && test_results[4]) ? ESP_OK : ESP_FAIL;
}

//////////////////////////////////////////////////////////////
/////////////////////// 1-Wire底层操作 ///////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire Reset + Presence检测（详细调试版本）
 * 
 * 使用GPIO直接实现，确保精确的时序控制
 * 
 * @param presence 输出参数，true=检测到设备
 * @return esp_err_t ESP_OK成功
 */
static esp_err_t onewire_reset(bool *presence)
{
    if (!presence) return ESP_ERR_INVALID_ARG;
    
    *presence = false;
    
#if MAX31850_DEBUG_LEVEL >= 4
    debug_check_bus_level("before reset");
#endif
    
    // 临界区保护，防止任务切换影响微秒级时序
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    // 主机拉低480μs
    onewire_set_opendrain();
    gpio_set_level(s_onewire_pin, 0);
    esp_rom_delay_us(ONEWIRE_RESET_PULSE_US);
    
    // 释放总线，切换到输入
    onewire_set_input();
    esp_rom_delay_us(ONEWIRE_RESET_WAIT_US);
    
    // 采样Presence (应该在70μs处采样，设备会将总线拉低)
    int level1 = gpio_get_level(s_onewire_pin);
    
    portEXIT_CRITICAL(&mux);
    
    // 等待剩余的恢复时间
    esp_rom_delay_us(ONEWIRE_RESET_RECOVERY_US);
    
    // 检测Presence：设备应在总线释放后15-60μs内拉低总线，保持60-240μs
    // level1在70μs处采样，应该为0（设备拉低）
    *presence = (level1 == 0);
    
#if MAX31850_DEBUG_LEVEL >= 3
    ESP_LOGI(TAG, "Reset: presence_level=%d, presence=%s", 
             level1, *presence ? "YES" : "NO");
#endif
    
    // 最终检查总线是否恢复高电平
    int final_level = gpio_get_level(s_onewire_pin);
    if (final_level != 1) {
        ESP_LOGW(TAG, "Bus stuck low after reset (possible short)");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Reset后额外恢复时间，确保设备准备就绪
    esp_rom_delay_us(10);
    
    return ESP_OK;
}

/**
 * @brief 写入单个bit（保守版本）
 * 
 * 1-Wire写时隙：
 * - 写1: 低电平1-15μs，然后释放，总时隙60-120μs
 * - 写0: 低电平60-120μs，然后释放
 */
static void onewire_write_bit(uint8_t bit)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    // 确保在开漏模式
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    
    if (bit & 0x01) {
        // 写1: 低电平后快速释放
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE1_LOW_US);
        gpio_set_level(s_onewire_pin, 1);  // 释放，上拉电阻拉高
        esp_rom_delay_us(ONEWIRE_WRITE1_HIGH_US);
    } else {
        // 写0: 保持低电平较长时间
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE0_LOW_US);
        gpio_set_level(s_onewire_pin, 1);  // 释放
        esp_rom_delay_us(ONEWIRE_WRITE0_HIGH_US);
    }
    
    portEXIT_CRITICAL(&mux);
}

/**
 * @brief 读取单个bit（保守版本）
 * 
 * 1-Wire读时隙：
 * 1. 主机拉低1-5μs
 * 2. 释放总线（从机开始驱动）
 * 3. 等待到15μs采样
 * 4. 等待时隙结束（总60μs以上）
 */
static uint8_t onewire_read_bit(void)
{
    uint8_t bit = 0;
    
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    // Step 1: 主机拉低启动读时隙
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_onewire_pin, 0);
    esp_rom_delay_us(ONEWIRE_READ_INIT_US);
    
    // Step 2: 释放总线（切换到输入，上拉电阻拉高）
    gpio_set_level(s_onewire_pin, 1);
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    
    // Step 3: 等待到采样点（总共15μs）
    esp_rom_delay_us(ONEWIRE_READ_SAMPLE_US);
    bit = gpio_get_level(s_onewire_pin);
    
    portEXIT_CRITICAL(&mux);
    
    // Step 4: 等待时隙结束
    esp_rom_delay_us(ONEWIRE_READ_RECOVERY_US);
    
    return bit;
}

/**
 * @brief 写入一个字节（LSB first）
 */
static void onewire_write_byte(uint8_t data)
{
#if MAX31850_DEBUG_LEVEL >= 4
    ESP_LOGI(TAG, "  Writing byte: 0x%02X", data);
#endif
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(data & 0x01);
        data >>= 1;
        // 位间微小延迟，确保设备稳定
        esp_rom_delay_us(2);
    }
    // 字节间延迟
    esp_rom_delay_us(ONEWIRE_INTER_BYTE_DELAY_US);
}

/**
 * @brief 读取一个字节（LSB first）
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (onewire_read_bit() << i);
        // 位间微小延迟
        esp_rom_delay_us(2);
    }
    // 字节间延迟
    esp_rom_delay_us(ONEWIRE_INTER_BYTE_DELAY_US);
#if MAX31850_DEBUG_LEVEL >= 4
    ESP_LOGI(TAG, "  Read byte: 0x%02X", data);
#endif
    return data;
}

//////////////////////////////////////////////////////////////
/////////////////////// ROM操作 //////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 发送Match ROM命令 + 64-bit ROM ID
 */
static esp_err_t onewire_match_rom(const uint8_t *rom_id)
{
    if (!rom_id) return ESP_ERR_INVALID_ARG;
    
    bool presence;
    ESP_ERROR_CHECK(onewire_reset(&presence));
    if (!presence) {
        return ESP_ERR_NOT_FOUND;
    }
    
    onewire_write_byte(ONEWIRE_CMD_MATCH_ROM);
    
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(rom_id[i]);
    }
    
    return ESP_OK;
}

/**
 * @brief 搜索ROM算法（Binary Search Tree）带详细调试
 * 
 * 搜索总线上的所有1-Wire设备
 * 
 * @param rom_ids 存储发现的ROM ID数组
 * @param max_devices 最大设备数
 * @param found_count 输出发现的设备数量
 * @return esp_err_t ESP_OK成功
 */
static esp_err_t onewire_search_rom(uint8_t rom_ids[][8], uint8_t max_devices, uint8_t *found_count)
{
    if (!rom_ids || !found_count || max_devices == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *found_count = 0;
    uint8_t last_discrepancy = 0;
    uint8_t rom_id[8] = {0};
    bool done = false;
    
    ESP_LOGI(TAG, "Starting 1-Wire ROM search...");
    
    while (!done && *found_count < max_devices) {
        bool presence;
        esp_err_t reset_err = onewire_reset(&presence);
        if (reset_err != ESP_OK || !presence) {
            ESP_LOGW(TAG, "Reset failed or no presence (err=%d, presence=%d)", 
                     reset_err, presence);
            break;
        }
        
        // 发送Search ROM命令
        ESP_LOGI(TAG, "  Search iteration %d, last_discrepancy=%d", 
                 *found_count + 1, last_discrepancy);
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
        // Search ROM命令后增加恢复时间
        esp_rom_delay_us(10);
        
        uint8_t last_zero = 0;
        bool search_direction = false;
        
        for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
            uint8_t byte_pos = bit_pos / 8;
            uint8_t bit_mask = 1 << (bit_pos % 8);
            
            // 读取两位：实际值和补码
            uint8_t bit_actual = onewire_read_bit();
            uint8_t bit_complement = onewire_read_bit();
            
            if (bit_actual == 1 && bit_complement == 1) {
                // 没有设备响应（不应该发生，因为前面reset检测到presence）
                ESP_LOGW(TAG, "  Search error at bit %d: no devices (1/1)", bit_pos);
                done = true;
                break;
            } else if (bit_actual == 0 && bit_complement == 0) {
                // 存在分歧，多个设备在该位不同
                if (bit_pos == last_discrepancy) {
                    search_direction = true;  // 走1分支
                } else if (bit_pos > last_discrepancy) {
                    search_direction = false; // 走0分支，记录分歧
                    last_zero = bit_pos;
                } else {
                    // 在之前的分歧点以下，按ROM值选择
                    search_direction = (rom_id[byte_pos] & bit_mask) != 0;
                    if (!search_direction) {
                        last_zero = bit_pos;
                    }
                }
            } else {
                // 只有一个值，所有设备都相同
                search_direction = bit_actual;
            }
            
            // 写入选择的方向
            onewire_write_bit(search_direction);
            
            // 更新ROM ID
            if (search_direction) {
                rom_id[byte_pos] |= bit_mask;
            } else {
                rom_id[byte_pos] &= ~bit_mask;
            }
        }
        
        if (!done) {
            // 验证ROM CRC
            uint8_t calc_crc = crc8_calculate(rom_id, 7);
            if (calc_crc == rom_id[7]) {
                memcpy(rom_ids[*found_count], rom_id, 8);
                ESP_LOGI(TAG, "  Found device %d: ROM ID %02X%02X%02X%02X%02X%02X%02X%02X (CRC OK)",
                         *found_count + 1,
                         rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                         rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
                (*found_count)++;
            } else {
                ESP_LOGW(TAG, "  ROM ID CRC error: calc=0x%02X, recv=0x%02X", calc_crc, rom_id[7]);
                ESP_LOGW(TAG, "  ROM data: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                         rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                         rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
            }
            
            // 更新last_discrepancy用于下一轮搜索
            last_discrepancy = last_zero;
            done = (last_discrepancy == 0);
        }
    }
    
    ESP_LOGI(TAG, "ROM search complete. Found %d device(s)", *found_count);
    return ESP_OK;
}

//////////////////////////////////////////////////////////////
/////////////////////// MAX31850操作 /////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 启动温度转换
 */
static esp_err_t max31850_start_conversion(const uint8_t *rom_id)
{
    esp_err_t err = onewire_match_rom(rom_id);
    if (err != ESP_OK) {
        return err;
    }
    
    onewire_write_byte(MAX31850_CMD_CONVERT_T);
    
    // 关键：发送Convert T命令后，释放总线让上拉电阻供电
    onewire_set_input();
    
    return ESP_OK;
}

/**
 * @brief 使用Skip ROM读取暂存器（单设备测试用）
 */
static esp_err_t max31850_read_scratchpad_skip_rom(uint8_t *scratchpad)
{
    if (!scratchpad) return ESP_ERR_INVALID_ARG;
    
    bool presence;
    esp_err_t err = onewire_reset(&presence);
    if (err != ESP_OK || !presence) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // 发送Skip ROM命令（仅当总线上只有一个设备时有效）
    onewire_write_byte(ONEWIRE_CMD_SKIP_ROM);
    esp_rom_delay_us(10);
    
    onewire_write_byte(MAX31850_CMD_READ_SCRATCH);
    esp_rom_delay_us(10);
    
    for (int i = 0; i < MAX31850_SCRATCHPAD_LEN; i++) {
        scratchpad[i] = onewire_read_byte();
    }
    
    return ESP_OK;
}

/**
 * @brief 读取暂存器
 */
static esp_err_t max31850_read_scratchpad(const uint8_t *rom_id, uint8_t *scratchpad)
{
    if (!scratchpad) return ESP_ERR_INVALID_ARG;
    
    esp_err_t err = onewire_match_rom(rom_id);
    if (err != ESP_OK) {
        return err;
    }
    
    onewire_write_byte(MAX31850_CMD_READ_SCRATCH);
    
    for (int i = 0; i < MAX31850_SCRATCHPAD_LEN; i++) {
        scratchpad[i] = onewire_read_byte();
    }
    
    return ESP_OK;
}

/**
 * @brief 解析暂存器数据
 */
static max31850_err_t max31850_parse_scratchpad(const uint8_t *scratchpad, float *temperature)
{
    if (!scratchpad || !temperature) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    *temperature = 0.0f;
    
    // CRC校验
    uint8_t crc = crc8_calculate(scratchpad, 8);
    if (crc != scratchpad[8]) {
        ESP_LOGW(TAG, "CRC error: calc=0x%02X, recv=0x%02X", crc, scratchpad[8]);
        ESP_LOGW(TAG, "Scratchpad: [%02X %02X %02X %02X %02X %02X %02X %02X %02X]",
                 scratchpad[0], scratchpad[1], scratchpad[2], scratchpad[3],
                 scratchpad[4], scratchpad[5], scratchpad[6], scratchpad[7], scratchpad[8]);
        return MAX31850_ERR_CRC;
    }
    
    // 检查故障寄存器（第4字节）
    uint8_t fault_reg = scratchpad[4];
    if (fault_reg & MAX31850_FAULT_OPEN) {
        return MAX31850_ERR_OPEN;
    }
    if (fault_reg & MAX31850_FAULT_SHORT_GND) {
        return MAX31850_ERR_SHORT_GND;
    }
    if (fault_reg & MAX31850_FAULT_SHORT_VCC) {
        return MAX31850_ERR_SHORT_VCC;
    }
    
    // 解析温度（16位有符号，0.0625°C分辨率）
    // 格式: [Temp LSB][Temp MSB][Reserved][Reserved][Fault][Reserved][Reserved][Reserved][CRC]
    int16_t raw_temp = ((int16_t)scratchpad[1] << 8) | scratchpad[0];
    
    // 处理符号扩展（14位有效数据）
    if (raw_temp & 0x8000) {
        // 负数
        raw_temp = (raw_temp >> 2) | 0xC000;  // 符号扩展
    } else {
        raw_temp >>= 2;
    }
    
    *temperature = (float)raw_temp * 0.0625f;
    
    return MAX31850_OK;
}

//////////////////////////////////////////////////////////////
/////////////////////// 轮询任务 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 轮询状态机
 */
typedef enum {
    POLL_STATE_IDLE = 0,
    POLL_STATE_START_CONVERT,
    POLL_STATE_WAIT_CONVERSION,
    POLL_STATE_READ_DATA,
    POLL_STATE_NEXT_SENSOR,
} poll_state_t;

static poll_state_t s_poll_state = POLL_STATE_IDLE;
static uint8_t s_current_sensor = 0;
static uint32_t s_conversion_start_time = 0;

/**
 * @brief 读取单个传感器温度（内部函数）
 */
static max31850_err_t read_single_sensor(uint8_t sensor_idx)
{
    if (sensor_idx >= s_found_devices) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_idx];
    
    if (!sensor->online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    // 持有互斥锁进行总线操作
    if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    esp_err_t err = max31850_read_scratchpad(sensor->rom_id, scratchpad);
    
    xSemaphoreGive(s_onewire_mutex);
    
    if (err != ESP_OK) {
        sensor->fail_count++;
        if (sensor->fail_count >= MAX31850_OFFLINE_THRESHOLD) {
            sensor->online = false;
            ESP_LOGW(TAG, "Sensor %d marked OFFLINE after %d consecutive failures",
                     sensor_idx, sensor->fail_count);
        }
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 解析数据
    float temp;
    max31850_err_t result = max31850_parse_scratchpad(scratchpad, &temp);
    
    sensor->fault_reg = scratchpad[4];
    sensor->raw_temp = ((int16_t)scratchpad[1] << 8) | scratchpad[0];
    sensor->last_error = result;
    
    if (result == MAX31850_OK) {
        sensor->temperature = temp;
        sensor->data_valid = true;
        sensor->fail_count = 0;
        sensor->last_read_time = xTaskGetTickCount();
    } else {
        sensor->fail_count++;
        sensor->data_valid = false;
        
        if (result == MAX31850_ERR_CRC) {
            ESP_LOGW(TAG, "Sensor %d: CRC error", sensor_idx);
        } else if (result == MAX31850_ERR_OPEN) {
            ESP_LOGW(TAG, "Sensor %d: Thermocouple open circuit (断线)", sensor_idx);
        } else if (result == MAX31850_ERR_SHORT_GND) {
            ESP_LOGW(TAG, "Sensor %d: Short to GND", sensor_idx);
        } else if (result == MAX31850_ERR_SHORT_VCC) {
            ESP_LOGW(TAG, "Sensor %d: Short to VCC", sensor_idx);
        }
        
        if (sensor->fail_count >= MAX31850_OFFLINE_THRESHOLD) {
            sensor->online = false;
            ESP_LOGW(TAG, "Sensor %d marked OFFLINE", sensor_idx);
        }
    }
    
    return result;
}

/**
 * @brief 启动传感器温度转换
 */
static max31850_err_t start_sensor_conversion(uint8_t sensor_idx)
{
    if (sensor_idx >= s_found_devices) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_idx];
    
    if (!sensor->online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    esp_err_t err = max31850_start_conversion(sensor->rom_id);
    
    xSemaphoreGive(s_onewire_mutex);
    
    if (err != ESP_OK) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    return MAX31850_OK;
}

/**
 * @brief 轮询任务主循环
 */
static void max31850_poll_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Poll task started");
    
    // 初始延迟，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    while (1) {
        switch (s_poll_state) {
            case POLL_STATE_IDLE:
                s_current_sensor = 0;
                s_poll_state = POLL_STATE_START_CONVERT;
                break;
                
            case POLL_STATE_START_CONVERT:
                // 启动当前传感器的温度转换
                if (s_sensors[s_current_sensor].online) {
                    max31850_err_t err = start_sensor_conversion(s_current_sensor);
                    if (err == MAX31850_OK) {
                        s_conversion_start_time = xTaskGetTickCount();
                        s_poll_state = POLL_STATE_WAIT_CONVERSION;
                    } else {
                        // 启动失败，尝试下一个
                        s_poll_state = POLL_STATE_NEXT_SENSOR;
                    }
                } else {
                    // 离线传感器，跳过
                    s_poll_state = POLL_STATE_NEXT_SENSOR;
                }
                break;
                
            case POLL_STATE_WAIT_CONVERSION:
                // 等待转换完成（非阻塞）
                if ((xTaskGetTickCount() - s_conversion_start_time) >= 
                    pdMS_TO_TICKS(MAX31850_CONVERSION_TIME_MS)) {
                    s_poll_state = POLL_STATE_READ_DATA;
                } else {
                    // 给其他任务运行时间
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                break;
                
            case POLL_STATE_READ_DATA:
                // 读取当前传感器的温度数据
                read_single_sensor(s_current_sensor);
                s_poll_state = POLL_STATE_NEXT_SENSOR;
                break;
                
            case POLL_STATE_NEXT_SENSOR:
                s_current_sensor++;
                if (s_current_sensor >= s_found_devices) {
                    // 所有传感器完成一轮，等待下一次轮询
                    s_current_sensor = 0;
                    s_poll_state = POLL_STATE_IDLE;
                    vTaskDelay(pdMS_TO_TICKS(1000 - MAX31850_CONVERSION_TIME_MS));
                } else {
                    s_poll_state = POLL_STATE_START_CONVERT;
                }
                break;
                
            default:
                s_poll_state = POLL_STATE_IDLE;
                break;
        }
        
        // 检查是否有离线传感器需要尝试恢复
        static uint32_t last_recovery_attempt = 0;
        if ((xTaskGetTickCount() - last_recovery_attempt) > pdMS_TO_TICKS(30000)) {
            last_recovery_attempt = xTaskGetTickCount();
            for (int i = 0; i < s_found_devices; i++) {
                if (!s_sensors[i].online) {
                    // 尝试恢复离线传感器
                    bool presence;
                    if (onewire_reset(&presence) == ESP_OK && presence) {
                        s_sensors[i].fail_count = 0;
                        s_sensors[i].online = true;
                        ESP_LOGI(TAG, "Sensor %d recovery attempt: back ONLINE", i);
                    }
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////
/////////////////////// 公共API实现 //////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t onewire_pin)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing MAX31850 on GPIO%d...", onewire_pin);
    
    s_onewire_pin = onewire_pin;
    
    // 初始化GPIO为开漏模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << onewire_pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(onewire_pin, 1);  // 释放总线
    
    // 创建互斥锁
    s_onewire_mutex = xSemaphoreCreateMutex();
    if (!s_onewire_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 执行GPIO诊断测试
    ESP_LOGI(TAG, "Running GPIO diagnostic...");
    esp_err_t diag_err = onewire_diagnose_gpio();
    if (diag_err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO diagnostic failed - check hardware connections");
    }
    
    // 检查总线状态
    bool presence;
    esp_err_t err = onewire_reset(&presence);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bus check failed: %d", err);
        vSemaphoreDelete(s_onewire_mutex);
        return err;
    }
    
    if (!presence) {
        ESP_LOGW(TAG, "No device present on 1-Wire bus");
    } else {
        ESP_LOGI(TAG, "Device presence detected!");
    }
    
    // 搜索设备
    uint8_t rom_ids[MAX31850_SENSOR_COUNT][8];
    uint8_t found = 0;
    
    // 多次尝试搜索
    for (int attempt = 0; attempt < 3 && found < MAX31850_SENSOR_COUNT; attempt++) {
        ESP_LOGI(TAG, "ROM search attempt %d/3...", attempt + 1);
        uint8_t temp_found = 0;
        uint8_t temp_roms[MAX31850_SENSOR_COUNT][8];
        
        err = onewire_search_rom(temp_roms, MAX31850_SENSOR_COUNT, &temp_found);
        if (err == ESP_OK && temp_found > found) {
            found = temp_found;
            memcpy(rom_ids, temp_roms, sizeof(rom_ids));
        }
        
        if (found >= MAX31850_SENSOR_COUNT) {
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (found == 0) {
        ESP_LOGW(TAG, "No MAX31850 devices found after all attempts");
        
        // 尝试使用Skip ROM直接读取（仅测试单设备）
        ESP_LOGI(TAG, "Trying Skip ROM method for single device test...");
        uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
        if (max31850_read_scratchpad_skip_rom(scratchpad) == ESP_OK) {
            ESP_LOGI(TAG, "Skip ROM read successful!");
            ESP_LOGI(TAG, "Scratchpad: [%02X %02X %02X %02X %02X %02X %02X %02X %02X]",
                     scratchpad[0], scratchpad[1], scratchpad[2], scratchpad[3],
                     scratchpad[4], scratchpad[5], scratchpad[6], scratchpad[7], scratchpad[8]);
            
            float temp;
            max31850_err_t parse_err = max31850_parse_scratchpad(scratchpad, &temp);
            if (parse_err == MAX31850_OK) {
                ESP_LOGI(TAG, "Temperature read via Skip ROM: %.2f C", temp);
            } else {
                ESP_LOGW(TAG, "Skip ROM parse error: %s", max31850_err_to_string(parse_err));
            }
        } else {
            ESP_LOGW(TAG, "Skip ROM read also failed");
        }
        
        vSemaphoreDelete(s_onewire_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    s_found_devices = found;
    
    // 初始化传感器结构
    memset(s_sensors, 0, sizeof(s_sensors));
    for (int i = 0; i < found; i++) {
        memcpy(s_sensors[i].rom_id, rom_ids[i], 8);
        s_sensors[i].online = true;
        s_sensors[i].temperature = 0.0f;
        s_sensors[i].fail_count = 0;
        s_sensors[i].data_valid = false;
    }
    
    // 打印发现的设备列表
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "MAX31850 ROM ID List (%d device(s) found):", found);
    for (int i = 0; i < found; i++) {
        ESP_LOGI(TAG, "  Sensor %d (P%d): %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X  %s",
                 i, i + 1,
                 rom_ids[i][0], rom_ids[i][1], rom_ids[i][2], rom_ids[i][3],
                 rom_ids[i][4], rom_ids[i][5], rom_ids[i][6], rom_ids[i][7],
                 s_sensors[i].online ? "ONLINE" : "OFFLINE");
        if (rom_ids[i][0] == MAX31850_FAMILY_CODE) {
            ESP_LOGI(TAG, "    Family: 0x%02X (MAX31850/MAX31851)", rom_ids[i][0]);
        }
    }
    ESP_LOGI(TAG, "===============================================");
    
    // 创建轮询任务
    BaseType_t task_err = xTaskCreate(max31850_poll_task, "MAX31850_POLL", 4096, NULL, 2, &s_poll_task_handle);
    if (task_err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_onewire_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "MAX31850 initialized successfully");
    
    return ESP_OK;
}

void max31850_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    if (s_poll_task_handle) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }
    
    if (s_onewire_mutex) {
        vSemaphoreDelete(s_onewire_mutex);
        s_onewire_mutex = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "MAX31850 deinitialized");
}

max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out)
{
    if (!s_initialized) {
        return MAX31850_ERR_BUS_FAULT;
    }
    
    if (sensor_id >= s_found_devices) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (!temp_out) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (!sensor->online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (!sensor->data_valid) {
        return sensor->last_error;
    }
    
    *temp_out = sensor->temperature;
    return MAX31850_OK;
}

max31850_err_t max31850_get_raw_data(uint8_t sensor_id, int16_t *raw_out, uint8_t *fault_reg)
{
    if (!s_initialized || sensor_id >= s_found_devices) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (!sensor->online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (raw_out) {
        *raw_out = sensor->raw_temp;
    }
    
    if (fault_reg) {
        *fault_reg = sensor->fault_reg;
    }
    
    return MAX31850_OK;
}

max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout)
{
    if (!s_initialized) {
        return MAX31850_ERR_BUS_FAULT;
    }
    
    if (sensor_id >= s_found_devices || !temp_out) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (!sensor->online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (xSemaphoreTake(s_onewire_mutex, timeout) != pdTRUE) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 启动转换
    esp_err_t err = max31850_start_conversion(sensor->rom_id);
    if (err != ESP_OK) {
        xSemaphoreGive(s_onewire_mutex);
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 阻塞等待转换完成
    vTaskDelay(pdMS_TO_TICKS(MAX31850_CONVERSION_TIME_MS));
    
    // 读取数据
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    err = max31850_read_scratchpad(sensor->rom_id, scratchpad);
    
    xSemaphoreGive(s_onewire_mutex);
    
    if (err != ESP_OK) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 解析
    float temp;
    max31850_err_t result = max31850_parse_scratchpad(scratchpad, &temp);
    
    if (result == MAX31850_OK) {
        *temp_out = temp;
        sensor->temperature = temp;
        sensor->data_valid = true;
        sensor->fail_count = 0;
    }
    
    return result;
}

bool max31850_is_online(uint8_t sensor_id)
{
    if (!s_initialized || sensor_id >= s_found_devices) {
        return false;
    }
    
    return s_sensors[sensor_id].online;
}

const char* max31850_err_to_string(max31850_err_t err)
{
    switch (err) {
        case MAX31850_OK:           return "OK";
        case MAX31850_ERR_OPEN:     return "Thermocouple OPEN";
        case MAX31850_ERR_SHORT_GND: return "Short to GND";
        case MAX31850_ERR_SHORT_VCC: return "Short to VCC";
        case MAX31850_ERR_CRC:      return "CRC Error";
        case MAX31850_ERR_TIMEOUT:  return "Timeout";
        case MAX31850_ERR_OFFLINE:  return "Sensor Offline";
        case MAX31850_ERR_INVALID_ID: return "Invalid ID";
        case MAX31850_ERR_BUS_FAULT:  return "Bus Fault";
        case MAX31850_ERR_NOT_FOUND:  return "Not Found";
        default:                    return "Unknown";
    }
}

void max31850_dump_scratchpad(uint8_t sensor_id)
{
    if (!s_initialized || sensor_id >= s_found_devices) {
        ESP_LOGW(TAG, "Invalid sensor ID: %d", sensor_id);
        return;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex for dump");
        return;
    }
    
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    esp_err_t err = max31850_read_scratchpad(sensor->rom_id, scratchpad);
    
    xSemaphoreGive(s_onewire_mutex);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor %d: Failed to read scratchpad", sensor_id);
        return;
    }
    
    ESP_LOGI(TAG, "Sensor %d Scratchpad Dump:", sensor_id);
    ESP_LOGI(TAG, "  [0] Temp LSB: 0x%02X", scratchpad[0]);
    ESP_LOGI(TAG, "  [1] Temp MSB: 0x%02X", scratchpad[1]);
    ESP_LOGI(TAG, "  [2] Reserved: 0x%02X", scratchpad[2]);
    ESP_LOGI(TAG, "  [3] Config:   0x%02X", scratchpad[3]);
    ESP_LOGI(TAG, "  [4] Fault:    0x%02X (%s)", scratchpad[4],
             (scratchpad[4] & 0x01) ? "OC Fault" :
             (scratchpad[4] & 0x02) ? "SCG Fault" :
             (scratchpad[4] & 0x04) ? "SCV Fault" : "OK");
    ESP_LOGI(TAG, "  [5] Reserved: 0x%02X", scratchpad[5]);
    ESP_LOGI(TAG, "  [6] Reserved: 0x%02X", scratchpad[6]);
    ESP_LOGI(TAG, "  [7] Reserved: 0x%02X", scratchpad[7]);
    ESP_LOGI(TAG, "  [8] CRC:      0x%02X (calc: 0x%02X)", 
             scratchpad[8], crc8_calculate(scratchpad, 8));
    
    // 解析原始温度
    int16_t raw = ((int16_t)scratchpad[1] << 8) | scratchpad[0];
    ESP_LOGI(TAG, "  Raw temp: 0x%04X (%d)", raw, raw);
}

uint8_t max31850_get_all_status(max31850_sensor_t *sensors)
{
    if (!sensors || !s_initialized) {
        return 0;
    }
    
    memcpy(sensors, s_sensors, sizeof(s_sensors));
    return s_found_devices;
}
