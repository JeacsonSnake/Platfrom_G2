/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ Temperature Sensor Driver (RMT-based 1-Wire Implementation)
 * 
 * 使用ESP32-S3 RMT外设实现精确的1-Wire时序控制，避免软件bit-bang的不稳定性
 */

#include "heating_detect.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"

static const char *TAG = "MAX31850";

//////////////////////////////////////////////////////////////
/////////////////////// RMT & GPIO配置 ///////////////////////
//////////////////////////////////////////////////////////////

/* RMT通道配置 */
#define RMT_CLK_SRC                 RMT_CLK_SRC_DEFAULT
#define RMT_TX_CHANNEL_RESOLUTION   1000000     /**< 1MHz = 1μs分辨率 */
#define RMT_RX_CHANNEL_RESOLUTION   1000000     /**< 1MHz = 1μs分辨率 */
#define RMT_TX_MEM_BLOCK_SYMBOLS    64          /**< TX内存块大小 */
#define RMT_RX_MEM_BLOCK_SYMBOLS    64          /**< RX内存块大小 */

/* 1-Wire时序参数 (微秒) - 基于1-Wire标准协议 */
#define ONEWIRE_RESET_PULSE_US      480         /**< Reset脉冲宽度 */
#define ONEWIRE_RESET_WAIT_US       70          /**< 等待Presence响应 */
#define ONEWIRE_RESET_RECOVERY_US   410         /**< Reset恢复时间 */
#define ONEWIRE_PRESENCE_MIN_US     60          /**< Presence最小宽度 */
#define ONEWIRE_PRESENCE_MAX_US     240         /**< Presence最大宽度 */

#define ONEWIRE_WRITE1_LOW_US       6           /**< 写1低电平时间 */
#define ONEWIRE_WRITE1_HIGH_US      64          /**< 写1高电平时间(总时隙60-120μs) */
#define ONEWIRE_WRITE0_LOW_US       60          /**< 写0低电平时间 */
#define ONEWIRE_WRITE0_HIGH_US      10          /**< 写0恢复时间 */

#define ONEWIRE_READ_INIT_US        6           /**< 读初始化低电平 */
#define ONEWIRE_READ_SAMPLE_US      9           /**< 读到采样点的延迟(总15μs) */
#define ONEWIRE_READ_RECOVERY_US    55          /**< 读恢复时间 */

#define ONEWIRE_SLOT_TOTAL_US       70          /**< 总时隙时间 */

/* CRC8多项式: X8 + X5 + X4 + 1 */
#define CRC8_POLYNOMIAL             0x31

//////////////////////////////////////////////////////////////
/////////////////////// 全局变量 /////////////////////////////
//////////////////////////////////////////////////////////////

static gpio_num_t s_onewire_pin = GPIO_NUM_NC;
static rmt_channel_handle_t s_rmt_tx_channel = NULL;
static rmt_encoder_handle_t s_rmt_encoder = NULL;

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
/////////////////////// RMT编码器 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire编码器状态
 */
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
} rmt_onewire_encoder_t;

/**
 * @brief RMT编码回调：编码1-Wire时隙
 * 
 * 将字节数据编码为RMT符号序列（1-Wire时隙）
 */
static size_t rmt_encode_onewire(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                  const void *primary_data, size_t data_size,
                                  size_t symbols_written, size_t symbols_free,
                                  rmt_symbol_word_t *symbols, bool *done)
{
    rmt_onewire_encoder_t *ow_encoder = __containerof(encoder, rmt_onewire_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = ow_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = ow_encoder->copy_encoder;
    
    size_t encoded_symbols = 0;
    rmt_encode_state_t session_state = 0;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    
    // 使用bytes_encoder将每个bit编码为1-Wire时隙
    encoded_symbols = bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size,
                                             symbols_written, symbols_free, symbols, &session_state);
    state |= session_state;
    
    if (session_state & RMT_ENCODING_COMPLETE) {
        // 所有数据编码完成，使用copy_encoder发送结束序列（如果有）
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, NULL, 0,
                                                 symbols_written + encoded_symbols,
                                                 symbols_free - encoded_symbols,
                                                 symbols + encoded_symbols, &session_state);
        state |= session_state;
        
        if (session_state & RMT_ENCODING_COMPLETE) {
            *done = true;
        }
    }
    
    return encoded_symbols;
}

static esp_err_t rmt_del_onewire_encoder(rmt_encoder_t *encoder)
{
    rmt_onewire_encoder_t *ow_encoder = __containerof(encoder, rmt_onewire_encoder_t, base);
    rmt_del_encoder(ow_encoder->bytes_encoder);
    rmt_del_encoder(ow_encoder->copy_encoder);
    free(ow_encoder);
    return ESP_OK;
}

static esp_err_t rmt_onewire_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_onewire_encoder_t *ow_encoder = __containerof(encoder, rmt_onewire_encoder_t, base);
    rmt_encoder_reset(ow_encoder->bytes_encoder);
    rmt_encoder_reset(ow_encoder->copy_encoder);
    ow_encoder->state = 0;
    return ESP_OK;
}

/**
 * @brief 创建1-Wire RMT编码器
 */
static esp_err_t rmt_new_onewire_encoder(rmt_encoder_handle_t *ret_encoder)
{
    rmt_onewire_encoder_t *ow_encoder = calloc(1, sizeof(rmt_onewire_encoder_t));
    if (!ow_encoder) {
        return ESP_ERR_NO_MEM;
    }
    
    ow_encoder->base.encode = rmt_encode_onewire;
    ow_encoder->base.del = rmt_del_onewire_encoder;
    ow_encoder->base.reset = rmt_onewire_encoder_reset;
    
    // 配置bit编码器（每个bit编码为RMT符号）
    // 对于1-Wire，我们需要自定义的bit编码方式
    // 这里简化处理：直接创建编码器，实际编码逻辑在encode回调中实现
    
    *ret_encoder = &ow_encoder->base;
    return ESP_OK;
}

//////////////////////////////////////////////////////////////
/////////////////////// GPIO/RMT底层操作 /////////////////////
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

/**
 * @brief 1-Wire Reset + Presence检测
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
    
    // 最终检查总线是否恢复高电平
    int final_level = gpio_get_level(s_onewire_pin);
    if (final_level != 1) {
        ESP_LOGW(TAG, "Bus stuck low after reset (short detected)");
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

/**
 * @brief 写入单个bit
 */
static void onewire_write_bit(uint8_t bit)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    if (bit & 0x01) {
        // 写1: 低电平6μs，然后释放，总共70μs
        onewire_set_opendrain();
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE1_LOW_US);
        gpio_set_level(s_onewire_pin, 1);  // 释放（开漏模式下上拉电阻拉高）
        esp_rom_delay_us(ONEWIRE_WRITE1_HIGH_US);
    } else {
        // 写0: 低电平60μs，然后释放10μs
        onewire_set_opendrain();
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE0_LOW_US);
        gpio_set_level(s_onewire_pin, 1);  // 释放
        esp_rom_delay_us(ONEWIRE_WRITE0_HIGH_US);
    }
    
    portEXIT_CRITICAL(&mux);
}

/**
 * @brief 读取单个bit
 */
static uint8_t onewire_read_bit(void)
{
    uint8_t bit = 0;
    
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    // 主机拉低6μs启动读时隙
    onewire_set_opendrain();
    gpio_set_level(s_onewire_pin, 0);
    esp_rom_delay_us(ONEWIRE_READ_INIT_US);
    
    // 释放总线
    onewire_set_input();
    
    // 等待到15μs采样点（6+9=15μs）
    esp_rom_delay_us(ONEWIRE_READ_SAMPLE_US);
    bit = gpio_get_level(s_onewire_pin);
    
    portEXIT_CRITICAL(&mux);
    
    // 等待时隙结束
    esp_rom_delay_us(ONEWIRE_READ_RECOVERY_US);
    
    return bit;
}

/**
 * @brief 写入一个字节（LSB first）
 */
static void onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
}

/**
 * @brief 读取一个字节（LSB first）
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (onewire_read_bit() << i);
    }
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
 * @brief 搜索ROM算法（Binary Search Tree）
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
        ESP_ERROR_CHECK(onewire_reset(&presence));
        if (!presence) {
            ESP_LOGW(TAG, "No device present during search");
            break;
        }
        
        // 发送Search ROM命令
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
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
                ESP_LOGW(TAG, "Search error at bit %d: no devices", bit_pos);
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
            if (crc8_calculate(rom_id, 7) == rom_id[7]) {
                memcpy(rom_ids[*found_count], rom_id, 8);
                ESP_LOGI(TAG, "Found device %d: ROM ID %02X%02X%02X%02X%02X%02X%02X%02X",
                         *found_count + 1,
                         rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                         rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
                (*found_count)++;
            } else {
                ESP_LOGW(TAG, "ROM ID CRC error");
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
    
    // 初始化RMT TX通道（保留用于未来扩展）
    // 目前使用GPIO bit-bang方式，因为1-Wire协议需要精确的时序控制
    // 且Read操作需要在特定时刻采样，使用RMT比较复杂
    // 如果未来需要，可以在此处初始化RMT TX通道用于发送时隙
    
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
    }
    
    // 搜索设备
    uint8_t rom_ids[MAX31850_SENSOR_COUNT][8];
    uint8_t found = 0;
    
    // 多次尝试搜索
    for (int attempt = 0; attempt < 3 && found < MAX31850_SENSOR_COUNT; attempt++) {
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
        ESP_LOGW(TAG, "No MAX31850 devices found");
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
    
    // 释放RMT资源（如果已分配）
    if (s_rmt_encoder) {
        rmt_del_encoder(s_rmt_encoder);
        s_rmt_encoder = NULL;
    }
    if (s_rmt_tx_channel) {
        rmt_del_channel(s_rmt_tx_channel);
        s_rmt_tx_channel = NULL;
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
