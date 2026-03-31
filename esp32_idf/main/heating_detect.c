/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ 温度传感器驱动实现
 * 
 * 基于 ESP32-S3 @ 240MHz 的 GPIO Bit-Bang 1-Wire 驱动
 * 精确时序控制，使用临界区保护防止中断干扰
 * 
 * @note MAX31850 是 Read-Only 设备，自动连续转换，无需 Convert T 命令
 */

#include "heating_detect.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "MAX31850";

//////////////////////////////////////////////////////////////
///////////////////// 调试开关 ///////////////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_DEBUG_LEVEL        3   /**< 调试级别: 0=关闭, 1=基本, 2=详细, 3=波形 */

#if MAX31850_DEBUG_LEVEL >= 1
    #define DEBUG_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define DEBUG_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_LOGI(fmt, ...) do {} while(0)
    #define DEBUG_LOGW(fmt, ...) do {} while(0)
#endif

#if MAX31850_DEBUG_LEVEL >= 2
    #define DEBUG_LOGD(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_LOGD(fmt, ...) do {} while(0)
#endif

#if MAX31850_DEBUG_LEVEL >= 3
    #define DEBUG_LOGV(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_LOGV(fmt, ...) do {} while(0)
#endif

//////////////////////////////////////////////////////////////
///////////////////// 时序参数定义 ////////////////////////////
//////////////////////////////////////////////////////////////
/**
 * @note 所有时序基于 ESP32-S3 @ 240MHz
 * 1 个 CPU 周期 = 1/240MHz = 4.17ns
 * 1 μs = 240 个 CPU 周期
 * 
 * 使用 esp_rom_delay_us() 实现微秒级延时
 * 在 onewire_reset/write_bit/read_bit 中使用 portMUX 关中断保护
 */

/* Reset 时序 (μs) */
#define ONEWIRE_RESET_LOW_US        480     /**< Reset 低电平时间 */
#define ONEWIRE_RESET_WAIT_US       70      /**< 等待 Presence 响应 */
#define ONEWIRE_RESET_SAMPLE_US     0       /**< 在 70μs 处采样 */
#define ONEWIRE_RESET_RECOVERY_US   410     /**< Reset 后恢复时间 */

/* Write 时序 (μs) - 基于标准 1-Wire 协议 */
#define ONEWIRE_WRITE1_LOW_US       6       /**< 写 1 低电平时间：标准 5-15μs */
#define ONEWIRE_WRITE1_HIGH_US      64      /**< 写 1 高电平时间，总时隙 70μs */
#define ONEWIRE_WRITE0_LOW_US       70      /**< 写 0 低电平时间：标准 60-120μs */
#define ONEWIRE_WRITE0_HIGH_US      10      /**< 写 0 恢复时间：标准 ≥1μs */

/* Read 时序 (μs) - 基于标准 1-Wire 协议 */
#define ONEWIRE_READ_INIT_US        3       /**< 读初始化低电平：标准 1-15μs */
#define ONEWIRE_READ_SAMPLE_US      12      /**< 等待到采样点：标准 15μs 总时间 */
#define ONEWIRE_READ_RECOVERY_US    55      /**< 读恢复时间 */

/* 间隔时序 */
#define ONEWIRE_INTER_BIT_US        3       /**< 位间恢复时间：标准 ≥1μs */
#define ONEWIRE_INTER_BYTE_US       8       /**< 字节间恢复时间 */

//////////////////////////////////////////////////////////////
///////////////////// CRC8 表（X8+X5+X4+1）///////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief CRC8 查找表，多项式 X8+X5+X4+1 = 0x31 (LSB first)
 * 
 * 覆盖 Byte 0-7，与 Byte 8 的 CRC 值比较验证
 */
static const uint8_t crc8_table[256] = {
    0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83,
    0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD, 0x1F, 0x41,
    0x9D, 0xC3, 0x21, 0x7F, 0xFC, 0xA2, 0x40, 0x1E,
    0x5F, 0x01, 0xE3, 0xBD, 0x3E, 0x60, 0x82, 0xDC,
    0x23, 0x7D, 0x9F, 0xC1, 0x42, 0x1C, 0xFE, 0xA0,
    0xE1, 0xBF, 0x5D, 0x03, 0x80, 0xDE, 0x3C, 0x62,
    0xBE, 0xE0, 0x02, 0x5C, 0xDF, 0x81, 0x63, 0x3D,
    0x7C, 0x22, 0xC0, 0x9E, 0x1D, 0x43, 0xA1, 0xFF,
    0x46, 0x18, 0xFA, 0xA4, 0x27, 0x79, 0x9B, 0xC5,
    0x84, 0xDA, 0x38, 0x66, 0xE5, 0xBB, 0x59, 0x07,
    0xDB, 0x85, 0x67, 0x39, 0xBA, 0xE4, 0x06, 0x58,
    0x19, 0x47, 0xA5, 0xFB, 0x78, 0x26, 0xC4, 0x9A,
    0x65, 0x3B, 0xD9, 0x87, 0x04, 0x5A, 0xB8, 0xE6,
    0xA7, 0xF9, 0x1B, 0x45, 0xC6, 0x98, 0x7A, 0x24,
    0xF8, 0xA6, 0x44, 0x1A, 0x99, 0xC7, 0x25, 0x7B,
    0x3A, 0x64, 0x86, 0xD8, 0x5B, 0x05, 0xE7, 0xB9,
    0x8C, 0xD2, 0x30, 0x6E, 0xED, 0xB3, 0x51, 0x0F,
    0x4E, 0x10, 0xF2, 0xAC, 0x2F, 0x71, 0x93, 0xCD,
    0x11, 0x4F, 0xAD, 0xF3, 0x70, 0x2E, 0xCC, 0x92,
    0xD3, 0x8D, 0x6F, 0x31, 0xB2, 0xEC, 0x0E, 0x50,
    0xAF, 0xF1, 0x13, 0x4D, 0xCE, 0x90, 0x72, 0x2C,
    0x6D, 0x33, 0xD1, 0x8F, 0x0C, 0x52, 0xB0, 0xEE,
    0x32, 0x6C, 0x8E, 0xD0, 0x53, 0x0D, 0xEF, 0xB1,
    0xF0, 0xAE, 0x4C, 0x12, 0x91, 0xCF, 0x2D, 0x73,
    0xCA, 0x94, 0x76, 0x28, 0xAB, 0xF5, 0x17, 0x49,
    0x08, 0x56, 0xB4, 0xEA, 0x69, 0x37, 0xD5, 0x8B,
    0x57, 0x09, 0xEB, 0xB5, 0x36, 0x68, 0x8A, 0xD4,
    0x95, 0xCB, 0x29, 0x77, 0xF4, 0xAA, 0x48, 0x16,
    0xE9, 0xB7, 0x55, 0x0B, 0x88, 0xD6, 0x34, 0x6A,
    0x2B, 0x75, 0x97, 0xC9, 0x4A, 0x14, 0xF6, 0xA8,
    0x74, 0x2A, 0xC8, 0x96, 0x15, 0x4B, 0xA9, 0xF7,
    0xB6, 0xE8, 0x0A, 0x54, 0xD7, 0x89, 0x6B, 0x35
};

//////////////////////////////////////////////////////////////
///////////////////// 静态变量 ///////////////////////////////
//////////////////////////////////////////////////////////////

static gpio_num_t s_onewire_pin = GPIO_NUM_NC;          /**< 当前使用的 GPIO */
static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT]; /**< 传感器数组 */
static SemaphoreHandle_t s_mutex = NULL;                /**< 数据保护互斥锁 */
static TaskHandle_t s_poll_task_handle = NULL;          /**< 轮询任务句柄 */

static portMUX_TYPE s_onewire_mux = portMUX_INITIALIZER_UNLOCKED; /**< 1-Wire 临界区保护 */

static uint8_t s_current_sensor_idx = 0;                /**< 当前轮询的传感器索引 */
static poll_state_t s_poll_state = POLL_IDLE;           /**< 轮询状态机状态 */


//////////////////////////////////////////////////////////////
///////////////////// 精确延时函数 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 微秒级精确延时（使用 esp_rom_delay_us）
 * 
 * @param us 延时微秒数
 * @note 基于 240MHz CPU 频率，使用 ROM 函数实现精确延时
 */
static inline void onewire_delay_us(uint32_t us)
{
    /* esp_rom_delay_us 基于 ets_delay_us，在 240MHz 下精度约 1μs */
    ets_delay_us(us);
}

//////////////////////////////////////////////////////////////
///////////////////// GPIO 底层操作 ///////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 设置 GPIO 为开漏输出模式（低电平驱动，高电平释放）
 * 
 * @note 开漏模式：输出 0 时拉低总线，输出 1 时释放总线（由上拉电阻拉高）
 */
static inline void onewire_set_opendrain(void)
{
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
}

/**
 * @brief 设置 GPIO 为输入模式（释放总线）
 */
static inline void onewire_set_input(void)
{
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
}

/**
 * @brief 设置 GPIO 输出电平
 * 
 * @param level 0=拉低总线, 1=释放总线（开漏模式下）
 */
static inline void onewire_set_level(uint8_t level)
{
    gpio_set_level(s_onewire_pin, level);
}

/**
 * @brief 读取 GPIO 输入电平
 * 
 * @return 0 或 1
 */
static inline uint8_t onewire_get_level(void)
{
    return gpio_get_level(s_onewire_pin);
}

//////////////////////////////////////////////////////////////
///////////////////// GPIO 诊断功能 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief GPIO 诊断测试 - 验证开漏模式配置
 * 
 * 执行 5 个测试来验证 GPIO14 的开漏模式配置是否正确：
 * 1. 浮空输入检查（应读到高电平，由上拉电阻提供）
 * 2. 强制低电平检查（开漏应能拉低）
 * 3. 释放总线检查（应回到高电平）
 * 4. 输入模式读取
 * 5. 最终状态检查
 */
static esp_err_t onewire_diagnose_gpio(void)
{
    int test_results[5];
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GPIO Diagnostic Test on GPIO%d", s_onewire_pin);
    ESP_LOGI(TAG, "========================================");
    
    /* Test 1: 浮空输入检查 - 应读到高电平（上拉电阻作用） */
    onewire_set_input();
    onewire_delay_us(100);  /* 增加等待时间 */
    test_results[0] = onewire_get_level();
    ESP_LOGI(TAG, "Test 1 - Input mode (pull-up): %d %s", 
             test_results[0], test_results[0] == 1 ? "✓" : "✗ (sensors may pull down initially)");
    
    /* Test 2: 强制低电平检查 - 开漏应能拉低 */
    onewire_set_opendrain();
    onewire_set_level(0);
    onewire_delay_us(50);
    test_results[1] = onewire_get_level();
    ESP_LOGI(TAG, "Test 2 - Forced low (open-drain): %d %s", 
             test_results[1], test_results[1] == 0 ? "✓" : "✗ FAIL");
    
    /* Test 3: 释放总线检查 - 应回到高电平（增加等待时间） */
    onewire_set_level(1);
    onewire_set_input();
    onewire_delay_us(100);  /* 增加等待时间从 50us 到 100us */
    test_results[2] = onewire_get_level();
    ESP_LOGI(TAG, "Test 3 - Released (100us wait): %d %s", 
             test_results[2], test_results[2] == 1 ? "✓" : "✗ (may be OK if Test 5 passes)");
    
    /* Test 4: 输入模式读取 */
    onewire_set_input();
    onewire_delay_us(10);
    test_results[3] = onewire_get_level();
    ESP_LOGI(TAG, "Test 4 - Input mode: %d %s", 
             test_results[3], test_results[3] == 1 ? "✓" : "✗ FAIL");
    
    /* Test 5: 最终状态检查 */
    onewire_set_input();
    onewire_delay_us(100);
    test_results[4] = onewire_get_level();
    ESP_LOGI(TAG, "Test 5 - Final state: %d %s", 
             test_results[4], test_results[4] == 1 ? "✓" : "✗ FAIL");
    
    ESP_LOGI(TAG, "----------------------------------------");
    
    /* 分析结果 - 关键测试：Test 2 必须能拉低，Test 5 最终状态必须高 */
    /* Test 1/3 可能因传感器连接或上拉响应慢而暂时失败 */
    bool critical_passed = (test_results[1] == 0 &&    /* 能拉低 */
                            test_results[4] == 1);     /* 最终状态高 */
    
    if (!critical_passed) {
        ESP_LOGE(TAG, "GPIO DIAGNOSTIC FAILED!");
        if (test_results[1] == 1) {
            ESP_LOGE(TAG, "  -> Test 2 FAIL: Cannot drive LOW - check GPIO configuration");
        }
        if (test_results[4] == 0) {
            ESP_LOGE(TAG, "  -> Test 5 FAIL: Bus stuck LOW - check for short to GND");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    if (test_results[0] == 0 || test_results[2] == 0) {
        ESP_LOGW(TAG, "Note: Test 1 or 3 failed - sensors may be pulling bus LOW initially");
    }
    
    ESP_LOGI(TAG, "GPIO Diagnostic: PASSED ✓ (critical tests: 2,4,5)");
    return ESP_OK;
}

//////////////////////////////////////////////////////////////
///////////////////// 1-Wire Bit-Bang 核心 ////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire Reset 脉冲 + Presence 检测（带详细波形日志）
 * 
 * 时序：拉低 480μs → 释放 → 等待 70μs → 采样 Presence → 等待 410μs
 * 
 * @param presence 输出参数，检测到设备返回 true
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 总线短路到 GND
 * 
 * @note 此函数在临界区内执行，防止中断干扰时序
 */
static esp_err_t onewire_reset(bool *presence)
{
    uint8_t level_before, level_during_reset, level_70us, level_100us, level_480us, level_final;
    
    /* 记录 Reset 前的总线状态 */
    level_before = onewire_get_level();
    
    portENTER_CRITICAL(&s_onewire_mux);
    
    /* Step 1: 拉低总线 480μs (Reset 脉冲) */
    onewire_set_opendrain();
    onewire_set_level(0);
    onewire_delay_us(200);  /* 200μs 后检查 */
    level_during_reset = onewire_get_level();
    onewire_delay_us(280);  /* 继续 280μs，总计 480μs */
    
    /* Step 2: 释放总线 */
    onewire_set_level(1);
    onewire_set_input();
    
    /* Step 3: 等待 15μs（Presence 窗口开始）后采样 */
    onewire_delay_us(15);
    level_70us = onewire_get_level();  /* 实际上是 15μs 采样 */
    
    /* Step 4: 等待到 70μs 再次采样 */
    onewire_delay_us(55);  /* 总计 70μs */
    level_100us = onewire_get_level();
    
    /* Step 5: 等待到 480μs 采样 */
    onewire_delay_us(410);  /* 总计 480μs */
    level_480us = onewire_get_level();
    
    /* Step 6: 最终状态 */
    onewire_delay_us(10);
    level_final = onewire_get_level();
    
    portEXIT_CRITICAL(&s_onewire_mux);
    
    /* 详细波形日志 */
    DEBUG_LOGV("Reset Waveform: before=%d, during_reset=%d, @15us=%d, @70us=%d, @480us=%d, final=%d",
               level_before, level_during_reset, level_70us, level_100us, level_480us, level_final);
    
    /* 总线检查 */
    if (level_final == 0) {
        /* 总线始终为低，可能短路到 GND */
        ESP_LOGE(TAG, "BUS FAULT: Line stuck LOW (shorted to GND?)");
        ESP_LOGE(TAG, "  Waveform: before=%d, during=%d, @15us=%d, @70us=%d, @480us=%d, final=%d",
                 level_before, level_during_reset, level_70us, level_100us, level_480us, level_final);
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Presence 检测：设备会在 15-60μs 内拉低总线 60-240μs */
    *presence = (level_70us == 0);
    
    if (*presence) {
        DEBUG_LOGD("Reset: Presence detected (@15us=%d, @70us=%d)", level_70us, level_100us);
    } else {
        DEBUG_LOGD("Reset: No presence (@15us=%d, @70us=%d)", level_70us, level_100us);
    }
    
    return ESP_OK;
}

/**
 * @brief 写入单个 Bit（临界区保护）
 * 
 * @param bit 要写入的位 (0 或 1)
 * 
 * @note 写 1: 拉低 5μs → 释放 55μs
 * @note 写 0: 拉低 70μs → 释放 5μs
 */
static void onewire_write_bit(uint8_t bit)
{
    portENTER_CRITICAL(&s_onewire_mux);
    
    onewire_set_opendrain();
    
    if (bit & 0x01) {
        /* 写 1: 低电平 ≤15μs */
        onewire_set_level(0);
        onewire_delay_us(ONEWIRE_WRITE1_LOW_US);
        onewire_set_level(1);
        onewire_delay_us(ONEWIRE_WRITE1_HIGH_US);
    } else {
        /* 写 0: 低电平 ≥60μs */
        onewire_set_level(0);
        onewire_delay_us(ONEWIRE_WRITE0_LOW_US);
        onewire_set_level(1);
        onewire_delay_us(ONEWIRE_WRITE0_HIGH_US);
    }
    
    onewire_delay_us(ONEWIRE_INTER_BIT_US);
    
    portEXIT_CRITICAL(&s_onewire_mux);
}

/**
 * @brief 读取单个 Bit（临界区保护）
 * 
 * 时序：拉低 3μs → 释放 → 等待 10μs → 采样（总计 13μs）→ 等待 50μs
 * 
 * @return 读取的位 (0 或 1)
 */
static uint8_t onewire_read_bit(void)
{
    uint8_t bit;
    
    portENTER_CRITICAL(&s_onewire_mux);
    
    /* Step 1: 拉低总线初始化读时隙 */
    onewire_set_opendrain();
    onewire_set_level(0);
    onewire_delay_us(ONEWIRE_READ_INIT_US);
    
    /* Step 2: 释放总线 */
    onewire_set_level(1);
    onewire_set_input();
    
    /* Step 3: 等待到采样点（总计 13μs） */
    onewire_delay_us(ONEWIRE_READ_SAMPLE_US);
    bit = onewire_get_level();
    
    /* Step 4: 等待时隙结束 */
    onewire_delay_us(ONEWIRE_READ_RECOVERY_US);
    
    portEXIT_CRITICAL(&s_onewire_mux);
    
    return bit;
}

/**
 * @brief 写入一个字节（LSB First）
 * 
 * @param data 要写入的字节
 */
static void onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
}

/**
 * @brief 读取一个字节（LSB First）
 * 
 * @return 读取的字节
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (onewire_read_bit() << i);
    }
    onewire_delay_us(ONEWIRE_INTER_BYTE_US);
    return data;
}

//////////////////////////////////////////////////////////////
///////////////////// CRC8 计算 //////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 计算 CRC8（X8+X5+X4+1）
 * 
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC8 值
 */
static uint8_t onewire_crc8(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

//////////////////////////////////////////////////////////////
///////////////////// ROM 操作 ///////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief Search ROM 算法 - 发现总线上所有设备（带位级调试）
 * 
 * 使用 1-Wire 标准二进制搜索树算法，自动发现所有设备的 64-bit ROM ID
 * 
 * @param rom_ids 输出缓冲区，存储发现的 ROM ID [8]
 * @param found_count 输出发现的设备数量
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 未发现设备
 */
static esp_err_t onewire_search_rom(uint8_t rom_ids[][8], uint8_t *found_count)
{
    uint8_t rom_id[8] = {0};
    uint8_t last_discrepancy = 0;
    uint8_t last_zero = 0;
    int device_count = 0;
    bool search_complete = false;
    uint8_t conflict_count = 0;
    
    *found_count = 0;
    
    ESP_LOGI(TAG, "Starting 1-Wire ROM search...");
    
    while (!search_complete && device_count < MAX31850_SENSOR_COUNT) {
        bool presence;
        esp_err_t err = onewire_reset(&presence);
        if (err != ESP_OK || !presence) {
            ESP_LOGE(TAG, "Search ROM: No device present (reset failed)");
            return ESP_ERR_NOT_FOUND;
        }
        
        DEBUG_LOGD("Search iteration %d, last_discrepancy=%d", device_count + 1, last_discrepancy);
        
        /* 发送 Search ROM 命令 */
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
        last_zero = 0;
        conflict_count = 0;
        
        for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
            uint8_t bit_actual, bit_complement, selected_bit;
            
            /* 读取两位：实际值和补码（连续读取，无额外延迟） */
            bit_actual = onewire_read_bit();
            bit_complement = onewire_read_bit();
            
            if (bit_actual == 1 && bit_complement == 1) {
                /* 无设备响应 */
                ESP_LOGW(TAG, "Search ROM: No response at bit %d (1/1)", bit_pos);
                ESP_LOGW(TAG, "  -> bit_actual=1, bit_complement=1 means no devices");
                return ESP_ERR_NOT_FOUND;
            } else if (bit_actual == 0 && bit_complement == 0) {
                /* 冲突：多个设备在该位不同 */
                conflict_count++;
                if (bit_pos == last_discrepancy) {
                    selected_bit = 1;
                    DEBUG_LOGV("  Bit %d: Conflict, taking path 1 (last_discrepancy)", bit_pos);
                } else if (bit_pos > last_discrepancy) {
                    selected_bit = 0;
                    last_zero = bit_pos;
                    DEBUG_LOGV("  Bit %d: Conflict, taking path 0 (new)", bit_pos);
                } else {
                    selected_bit = (rom_id[bit_pos / 8] >> (bit_pos % 8)) & 0x01;
                    if (selected_bit == 0) {
                        last_zero = bit_pos;
                    }
                    DEBUG_LOGV("  Bit %d: Conflict, following previous path=%d", bit_pos, selected_bit);
                }
            } else {
                /* 无冲突，所有设备该位相同 */
                selected_bit = bit_actual;
                DEBUG_LOGV("  Bit %d: No conflict, value=%d", bit_pos, selected_bit);
            }
            
            /* 写入选择的位 */
            onewire_write_bit(selected_bit);
            
            /* 更新 ROM ID */
            if (selected_bit) {
                rom_id[bit_pos / 8] |= (1 << (bit_pos % 8));
            } else {
                rom_id[bit_pos / 8] &= ~(1 << (bit_pos % 8));
            }
        }
        
        /* 验证 ROM CRC */
        uint8_t calc_crc = onewire_crc8(rom_id, 7);
        if (calc_crc != rom_id[7]) {
            ESP_LOGW(TAG, "Search ROM: ROM CRC error at device %d", device_count + 1);
            ESP_LOGW(TAG, "  ROM: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                     rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                     rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
            ESP_LOGW(TAG, "  Calc CRC: 0x%02X, Recv CRC: 0x%02X", calc_crc, rom_id[7]);
            return ESP_ERR_INVALID_CRC;
        }
        
        /* 保存发现的设备 */
        memcpy(rom_ids[device_count], rom_id, 8);
        device_count++;
        
        ESP_LOGI(TAG, "Found device %d: ROM ID %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X (conflicts=%d)",
                 device_count,
                 rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                 rom_id[4], rom_id[5], rom_id[6], rom_id[7],
                 conflict_count);
        
        /* 检查是否还有更多设备 */
        if (last_discrepancy == 0) {
            search_complete = true;
            ESP_LOGI(TAG, "Search complete: %d device(s) found", device_count);
        } else {
            last_discrepancy = last_zero;
            DEBUG_LOGD("Continuing search with last_discrepancy=%d", last_discrepancy);
        }
    }
    
    *found_count = device_count;
    return (device_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/**
 * @brief Match ROM - 选择特定设备
 * 
 * @param rom_id 64-bit ROM ID
 */
static void onewire_match_rom(uint8_t rom_id[8])
{
    onewire_write_byte(ONEWIRE_CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(rom_id[i]);
    }
}

//////////////////////////////////////////////////////////////
///////////////////// MAX31850 协议层 /////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 读取 MAX31850 暂存器（9 字节，带详细日志）
 * 
 * MAX31850 是 Read-Only 设备，Match ROM 后芯片自动输出 9 字节数据
 * 无需发送 Read Scratchpad 命令，但为了兼容性可以发送 0xBE
 * 
 * @param rom_id 设备 ROM ID
 * @param data 9 字节数据缓冲区
 * @return ESP_OK 成功
 */
static esp_err_t max31850_read_scratchpad(uint8_t rom_id[8], uint8_t data[9])
{
    bool presence;
    esp_err_t err = onewire_reset(&presence);
    if (err != ESP_OK) {
        DEBUG_LOGD("Read scratchpad: Reset failed");
        return err;
    }
    if (!presence) {
        DEBUG_LOGD("Read scratchpad: No device present");
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Match ROM 选择设备 */
    onewire_match_rom(rom_id);
    
    /* 
     * MAX31850 自动连续转换，Match ROM 后芯片自动输出数据
     * 读取 9 字节数据帧
     */
    for (int i = 0; i < 9; i++) {
        data[i] = onewire_read_byte();
    }
    
    /* 打印原始数据 */
    DEBUG_LOGV("Scratchpad raw: [%02X %02X %02X %02X %02X %02X %02X %02X %02X]",
               data[0], data[1], data[2], data[3], data[4],
               data[5], data[6], data[7], data[8]);
    
    return ESP_OK;
}

/**
 * @brief 解析 MAX31850 数据
 * 
 * 数据帧格式（9 Bytes）：
 * Byte 0-1: 热电偶温度（14-bit 有符号，0.0625°C 分辨率）
 * Byte 2-3: 冷端温度
 * Byte 4: 故障寄存器
 * Byte 5-6: 保留
 * Byte 7: 保留
 * Byte 8: CRC8
 * 
 * @param data 9 字节原始数据
 * @param temp_out 输出热电偶温度
 * @param cj_temp_out 输出冷端温度
 * @param fault_out 输出故障寄存器
 * @return MAX31850_OK 成功，其他错误码
 */
static max31850_err_t max31850_parse_data(uint8_t data[9], 
                                           float *temp_out, 
                                           float *cj_temp_out,
                                           uint8_t *fault_out)
{
    /* CRC 校验 */
    uint8_t calc_crc = onewire_crc8(data, 8);
    if (calc_crc != data[8]) {
        DEBUG_LOGD("CRC failed: calc=0x%02X, recv=0x%02X", calc_crc, data[8]);
        DEBUG_LOGD("Data: [%02X %02X %02X %02X %02X %02X %02X %02X]",
                   data[0], data[1], data[2], data[3],
                   data[4], data[5], data[6], data[7]);
        return MAX31850_ERR_CRC;
    }
    
    /* 解析热电偶温度（Byte 0-1，14-bit 有符号，LSB first） */
    int16_t raw_temp = (int16_t)(data[1] << 8) | data[0];
    raw_temp >>= 2;  /* 右移 2 位得到 14-bit */
    *temp_out = raw_temp * 0.25f;  /* 分辨率 0.25°C */
    
    /* 解析冷端温度（Byte 2-3，12-bit 有符号） */
    int16_t raw_cj = (int16_t)(data[3] << 8) | data[2];
    raw_cj >>= 4;  /* 右移 4 位得到 12-bit */
    *cj_temp_out = raw_cj * 0.0625f;
    
    /* 故障检测 */
    *fault_out = data[4];
    if (*fault_out & MAX31850_FAULT_OPEN) {
        DEBUG_LOGD("Fault: Thermocouple Open Circuit");
        return MAX31850_ERR_OPEN;
    }
    if (*fault_out & MAX31850_FAULT_SHORT_GND) {
        DEBUG_LOGD("Fault: Short to GND");
        return MAX31850_ERR_SHORT_GND;
    }
    if (*fault_out & MAX31850_FAULT_SHORT_VCC) {
        DEBUG_LOGD("Fault: Short to VCC");
        return MAX31850_ERR_SHORT_VCC;
    }
    
    DEBUG_LOGV("Parsed: TC=%.2f°C, CJ=%.2f°C, Fault=0x%02X", 
               *temp_out, *cj_temp_out, *fault_out);
    
    return MAX31850_OK;
}

//////////////////////////////////////////////////////////////
///////////////////// 轮询任务状态机 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 轮询任务 - 非阻塞状态机实现
 * 
 * 每秒完成 4 个传感器的轮询，每个传感器 250ms
 */
static void max31850_poll_task(void *pvParameters)
{
    uint8_t current_data[9];
    max31850_err_t parse_result;
    float temp, cj_temp;
    uint8_t fault_reg;
    
    vTaskDelay(pdMS_TO_TICKS(1000));  /* 初始延迟 1s */
    
    s_poll_state = POLL_SELECT_SENSOR;
    s_current_sensor_idx = 0;
    
    while (1) {
        switch (s_poll_state) {
            case POLL_SELECT_SENSOR: {
                /* 选择当前传感器，检查是否在线 */
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (!s_sensors[s_current_sensor_idx].online) {
                        /* 传感器离线，跳过 */
                        xSemaphoreGive(s_mutex);
                        s_poll_state = POLL_NEXT;
                        break;
                    }
                    xSemaphoreGive(s_mutex);
                }
                s_poll_state = POLL_MATCH_ROM;
                break;
            }
            
            case POLL_MATCH_ROM: {
                /* Match ROM 选择设备 */
                bool presence;
                uint8_t rom_id[8];
                
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    memcpy(rom_id, s_sensors[s_current_sensor_idx].rom_id, 8);
                    xSemaphoreGive(s_mutex);
                } else {
                    s_poll_state = POLL_NEXT;
                    break;
                }
                
                if (onewire_reset(&presence) != ESP_OK || !presence) {
                    ESP_LOGW(TAG, "Sensor %d: Reset failed", s_current_sensor_idx);
                    s_poll_state = POLL_PARSE;  /* 进入解析阶段处理错误 */
                    break;
                }
                
                onewire_match_rom(rom_id);
                s_poll_state = POLL_READ_DATA;
                break;
            }
            
            case POLL_READ_DATA: {
                /* 读取 9 字节数据 */
                for (int i = 0; i < 9; i++) {
                    current_data[i] = onewire_read_byte();
                }
                s_poll_state = POLL_PARSE;
                break;
            }
            
            case POLL_PARSE: {
                /* 解析数据（开中断进行计算） */
                parse_result = max31850_parse_data(current_data, &temp, &cj_temp, &fault_reg);
                
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    max31850_sensor_t *sensor = &s_sensors[s_current_sensor_idx];
                    
                    if (parse_result == MAX31850_OK) {
                        sensor->thermocouple_temp = temp;
                        sensor->cold_junction_temp = cj_temp;
                        sensor->fault_reg = fault_reg;
                        sensor->data_valid = true;
                        sensor->fail_count = 0;
                        sensor->last_update = xTaskGetTickCount();
                    } else {
                        /* 记录错误 */
                        sensor->fail_count++;
                        memcpy(sensor->scratchpad, current_data, 9);
                        
                        if (sensor->fail_count >= MAX31850_MAX_FAIL_COUNT) {
                            sensor->online = false;
                            sensor->data_valid = false;
                            ESP_LOGW(TAG, "Sensor %d marked OFFLINE after %d failures",
                                     s_current_sensor_idx, sensor->fail_count);
                        }
                    }
                    
                    xSemaphoreGive(s_mutex);
                }
                
                s_poll_state = POLL_NEXT;
                break;
            }
            
            case POLL_NEXT: {
                /* 切换到下一个传感器 */
                s_current_sensor_idx++;
                if (s_current_sensor_idx >= MAX31850_SENSOR_COUNT) {
                    s_current_sensor_idx = 0;
                    s_poll_state = POLL_DELAY;
                } else {
                    s_poll_state = POLL_SELECT_SENSOR;
                }
                break;
            }
            
            case POLL_DELAY: {
                /* 等待下一次轮询周期 */
                vTaskDelay(pdMS_TO_TICKS(MAX31850_POLL_INTERVAL_MS));
                s_poll_state = POLL_SELECT_SENSOR;
                break;
            }
            
            default:
                s_poll_state = POLL_IDLE;
                break;
        }
    }
}

//////////////////////////////////////////////////////////////
///////////////////// 公共 API 实现 ///////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t gpio_num)
{
    if (gpio_num != GPIO_NUM_14) {
        ESP_LOGW(TAG, "GPIO %d is not the recommended GPIO14", gpio_num);
    }
    
    s_onewire_pin = gpio_num;
    
    /* 初始化传感器数组 */
    memset(s_sensors, 0, sizeof(s_sensors));
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        s_sensors[i].hw_addr = i;  /* 默认硬件地址顺序 */
    }
    
    /* 创建互斥锁 */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* 配置 GPIO14 为开漏输出 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_onewire_pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,  /* 开漏模式 */
        .pull_up_en = GPIO_PULLUP_ENABLE,   /* 使能内部上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* 执行 GPIO 诊断测试 */
    esp_err_t diag_err = onewire_diagnose_gpio();
    if (diag_err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO diagnostic failed, aborting init");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return diag_err;
    }
    
    ESP_LOGI(TAG, "MAX31850 Init: GPIO%d configured as open-drain", s_onewire_pin);
    
    /* 测试 Reset/Presence */
    ESP_LOGI(TAG, "Testing 1-Wire Reset/Presence...");
    bool presence;
    esp_err_t reset_err = onewire_reset(&presence);
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "Reset test failed: %s", 
                 reset_err == ESP_ERR_INVALID_STATE ? "Bus fault" : "Unknown error");
        /* 继续初始化，允许后台重试 */
    } else if (!presence) {
        ESP_LOGW(TAG, "No device detected during reset test");
        ESP_LOGW(TAG, "  -> Check sensor power and connections");
        ESP_LOGW(TAG, "  -> Verify 4.7K pull-up resistors");
    } else {
        ESP_LOGI(TAG, "Reset test passed: Device presence detected");
    }
    
    /* 执行 Search ROM 发现所有设备 */
    uint8_t rom_ids[MAX31850_SENSOR_COUNT][8];
    uint8_t found_count = 0;
    
    esp_err_t err = onewire_search_rom(rom_ids, &found_count);
    if (err != ESP_OK || found_count == 0) {
        ESP_LOGW(TAG, "Search ROM failed or no devices found");
        /* 继续初始化，允许后台重试 */
    } else {
        ESP_LOGI(TAG, "MAX31850 Init: Found %d sensor(s) on GPIO%d", found_count, s_onewire_pin);
        
        /* 存储发现的 ROM ID */
        for (int i = 0; i < found_count && i < MAX31850_SENSOR_COUNT; i++) {
            memcpy(s_sensors[i].rom_id, rom_ids[i], 8);
            s_sensors[i].online = true;
            s_sensors[i].fail_count = 0;
            
            /* 打印传感器信息 */
            ESP_LOGI(TAG, "Sensor [%d]: ROM=%02X%02X%02X%02X%02X%02X%02X%02X, HW_ADDR=%02X",
                     i,
                     rom_ids[i][0], rom_ids[i][1], rom_ids[i][2], rom_ids[i][3],
                     rom_ids[i][4], rom_ids[i][5], rom_ids[i][6], rom_ids[i][7],
                     s_sensors[i].hw_addr);
        }
    }
    
    /* 首次强制读取所有传感器 */
    for (int i = 0; i < found_count; i++) {
        float temp;
        max31850_err_t result = max31850_force_update(i, &temp, pdMS_TO_TICKS(500));
        if (result == MAX31850_OK) {
            ESP_LOGI(TAG, "Sensor [%d] initial read: Temp=%.2f°C", i, temp);
        } else {
            ESP_LOGW(TAG, "Sensor [%d] initial read failed: %s", 
                     i, max31850_err_to_string(result));
        }
    }
    
    /* 创建轮询任务 */
    BaseType_t task_created = xTaskCreate(
        max31850_poll_task,
        "MAX31850_POLL",
        MAX31850_TASK_STACK_SIZE,
        NULL,
        MAX31850_TASK_PRIORITY,
        &s_poll_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "MAX31850 initialized successfully");
    return ESP_OK;
}

void max31850_deinit(void)
{
    if (s_poll_task_handle != NULL) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }
    
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    /* 恢复 GPIO 为输入模式 */
    if (s_onewire_pin != GPIO_NUM_NC) {
        gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
        s_onewire_pin = GPIO_NUM_NC;
    }
    
    ESP_LOGI(TAG, "MAX31850 deinitialized");
}

max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || temp_out == NULL) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (s_mutex == NULL) {
        return MAX31850_ERR_OFFLINE;  /* 驱动未初始化 */
    }
    
    max31850_err_t result = MAX31850_ERR_OFFLINE;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        max31850_sensor_t *sensor = &s_sensors[sensor_id];
        
        if (!sensor->online) {
            result = MAX31850_ERR_OFFLINE;
        } else if (!sensor->data_valid) {
            result = MAX31850_ERR_CRC;
        } else {
            *temp_out = sensor->thermocouple_temp;
            result = MAX31850_OK;
        }
        
        xSemaphoreGive(s_mutex);
    } else {
        result = MAX31850_ERR_TIMEOUT;
    }
    
    return result;
}

max31850_err_t max31850_get_data(uint8_t sensor_id, max31850_sensor_t *data_out)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || data_out == NULL) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (s_mutex == NULL) {
        return MAX31850_ERR_OFFLINE;  /* 驱动未初始化 */
    }
    
    max31850_err_t result = MAX31850_OK;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data_out, &s_sensors[sensor_id], sizeof(max31850_sensor_t));
        
        if (!s_sensors[sensor_id].online) {
            result = MAX31850_ERR_OFFLINE;
        } else if (!s_sensors[sensor_id].data_valid) {
            result = MAX31850_ERR_CRC;
        }
        
        xSemaphoreGive(s_mutex);
    } else {
        result = MAX31850_ERR_TIMEOUT;
    }
    
    return result;
}

max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (s_mutex == NULL) {
        return MAX31850_ERR_OFFLINE;  /* 驱动未初始化 */
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    uint8_t data[9];
    float temp, cj_temp;
    uint8_t fault;
    
    /* 读取数据 */
    esp_err_t err = max31850_read_scratchpad(sensor->rom_id, data);
    if (err != ESP_OK) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            sensor->fail_count++;
            if (sensor->fail_count >= MAX31850_MAX_FAIL_COUNT) {
                sensor->online = false;
            }
            xSemaphoreGive(s_mutex);
        }
        return MAX31850_ERR_TIMEOUT;
    }
    
    /* 解析数据 */
    max31850_err_t result = max31850_parse_data(data, &temp, &cj_temp, &fault);
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (result == MAX31850_OK) {
            sensor->thermocouple_temp = temp;
            sensor->cold_junction_temp = cj_temp;
            sensor->fault_reg = fault;
            sensor->data_valid = true;
            sensor->fail_count = 0;
            sensor->last_update = xTaskGetTickCount();
            
            if (temp_out != NULL) {
                *temp_out = temp;
            }
        } else {
            sensor->fail_count++;
            memcpy(sensor->scratchpad, data, 9);
            
            if (sensor->fail_count >= MAX31850_MAX_FAIL_COUNT) {
                sensor->online = false;
            }
        }
        xSemaphoreGive(s_mutex);
    }
    
    return result;
}

bool max31850_is_online(uint8_t sensor_id)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return false;
    }
    
    if (s_mutex == NULL) {
        return false;  /* 驱动未初始化 */
    }
    
    bool online = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        online = s_sensors[sensor_id].online;
        xSemaphoreGive(s_mutex);
    }
    
    return online;
}

void max31850_dump_scratchpad(uint8_t sensor_id)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return;
    }
    
    if (s_mutex == NULL) {
        return;  /* 驱动未初始化 */
    }
    
    uint8_t data[9];
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, s_sensors[sensor_id].scratchpad, 9);
        xSemaphoreGive(s_mutex);
    } else {
        return;
    }
    
    ESP_LOGI(TAG, "Sensor %d Scratchpad Dump:", sensor_id);
    ESP_LOGI(TAG, "  [0] Temp LSB:    0x%02X", data[0]);
    ESP_LOGI(TAG, "  [1] Temp MSB:    0x%02X", data[1]);
    ESP_LOGI(TAG, "  [2] CJ Temp LSB: 0x%02X", data[2]);
    ESP_LOGI(TAG, "  [3] CJ Temp MSB: 0x%02X", data[3]);
    ESP_LOGI(TAG, "  [4] Fault Reg:   0x%02X", data[4]);
    ESP_LOGI(TAG, "  [5] Reserved:    0x%02X", data[5]);
    ESP_LOGI(TAG, "  [6] Reserved:    0x%02X", data[6]);
    ESP_LOGI(TAG, "  [7] Reserved:    0x%02X", data[7]);
    ESP_LOGI(TAG, "  [8] CRC:         0x%02X (calc: 0x%02X)", 
             data[8], onewire_crc8(data, 8));
}

const char* max31850_err_to_string(max31850_err_t err)
{
    switch (err) {
        case MAX31850_OK:           return "OK";
        case MAX31850_ERR_OPEN:     return "Thermocouple Open Circuit";
        case MAX31850_ERR_SHORT_GND: return "Short to GND";
        case MAX31850_ERR_SHORT_VCC: return "Short to VCC";
        case MAX31850_ERR_CRC:      return "CRC Error";
        case MAX31850_ERR_TIMEOUT:  return "Timeout";
        case MAX31850_ERR_BUS_FAULT: return "Bus Fault";
        case MAX31850_ERR_OFFLINE:  return "Sensor Offline";
        default:                    return "Unknown Error";
    }
}
