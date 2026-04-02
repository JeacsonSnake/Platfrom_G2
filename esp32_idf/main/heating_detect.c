/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ Temperature Sensor Driver Implementation - Software Fix
 * 
 * 1-Wire Bit-Bang implementation for ESP32-S3 @ 240MHz
 * Critical section protection during 1-Wire transactions
 * 
 * SOFTWARE FIX for 4.7K weak pull-up:
 * 1. Active pull-up using push-pull mode after releasing bus
 * 2. Multiple sampling with majority voting for bit reading
 * 3. Extended recovery times between bits/bytes
 * 4. Slower bit rate for better signal integrity
 * 
 * Hardware Configuration (Extracted from ESP32-S3-DevKitC-1主控.SchDoc):
 * - MCU: ESP32-S3-DevKitC-1 @ 240MHz
 * - 1-Wire Bus: GPIO14 (IO14 network)
 * - Pull-up: R1=4.7KΩ (R 0805_L) per sensor to +3.3V
 * - Sensors: 4× MAX31850KATB+ (TDFN-10-EP 3x4)
 * 
 * @version 3.2
 * @date 2026-04-02
 */

#include "heating_detect.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "MAX31850";

//////////////////////////////////////////////////////////////
//////////////////////// 私有变量 ////////////////////////////
//////////////////////////////////////////////////////////////

static gpio_num_t s_gpio_num = GPIO_NUM_NC;
static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT];
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_poll_task_handle = NULL;
static bool s_initialized = false;

static uint8_t s_rom_ids[MAX31850_SENSOR_COUNT][8];
static uint8_t s_sensor_count = 0;

//////////////////////////////////////////////////////////////
//////////////////////// 精确延时 ////////////////////////////
//////////////////////////////////////////////////////////////

static inline void onewire_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

//////////////////////////////////////////////////////////////
//////////////////////// GPIO控制 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 设置GPIO为开漏输出模式（低电平）
 */
static inline void onewire_set_low(void)
{
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_gpio_num, 0);
}

/**
 * @brief 释放总线 - 使用主动上拉（Push-Pull）补偿弱上拉
 * 
 * 软件修复：短暂使用 Push-Pull 模式主动驱动 GPIO 高电平，
 * 加速上升沿，补偿 4.7K 上拉电阻的不足
 */
static inline void onewire_release(void)
{
#if ONEWIRE_USE_STRONG_PU
    // 主动上拉：使用 Push-Pull 模式短暂驱动高电平
    gpio_set_direction(s_gpio_num, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio_num, 1);
    onewire_delay_us(ONEWIRE_STRONG_PU_US);
    // 切换回开漏模式，释放总线
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_gpio_num, 1);
#else
    // 传统方式：仅依赖上拉电阻
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_gpio_num, 1);
#endif
}

/**
 * @brief 设置GPIO为输入模式（释放总线）
 */
static inline void onewire_set_input(void)
{
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT);
}

/**
 * @brief 读取总线电平
 */
static inline uint8_t onewire_read_level(void)
{
    return (uint8_t)gpio_get_level(s_gpio_num);
}

/**
 * @brief 等待总线恢复
 * 
 * 软件修复：在关键操作后等待更长时间，让总线充分恢复
 */
static inline void onewire_wait_bus_recovery(void)
{
    onewire_delay_us(ONEWIRE_BUS_RECOVERY_US);
}

//////////////////////////////////////////////////////////////
//////////////////////// 调试开关 ////////////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_DEBUG_LEVEL        3
#define MAX31850_DEBUG_WAVEFORM     1

//////////////////////////////////////////////////////////////
//////////////////////// 调试功能 ////////////////////////////
//////////////////////////////////////////////////////////////

#if MAX31850_DEBUG_LEVEL >= 3

static void onewire_diagnose_gpio(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GPIO Diagnostic Test on GPIO%d", s_gpio_num);
    ESP_LOGI(TAG, "========================================");
    
    int test1, test2, test3, test4, test5;
    
    // Test 1: 输入模式
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT);
    onewire_delay_us(10);
    test1 = gpio_get_level(s_gpio_num);
    ESP_LOGI(TAG, "Test 1 - Input mode (pull-up): %d %s", test1, test1 ? "✓" : "✗");
    
    // Test 2: 开漏输出低电平
    onewire_set_low();
    onewire_delay_us(10);
    test2 = gpio_get_level(s_gpio_num);
    ESP_LOGI(TAG, "Test 2 - Forced low (open-drain): %d %s", test2, test2 == 0 ? "✓" : "✗");
    
    // Test 3: 释放总线后电平（使用主动上拉）
    onewire_release();
    onewire_delay_us(100);
    test3 = gpio_get_level(s_gpio_num);
    ESP_LOGI(TAG, "Test 3 - Released with strong PU (100us): %d %s", test3, test3 ? "✓" : "✗");
    
    // Test 4: 输入模式
    onewire_set_input();
    onewire_delay_us(10);
    test4 = gpio_get_level(s_gpio_num);
    ESP_LOGI(TAG, "Test 4 - Input mode: %d %s", test4, test4 ? "✓" : "✗");
    
    // Test 5: 最终状态
    test5 = gpio_get_level(s_gpio_num);
    ESP_LOGI(TAG, "Test 5 - Final state: %d %s", test5, test5 ? "✓" : "✗");
    
    ESP_LOGI(TAG, "----------------------------------------");
    
    bool passed = (test1 == 1) && (test2 == 0) && (test5 == 1);
    if (passed) {
        ESP_LOGI(TAG, "GPIO Diagnostic: PASSED ✓");
    } else {
        ESP_LOGW(TAG, "GPIO Diagnostic: PARTIAL ⚠");
        if (test1 != 1) ESP_LOGW(TAG, "  Test 1 fail: Check pull-up resistor");
        if (test2 != 0) ESP_LOGW(TAG, "  Test 2 fail: Cannot drive LOW");
        if (test5 != 1) ESP_LOGW(TAG, "  Test 5 fail: Bus stuck LOW, check short");
    }
    ESP_LOGI(TAG, "========================================");
}

static void onewire_log_scratchpad(const uint8_t *data, const char *prefix)
{
    ESP_LOGI(TAG, "%s Scratchpad (MAX31850 K-Type):", prefix);
    ESP_LOGI(TAG, "  [0] 0x%02X | [1] 0x%02X | [2] 0x%02X | [3] 0x%02X",
             data[0], data[1], data[2], data[3]);
    ESP_LOGI(TAG, "  [4] 0x%02X | [5] 0x%02X | [6] 0x%02X | [7] 0x%02X",
             data[4], data[5], data[6], data[7]);
    ESP_LOGI(TAG, "  [8] 0x%02X (CRC)", data[8]);
    
    int16_t raw_tc = ((int16_t)data[1] << 8) | data[0];
    int16_t tc_temp = raw_tc >> 2;
    float tc_celsius = tc_temp * 0.25f;
    
    ESP_LOGI(TAG, "  Thermocouple (K-Type):");
    ESP_LOGI(TAG, "    Raw: 0x%04X -> %d counts -> %.2f°C", 
             (uint16_t)raw_tc, tc_temp, tc_celsius);
    
    int16_t raw_cj = ((int16_t)data[3] << 8) | data[2];
    int16_t cj_temp = raw_cj >> 4;
    float cj_celsius = cj_temp * 0.0625f;
    
    ESP_LOGI(TAG, "  Cold Junction:");
    ESP_LOGI(TAG, "    Raw: 0x%04X -> %d counts -> %.2f°C",
             (uint16_t)raw_cj, cj_temp, cj_celsius);
    
    uint8_t fault_bits = data[0] & 0x0F;
    if (fault_bits != 0) {
        ESP_LOGW(TAG, "  Fault Status (Byte0[3:0]=0x%X):", fault_bits);
        if (fault_bits & 0x01) ESP_LOGW(TAG, "    FAULT: Any fault detected");
        if (fault_bits & 0x02) ESP_LOGW(TAG, "    OPEN: Thermocouple open circuit");
        if (fault_bits & 0x04) ESP_LOGW(TAG, "    SHORT GND: Thermocouple short to GND");
        if (fault_bits & 0x08) ESP_LOGW(TAG, "    SHORT VCC: Thermocouple short to VCC");
    }
    
    uint8_t hw_addr = data[4] & 0x03;
    ESP_LOGI(TAG, "  Config Register: 0x%02X", data[4]);
    ESP_LOGI(TAG, "    Hardware Address (AD[1:0]): %d (AD1=%d, AD0=%d)",
             hw_addr, (data[4] >> 1) & 0x01, data[4] & 0x01);
}

static bool onewire_check_bus_level(void)
{
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT);
    onewire_delay_us(10);
    int level = gpio_get_level(s_gpio_num);
    
    if (level == 0) {
        ESP_LOGE(TAG, "BUS FAULT: Line shorted to GND or no pull-up!");
        ESP_LOGE(TAG, "  Check: 1) R1(4.7K) connected to DQ and +3.3V");
        ESP_LOGE(TAG, "         2) No short between DQ and GND");
        ESP_LOGE(TAG, "         3) Sensors powered (VDD=3.3V)");
        return false;
    }
    
    ESP_LOGI(TAG, "Bus level check: HIGH ✓ (pull-up working)");
    return true;
}

#endif

//////////////////////////////////////////////////////////////
//////////////////////// 1-Wire协议 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire Reset脉冲 + Presence检测
 */
static esp_err_t onewire_reset(bool *presence)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    
#if MAX31850_DEBUG_WAVEFORM
    uint8_t level_before, level_at_15us, level_at_70us, level_final;
    
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT);
    level_before = gpio_get_level(s_gpio_num);
#endif

    portENTER_CRITICAL(&mux);
    
    // 拉低480μs（Reset脉冲）
    onewire_set_low();
    onewire_delay_us(ONEWIRE_RESET_LOW_US);
    
    // 释放总线（使用主动上拉）
    onewire_release();
    
#if MAX31850_DEBUG_WAVEFORM
    onewire_delay_us(15);
    level_at_15us = gpio_get_level(s_gpio_num);
    onewire_delay_us(55);
#else
    onewire_delay_us(ONEWIRE_PRESENCE_WAIT_US);
#endif
    
    // 采样Presence
    uint8_t presence_level = onewire_read_level();
    
#if MAX31850_DEBUG_WAVEFORM
    level_at_70us = presence_level;
#endif
    
    // 等待Reset周期完成
    onewire_delay_us(ONEWIRE_RESET_RECOVERY_US);
    
#if MAX31850_DEBUG_WAVEFORM
    level_final = gpio_get_level(s_gpio_num);
#endif
    
    portEXIT_CRITICAL(&mux);
    
    *presence = (presence_level == 0);
    
#if MAX31850_DEBUG_WAVEFORM
    ESP_LOGI(TAG, "Reset Waveform: before=%d, reset=0, @15us=%d, @70us=%d, final=%d",
             level_before, level_at_15us, level_at_70us, level_final);
    ESP_LOGI(TAG, "Reset: %s (@70us presence_level=%d)",
             *presence ? "Device detected ✓" : "No device ✗", presence_level);
#elif MAX31850_DEBUG_LEVEL >= 3
    ESP_LOGI(TAG, "Reset: presence=%s (level at 70us=%d)",
             *presence ? "YES" : "NO", presence_level);
#endif
    
    // 额外等待总线恢复
    onewire_wait_bus_recovery();
    
    return ESP_OK;
}

/**
 * @brief 写入单个位
 */
static void onewire_write_bit(uint8_t bit)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    
    portENTER_CRITICAL(&mux);
    
    if (bit & 0x01) {
        // 写1: 拉低6μs → 释放（主动上拉）→ 等待80μs
        onewire_set_low();
        onewire_delay_us(ONEWIRE_WRITE1_LOW_US);
        onewire_release();
        onewire_delay_us(ONEWIRE_WRITE1_RECOVERY_US);
    } else {
        // 写0: 拉低70μs → 释放（主动上拉）→ 等待30μs
        onewire_set_low();
        onewire_delay_us(ONEWIRE_WRITE0_LOW_US);
        onewire_release();
        onewire_delay_us(ONEWIRE_WRITE0_RECOVERY_US);
    }
    
    onewire_delay_us(ONEWIRE_BIT_INTERVAL_US);
    
    portEXIT_CRITICAL(&mux);
}

/**
 * @brief 读取单个位 - 使用多数表决采样
 * 
 * 软件修复：多次采样取多数，提高读取可靠性
 */
static uint8_t onewire_read_bit(void)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint8_t samples[ONEWIRE_READ_SAMPLE_TIMES];
    
    portENTER_CRITICAL(&mux);
    
    // 拉低3μs
    onewire_set_low();
    onewire_delay_us(ONEWIRE_READ_INIT_US);
    
    // 释放总线（使用主动上拉）
    onewire_release();
    
    // 等待到采样点
    onewire_delay_us(ONEWIRE_READ_SAMPLE_US);
    
    // 多次采样
    for (int i = 0; i < ONEWIRE_READ_SAMPLE_TIMES; i++) {
        samples[i] = onewire_read_level();
        onewire_delay_us(1);  // 1μs间隔采样
    }
    
    // 等待时隙结束
    onewire_delay_us(ONEWIRE_READ_RECOVERY_US);
    
    onewire_delay_us(ONEWIRE_BIT_INTERVAL_US);
    
    portEXIT_CRITICAL(&mux);
    
    // 多数表决
    uint8_t ones = 0;
    for (int i = 0; i < ONEWIRE_READ_SAMPLE_TIMES; i++) {
        if (samples[i]) ones++;
    }
    
    uint8_t result = (ones > ONEWIRE_READ_SAMPLE_TIMES / 2) ? 1 : 0;
    
#if MAX31850_DEBUG_LEVEL >= 4
    ESP_LOGI(TAG, "Read bit samples: %d,%d,%d -> result=%d",
             samples[0], samples[1], samples[2], result);
#endif
    
    return result;
}

/**
 * @brief 写入一个字节（LSB First）
 */
static void onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
    onewire_delay_us(ONEWIRE_BYTE_INTERVAL_US);
}

/**
 * @brief 读取一个字节（LSB First）
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (onewire_read_bit() << i);
    }
    onewire_delay_us(ONEWIRE_BYTE_INTERVAL_US);
    return data;
}

//////////////////////////////////////////////////////////////
//////////////////////// CRC8校验 ////////////////////////////
//////////////////////////////////////////////////////////////

static const uint8_t crc8_table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

static uint8_t crc8_calculate(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

//////////////////////////////////////////////////////////////
//////////////////////// ROM操作 /////////////////////////////
//////////////////////////////////////////////////////////////

static esp_err_t onewire_match_rom(const uint8_t *rom_id)
{
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
 * @brief 执行一次ROM搜索迭代
 */
static bool onewire_search_rom_iteration(uint8_t *last_discrepancy, uint8_t *rom_id)
{
    bool presence;
    uint8_t last_zero = 0;
    
    if (onewire_reset(&presence) != ESP_OK || !presence) {
        return false;
    }
    
    onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
    
    for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
        uint8_t bit_actual = onewire_read_bit();
        uint8_t bit_complement = onewire_read_bit();
        
        uint8_t selected_bit;
        
        if (bit_actual == 0 && bit_complement == 0) {
            if (bit_pos == *last_discrepancy) {
                selected_bit = 1;
            } else if (bit_pos > *last_discrepancy) {
                selected_bit = 0;
                last_zero = bit_pos;
            } else {
                selected_bit = (rom_id[bit_pos / 8] >> (bit_pos % 8)) & 0x01;
                if (selected_bit == 0) {
                    last_zero = bit_pos;
                }
            }
        } else if (bit_actual == 0 && bit_complement == 1) {
            selected_bit = 0;
        } else if (bit_actual == 1 && bit_complement == 0) {
            selected_bit = 1;
        } else {
            ESP_LOGW(TAG, "Search error at bit %d: no devices (11)", bit_pos);
            return false;
        }
        
        onewire_write_bit(selected_bit);
        
        if (selected_bit) {
            rom_id[bit_pos / 8] |= (1 << (bit_pos % 8));
        } else {
            rom_id[bit_pos / 8] &= ~(1 << (bit_pos % 8));
        }
    }
    
    // CRC验证
    uint8_t crc_calc = crc8_calculate(rom_id, 7);
    if (crc_calc != rom_id[7]) {
        return false;
    }
    
    *last_discrepancy = last_zero;
    return true;
}

/**
 * @brief ROM搜索主函数（带重试机制）
 */
static esp_err_t onewire_search_rom(void)
{
    uint8_t last_discrepancy = 0;
    uint8_t rom_id[8] = {0};
    
    s_sensor_count = 0;
    
    ESP_LOGI(TAG, "Starting ROM search with retry mechanism...");
    ESP_LOGI(TAG, "  Software fix enabled: Strong PU=%d, Sample times=%d",
             ONEWIRE_USE_STRONG_PU, ONEWIRE_READ_SAMPLE_TIMES);
    ESP_LOGI(TAG, "  Bit interval: %dμs, Byte interval: %dμs", 
             ONEWIRE_BIT_INTERVAL_US, ONEWIRE_BYTE_INTERVAL_US);
    
    while (s_sensor_count < MAX31850_SENSOR_COUNT) {
        bool device_found = false;
        
        for (int retry = 0; retry < MAX31850_ROM_SEARCH_RETRY; retry++) {
            memset(rom_id, 0, 8);
            
            if (onewire_search_rom_iteration(&last_discrepancy, rom_id)) {
                ESP_LOGI(TAG, "ROM ID CRC OK (attempt %d)", retry + 1);
                
                memcpy(s_rom_ids[s_sensor_count], rom_id, 8);
                ESP_LOGI(TAG, "Found device %d: ROM ID %02X%02X%02X%02X%02X%02X%02X%02X",
                         s_sensor_count + 1,
                         rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                         rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
                
                s_sensor_count++;
                device_found = true;
                break;
            } else {
                if (retry < MAX31850_ROM_SEARCH_RETRY - 1) {
                    ESP_LOGW(TAG, "ROM search attempt %d failed, retrying...", retry + 1);
                    esp_rom_delay_us(200);  // 增加重试间隔
                }
            }
        }
        
        if (!device_found) {
            if (s_sensor_count == 0) {
                ESP_LOGW(TAG, "ROM search failed after %d retries", MAX31850_ROM_SEARCH_RETRY);
                ESP_LOGW(TAG, "  Hardware issue - suggestions:");
                ESP_LOGW(TAG, "    1. Reduce pull-up resistor to 2.2KΩ (hardware fix)");
                ESP_LOGW(TAG, "    2. Shorten bus wires (<10m)");
                ESP_LOGW(TAG, "    3. Check 3.3V power stability");
            }
            break;
        }
        
        if (last_discrepancy == 0) {
            break;
        }
    }
    
    ESP_LOGI(TAG, "ROM search complete. Found %d device(s)", s_sensor_count);
    
    return (s_sensor_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

//////////////////////////////////////////////////////////////
//////////////////////// MAX31850操作 ////////////////////////
//////////////////////////////////////////////////////////////

static uint8_t max31850_get_hw_addr(const uint8_t *rom_id)
{
    uint8_t addr = (rom_id[1] ^ rom_id[2] ^ rom_id[3]) & 0x03;
    return addr;
}

static esp_err_t max31850_read_data_frame(const uint8_t *rom_id, uint8_t *data)
{
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    
    onewire_write_byte(MAX31850_CMD_READ_DATA);
    
    for (int i = 0; i < 9; i++) {
        data[i] = onewire_read_byte();
    }
    
    return ESP_OK;
}

static max31850_err_t max31850_parse_data(const uint8_t *data, max31850_sensor_t *sensor)
{
    memcpy(sensor->scratchpad, data, 9);
    
#if MAX31850_DEBUG_LEVEL >= 4
    onewire_log_scratchpad(data, "Parsing");
#endif
    
    uint8_t crc_calc = crc8_calculate(data, 8);
    if (crc_calc != data[8]) {
        ESP_LOGW(TAG, "CRC error: calc=0x%02X, recv=0x%02X", crc_calc, data[8]);
#if MAX31850_DEBUG_LEVEL >= 3
        onewire_log_scratchpad(data, "CRC Error");
#endif
        return MAX31850_ERR_CRC;
    }
    
    int16_t raw_temp = ((int16_t)data[1] << 8) | data[0];
    raw_temp = raw_temp >> 2;
    sensor->thermocouple_temp = (float)raw_temp * 0.25f;
    
    int16_t raw_cj = ((int16_t)data[3] << 8) | data[2];
    raw_cj = raw_cj >> 4;
    sensor->cold_junction_temp = (float)raw_cj * 0.0625f;
    
    sensor->fault_reg = data[4];
    
    if (sensor->fault_reg & MAX31850_FAULT_OPEN) {
        ESP_LOGW(TAG, "Sensor fault: Thermocouple Open Circuit");
        return MAX31850_ERR_OPEN;
    }
    if (sensor->fault_reg & MAX31850_FAULT_SHORT_GND) {
        ESP_LOGW(TAG, "Sensor fault: Short to GND");
        return MAX31850_ERR_SHORT_GND;
    }
    if (sensor->fault_reg & MAX31850_FAULT_SHORT_VCC) {
        ESP_LOGW(TAG, "Sensor fault: Short to VCC");
        return MAX31850_ERR_SHORT_VCC;
    }
    
    sensor->data_valid = true;
    sensor->last_update = xTaskGetTickCount();
    
    return MAX31850_OK;
}

//////////////////////////////////////////////////////////////
//////////////////////// 轮询任务 ////////////////////////////
//////////////////////////////////////////////////////////////

static void max31850_poll_task(void *pvParameters)
{
    uint8_t current_sensor = 0;
    uint8_t data[9];
    
    ESP_LOGI(TAG, "Poll task started");
    
    while (1) {
        if (current_sensor < s_sensor_count) {
            int sensor_idx = -1;
            for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
                if (s_sensors[i].hw_addr == current_sensor) {
                    sensor_idx = i;
                    break;
                }
            }
            
            if (sensor_idx >= 0 && s_sensors[sensor_idx].online) {
                esp_err_t err = max31850_read_data_frame(s_sensors[sensor_idx].rom_id, data);
                
                if (err == ESP_OK) {
                    max31850_err_t result = max31850_parse_data(data, &s_sensors[sensor_idx]);
                    
                    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        s_sensors[sensor_idx].last_error = result;
                        
                        if (result == MAX31850_OK) {
                            s_sensors[sensor_idx].fail_count = 0;
                        } else {
                            s_sensors[sensor_idx].fail_count++;
                            if (s_sensors[sensor_idx].fail_count >= MAX31850_MAX_RETRY) {
                                s_sensors[sensor_idx].online = false;
                                ESP_LOGW(TAG, "Sensor [%d] marked OFFLINE after %d failures",
                                         sensor_idx, MAX31850_MAX_RETRY);
                            }
                        }
                        
                        xSemaphoreGive(s_mutex);
                    }
                }
            }
        }
        
        current_sensor++;
        if (current_sensor >= s_sensor_count) {
            current_sensor = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(MAX31850_POLL_INTERVAL_MS));
    }
}

//////////////////////////////////////////////////////////////
//////////////////////// 公共API /////////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t gpio_num)
{
    ESP_LOGI(TAG, "Initializing MAX31850 on GPIO%d...", gpio_num);
    ESP_LOGI(TAG, "Sensor: MAX31850KATB+ K-Type Thermocouple Digitizer");
    ESP_LOGI(TAG, "  Range: -270°C to +1372°C, Accuracy: ±2°C (-200°C to +700°C)");
    ESP_LOGI(TAG, "  Resolution: TC=0.25°C (14-bit), CJ=0.0625°C (12-bit)");
    ESP_LOGI(TAG, "  Conversion: Auto-continuous (~100ms)");
    ESP_LOGI(TAG, "Hardware: ESP32-S3-DevKitC-1 + 4x MAX31850");
    ESP_LOGI(TAG, "1-Wire Bus: GPIO%d, Pull-up: R1=4.7K to +3.3V", gpio_num);
    ESP_LOGI(TAG, "Software Fix: Strong PU enabled, Multi-sampling enabled");
    
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    s_gpio_num = gpio_num;
    
    // 配置GPIO为开漏输出模式，启用内部上拉作为辅助
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // 释放总线
    onewire_release();
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
#if MAX31850_DEBUG_LEVEL >= 3
    onewire_diagnose_gpio();
    
    if (!onewire_check_bus_level()) {
        ESP_LOGE(TAG, "Bus level check FAILED - check hardware connections");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Testing 1-Wire Reset/Presence...");
    bool test_presence;
    for (int i = 0; i < 3; i++) {
        onewire_reset(&test_presence);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#else
    if (onewire_read_level() == 0) {
        ESP_LOGE(TAG, "BUS FAULT: Line shorted to GND");
        return ESP_ERR_INVALID_STATE;
    }
#endif
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t err = onewire_search_rom();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ROM search failed: %d", err);
    }
    
    memset(s_sensors, 0, sizeof(s_sensors));
    
    for (int i = 0; i < s_sensor_count && i < MAX31850_SENSOR_COUNT; i++) {
        memcpy(s_sensors[i].rom_id, s_rom_ids[i], 8);
        s_sensors[i].hw_addr = max31850_get_hw_addr(s_rom_ids[i]);
        s_sensors[i].online = true;
        s_sensors[i].data_valid = false;
        s_sensors[i].fail_count = 0;
        
        const char* pcb_label[] = {"U1(P1)", "U2(P2)", "U3(P3)", "U4(P4)"};
        uint8_t addr = s_sensors[i].hw_addr;
        
        ESP_LOGI(TAG, "Sensor [%d]: ROM=%02X%02X%02X%02X%02X%02X%02X%02X, HW_ADDR=%02X (%s)",
                 i,
                 s_sensors[i].rom_id[0], s_sensors[i].rom_id[1],
                 s_sensors[i].rom_id[2], s_sensors[i].rom_id[3],
                 s_sensors[i].rom_id[4], s_sensors[i].rom_id[5],
                 s_sensors[i].rom_id[6], s_sensors[i].rom_id[7],
                 addr, (addr < 4) ? pcb_label[addr] : "?");
    }
    
    for (int i = 0; i < s_sensor_count && i < MAX31850_SENSOR_COUNT; i++) {
        uint8_t data[9];
        
        for (int retry = 0; retry < MAX31850_MAX_RETRY; retry++) {
            if (max31850_read_data_frame(s_sensors[i].rom_id, data) == ESP_OK) {
                max31850_err_t result = max31850_parse_data(data, &s_sensors[i]);
                if (result == MAX31850_OK) {
                    ESP_LOGI(TAG, "Sensor [%d] initial read: Temp=%.2f°C, CJ=%.2f°C",
                             i, s_sensors[i].thermocouple_temp, s_sensors[i].cold_junction_temp);
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    BaseType_t task_err = xTaskCreate(
        max31850_poll_task,
        "MAX31850_POLL",
        MAX31850_TASK_STACK_SIZE,
        NULL,
        MAX31850_TASK_PRIORITY,
        &s_poll_task_handle
    );
    
    if (task_err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    
    ESP_LOGI(TAG, "MAX31850 initialized successfully, found %d sensor(s)", s_sensor_count);
    
    return (s_sensor_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || temp == NULL) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (s_mutex == NULL) {
        return MAX31850_ERR_OFFLINE;
    }
    
    max31850_err_t result = MAX31850_ERR_OFFLINE;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_sensors[sensor_id].online) {
            result = MAX31850_ERR_OFFLINE;
        } else if (!s_sensors[sensor_id].data_valid) {
            result = MAX31850_ERR_TIMEOUT;
        } else {
            *temp = s_sensors[sensor_id].thermocouple_temp;
            result = s_sensors[sensor_id].last_error;
        }
        xSemaphoreGive(s_mutex);
    } else {
        result = MAX31850_ERR_TIMEOUT;
    }
    
    return result;
}

max31850_err_t max31850_get_data(uint8_t sensor_id, max31850_sensor_t *data)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || data == NULL) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (s_mutex == NULL) {
        return MAX31850_ERR_OFFLINE;
    }
    
    max31850_err_t result = MAX31850_ERR_OFFLINE;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &s_sensors[sensor_id], sizeof(max31850_sensor_t));
        result = s_sensors[sensor_id].online ? s_sensors[sensor_id].last_error : MAX31850_ERR_OFFLINE;
        xSemaphoreGive(s_mutex);
    } else {
        result = MAX31850_ERR_TIMEOUT;
    }
    
    return result;
}

max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp, TickType_t timeout)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || temp == NULL) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (!s_sensors[sensor_id].online) {
        return MAX31850_ERR_OFFLINE;
    }
    
    uint8_t data[9];
    TickType_t start_tick = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_tick) < timeout) {
        if (max31850_read_data_frame(s_sensors[sensor_id].rom_id, data) == ESP_OK) {
            max31850_sensor_t temp_sensor;
            memcpy(temp_sensor.rom_id, s_sensors[sensor_id].rom_id, 8);
            
            max31850_err_t result = max31850_parse_data(data, &temp_sensor);
            
            if (result == MAX31850_OK) {
                *temp = temp_sensor.thermocouple_temp;
                
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    s_sensors[sensor_id].thermocouple_temp = temp_sensor.thermocouple_temp;
                    s_sensors[sensor_id].cold_junction_temp = temp_sensor.cold_junction_temp;
                    s_sensors[sensor_id].fault_reg = temp_sensor.fault_reg;
                    memcpy(s_sensors[sensor_id].scratchpad, temp_sensor.scratchpad, 9);
                    s_sensors[sensor_id].data_valid = true;
                    s_sensors[sensor_id].last_error = MAX31850_OK;
                    xSemaphoreGive(s_mutex);
                }
                
                return MAX31850_OK;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return MAX31850_ERR_TIMEOUT;
}

bool max31850_is_online(uint8_t sensor_id)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return false;
    }
    
    return s_sensors[sensor_id].online;
}

void max31850_dump_scratchpad(uint8_t sensor_id)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        ESP_LOGW(TAG, "Invalid sensor ID: %d", sensor_id);
        return;
    }
    
    ESP_LOGI(TAG, "Sensor %d Scratchpad Dump:", sensor_id);
    ESP_LOGI(TAG, "  ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X",
             s_sensors[sensor_id].rom_id[0], s_sensors[sensor_id].rom_id[1],
             s_sensors[sensor_id].rom_id[2], s_sensors[sensor_id].rom_id[3],
             s_sensors[sensor_id].rom_id[4], s_sensors[sensor_id].rom_id[5],
             s_sensors[sensor_id].rom_id[6], s_sensors[sensor_id].rom_id[7]);
    
    for (int i = 0; i < 9; i++) {
        ESP_LOGI(TAG, "  [%d] 0x%02X", i, s_sensors[sensor_id].scratchpad[i]);
    }
    
    ESP_LOGI(TAG, "  Thermocouple: %.2f°C", s_sensors[sensor_id].thermocouple_temp);
    ESP_LOGI(TAG, "  Cold Junction: %.2f°C", s_sensors[sensor_id].cold_junction_temp);
    ESP_LOGI(TAG, "  Fault Reg: 0x%02X", s_sensors[sensor_id].fault_reg);
    ESP_LOGI(TAG, "  Online: %s", s_sensors[sensor_id].online ? "YES" : "NO");
    ESP_LOGI(TAG, "  Data Valid: %s", s_sensors[sensor_id].data_valid ? "YES" : "NO");
}

const char* max31850_err_to_string(max31850_err_t err)
{
    switch (err) {
        case MAX31850_OK:           return "OK";
        case MAX31850_ERR_OPEN:     return "Thermocouple Open";
        case MAX31850_ERR_SHORT_GND:return "Short to GND";
        case MAX31850_ERR_SHORT_VCC:return "Short to VCC";
        case MAX31850_ERR_CRC:      return "CRC Error";
        case MAX31850_ERR_TIMEOUT:  return "Timeout";
        case MAX31850_ERR_BUS_FAULT:return "Bus Fault";
        case MAX31850_ERR_OFFLINE:  return "Offline";
        case MAX31850_ERR_INVALID_ID:return "Invalid ID";
        case MAX31850_ERR_NOT_FOUND:return "Not Found";
        default:                    return "Unknown";
    }
}

void max31850_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    if (s_poll_task_handle != NULL) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }
    
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    gpio_reset_pin(s_gpio_num);
    s_gpio_num = GPIO_NUM_NC;
    
    s_initialized = false;
    s_sensor_count = 0;
    
    ESP_LOGI(TAG, "MAX31850 deinitialized");
}

//////////////////////////////////////////////////////////////
//////////////////////// 调试任务 ////////////////////////////
//////////////////////////////////////////////////////////////

// NOTE: heating_print_task() is implemented in main.c
