/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ 温度传感器驱动模块实现
 * 
 * 基于ESP32-S3 RMT外设实现1-Wire协议，支持多设备并联
 * 使用非阻塞状态机进行温度轮询
 * 
 * @author Kimi Code CLI
 * @date 2026-03-27
 */

#include "heating_detect.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

//////////////////////////////////////////////////////////////
//////////////////////// 日志标签 /////////////////////////////
//////////////////////////////////////////////////////////////

static const char *TAG = "MAX31850";

//////////////////////////////////////////////////////////////
//////////////////////// 1-Wire时序常量 ////////////////////////
//////////////////////////////////////////////////////////////
// 时序基于标准1-Wire协议（高速模式未使用）
// 参考：MAX31850数据手册和1-Wire标准

#define ONEWIRE_RESET_US        480     /**< Reset脉冲持续时间 */
#define ONEWIRE_PRESENCE_WAIT_US 70     /**< 等待Presence响应时间 */
#define ONEWIRE_PRESENCE_SAMPLE_US 100  /**< Presence采样时间点 */
#define ONEWIRE_SLOT_US         70      /**< 标准时隙持续时间 */
#define ONEWIRE_WRITE1_US       10      /**< 写1低电平时间 */
#define ONEWIRE_WRITE0_US       70      /**< 写0低电平时间 */
#define ONEWIRE_READ_INIT_US    5       /**< 读初始化低电平时间 */
#define ONEWIRE_READ_SAMPLE_US  15      /**< 读采样时间点（从slot开始） */
#define ONEWIRE_RECOVERY_US     5       /**< 恢复时间 */

//////////////////////////////////////////////////////////////
//////////////////////// RMT配置常量 //////////////////////////
//////////////////////////////////////////////////////////////

#define RMT_RESOLUTION_HZ       1000000     /**< RMT时钟1MHz = 1us精度 */
#define RMT_TX_MEM_BLOCK_SIZE   64          /**< TX内存块大小 */
#define RMT_RX_MEM_BLOCK_SIZE   64          /**< RX内存块大小 */
#define RMT_TX_QUEUE_DEPTH      4           /**< TX队列深度 */
#define RMT_RX_QUEUE_DEPTH      4           /**< RX队列深度 */

//////////////////////////////////////////////////////////////
//////////////////////// CRC8表（X8+X5+X4+1）///////////////////
//////////////////////////////////////////////////////////////

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
//////////////////////// 静态变量 /////////////////////////////
//////////////////////////////////////////////////////////////

static rmt_channel_handle_t s_rmt_tx_channel = NULL;    /**< RMT TX通道 */
static rmt_channel_handle_t s_rmt_rx_channel = NULL;    /**< RMT RX通道 */
static rmt_encoder_handle_t s_rmt_encoder = NULL;       /**< RMT编码器 */
static SemaphoreHandle_t s_onewire_mutex = NULL;        /**< 1-Wire总线互斥锁 */
static TaskHandle_t s_poll_task_handle = NULL;          /**< 轮询任务句柄 */

static gpio_num_t s_onewire_pin = GPIO_NUM_NC;          /**< 当前使用的GPIO */
static uint8_t s_sensor_count = 0;                      /**< 实际发现的传感器数量 */
static bool s_initialized = false;                      /**< 初始化标志 */

// 传感器数据数组（索引0-3对应P1-P4）
static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT];

//////////////////////////////////////////////////////////////
//////////////////////// 轮询状态机 ///////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 轮询状态
 */
typedef enum {
    POLL_STATE_IDLE = 0,            /**< 空闲状态 */
    POLL_STATE_CONVERT,             /**< 发送转换命令 */
    POLL_STATE_WAIT_CONVERSION,     /**< 等待转换完成 */
    POLL_STATE_READ,                /**< 读取温度 */
    POLL_STATE_PARSE,               /**< 解析数据 */
    POLL_STATE_NEXT_SENSOR,         /**< 切换到下一个传感器 */
} poll_state_t;

static poll_state_t s_poll_state = POLL_STATE_IDLE;     /**< 当前轮询状态 */
static uint8_t s_current_sensor = 0;                    /**< 当前处理的传感器索引 */
static uint32_t s_conversion_start_time = 0;            /**< 转换开始时间 */

//////////////////////////////////////////////////////////////
//////////////////////// 函数原型 /////////////////////////////
//////////////////////////////////////////////////////////////

// 底层1-Wire操作
static esp_err_t onewire_reset(bool *presence);
static esp_err_t onewire_write_bit(uint8_t bit);
static esp_err_t onewire_read_bit(uint8_t *bit);
static esp_err_t onewire_write_byte(uint8_t data);
static esp_err_t onewire_read_byte(uint8_t *data);

// ROM操作
static esp_err_t onewire_search_rom(void);
static esp_err_t onewire_match_rom(const uint8_t *rom_id);

// MAX31850特定操作
static esp_err_t max31850_start_conversion(const uint8_t *rom_id);
static esp_err_t max31850_read_scratchpad(const uint8_t *rom_id, uint8_t *scratchpad);
static max31850_err_t max31850_parse_scratchpad(const uint8_t *scratchpad, float *temp_out);

// CRC校验
static uint8_t crc8_calculate(const uint8_t *data, uint8_t len);

// 轮询任务
static void max31850_poll_task(void *pvParameters);

// 工具函数
static void max31850_update_sensor_status(uint8_t sensor_id, max31850_err_t err);

//////////////////////////////////////////////////////////////
//////////////////////// CRC8计算 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 计算CRC8校验值（多项式X8+X5+X4+1）
 */
static uint8_t crc8_calculate(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

//////////////////////////////////////////////////////////////
//////////////////////// RMT编码器 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire RMT编码器回调 - 将字节编码为RMT符号
 * 
 * 1-Wire协议使用特定的时隙格式：
 * - 写1：低电平~10us，然后拉高
 * - 写0：低电平~70us
 * - 读：低电平~5us，然后释放，在~15us采样
 */
static size_t rmt_onewire_encoder(rmt_channel_handle_t channel, const void *primary_data,
                                   size_t data_size, size_t symbols_written,
                                   size_t symbols_free, rmt_symbol_word_t *symbols,
                                   bool *done, void *arg)
{
    const uint8_t *data = (const uint8_t *)primary_data;
    size_t data_pos = symbols_written / 8;  // 每个字节需要8个时隙
    size_t bit_pos = symbols_written % 8;
    size_t symbols_to_write = 0;
    
    while (data_pos < data_size && symbols_to_write + 1 <= symbols_free) {
        uint8_t byte = data[data_pos];
        
        // 处理当前字节的剩余位
        for (; bit_pos < 8 && symbols_to_write + 1 <= symbols_free; bit_pos++) {
            uint8_t bit = (byte >> bit_pos) & 0x01;
            
            if (bit) {
                // 写1：低电平10us，高电平恢复
                symbols[symbols_to_write].level0 = 0;
                symbols[symbols_to_write].duration0 = ONEWIRE_WRITE1_US;
                symbols[symbols_to_write].level1 = 1;
                symbols[symbols_to_write].duration1 = ONEWIRE_SLOT_US - ONEWIRE_WRITE1_US;
            } else {
                // 写0：低电平70us
                symbols[symbols_to_write].level0 = 0;
                symbols[symbols_to_write].duration0 = ONEWIRE_WRITE0_US;
                symbols[symbols_to_write].level1 = 1;
                symbols[symbols_to_write].duration1 = ONEWIRE_RECOVERY_US;
            }
            symbols_to_write++;
        }
        
        if (bit_pos >= 8) {
            bit_pos = 0;
            data_pos++;
        }
    }
    
    if (data_pos >= data_size) {
        *done = true;
    }
    
    return symbols_to_write;
}

//////////////////////////////////////////////////////////////
//////////////////////// 1-Wire底层操作 ////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 发送1-Wire Reset脉冲并检测Presence
 * 
 * Reset时序：主机拉低480us，然后释放
 * 从机在15-60us内拉低60-240us表示Presence
 */
static esp_err_t onewire_reset(bool *presence)
{
    if (presence) *presence = false;
    
    // 使用GPIO直接控制Reset（RMT不适合这种双向时序）
    // 步骤1：配置GPIO为输出，拉低480us
    gpio_set_direction(s_onewire_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(s_onewire_pin, 0);
    esp_rom_delay_us(ONEWIRE_RESET_US);
    
    // 步骤2：释放总线，切换到输入模式
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(ONEWIRE_PRESENCE_WAIT_US);
    
    // 步骤3：采样Presence（应在60us内检测到）
    int level1 = gpio_get_level(s_onewire_pin);
    esp_rom_delay_us(ONEWIRE_PRESENCE_SAMPLE_US - ONEWIRE_PRESENCE_WAIT_US);
    int level2 = gpio_get_level(s_onewire_pin);
    
    // 等待Reset周期结束
    esp_rom_delay_us(ONEWIRE_RESET_US - ONEWIRE_PRESENCE_SAMPLE_US);
    
    // 检测Presence：如果总线被从机拉低（低电平）表示存在
    // 正常情况下：level1=1（刚释放），level2=0（从机拉低）
    if (level1 == 1 && level2 == 0) {
        if (presence) *presence = true;
    } else if (level1 == 0 && level2 == 0) {
        // 总线短路到GND
        ESP_LOGW(TAG, "1-Wire bus short to GND detected");
        return ESP_ERR_INVALID_STATE;
    } else if (level1 == 1 && level2 == 1) {
        // 总线开路或无设备
        ESP_LOGW(TAG, "1-Wire bus open circuit or no device");
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

/**
 * @brief 写单个位到1-Wire总线
 */
static esp_err_t onewire_write_bit(uint8_t bit)
{
    gpio_set_direction(s_onewire_pin, GPIO_MODE_OUTPUT);
    
    if (bit & 0x01) {
        // 写1：低电平10us
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE1_US);
        gpio_set_level(s_onewire_pin, 1);
        esp_rom_delay_us(ONEWIRE_SLOT_US - ONEWIRE_WRITE1_US);
    } else {
        // 写0：低电平70us
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE0_US);
        gpio_set_level(s_onewire_pin, 1);
        esp_rom_delay_us(ONEWIRE_RECOVERY_US);
    }
    
    return ESP_OK;
}

/**
 * @brief 从1-Wire总线读取单个位
 */
static esp_err_t onewire_read_bit(uint8_t *bit)
{
    if (bit) *bit = 1;
    
    // 读时隙：主机拉低5us，然后释放，在15us采样
    gpio_set_direction(s_onewire_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(s_onewire_pin, 0);
    esp_rom_delay_us(ONEWIRE_READ_INIT_US);
    
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(ONEWIRE_READ_SAMPLE_US - ONEWIRE_READ_INIT_US);
    
    int level = gpio_get_level(s_onewire_pin);
    if (bit) *bit = level & 0x01;
    
    // 等待时隙结束
    esp_rom_delay_us(ONEWIRE_SLOT_US - ONEWIRE_READ_SAMPLE_US);
    
    return ESP_OK;
}

/**
 * @brief 写一个字节到1-Wire总线（LSB first）
 */
static esp_err_t onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(onewire_write_bit(data & 0x01));
        data >>= 1;
    }
    return ESP_OK;
}

/**
 * @brief 从1-Wire总线读取一个字节（LSB first）
 */
static esp_err_t onewire_read_byte(uint8_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    *data = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t bit = 0;
        ESP_ERROR_CHECK(onewire_read_bit(&bit));
        *data |= (bit << i);
    }
    return ESP_OK;
}


//////////////////////////////////////////////////////////////
//////////////////////// ROM搜索算法 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 搜索1-Wire总线上的所有设备（Search ROM）
 * 
 * 实现1-Wire标准搜索算法，自动发现所有连接的ROM ID
 * 使用二进制搜索树遍历所有设备
 */
static esp_err_t onewire_search_rom(void)
{
    uint8_t rom_id[MAX31850_ROM_ID_LEN];
    uint8_t last_discrepancy = 0;
    uint8_t last_zero = 0;
    bool done = false;
    int device_count = 0;
    
    ESP_LOGI(TAG, "Starting 1-Wire ROM search...");
    
    while (!done && device_count < MAX31850_SENSOR_COUNT) {
        bool presence = false;
        esp_err_t err = onewire_reset(&presence);
        if (err != ESP_OK || !presence) {
            ESP_LOGE(TAG, "No device present during search");
            break;
        }
        
        // 发送Search ROM命令
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
        uint8_t discrepancy = 0;
        
        // 搜索64位ROM ID（8字节）
        for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
            uint8_t byte_pos = bit_pos / 8;
            uint8_t bit_mask = 1 << (bit_pos % 8);
            
            // 读取两个位：实际值和补码
            uint8_t bit_actual = 0, bit_complement = 0;
            onewire_read_bit(&bit_actual);
            onewire_read_bit(&bit_complement);
            
            uint8_t selected_bit;
            
            if (bit_actual == 0 && bit_complement == 0) {
                // 有分歧（多个设备在此位不同）
                if (bit_pos < last_discrepancy) {
                    // 沿用之前的路径
                    selected_bit = (rom_id[byte_pos] & bit_mask) ? 1 : 0;
                } else if (bit_pos == last_discrepancy) {
                    // 选择1
                    selected_bit = 1;
                } else {
                    // 记录新的分歧点，选择0
                    selected_bit = 0;
                    last_zero = bit_pos;
                }
                
                if (selected_bit == 0) {
                    discrepancy = bit_pos;
                }
            } else if (bit_actual == 0 && bit_complement == 1) {
                // 所有设备在此位都是0
                selected_bit = 0;
            } else if (bit_actual == 1 && bit_complement == 0) {
                // 所有设备在此位都是1
                selected_bit = 1;
            } else {
                // 00=错误（总线故障或无设备）
                // 11=错误（总线故障）
                ESP_LOGE(TAG, "Search error at bit %d: %d/%d", bit_pos, bit_actual, bit_complement);
                return ESP_ERR_INVALID_STATE;
            }
            
            // 写入选择的位
            onewire_write_bit(selected_bit);
            
            // 保存到ROM ID
            if (selected_bit) {
                rom_id[byte_pos] |= bit_mask;
            } else {
                rom_id[byte_pos] &= ~bit_mask;
            }
        }
        
        // 验证CRC
        if (crc8_calculate(rom_id, 7) != rom_id[7]) {
            ESP_LOGW(TAG, "ROM ID CRC error, retrying search...");
            continue;
        }
        
        // 检查是否为MAX31850家族码（0x3B）
        if (rom_id[0] != 0x3B) {
            ESP_LOGW(TAG, "Unknown family code: 0x%02X, expected 0x3B (MAX31850)", rom_id[0]);
        }
        
        // 保存ROM ID
        memcpy(s_sensors[device_count].rom_id, rom_id, MAX31850_ROM_ID_LEN);
        s_sensors[device_count].online = true;
        device_count++;
        
        ESP_LOGI(TAG, "Found device %d: ROM ID %02X%02X%02X%02X%02X%02X%02X%02X",
                 device_count,
                 rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                 rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
        
        // 检查是否还有更多设备
        if (discrepancy == 0) {
            done = true;
        } else {
            last_discrepancy = discrepancy;
        }
    }
    
    s_sensor_count = device_count;
    ESP_LOGI(TAG, "ROM search complete. Found %d device(s)", device_count);
    
    return (device_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/**
 * @brief 选择特定的ROM ID进行通信（Match ROM）
 */
static esp_err_t onewire_match_rom(const uint8_t *rom_id)
{
    if (!rom_id) return ESP_ERR_INVALID_ARG;
    
    bool presence = false;
    ESP_ERROR_CHECK(onewire_reset(&presence));
    if (!presence) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // 发送Match ROM命令
    onewire_write_byte(ONEWIRE_CMD_MATCH_ROM);
    
    // 发送64位ROM ID
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(rom_id[i]);
    }
    
    return ESP_OK;
}

//////////////////////////////////////////////////////////////
//////////////////////// MAX31850操作 /////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 启动温度转换
 */
static esp_err_t max31850_start_conversion(const uint8_t *rom_id)
{
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    onewire_write_byte(MAX31850_CMD_CONVERT_T);
    return ESP_OK;
}

/**
 * @brief 读取暂存器（9字节）
 */
static esp_err_t max31850_read_scratchpad(const uint8_t *rom_id, uint8_t *scratchpad)
{
    if (!scratchpad) return ESP_ERR_INVALID_ARG;
    
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    onewire_write_byte(MAX31850_CMD_READ_SCRATCH);
    
    for (int i = 0; i < MAX31850_SCRATCHPAD_LEN; i++) {
        onewire_read_byte(&scratchpad[i]);
    }
    
    return ESP_OK;
}

/**
 * @brief 解析暂存器数据并转换为温度
 * 
 * MAX31850温度数据格式（16位，大端）：
 * - 第0-1字节：温度（有符号，0.0625°C分辨率）
 * - 第2字节：未使用
 * - 第3字节：配置寄存器
 * - 第4字节：故障寄存器
 * - 第5-6字节：未使用
 * - 第7字节：未使用
 * - 第8字节：CRC校验
 * 
 * 温度计算：
 * - 正温度：直接乘以0.0625
 * - 负温度：补码转换后乘以0.0625
 */
static max31850_err_t max31850_parse_scratchpad(const uint8_t *scratchpad, float *temp_out)
{
    if (!scratchpad || !temp_out) return MAX31850_ERR_CRC;
    
    // CRC校验
    if (crc8_calculate(scratchpad, 8) != scratchpad[8]) {
        ESP_LOGW(TAG, "Scratchpad CRC error: calc=0x%02X, read=0x%02X",
                 crc8_calculate(scratchpad, 8), scratchpad[8]);
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
    
    // 解析温度（第0-1字节，大端）
    int16_t raw_temp = ((int16_t)scratchpad[0] << 8) | scratchpad[1];
    
    // 扩展符号位（16位有符号）
    if (raw_temp & 0x8000) {
        raw_temp |= 0x0000;  // 已经是16位，无需扩展
    }
    
    // 转换为摄氏度（分辨率0.0625°C）
    *temp_out = (float)raw_temp * 0.0625f;
    
    // 检查温度范围有效性（MAX31850范围-270~+1372°C，但超出-200~+1250可能有问题）
    if (*temp_out < -270.0f || *temp_out > 1372.0f) {
        ESP_LOGW(TAG, "Temperature out of valid range: %.2f°C", *temp_out);
        return MAX31850_ERR_CRC;  // 使用CRC错误表示数据异常
    }
    
    return MAX31850_OK;
}

//////////////////////////////////////////////////////////////
//////////////////////// 传感器状态管理 ////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 更新传感器状态（错误处理和离线检测）
 */
static void max31850_update_sensor_status(uint8_t sensor_id, max31850_err_t err)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT) return;
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    sensor->last_error = err;
    sensor->last_read_time = xTaskGetTickCount();
    
    if (err == MAX31850_OK) {
        // 成功读取
        sensor->fail_count = 0;
        sensor->online = true;
        sensor->data_valid = true;
    } else {
        // 读取失败
        sensor->fail_count++;
        sensor->data_valid = false;
        
        // 打印详细的错误信息
        if (err == MAX31850_ERR_OPEN) {
            ESP_LOGW(TAG, "Sensor %d: Thermocouple open circuit (断线)", sensor_id);
        } else if (err == MAX31850_ERR_SHORT_GND) {
            ESP_LOGW(TAG, "Sensor %d: Thermocouple short to GND", sensor_id);
        } else if (err == MAX31850_ERR_SHORT_VCC) {
            ESP_LOGW(TAG, "Sensor %d: Thermocouple short to VCC", sensor_id);
        } else if (err == MAX31850_ERR_CRC) {
            ESP_LOGW(TAG, "Sensor %d: CRC error or data corruption", sensor_id);
        }
        
        // 检查是否超过失败阈值
        if (sensor->fail_count >= MAX31850_MAX_FAIL_COUNT) {
            if (sensor->online) {
                ESP_LOGE(TAG, "Sensor %d: Marked OFFLINE after %d consecutive failures",
                         sensor_id, sensor->fail_count);
                sensor->online = false;
            }
        }
    }
}

//////////////////////////////////////////////////////////////
//////////////////////// 轮询任务 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 轮询任务 - 非阻塞式温度读取
 * 
 * 状态机实现：
 * 1. CONVERT - 发送温度转换命令
 * 2. WAIT_CONVERSION - 等待100ms转换时间
 * 3. READ - 读取暂存器
 * 4. PARSE - 解析数据
 * 5. NEXT_SENSOR - 切换到下一个传感器
 */
static void max31850_poll_task(void *pvParameters)
{
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    float temp;
    
    ESP_LOGI(TAG, "Poll task started");
    
    // 初始延迟，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    while (1) {
        // 如果没有传感器，等待后重试
        if (s_sensor_count == 0) {
            ESP_LOGW(TAG, "No sensors found, retrying search...");
            onewire_search_rom();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // 轮询每个传感器
        for (uint8_t i = 0; i < s_sensor_count; i++) {
            max31850_sensor_t *sensor = &s_sensors[i];
            
            // 如果传感器离线，跳过轮询但定期尝试恢复
            if (!sensor->online) {
                if (sensor->fail_count % 10 == 0) {
                    // 每10个周期尝试一次恢复
                    ESP_LOGI(TAG, "Attempting to recover sensor %d...", i);
                } else {
                    continue;
                }
            }
            
            if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to acquire bus mutex for sensor %d", i);
                continue;
            }
            
            // 步骤1：启动温度转换
            esp_err_t err = max31850_start_conversion(sensor->rom_id);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Sensor %d: Failed to start conversion", i);
                max31850_update_sensor_status(i, MAX31850_ERR_TIMEOUT);
                xSemaphoreGive(s_onewire_mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            xSemaphoreGive(s_onewire_mutex);
            
            // 步骤2：等待转换完成（非阻塞）
            vTaskDelay(pdMS_TO_TICKS(MAX31850_CONVERSION_TIME_MS));
            
            if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to acquire bus mutex for reading sensor %d", i);
                continue;
            }
            
            // 步骤3：读取暂存器
            err = max31850_read_scratchpad(sensor->rom_id, scratchpad);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Sensor %d: Failed to read scratchpad", i);
                max31850_update_sensor_status(i, MAX31850_ERR_TIMEOUT);
                xSemaphoreGive(s_onewire_mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            xSemaphoreGive(s_onewire_mutex);
            
            // 保存原始数据用于调试
            sensor->raw_temp = ((int16_t)scratchpad[0] << 8) | scratchpad[1];
            sensor->fault_reg = scratchpad[4];
            
            // 步骤4：解析数据
            max31850_err_t parse_err = max31850_parse_scratchpad(scratchpad, &temp);
            max31850_update_sensor_status(i, parse_err);
            
            if (parse_err == MAX31850_OK) {
                sensor->temperature = temp;
                ESP_LOGD(TAG, "Sensor %d: Temperature = %.2f°C", i, temp);
            }
        }
        
        // 等待下一次轮询周期
        vTaskDelay(pdMS_TO_TICKS(MAX31850_POLL_INTERVAL_MS));
    }
}


//////////////////////////////////////////////////////////////
//////////////////////// 公共API实现 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化MAX31850模块
 */
esp_err_t max31850_init(gpio_num_t onewire_pin)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing MAX31850 on GPIO%d...", onewire_pin);
    
    s_onewire_pin = onewire_pin;
    
    // 初始化传感器数组
    memset(s_sensors, 0, sizeof(s_sensors));
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        s_sensors[i].online = false;
        s_sensors[i].data_valid = false;
        s_sensors[i].temperature = 0.0f;
    }
    
    // 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << onewire_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // 创建互斥锁
    s_onewire_mutex = xSemaphoreCreateMutex();
    if (!s_onewire_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 搜索总线上的设备
    esp_err_t err = onewire_search_rom();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial ROM search failed, will retry in poll task");
        // 继续初始化，轮询任务会定期重试
    }
    
    // 打印ROM ID
    max31850_print_rom_ids();
    
    // 创建轮询任务
    BaseType_t task_created = xTaskCreate(
        max31850_poll_task,
        "max31850_poll",
        4096,
        NULL,
        2,  // 优先级（低于主业务逻辑）
        &s_poll_task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_onewire_mutex);
        s_onewire_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "MAX31850 initialized successfully");
    
    return ESP_OK;
}

/**
 * @brief 获取指定传感器的温度
 */
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out)
{
    if (!s_initialized) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (!temp_out) {
        return MAX31850_ERR_CRC;  // 使用CRC错误表示参数错误
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

/**
 * @brief 获取原始数据和故障寄存器
 */
max31850_err_t max31850_get_raw_data(uint8_t sensor_id, int16_t *raw_out, uint8_t *fault_reg)
{
    if (!s_initialized) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (raw_out) {
        *raw_out = sensor->raw_temp;
    }
    
    if (fault_reg) {
        *fault_reg = sensor->fault_reg;
    }
    
    return sensor->online ? sensor->last_error : MAX31850_ERR_OFFLINE;
}

/**
 * @brief 强制立即更新（阻塞式）
 */
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout)
{
    if (!s_initialized) {
        return MAX31850_ERR_OFFLINE;
    }
    
    if (sensor_id >= MAX31850_SENSOR_COUNT) {
        return MAX31850_ERR_INVALID_ID;
    }
    
    if (!temp_out) {
        return MAX31850_ERR_CRC;
    }
    
    max31850_sensor_t *sensor = &s_sensors[sensor_id];
    
    if (xSemaphoreTake(s_onewire_mutex, timeout) != pdTRUE) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 启动转换
    esp_err_t err = max31850_start_conversion(sensor->rom_id);
    if (err != ESP_OK) {
        xSemaphoreGive(s_onewire_mutex);
        return MAX31850_ERR_TIMEOUT;
    }
    
    xSemaphoreGive(s_onewire_mutex);
    
    // 等待转换完成（阻塞）
    vTaskDelay(pdMS_TO_TICKS(MAX31850_CONVERSION_TIME_MS));
    
    if (xSemaphoreTake(s_onewire_mutex, timeout) != pdTRUE) {
        return MAX31850_ERR_TIMEOUT;
    }
    
    // 读取暂存器
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    err = max31850_read_scratchpad(sensor->rom_id, scratchpad);
    if (err != ESP_OK) {
        xSemaphoreGive(s_onewire_mutex);
        return MAX31850_ERR_TIMEOUT;
    }
    
    xSemaphoreGive(s_onewire_mutex);
    
    // 保存原始数据
    sensor->raw_temp = ((int16_t)scratchpad[0] << 8) | scratchpad[1];
    sensor->fault_reg = scratchpad[4];
    
    // 解析温度
    float temp;
    max31850_err_t parse_err = max31850_parse_scratchpad(scratchpad, &temp);
    max31850_update_sensor_status(sensor_id, parse_err);
    
    if (parse_err == MAX31850_OK) {
        sensor->temperature = temp;
        *temp_out = temp;
    }
    
    return parse_err;
}

/**
 * @brief 检查传感器是否在线
 */
bool max31850_is_online(uint8_t sensor_id)
{
    if (!s_initialized || sensor_id >= MAX31850_SENSOR_COUNT) {
        return false;
    }
    return s_sensors[sensor_id].online;
}

/**
 * @brief 获取最后一次错误
 */
max31850_err_t max31850_get_last_error(uint8_t sensor_id)
{
    if (!s_initialized || sensor_id >= MAX31850_SENSOR_COUNT) {
        return MAX31850_ERR_INVALID_ID;
    }
    return s_sensors[sensor_id].last_error;
}

/**
 * @brief 获取ROM ID
 */
esp_err_t max31850_get_rom_id(uint8_t sensor_id, uint8_t *rom_out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (sensor_id >= MAX31850_SENSOR_COUNT || !rom_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(rom_out, s_sensors[sensor_id].rom_id, MAX31850_ROM_ID_LEN);
    return ESP_OK;
}

/**
 * @brief 反初始化
 */
void max31850_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing MAX31850...");
    
    // 删除轮询任务
    if (s_poll_task_handle) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }
    
    // 删除互斥锁
    if (s_onewire_mutex) {
        vSemaphoreDelete(s_onewire_mutex);
        s_onewire_mutex = NULL;
    }
    
    // 释放RMT资源（如果使用的话）
    // 当前实现使用GPIO直接控制，无需释放RMT
    
    s_initialized = false;
    s_sensor_count = 0;
    
    ESP_LOGI(TAG, "MAX31850 deinitialized");
}

//////////////////////////////////////////////////////////////
//////////////////////// 调试工具函数 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 打印暂存器原始数据
 */
void max31850_dump_scratchpad(uint8_t sensor_id)
{
    if (!s_initialized || sensor_id >= MAX31850_SENSOR_COUNT) {
        ESP_LOGE(TAG, "Invalid sensor ID or not initialized");
        return;
    }
    
    if (!s_sensors[sensor_id].online) {
        ESP_LOGW(TAG, "Sensor %d is offline", sensor_id);
        return;
    }
    
    // 临时读取暂存器
    if (xSemaphoreTake(s_onewire_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for dump");
        return;
    }
    
    uint8_t scratchpad[MAX31850_SCRATCHPAD_LEN];
    esp_err_t err = max31850_read_scratchpad(s_sensors[sensor_id].rom_id, scratchpad);
    
    xSemaphoreGive(s_onewire_mutex);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read scratchpad for dump");
        return;
    }
    
    ESP_LOGI(TAG, "Sensor %d Scratchpad Dump:", sensor_id);
    ESP_LOGI(TAG, "  [0] Temp LSB: 0x%02X", scratchpad[0]);
    ESP_LOGI(TAG, "  [1] Temp MSB: 0x%02X", scratchpad[1]);
    ESP_LOGI(TAG, "  [2] Reserved: 0x%02X", scratchpad[2]);
    ESP_LOGI(TAG, "  [3] Config:   0x%02X", scratchpad[3]);
    ESP_LOGI(TAG, "  [4] Fault:    0x%02X", scratchpad[4]);
    ESP_LOGI(TAG, "  [5] Reserved: 0x%02X", scratchpad[5]);
    ESP_LOGI(TAG, "  [6] Reserved: 0x%02X", scratchpad[6]);
    ESP_LOGI(TAG, "  [7] Reserved: 0x%02X", scratchpad[7]);
    ESP_LOGI(TAG, "  [8] CRC:      0x%02X", scratchpad[8]);
    
    // 验证CRC
    uint8_t calc_crc = crc8_calculate(scratchpad, 8);
    ESP_LOGI(TAG, "  CRC Check: calc=0x%02X, actual=0x%02X (%s)",
             calc_crc, scratchpad[8], (calc_crc == scratchpad[8]) ? "OK" : "FAIL");
    
    // 解析温度
    int16_t raw = ((int16_t)scratchpad[0] << 8) | scratchpad[1];
    float temp = (float)raw * 0.0625f;
    ESP_LOGI(TAG, "  Raw Temp: 0x%04X = %.2f°C", (uint16_t)raw, temp);
    
    // 解析故障
    if (scratchpad[4] & MAX31850_FAULT_OPEN) {
        ESP_LOGW(TAG, "  Fault: Thermocouple OPEN");
    }
    if (scratchpad[4] & MAX31850_FAULT_SHORT_GND) {
        ESP_LOGW(TAG, "  Fault: Short to GND");
    }
    if (scratchpad[4] & MAX31850_FAULT_SHORT_VCC) {
        ESP_LOGW(TAG, "  Fault: Short to VCC");
    }
}

/**
 * @brief 打印所有ROM ID
 */
void max31850_print_rom_ids(void)
{
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "MAX31850 ROM ID List (%d device(s) found):", s_sensor_count);
    
    for (int i = 0; i < s_sensor_count; i++) {
        uint8_t *rom = s_sensors[i].rom_id;
        ESP_LOGI(TAG, "  Sensor %d (P%d): %02X-%02X%02X%02X%02X%02X%02X-%02X  %s",
                 i, i + 1,
                 rom[0], rom[1], rom[2], rom[3],
                 rom[4], rom[5], rom[6], rom[7],
                 s_sensors[i].online ? "ONLINE" : "OFFLINE");
        
        // 验证家族码
        if (rom[0] == 0x3B) {
            ESP_LOGI(TAG, "    Family: 0x3B (MAX31850/MAX31851)");
        } else if (rom[0] != 0) {
            ESP_LOGW(TAG, "    Family: 0x%02X (Unknown device)", rom[0]);
        }
    }
    
    if (s_sensor_count < MAX31850_SENSOR_COUNT) {
        ESP_LOGW(TAG, "  Warning: Expected %d sensors, found %d",
                 MAX31850_SENSOR_COUNT, s_sensor_count);
    }
    
    ESP_LOGI(TAG, "===============================================");
}

/**
 * @brief 错误码转字符串
 */
const char* max31850_err_to_string(max31850_err_t err)
{
    switch (err) {
        case MAX31850_OK:           return "OK";
        case MAX31850_ERR_OPEN:     return "Thermocouple OPEN (断线)";
        case MAX31850_ERR_SHORT_GND:return "Short to GND";
        case MAX31850_ERR_SHORT_VCC:return "Short to VCC";
        case MAX31850_ERR_CRC:      return "CRC Error";
        case MAX31850_ERR_TIMEOUT:  return "Timeout";
        case MAX31850_ERR_OFFLINE:  return "Sensor Offline";
        case MAX31850_ERR_INVALID_ID:return "Invalid Sensor ID";
        case MAX31850_ERR_BUS_FAULT:return "Bus Fault";
        default:                    return "Unknown Error";
    }
}
