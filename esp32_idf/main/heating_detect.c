/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ 1-Wire 温度传感器驱动实现
 * 
 * 支持4个MAX31850KATB+传感器并联在单一1-Wire总线上
 * 使用GPIO14作为1-Wire总线，Bit-Bang方式实现1-Wire协议
 * 
 * @author ESP32-S3 Motor Control IoT Project
 * @version 1.0.0
 */

#include "heating_detect.h"
#include "esp_rom_sys.h"
#include "esp_cpu.h"
#include "hal/cpu_hal.h"

//////////////////////////////////////////////////////////////
//////////////////////// MACROS //////////////////////////////
//////////////////////////////////////////////////////////////

/** @brief 日志标签 */
#define TAG "MAX31850"

/** @brief CRC8多项式 X8+X5+X4+1 (0x31) */
#define CRC8_POLYNOMIAL     0x31

/** @brief 1-Wire时序参数 (微秒) */
#define ONE_WIRE_RESET_TIME         480     // 复位脉冲最小480us
#define ONE_WIRE_RESET_WAIT         70      // 等待从机响应
#define ONE_WIRE_RESET_PRESENCE     240     // 检测presence脉冲
#define ONE_WIRE_WRITE0_LOW         60      // 写0低电平时间
#define ONE_WIRE_WRITE0_HIGH        10      // 写0恢复时间
#define ONE_WIRE_WRITE1_LOW         6       // 写1低电平时间 (1-15us)
#define ONE_WIRE_WRITE1_HIGH        64      // 写1恢复时间
#define ONE_WIRE_READ_LOW           6       // 读时隙低电平时间
#define ONE_WIRE_READ_SAMPLE        10      // 读采样延迟 (在读低后10us采样)
#define ONE_WIRE_READ_HIGH          55      // 读时隙恢复时间
#define ONE_WIRE_SLOT_MIN           60      // 最小时隙

/** @brief MAX31850家族码 */
#define MAX31850_FAMILY_CODE        0x3B

/** @brief 临界区宏定义 */
#define ONE_WIRE_CRITICAL_ENTER()   portENTER_CRITICAL(&g_spinlock)
#define ONE_WIRE_CRITICAL_EXIT()    portEXIT_CRITICAL(&g_spinlock)

//////////////////////////////////////////////////////////////
//////////////////////// GLOBALS /////////////////////////////
//////////////////////////////////////////////////////////////

/** @brief 驱动全局实例 */
static max31850_driver_t g_driver = {0};

/** @brief 自旋锁用于临界区保护 */
static portMUX_TYPE g_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** @brief CRC8查找表 (CRC8-MAXIM/Dallas, 多项式0x31) */
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

//////////////////////////////////////////////////////////////
//////////////////////// DEBUG MACROS ////////////////////////
//////////////////////////////////////////////////////////////

#if MAX31850_DEBUG_ENABLE
    #define MAX31850_LOGD(fmt, ...)     ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
    #define MAX31850_LOGI(fmt, ...)     ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define MAX31850_LOGW(fmt, ...)     ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
    #define MAX31850_LOGE(fmt, ...)     ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
    #define MAX31850_LOGD(fmt, ...)     ((void)0)
    #define MAX31850_LOGI(fmt, ...)     ((void)0)
    #define MAX31850_LOGW(fmt, ...)     ((void)0)
    #define MAX31850_LOGE(fmt, ...)     ((void)0)
#endif

//////////////////////////////////////////////////////////////
//////////////////////// GPIO DIAGNOSTICS ////////////////////
//////////////////////////////////////////////////////////////

#if MAX31850_DEBUG_GPIO
/**
 * @brief GPIO诊断信息结构
 */
typedef struct {
    gpio_mode_t mode;
    gpio_pull_mode_t pull_up;
    gpio_pull_mode_t pull_down;
    int level;
} gpio_diag_info_t;

/**
 * @brief 获取GPIO诊断信息
 */
static void gpio_get_diag_info(gpio_num_t gpio, gpio_diag_info_t *info)
{
    gpio_get_drive_capability(gpio, NULL);  // 仅用于检查GPIO是否有效
    info->mode = GPIO_MODE_INPUT_OUTPUT_OD; // 我们使用的模式
    info->pull_up = GPIO_PULLUP_ENABLE;
    info->pull_down = GPIO_PULLDOWN_DISABLE;
    info->level = gpio_get_level(gpio);
}

/**
 * @brief 打印GPIO诊断信息
 */
static void gpio_print_diag(gpio_num_t gpio)
{
    gpio_diag_info_t info;
    gpio_get_diag_info(gpio, &info);
    
    ESP_LOGI(TAG, "=== GPIO%d Diagnostic ===", gpio);
    ESP_LOGI(TAG, "  Mode: INPUT_OUTPUT_OD (Open-Drain)");
    ESP_LOGI(TAG, "  Pull-up: ENABLED (4.7K external recommended)");
    ESP_LOGI(TAG, "  Pull-down: DISABLED");
    ESP_LOGI(TAG, "  Current Level: %d", info.level);
    ESP_LOGI(TAG, "  Expected idle level: 1 (pulled high)");
    
    // 检查总线状态
    if (info.level == 0) {
        ESP_LOGW(TAG, "  WARNING: Bus is LOW, possible short or missing pull-up!");
    } else {
        ESP_LOGI(TAG, "  Bus state: OK (high)");
    }
}

/**
 * @brief 测试开漏模式
 * 
 * 验证GPIO正确配置为开漏模式：
 * 1. 设置为输出0，检查总线是否为低
 * 2. 设置为输出1（释放），检查总线是否被拉高
 */
static bool gpio_test_open_drain(gpio_num_t gpio)
{
    ESP_LOGI(TAG, "=== Testing Open-Drain Mode on GPIO%d ===", gpio);
    
    // 测试1: 拉低总线
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(100);
    int level_low = gpio_get_level(gpio);
    ESP_LOGI(TAG, "  Drive LOW: %d (expected 0)", level_low);
    
    // 测试2: 释放总线
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(100);
    int level_high = gpio_get_level(gpio);
    ESP_LOGI(TAG, "  Release (pull-up): %d (expected 1)", level_high);
    
    bool test_pass = (level_low == 0) && (level_high == 1);
    ESP_LOGI(TAG, "  Open-drain test: %s", test_pass ? "PASS" : "FAIL");
    
    return test_pass;
}
#endif

//////////////////////////////////////////////////////////////
//////////////////////// BUS LEVEL CHECK /////////////////////
//////////////////////////////////////////////////////////////

#if MAX31850_DEBUG_BUS_LEVEL && MAX31850_DEBUG_ENABLE
/**
 * @brief 检查总线电平
 * 
 * @param expected 期望电平 (0或1)
 * @param timeout_us 超时时间(微秒)
 * @return true 电平符合预期，false 超时或电平不符
 */
static bool check_bus_level(uint8_t expected, uint32_t timeout_us)
{
    uint32_t start = esp_cpu_get_cycle_count();
    // ESP32-S3 CPU频率为240MHz
    uint32_t cpu_freq_mhz = 240;
    uint32_t timeout_cycles = timeout_us * cpu_freq_mhz;
    
    while ((esp_cpu_get_cycle_count() - start) < timeout_cycles) {
        if (gpio_get_level(g_driver.gpio_num) == expected) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 打印总线电平采样
 * 
 * 在指定时间内多次采样总线电平，用于波形分析
 */
static void print_bus_waveform(const char *label, uint32_t sample_count, uint32_t interval_us)
{
    ESP_LOGI(TAG, "=== Bus Waveform: %s ===", label);
    
    // 分配缓冲区存储采样数据
    uint8_t *samples = malloc(sample_count);
    if (!samples) {
        ESP_LOGE(TAG, "Failed to allocate waveform buffer");
        return;
    }
    
    // 采样总线电平
    for (uint32_t i = 0; i < sample_count; i++) {
        samples[i] = gpio_get_level(g_driver.gpio_num);
        esp_rom_delay_us(interval_us);
    }
    
    // 打印波形（每80个字符一行）
    char line[81];
    uint32_t line_idx = 0;
    for (uint32_t i = 0; i < sample_count; i++) {
        line[line_idx++] = samples[i] ? '_' : '-';
        if (line_idx >= 80 || i == sample_count - 1) {
            line[line_idx] = '\0';
            ESP_LOGI(TAG, "  %s", line);
            line_idx = 0;
        }
    }
    
    free(samples);
}
#endif

//////////////////////////////////////////////////////////////
//////////////////////// GPIO HELPERS ////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 设置GPIO为输出模式（开漏）
 */
static inline void gpio_set_output(void)
{
    gpio_set_direction(g_driver.gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
}

/**
 * @brief 设置GPIO为输入模式（开漏）
 */
static inline void gpio_set_input(void)
{
    gpio_set_direction(g_driver.gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
}

/**
 * @brief 设置GPIO电平
 */
static inline void gpio_set_level_fast(gpio_num_t gpio, uint32_t level)
{
    gpio_set_level(gpio, level);
}

/**
 * @brief 读取GPIO电平
 */
static inline int gpio_get_level_fast(gpio_num_t gpio)
{
    return gpio_get_level(gpio);
}

//////////////////////////////////////////////////////////////
//////////////////////// CRC8 ////////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 计算CRC8（使用查找表）
 * 
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC8校验值
 */
static uint8_t calc_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    while (len--) {
        crc = crc8_table[crc ^ *data++];
    }
    return crc;
}

/**
 * @brief 验证ROM CRC
 * 
 * @param rom ROM ID数据
 * @return true CRC正确
 */
static bool verify_rom_crc(const max31850_rom_id_t *rom)
{
    uint8_t crc = calc_crc8(rom->bytes, 7);
    return (crc == rom->crc);
}

/**
 * @brief 验证Scratchpad CRC
 * 
 * @param scratch Scratchpad数据
 * @return true CRC正确
 */
static bool verify_scratchpad_crc(const max31850_scratchpad_t *scratch)
{
    uint8_t crc = calc_crc8(scratch->bytes, 8);
    return (crc == scratch->crc);
}

//////////////////////////////////////////////////////////////
//////////////////////// 1-WIRE LOW LEVEL ////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire复位和Presence检测
 * 
 * @return true 检测到从设备
 */
static bool one_wire_reset(void)
{
    bool presence = false;
    int level_before = 0, level_during = 0, level_after = 0;
    
#if MAX31850_DEBUG_WAVEFORM
    ESP_LOGI(TAG, "=== 1-Wire Reset Waveform ===");
    ESP_LOGI(TAG, "  Timing: reset=%dus, wait=%dus, presence=%dus",
             ONE_WIRE_RESET_TIME, ONE_WIRE_RESET_WAIT, ONE_WIRE_RESET_PRESENCE);
#endif
    
    ONE_WIRE_CRITICAL_ENTER();
    
    // 步骤1: 记录初始电平
    level_before = gpio_get_level_fast(g_driver.gpio_num);
    
    // 步骤2: 主机拉低总线 >=480us
    gpio_set_level_fast(g_driver.gpio_num, 0);
    esp_rom_delay_us(ONE_WIRE_RESET_TIME);
    
    // 步骤3: 释放总线（开漏模式下设为高电平输出1）
    gpio_set_level_fast(g_driver.gpio_num, 1);
    
    // 步骤4: 等待从机响应开始
    esp_rom_delay_us(ONE_WIRE_RESET_WAIT);
    
    // 步骤5: 采样presence信号（从机应拉低总线）
    level_during = gpio_get_level_fast(g_driver.gpio_num);
    presence = (level_during == 0);
    
    ONE_WIRE_CRITICAL_EXIT();
    
    // 步骤6: 等待剩余时间，确保时隙完成
    esp_rom_delay_us(ONE_WIRE_RESET_PRESENCE);
    
    // 步骤7: 记录最终电平
    level_after = gpio_get_level_fast(g_driver.gpio_num);
    
#if MAX31850_DEBUG_WAVEFORM
    ESP_LOGI(TAG, "  Level before reset: %d (expected 1)", level_before);
    ESP_LOGI(TAG, "  Level during presence window: %d (0=detected)", level_during);
    ESP_LOGI(TAG, "  Level after reset: %d (expected 1)", level_after);
    ESP_LOGI(TAG, "  Presence detected: %s", presence ? "YES" : "NO");
    
    if (level_before != 1) {
        ESP_LOGW(TAG, "  WARNING: Bus was not high before reset!");
    }
    if (level_after != 1) {
        ESP_LOGW(TAG, "  WARNING: Bus did not return to high after reset!");
    }
#endif
    
    return presence;
}

/**
 * @brief 写入单个bit
 * 
 * @param bit 要写入的bit (0或1)
 */
static void one_wire_write_bit(uint8_t bit)
{
    ONE_WIRE_CRITICAL_ENTER();
    
    if (bit) {
        // 写1: 拉低1-15us，然后释放
        gpio_set_level_fast(g_driver.gpio_num, 0);
        esp_rom_delay_us(ONE_WIRE_WRITE1_LOW);
        gpio_set_level_fast(g_driver.gpio_num, 1);
        esp_rom_delay_us(ONE_WIRE_WRITE1_HIGH);
    } else {
        // 写0: 拉低60-120us，然后释放
        gpio_set_level_fast(g_driver.gpio_num, 0);
        esp_rom_delay_us(ONE_WIRE_WRITE0_LOW);
        gpio_set_level_fast(g_driver.gpio_num, 1);
        esp_rom_delay_us(ONE_WIRE_WRITE0_HIGH);
    }
    
    ONE_WIRE_CRITICAL_EXIT();
}

/**
 * @brief 读取单个bit
 * 
 * @return 读取到的bit (0或1)
 */
static uint8_t one_wire_read_bit(void)
{
    uint8_t bit = 0;
    
    ONE_WIRE_CRITICAL_ENTER();
    
    // 主机拉低 >1us
    gpio_set_level_fast(g_driver.gpio_num, 0);
    esp_rom_delay_us(ONE_WIRE_READ_LOW);
    
    // 释放总线
    gpio_set_level_fast(g_driver.gpio_num, 1);
    esp_rom_delay_us(ONE_WIRE_READ_SAMPLE);
    
    // 采样
    bit = gpio_get_level_fast(g_driver.gpio_num) & 0x01;
    
    ONE_WIRE_CRITICAL_EXIT();
    
    // 等待时隙完成
    esp_rom_delay_us(ONE_WIRE_READ_HIGH);
    
    return bit;
}

/**
 * @brief 写入单个字节（LSB first）
 * 
 * @param data 要写入的字节
 */
static void one_wire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        one_wire_write_bit(data & 0x01);
        data >>= 1;
    }
}

/**
 * @brief 读取单个字节（LSB first）
 * 
 * @return 读取到的字节
 */
static uint8_t one_wire_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data >>= 1;
        if (one_wire_read_bit()) {
            data |= 0x80;
        }
    }
    return data;
}

//////////////////////////////////////////////////////////////
//////////////////////// ROM COMMANDS ////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief Match ROM - 选择特定设备
 * 
 * @param rom ROM ID
 */
static void max31850_match_rom(const max31850_rom_id_t *rom)
{
    one_wire_reset();
    one_wire_write_byte(ONE_WIRE_CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        one_wire_write_byte(rom->bytes[i]);
    }
}

/**
 * @brief Skip ROM - 广播命令
 */
static void max31850_skip_rom(void)
{
    one_wire_reset();
    one_wire_write_byte(ONE_WIRE_CMD_SKIP_ROM);
}

/**
 * @brief Search ROM算法 - 发现单个设备
 * 
 * @param rom 输出ROM ID
 * @param last_discrepancy 上次分歧位置
 * @return true 找到设备
 */
static bool one_wire_search_rom(max31850_rom_id_t *rom, int *last_discrepancy)
{
    int id_bit_number = 1;
    int last_zero = 0;
    int rom_byte_number = 0;
    uint8_t rom_byte_mask = 0x01;
    uint8_t id_bit, cmp_id_bit;
    bool search_result = false;
    
#if MAX31850_DEBUG_ROM_SEARCH
    ESP_LOGI(TAG, "=== ROM Search Started (last_discrepancy=%d) ===", *last_discrepancy);
    char bit_log[128];
    int bit_log_idx = 0;
#endif
    
    // 如果没有设备，直接返回
    if (!one_wire_reset()) {
        ESP_LOGW(TAG, "ROM Search: No presence pulse");
        return false;
    }
    
    one_wire_write_byte(ONE_WIRE_CMD_SEARCH_ROM);
    
    do {
        // 读取一位及其补码
        id_bit = one_wire_read_bit();
        cmp_id_bit = one_wire_read_bit();
        
#if MAX31850_DEBUG_ROM_SEARCH
        // 位级调试日志
        if (id_bit && cmp_id_bit) {
            ESP_LOGW(TAG, "  Bit %2d: NO RESPONSE (id=1, cmp=1)", id_bit_number);
        } else if (id_bit != cmp_id_bit) {
            ESP_LOGI(TAG, "  Bit %2d: CONSISTENT (value=%d)", id_bit_number, id_bit);
        } else {
            ESP_LOGI(TAG, "  Bit %2d: CONFLICT (0 and 1), choosing %d", 
                     id_bit_number, (id_bit_number <= *last_discrepancy) ? 
                     ((rom->bytes[rom_byte_number] & rom_byte_mask) ? 1 : 0) : 0);
        }
#endif
        
        if (id_bit && cmp_id_bit) {
            // 没有设备响应
            return false;
        }
        
        if (id_bit != cmp_id_bit) {
            // 所有设备在这一位相同
            search_result = id_bit ? true : false;
        } else {
            // 有分歧，需要选择路径
            if (id_bit_number == *last_discrepancy) {
                search_result = true;
            } else if (id_bit_number > *last_discrepancy) {
                search_result = false;
                last_zero = id_bit_number;
            } else {
                search_result = (rom->bytes[rom_byte_number] & rom_byte_mask) != 0;
                if (!search_result) {
                    last_zero = id_bit_number;
                }
            }
            
            if (!search_result) {
                last_zero = id_bit_number;
            }
        }
        
        // 写入选择的路径
        one_wire_write_bit(search_result ? 1 : 0);
        
        // 保存到ROM结构
        if (search_result) {
            rom->bytes[rom_byte_number] |= rom_byte_mask;
        } else {
            rom->bytes[rom_byte_number] &= ~rom_byte_mask;
        }
        
#if MAX31850_DEBUG_ROM_SEARCH
        // 记录选择的位
        if (bit_log_idx < sizeof(bit_log) - 1) {
            bit_log[bit_log_idx++] = search_result ? '1' : '0';
        }
#endif
        
        id_bit_number++;
        rom_byte_mask <<= 1;
        if (rom_byte_mask == 0) {
            rom_byte_number++;
            rom_byte_mask = 0x01;
        }
    } while (rom_byte_number < 8);
    
    *last_discrepancy = last_zero;
    
#if MAX31850_DEBUG_ROM_SEARCH
    bit_log[bit_log_idx] = '\0';
    ESP_LOGI(TAG, "  ROM bits: %s", bit_log);
    ESP_LOGI(TAG, "  Next discrepancy: %d", last_zero);
#endif
    
    // 验证ROM CRC
    bool crc_ok = verify_rom_crc(rom);
#if MAX31850_DEBUG_ROM_SEARCH
    ESP_LOGI(TAG, "  CRC check: %s", crc_ok ? "PASS" : "FAIL");
#endif
    
    return crc_ok;
}

/**
 * @brief Search ROM发现所有设备
 * 
 * @return 发现的设备数量
 */
static uint8_t max31850_search_all(void)
{
    max31850_rom_id_t rom;
    int last_discrepancy = 0;
    uint8_t found_count = 0;
    bool last_device = false;
    
    // 临时存储发现的ROM ID
    max31850_rom_id_t found_roms[MAX31850_SENSOR_COUNT];
    
    while (!last_device && found_count < MAX31850_SENSOR_COUNT) {
        memset(&rom, 0, sizeof(rom));
        
        if (!one_wire_search_rom(&rom, &last_discrepancy)) {
            break;
        }
        
        // 检查家族码
        if (rom.family_code != MAX31850_FAMILY_CODE) {
            ESP_LOGW(TAG, "Unknown family code: 0x%02X", rom.family_code);
            continue;
        }
        
        // 保存ROM ID
        found_roms[found_count] = rom;
        found_count++;
        
        // 检查是否是最后一个设备
        last_device = (last_discrepancy == 0);
    }
    
    // 现在读取每个设备的scratchpad，获取硬件地址，建立映射
    for (int i = 0; i < found_count; i++) {
        max31850_scratchpad_t scratch;
        
        // 选择设备并读取scratchpad
        max31850_match_rom(&found_roms[i]);
        one_wire_write_byte(MAX31850_CMD_READ_SCRATCH);
        
        for (int j = 0; j < 9; j++) {
            scratch.bytes[j] = one_wire_read_byte();
        }
        
        // 验证CRC
        if (!verify_scratchpad_crc(&scratch)) {
            ESP_LOGW(TAG, "CRC error during address mapping");
            continue;
        }
        
        // 从Config寄存器获取硬件地址 (Byte 4的低4位)
        // MAX31850KATB+: AD0/AD1引脚决定硬件地址
        // AD0=GND,AD1=GND -> 0xF0 -> 位置0
        // AD0=3.3V,AD1=GND -> 0xF1 -> 位置1
        // AD0=GND,AD1=3.3V -> 0xF2 -> 位置2
        // AD0=3.3V,AD1=3.3V -> 0xF3 -> 位置3
        uint8_t hw_addr = scratch.config & 0x0F;
        
        // 映射到数组索引 (0xF0-0xF3 -> 0-3)
        if (hw_addr <= 0x03) {
            // 有些版本使用0x00-0x03，直接作为索引
            // 不做转换
        } else if (hw_addr >= 0xF0 && hw_addr <= 0xF3) {
            // 转换为0-3
            hw_addr -= 0xF0;
        } else {
            ESP_LOGW(TAG, "Invalid HW address: 0x%02X, using index %d", hw_addr, i);
            hw_addr = i;
        }
        
        if (hw_addr < MAX31850_SENSOR_COUNT) {
            g_driver.sensors[hw_addr].rom_id = found_roms[i];
            g_driver.sensors[hw_addr].hw_addr = hw_addr;
            g_driver.sensors[hw_addr].present = true;
            ESP_LOGI(TAG, "Sensor [%d]: ROM=0x%02X%02X%02X%02X%02X%02X%02X%02X, HW_ADDR=%d, Location confirmed",
                     hw_addr,
                     found_roms[i].bytes[0], found_roms[i].bytes[1],
                     found_roms[i].bytes[2], found_roms[i].bytes[3],
                     found_roms[i].bytes[4], found_roms[i].bytes[5],
                     found_roms[i].bytes[6], found_roms[i].bytes[7],
                     hw_addr);
        }
    }
    
    return found_count;
}

//////////////////////////////////////////////////////////////
//////////////////////// FUNCTION COMMANDS ///////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 触发温度转换
 * 
 * @param rom 目标设备ROM（NULL表示广播）
 */
static void max31850_convert_t(const max31850_rom_id_t *rom)
{
    if (rom != NULL) {
        max31850_match_rom(rom);
    } else {
        max31850_skip_rom();
    }
    one_wire_write_byte(MAX31850_CMD_CONVERT_T);
}

/**
 * @brief 读取Scratchpad
 * 
 * @param rom 目标设备ROM
 * @param scratch 输出数据
 * @return true 成功
 */
static bool max31850_read_scratchpad(const max31850_rom_id_t *rom, max31850_scratchpad_t *scratch)
{
#if MAX31850_DEBUG_SCRATCHPAD
    ESP_LOGI(TAG, "=== Reading Scratchpad ===");
#endif
    
    if (rom != NULL) {
        max31850_match_rom(rom);
#if MAX31850_DEBUG_SCRATCHPAD
        ESP_LOGI(TAG, "  Matched ROM: %02X%02X%02X%02X%02X%02X%02X%02X",
                 rom->bytes[0], rom->bytes[1], rom->bytes[2], rom->bytes[3],
                 rom->bytes[4], rom->bytes[5], rom->bytes[6], rom->bytes[7]);
#endif
    } else {
        max31850_skip_rom();
#if MAX31850_DEBUG_SCRATCHPAD
        ESP_LOGI(TAG, "  Skip ROM (broadcast)");
#endif
    }
    
    one_wire_write_byte(MAX31850_CMD_READ_SCRATCH);
    
    for (int i = 0; i < 9; i++) {
        scratch->bytes[i] = one_wire_read_byte();
    }
    
#if MAX31850_DEBUG_SCRATCHPAD
    // 打印原始数据
    ESP_LOGI(TAG, "  Raw Scratchpad Data:");
    ESP_LOGI(TAG, "    [0] Temp LSB : 0x%02X (Fault=%d)", 
             scratch->temp_lsb, scratch->temp_lsb & 0x01);
    ESP_LOGI(TAG, "    [1] Temp MSB : 0x%02X", scratch->temp_msb);
    ESP_LOGI(TAG, "    [2] CJ LSB   : 0x%02X (Fault bits: OC=%d SCG=%d SCV=%d)",
             scratch->cj_lsb,
             (scratch->cj_lsb >> 0) & 0x01,  // OC
             (scratch->cj_lsb >> 1) & 0x01,  // SCG
             (scratch->cj_lsb >> 2) & 0x01); // SCV
    ESP_LOGI(TAG, "    [3] CJ MSB   : 0x%02X", scratch->cj_msb);
    ESP_LOGI(TAG, "    [4] Config   : 0x%02X (HW_ADDR=%d)", 
             scratch->config, scratch->config & 0x0F);
    ESP_LOGI(TAG, "    [5] Reserved : 0x%02X", scratch->bytes[5]);
    ESP_LOGI(TAG, "    [6] Reserved : 0x%02X", scratch->bytes[6]);
    ESP_LOGI(TAG, "    [7] Reserved : 0x%02X", scratch->bytes[7]);
    ESP_LOGI(TAG, "    [8] CRC      : 0x%02X", scratch->crc);
    
    // 计算并显示CRC
    uint8_t calc_crc = calc_crc8(scratch->bytes, 8);
    ESP_LOGI(TAG, "  CRC: Calculated=0x%02X, Read=0x%02X, %s",
             calc_crc, scratch->crc, (calc_crc == scratch->crc) ? "MATCH" : "MISMATCH");
    
    // 解析温度原始值
    int16_t temp_raw = ((int16_t)(scratch->temp_msb) << 8) | scratch->temp_lsb;
    temp_raw >>= 2;  // 14-bit value, right-aligned
    ESP_LOGI(TAG, "  Temp Raw: 0x%04X (%d), Fault bit: %d",
             ((scratch->temp_msb << 8) | scratch->temp_lsb), temp_raw,
             scratch->temp_lsb & 0x01);
    
    // 解析冷端温度原始值
    int16_t cj_raw = ((int16_t)(scratch->cj_msb) << 8) | scratch->cj_lsb;
    cj_raw >>= 4;  // 12-bit value, right-aligned
    ESP_LOGI(TAG, "  CJ Raw: 0x%04X (%d)",
             ((scratch->cj_msb << 8) | scratch->cj_lsb), cj_raw);
#endif
    
    return verify_scratchpad_crc(scratch);
}

/**
 * @brief 从Scratchpad解析温度
 * 
 * MAX31850使用14位有符号温度值
 * Byte 0: Bit7-Bit2 = 温度Bit5-Bit0, Bit1=保留, Bit0=Fault
 * Byte 1: Bit7-Bit0 = 温度Bit13-Bit6
 * 
 * 温度值 = (temp_msb << 6) | (temp_lsb >> 2)
 * 分辨率: 0.25°C (2-bit小数)
 * 
 * @param scratch Scratchpad数据
 * @param temp 输出温度值
 * @return true 成功，false 有故障
 */
static bool max31850_parse_temperature(const max31850_scratchpad_t *scratch, float *temp)
{
    // 检查故障标志
    if (scratch->temp_lsb & 0x01) {
        return false;
    }
    
    // 提取14位温度值
    int16_t raw_temp = ((int16_t)scratch->temp_msb << 6) | (scratch->temp_lsb >> 2);
    
    // 符号扩展（14位有符号数）
    if (raw_temp & 0x2000) {
        raw_temp |= 0xC000;  // 符号扩展到16位
    }
    
    // 转换为摄氏度（分辨率0.25°C）
    *temp = raw_temp * 0.25f;
    
    return true;
}

/**
 * @brief 从Scratchpad解析冷端温度
 * 
 * 冷端温度使用12位有符号值
 * Byte 2: Bit7-Bit4 = 冷端温度Bit3-Bit0, Bit2-Bit0=故障位
 * Byte 3: Bit7-Bit0 = 冷端温度Bit11-Bit4
 * 
 * @param scratch Scratchpad数据
 * @param cj_temp 输出冷端温度
 * @return true 成功
 */
static bool max31850_parse_cold_junction(const max31850_scratchpad_t *scratch, float *cj_temp)
{
    // 提取12位冷端温度值 (高4位在Byte 2的Bit7-4，低8位在Byte 3)
    int16_t raw_cj = ((int16_t)scratch->cj_msb << 4) | (scratch->cj_lsb >> 4);
    
    // 符号扩展（12位有符号数）
    if (raw_cj & 0x0800) {
        raw_cj |= 0xF000;  // 符号扩展到16位
    }
    
    // 转换为摄氏度（分辨率0.0625°C）
    *cj_temp = raw_cj * 0.0625f;
    
    return true;
}

/**
 * @brief 获取故障类型
 * 
 * @param scratch Scratchpad数据
 * @return 故障类型
 */
static max31850_fault_t max31850_get_fault(const max31850_scratchpad_t *scratch)
{
    // 故障位在Byte 2的Bit0-2
    return (max31850_fault_t)(scratch->cj_lsb & 0x07);
}

/**
 * @brief 从Scratchpad读取硬件地址
 * 
 * @param scratch Scratchpad数据
 * @return 硬件地址 (0-3)
 */
static uint8_t max31850_get_hw_addr_from_scratch(const max31850_scratchpad_t *scratch)
{
    uint8_t config = scratch->config;
    uint8_t hw_addr = config & 0x0F;
    
    // 处理不同的地址格式
    if (hw_addr >= 0xF0 && hw_addr <= 0xF3) {
        hw_addr -= 0xF0;
    }
    
    // 限制范围
    if (hw_addr >= MAX31850_SENSOR_COUNT) {
        hw_addr = 0;
    }
    
    return hw_addr;
}

//////////////////////////////////////////////////////////////
//////////////////////// TASK ////////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 温度轮询任务
 * 
 * 后台任务，定期轮询所有传感器
 */
static void max31850_polling_task(void *pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "Polling task started");
    
    while (1) {
        // 触发所有传感器转换（广播模式）
        max31850_trigger_all_conversion();
        
        // 等待转换完成
        vTaskDelay(pdMS_TO_TICKS(CONVERSION_TIME_MS));
        
        // 读取所有传感器数据
        for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
            if (!g_driver.sensors[i].present) {
                continue;
            }
            
            max31850_scratchpad_t scratch;
            
            if (xSemaphoreTake(g_driver.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                continue;
            }
            
            bool read_ok = max31850_read_scratchpad(&g_driver.sensors[i].rom_id, &scratch);
            
            if (!read_ok) {
                ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] CRC Error: CRC verification failed", i);
                g_driver.sensors[i].crc_error_count++;
                xSemaphoreGive(g_driver.mutex);
                continue;
            }
            
            // 检查故障
            max31850_fault_t fault = max31850_get_fault(&scratch);
            g_driver.sensors[i].fault = fault;
            
            if (fault != MAX31850_FAULT_NONE) {
                g_driver.sensors[i].fault_count++;
                
                if (fault & MAX31850_FAULT_OC) {
                    ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Open Circuit", i);
                }
                if (fault & MAX31850_FAULT_SCG) {
                    ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Short to GND", i);
                }
                if (fault & MAX31850_FAULT_SCV) {
                    ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Short to VCC", i);
                }
            }
            
            // 解析温度
            float temp, cj_temp;
            if (max31850_parse_temperature(&scratch, &temp)) {
                g_driver.sensors[i].thermocouple_temp = temp;
            }
            
            if (max31850_parse_cold_junction(&scratch, &cj_temp)) {
                g_driver.sensors[i].cold_junction_temp = cj_temp;
            }
            
            g_driver.sensors[i].last_update_tick = xTaskGetTickCount();
            
            // 验证硬件地址
            uint8_t hw_from_config = max31850_get_hw_addr_from_scratch(&scratch);
            if (hw_from_config != g_driver.sensors[i].hw_addr) {
                ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] Address mismatch: Config=0x%02X, Expected=%d",
                         i, scratch.config, g_driver.sensors[i].hw_addr);
            }
            
            ESP_LOGI(TAG, "Sensor [HW_ADDR=%02d]: Temp=%.2fC, CJ=%.2fC",
                     i, g_driver.sensors[i].thermocouple_temp,
                     g_driver.sensors[i].cold_junction_temp);
            
            xSemaphoreGive(g_driver.mutex);
        }
        
        // 等待下一次轮询
        vTaskDelay(pdMS_TO_TICKS(POLLING_INTERVAL_MS - CONVERSION_TIME_MS));
    }
}

//////////////////////////////////////////////////////////////
//////////////////////// PUBLIC API //////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t gpio_num)
{
    ESP_LOGI(TAG, "=== MAX31850 Driver Initialization ===");
    
    if (g_driver.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 保存GPIO号
    g_driver.gpio_num = gpio_num;
    
    ESP_LOGI(TAG, "Configuring GPIO%d as open-drain with pull-up", gpio_num);
    
    // 配置GPIO为开漏输出模式
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pin_bit_mask = (1ULL << gpio_num),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 1-Wire需要上拉
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 设置GPIO高电平（释放总线）
    gpio_set_level(gpio_num, 1);
    
#if MAX31850_DEBUG_GPIO
    // GPIO诊断
    gpio_print_diag(gpio_num);
    
    // 开漏模式测试
    if (!gpio_test_open_drain(gpio_num)) {
        ESP_LOGE(TAG, "GPIO open-drain test FAILED!");
    }
#endif
    
#if MAX31850_DEBUG_BUS_LEVEL
    // 总线电平检查
    ESP_LOGI(TAG, "Checking bus level...");
    vTaskDelay(pdMS_TO_TICKS(1));
    int bus_level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "Bus idle level: %d (expected 1)", bus_level);
    if (bus_level != 1) {
        ESP_LOGE(TAG, "BUS ERROR: Line is not high! Check pull-up resistor.");
    }
#endif
    
    // 创建互斥锁
    g_driver.mutex = xSemaphoreCreateMutex();
    if (g_driver.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化传感器数组
    memset(g_driver.sensors, 0, sizeof(g_driver.sensors));
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        g_driver.sensors[i].present = false;
        g_driver.sensors[i].hw_addr = i;
    }
    
    // 等待总线稳定（DataSheet: 上电后等待10ms）
    ESP_LOGI(TAG, "Waiting for bus stabilization (10ms)...");
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 执行Search ROM发现设备
    ESP_LOGI(TAG, "Starting device discovery...");
    uint8_t found = max31850_search_all();
    ESP_LOGI(TAG, "MAX31850 Init: Found %d sensors on GPIO%d", found, gpio_num);
    
    if (found == 0) {
        ESP_LOGW(TAG, "No MAX31850 sensors found!");
    }
    
    g_driver.sensor_count = found;
    g_driver.initialized = true;
    g_driver.polling_task = NULL;
    
    ESP_LOGI(TAG, "=== Initialization Complete ===");
    
    return ESP_OK;
}

esp_err_t max31850_deinit(void)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 停止轮询任务
    max31850_stop_polling();
    
    // 释放互斥锁
    if (g_driver.mutex != NULL) {
        vSemaphoreDelete(g_driver.mutex);
        g_driver.mutex = NULL;
    }
    
    // 重置GPIO
    gpio_reset_pin(g_driver.gpio_num);
    
    // 清零驱动结构
    memset(&g_driver, 0, sizeof(g_driver));
    
    return ESP_OK;
}

esp_err_t max31850_get_temperature(uint8_t hw_addr, float *temp)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (hw_addr >= MAX31850_SENSOR_COUNT || temp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_driver.sensors[hw_addr].present) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(g_driver.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *temp = g_driver.sensors[hw_addr].thermocouple_temp;
    
    xSemaphoreGive(g_driver.mutex);
    
    return ESP_OK;
}

esp_err_t max31850_get_temperature_full(uint8_t hw_addr, float *thermocouple_temp, float *cold_junction_temp)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (hw_addr >= MAX31850_SENSOR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_driver.sensors[hw_addr].present) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(g_driver.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (thermocouple_temp != NULL) {
        *thermocouple_temp = g_driver.sensors[hw_addr].thermocouple_temp;
    }
    
    if (cold_junction_temp != NULL) {
        *cold_junction_temp = g_driver.sensors[hw_addr].cold_junction_temp;
    }
    
    xSemaphoreGive(g_driver.mutex);
    
    return ESP_OK;
}

esp_err_t max31850_force_update(uint8_t hw_addr, float *temp, TickType_t timeout)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (hw_addr >= MAX31850_SENSOR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_driver.sensors[hw_addr].present) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(g_driver.mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 触发转换
    max31850_convert_t(&g_driver.sensors[hw_addr].rom_id);
    
    xSemaphoreGive(g_driver.mutex);
    
    // 等待转换完成
    vTaskDelay(pdMS_TO_TICKS(CONVERSION_TIME_MS));
    
    // 读取结果
    max31850_scratchpad_t scratch;
    
    if (xSemaphoreTake(g_driver.mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    bool read_ok = max31850_read_scratchpad(&g_driver.sensors[hw_addr].rom_id, &scratch);
    
    if (!read_ok) {
        xSemaphoreGive(g_driver.mutex);
        ESP_LOGE(TAG, "Sensor [HW_ADDR=%02d] CRC Error: Calculated=0x%02X, Read=0x%02X",
                 hw_addr, calc_crc8(scratch.bytes, 8), scratch.crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    // 检查故障
    max31850_fault_t fault = max31850_get_fault(&scratch);
    g_driver.sensors[hw_addr].fault = fault;
    
    if (fault != MAX31850_FAULT_NONE) {
        if (fault & MAX31850_FAULT_OC) {
            ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Open Circuit", hw_addr);
        }
        if (fault & MAX31850_FAULT_SCG) {
            ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Short to GND", hw_addr);
        }
        if (fault & MAX31850_FAULT_SCV) {
            ESP_LOGW(TAG, "Sensor [HW_ADDR=%02d] FAULT: Thermocouple Short to VCC", hw_addr);
        }
    }
    
    // 解析温度
    float thermo_temp, cj_temp;
    if (max31850_parse_temperature(&scratch, &thermo_temp)) {
        g_driver.sensors[hw_addr].thermocouple_temp = thermo_temp;
    }
    
    if (max31850_parse_cold_junction(&scratch, &cj_temp)) {
        g_driver.sensors[hw_addr].cold_junction_temp = cj_temp;
    }
    
    g_driver.sensors[hw_addr].last_update_tick = xTaskGetTickCount();
    
    if (temp != NULL) {
        *temp = g_driver.sensors[hw_addr].thermocouple_temp;
    }
    
    ESP_LOGI(TAG, "Sensor [HW_ADDR=%02d]: Temp=%.2fC, CJ=%.2fC",
             hw_addr, g_driver.sensors[hw_addr].thermocouple_temp,
             g_driver.sensors[hw_addr].cold_junction_temp);
    
    xSemaphoreGive(g_driver.mutex);
    
    return ESP_OK;
}

esp_err_t max31850_get_fault_status(uint8_t hw_addr, max31850_fault_t *fault)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (hw_addr >= MAX31850_SENSOR_COUNT || fault == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_driver.sensors[hw_addr].present) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (xSemaphoreTake(g_driver.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *fault = g_driver.sensors[hw_addr].fault;
    
    xSemaphoreGive(g_driver.mutex);
    
    return ESP_OK;
}

uint8_t max31850_get_sensor_count(void)
{
    if (!g_driver.initialized) {
        return 0;
    }
    return g_driver.sensor_count;
}

bool max31850_sensor_exists(uint8_t hw_addr)
{
    if (!g_driver.initialized || hw_addr >= MAX31850_SENSOR_COUNT) {
        return false;
    }
    return g_driver.sensors[hw_addr].present;
}

void max31850_print_sensor_info(void)
{
    if (!g_driver.initialized) {
        ESP_LOGI(TAG, "Driver not initialized");
        return;
    }
    
    ESP_LOGI(TAG, "========== MAX31850 Sensor Info ==========");
    ESP_LOGI(TAG, "Total sensors found: %d", g_driver.sensor_count);
    ESP_LOGI(TAG, "GPIO: %d", g_driver.gpio_num);
    
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        max31850_sensor_t *sensor = &g_driver.sensors[i];
        
        ESP_LOGI(TAG, "Sensor [%d]:", i);
        ESP_LOGI(TAG, "  Present: %s", sensor->present ? "YES" : "NO");
        
        if (sensor->present) {
            ESP_LOGI(TAG, "  HW Address: %d", sensor->hw_addr);
            ESP_LOGI(TAG, "  ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X",
                     sensor->rom_id.bytes[0], sensor->rom_id.bytes[1],
                     sensor->rom_id.bytes[2], sensor->rom_id.bytes[3],
                     sensor->rom_id.bytes[4], sensor->rom_id.bytes[5],
                     sensor->rom_id.bytes[6], sensor->rom_id.bytes[7]);
            ESP_LOGI(TAG, "  Temperature: %.2f C", sensor->thermocouple_temp);
            ESP_LOGI(TAG, "  Cold Junction: %.2f C", sensor->cold_junction_temp);
            ESP_LOGI(TAG, "  Fault: 0x%02X (%s)", sensor->fault,
                     max31850_fault_to_string(sensor->fault));
            ESP_LOGI(TAG, "  CRC Errors: %lu", sensor->crc_error_count);
            ESP_LOGI(TAG, "  Fault Count: %lu", sensor->fault_count);
        }
    }
    
    ESP_LOGI(TAG, "==========================================");
}

esp_err_t max31850_start_polling(void)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_driver.polling_task != NULL) {
        return ESP_ERR_INVALID_STATE;  // 已经在运行
    }
    
    BaseType_t ret = xTaskCreate(
        max31850_polling_task,
        "max31850_poll",
        HEATING_TASK_STACK_SIZE,
        NULL,
        HEATING_TASK_PRIORITY,
        &g_driver.polling_task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t max31850_stop_polling(void)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_driver.polling_task == NULL) {
        return ESP_OK;  // 已经停止
    }
    
    vTaskDelete(g_driver.polling_task);
    g_driver.polling_task = NULL;
    
    ESP_LOGI(TAG, "Polling task stopped");
    
    return ESP_OK;
}

esp_err_t max31850_trigger_all_conversion(void)
{
    if (!g_driver.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_driver.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 发送Skip ROM + Convert T命令
    max31850_convert_t(NULL);
    
    xSemaphoreGive(g_driver.mutex);
    
    return ESP_OK;
}

const char* max31850_fault_to_string(max31850_fault_t fault)
{
    switch (fault) {
        case MAX31850_FAULT_NONE:
            return "No Fault";
        case MAX31850_FAULT_OC:
            return "Open Circuit";
        case MAX31850_FAULT_SCG:
            return "Short to GND";
        case MAX31850_FAULT_SCV:
            return "Short to VCC";
        default:
            return "Multiple Faults";
    }
}
