/**
 * @file heating_detect.c
 * @brief MAX31850KATB+ Temperature Sensor Driver Implementation
 * 
 * 1-Wire Bit-Bang implementation for ESP32-S3 @ 240MHz
 * Critical section protection during 1-Wire transactions
 * 
 * @version 3.0
 * @date 2026-04-01
 */

#include "heating_detect.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "MAX31850";

//////////////////////////////////////////////////////////////
//////////////////////// 私有变量 ////////////////////////////
//////////////////////////////////////////////////////////////

static gpio_num_t s_gpio_num = GPIO_NUM_NC;                     /**< 1-Wire GPIO */
static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT];      /**< 传感器数组 */
static SemaphoreHandle_t s_mutex = NULL;                        /**< 数据保护互斥锁 */
static TaskHandle_t s_poll_task_handle = NULL;                  /**< 轮询任务句柄 */
static bool s_initialized = false;                              /**< 初始化标志 */

// ROM Search状态
static uint8_t s_rom_ids[MAX31850_SENSOR_COUNT][8];             /**< 发现的ROM ID */
static uint8_t s_sensor_count = 0;                              /**< 实际发现的传感器数 */

//////////////////////////////////////////////////////////////
//////////////////////// 精确延时 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 微秒级精确延时（基于nop循环）
 * 
 * ESP32-S3 @ 240MHz: 1μs = 240 cycles
 * 每个nop约1 cycle，循环开销约5 cycles
 * 
 * @param us 延时微秒数
 */
static inline void onewire_delay_us(uint32_t us)
{
    // 240MHz下，约240 cycles/μs
    // 循环体内5个nop + 开销 ≈ 10 cycles/iteration
    // 因此每μs需要约24次循环
    uint32_t cycles = us * 24;
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
    }
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
 * @brief 释放总线（上拉电阻拉高）
 */
static inline void onewire_release(void)
{
    gpio_set_direction(s_gpio_num, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_gpio_num, 1);
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
 * @return 0=低电平, 1=高电平
 */
static inline uint8_t onewire_read_level(void)
{
    return (uint8_t)gpio_get_level(s_gpio_num);
}

//////////////////////////////////////////////////////////////
//////////////////////// 1-Wire协议 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 1-Wire Reset脉冲 + Presence检测
 * 
 * 时序: 拉低480μs → 释放 → 等待70μs → 采样Presence → 等待410μs
 * 
 * @param presence 输出：true=检测到设备，false=无设备
 * @return esp_err_t ESP_OK成功
 */
static esp_err_t onewire_reset(bool *presence)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    
    portENTER_CRITICAL(&mux);
    
    // 拉低480μs（Reset脉冲）
    onewire_set_low();
    onewire_delay_us(ONEWIRE_RESET_LOW_US);
    
    // 释放总线
    onewire_release();
    onewire_delay_us(ONEWIRE_PRESENCE_WAIT_US);
    
    // 采样Presence（70μs时）
    uint8_t presence_level = onewire_read_level();
    
    // 等待Reset周期完成
    onewire_delay_us(ONEWIRE_RESET_RECOVERY_US);
    
    portEXIT_CRITICAL(&mux);
    
    // Presence: 设备会拉低总线60-240μs
    // 所以在70μs采样时，应该读到0
    *presence = (presence_level == 0);
    
    return ESP_OK;
}

/**
 * @brief 写入单个位
 * 
 * @param bit 要写入的位（0或1）
 */
static void onewire_write_bit(uint8_t bit)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    
    portENTER_CRITICAL(&mux);
    
    if (bit & 0x01) {
        // 写1: 拉低5μs → 释放 → 等待55μs
        onewire_set_low();
        onewire_delay_us(ONEWIRE_WRITE1_LOW_US);
        onewire_release();
        onewire_delay_us(ONEWIRE_WRITE1_RECOVERY_US);
    } else {
        // 写0: 拉低70μs → 释放 → 等待5μs
        onewire_set_low();
        onewire_delay_us(ONEWIRE_WRITE0_LOW_US);
        onewire_release();
        onewire_delay_us(ONEWIRE_WRITE0_RECOVERY_US);
    }
    
    // 位间间隔
    onewire_delay_us(ONEWIRE_BIT_INTERVAL_US);
    
    portEXIT_CRITICAL(&mux);
}

/**
 * @brief 读取单个位
 * 
 * @return 读取到的位（0或1）
 */
static uint8_t onewire_read_bit(void)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint8_t bit;
    
    portENTER_CRITICAL(&mux);
    
    // 拉低3μs
    onewire_set_low();
    onewire_delay_us(ONEWIRE_READ_INIT_US);
    
    // 释放总线
    onewire_release();
    onewire_delay_us(ONEWIRE_READ_SAMPLE_US);
    
    // 采样（总共13μs）
    bit = onewire_read_level();
    
    // 等待时隙结束
    onewire_delay_us(ONEWIRE_READ_RECOVERY_US);
    
    // 位间间隔
    onewire_delay_us(ONEWIRE_BIT_INTERVAL_US);
    
    portEXIT_CRITICAL(&mux);
    
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
    return data;
}

//////////////////////////////////////////////////////////////
//////////////////////// CRC8校验 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief CRC8计算表（X8+X5+X4+1 = 0x31）
 */
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

/**
 * @brief 计算CRC8校验值
 * 
 * @param data 数据指针
 * @param len 数据长度
 * @return uint8_t CRC8值
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
//////////////////////// ROM操作 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief Match ROM - 选择特定设备
 * 
 * @param rom_id 64-bit ROM ID
 * @return esp_err_t ESP_OK成功
 */
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
 * @brief Search ROM算法 - 自动发现所有设备
 * 
 * @return esp_err_t ESP_OK成功
 * 
 * @note 使用二叉树搜索算法遍历所有设备
 */
static esp_err_t onewire_search_rom(void)
{
    bool presence;
    uint8_t last_discrepancy = 0;
    uint8_t rom_id[8] = {0};
    uint8_t last_zero = 0;
    
    s_sensor_count = 0;
    
    do {
        // Reset并检查Presence
        ESP_ERROR_CHECK(onewire_reset(&presence));
        if (!presence) {
            ESP_LOGW(TAG, "No device present during search");
            break;
        }
        
        // 发送Search ROM命令
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
        last_zero = 0;
        
        // 遍历64个bit
        for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
            // 读取实际位和补码位
            uint8_t bit_actual = onewire_read_bit();
            uint8_t bit_complement = onewire_read_bit();
            
            uint8_t selected_bit;
            
            if (bit_actual == 0 && bit_complement == 0) {
                // 有分歧，多个设备在此位不同
                if (bit_pos == last_discrepancy) {
                    selected_bit = 1;
                } else if (bit_pos > last_discrepancy) {
                    selected_bit = 0;
                    last_zero = bit_pos;
                } else {
                    // bit_pos < last_discrepancy
                    selected_bit = (rom_id[bit_pos / 8] >> (bit_pos % 8)) & 0x01;
                    if (selected_bit == 0) {
                        last_zero = bit_pos;
                    }
                }
            } else if (bit_actual == 0 && bit_complement == 1) {
                // 所有设备在此位为0
                selected_bit = 0;
            } else if (bit_actual == 1 && bit_complement == 0) {
                // 所有设备在此位为1
                selected_bit = 1;
            } else {
                // 11 = 无设备
                ESP_LOGW(TAG, "Search error at bit %d: no devices", bit_pos);
                return ESP_ERR_NOT_FOUND;
            }
            
            // 写入选择的位
            onewire_write_bit(selected_bit);
            
            // 更新ROM ID
            if (selected_bit) {
                rom_id[bit_pos / 8] |= (1 << (bit_pos % 8));
            } else {
                rom_id[bit_pos / 8] &= ~(1 << (bit_pos % 8));
            }
        }
        
        // 验证ROM CRC（byte 7是bytes 0-6的CRC）
        if (crc8_calculate(rom_id, 7) != rom_id[7]) {
            ESP_LOGW(TAG, "ROM ID CRC error: calc=0x%02X, recv=0x%02X", 
                     crc8_calculate(rom_id, 7), rom_id[7]);
            // 继续搜索，不保存此设备
        } else {
            // 保存ROM ID
            if (s_sensor_count < MAX31850_SENSOR_COUNT) {
                memcpy(s_rom_ids[s_sensor_count], rom_id, 8);
                ESP_LOGI(TAG, "Found device %d: ROM ID %02X%02X%02X%02X%02X%02X%02X%02X",
                         s_sensor_count + 1,
                         rom_id[0], rom_id[1], rom_id[2], rom_id[3],
                         rom_id[4], rom_id[5], rom_id[6], rom_id[7]);
                s_sensor_count++;
            }
        }
        
        last_discrepancy = last_zero;
        
    } while (last_discrepancy != 0 && s_sensor_count < MAX31850_SENSOR_COUNT);
    
    ESP_LOGI(TAG, "ROM search complete. Found %d device(s)", s_sensor_count);
    
    return (s_sensor_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

//////////////////////////////////////////////////////////////
//////////////////////// MAX31850操作 ////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 从硬件地址推断传感器索引
 * 
 * MAX31850的硬件地址由AD0/AD1引脚决定：
 * - AD1=0, AD0=0 -> 地址0
 * - AD1=0, AD0=1 -> 地址1
 * - AD1=1, AD0=0 -> 地址2
 * - AD1=1, AD0=1 -> 地址3
 * 
 * ROM ID的byte 1-2包含地址信息
 * 
 * @param rom_id ROM ID
 * @return uint8_t 硬件地址（0-3）
 */
static uint8_t max31850_get_hw_addr(const uint8_t *rom_id)
{
    // MAX31850的地址编码在ROM ID中
    // 通常byte 1包含地址信息
    // 简化处理：根据ROM ID的序列号推断
    // 实际应根据硬件连接顺序匹配
    
    // 这里使用简单的哈希来分配索引
    // 实际应用中应该根据已知ROM ID列表映射
    uint8_t addr = (rom_id[1] & 0x03);
    
    // 限制在有效范围内
    if (addr >= MAX31850_SENSOR_COUNT) {
        addr = 0;
    }
    
    return addr;
}

/**
 * @brief 读取传感器数据帧（9字节）
 * 
 * @param rom_id ROM ID
 * @param data 9字节数据缓冲区
 * @return esp_err_t ESP_OK成功
 */
static esp_err_t max31850_read_data_frame(const uint8_t *rom_id, uint8_t *data)
{
    // Match ROM选择设备
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    
    // 发送Read Data命令
    onewire_write_byte(MAX31850_CMD_READ_DATA);
    
    // 读取9字节
    for (int i = 0; i < 9; i++) {
        data[i] = onewire_read_byte();
    }
    
    return ESP_OK;
}

/**
 * @brief 解析数据帧
 * 
 * @param data 9字节数据
 * @param sensor 传感器结构体
 * @return max31850_err_t 错误代码
 */
static max31850_err_t max31850_parse_data(const uint8_t *data, max31850_sensor_t *sensor)
{
    // 保存原始数据
    memcpy(sensor->scratchpad, data, 9);
    
    // CRC校验（覆盖bytes 0-7）
    uint8_t crc_calc = crc8_calculate(data, 8);
    if (crc_calc != data[8]) {
        ESP_LOGW(TAG, "CRC error: calc=0x%02X, recv=0x%02X", crc_calc, data[8]);
        return MAX31850_ERR_CRC;
    }
    
    // 提取热电偶温度（Byte 0-1，14-bit有符号，右移2位）
    // 数据格式：MSB在Byte 1，LSB在Byte 0
    int16_t raw_temp = ((int16_t)data[1] << 8) | data[0];
    
    // 14-bit有符号值（右移2位）
    // 注意：温度数据是左对齐的，需要右移2位得到实际值
    raw_temp = raw_temp >> 2;
    
    // 转换为摄氏度（分辨率0.25°C）
    sensor->thermocouple_temp = (float)raw_temp * 0.25f;
    
    // 提取冷端温度（Byte 2-3，12-bit有符号，右移4位）
    int16_t raw_cj = ((int16_t)data[3] << 8) | data[2];
    raw_cj = raw_cj >> 4;
    sensor->cold_junction_temp = (float)raw_cj * 0.0625f;
    
    // 故障寄存器
    sensor->fault_reg = data[4];
    
    // 检查故障
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

/**
 * @brief 轮询任务 - 周期性读取所有传感器
 */
static void max31850_poll_task(void *pvParameters)
{
    uint8_t current_sensor = 0;
    uint8_t data[9];
    
    ESP_LOGI(TAG, "Poll task started");
    
    while (1) {
        // 检查传感器是否有效
        if (current_sensor < s_sensor_count) {
            // 找到对应的传感器索引
            int sensor_idx = -1;
            for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
                if (s_sensors[i].hw_addr == current_sensor) {
                    sensor_idx = i;
                    break;
                }
            }
            
            if (sensor_idx >= 0 && s_sensors[sensor_idx].online) {
                // 读取数据帧
                esp_err_t err = max31850_read_data_frame(s_sensors[sensor_idx].rom_id, data);
                
                if (err == ESP_OK) {
                    // 解析数据
                    max31850_err_t result = max31850_parse_data(data, &s_sensors[sensor_idx]);
                    
                    // 更新互斥锁保护的数据
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
        
        // 下一个传感器
        current_sensor++;
        if (current_sensor >= s_sensor_count) {
            current_sensor = 0;
        }
        
        // 延迟到下一次轮询
        vTaskDelay(pdMS_TO_TICKS(MAX31850_POLL_INTERVAL_MS));
    }
}

//////////////////////////////////////////////////////////////
//////////////////////// 公共API /////////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t gpio_num)
{
    ESP_LOGI(TAG, "Initializing MAX31850 on GPIO%d...", gpio_num);
    
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    s_gpio_num = gpio_num;
    
    // 配置GPIO为开漏输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // 启用内部上拉（辅助外部4.7K）
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // 释放总线
    onewire_release();
    
    // 短暂延迟等待总线稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 检查总线是否被短路到GND
    if (onewire_read_level() == 0) {
        ESP_LOGE(TAG, "BUS FAULT: Line shorted to GND");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 执行ROM Search
    esp_err_t err = onewire_search_rom();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ROM search failed: %d", err);
        // 继续初始化，后台会重试
    }
    
    // 初始化传感器结构体
    memset(s_sensors, 0, sizeof(s_sensors));
    
    for (int i = 0; i < s_sensor_count && i < MAX31850_SENSOR_COUNT; i++) {
        memcpy(s_sensors[i].rom_id, s_rom_ids[i], 8);
        s_sensors[i].hw_addr = max31850_get_hw_addr(s_rom_ids[i]);
        s_sensors[i].online = true;
        s_sensors[i].data_valid = false;
        s_sensors[i].fail_count = 0;
        
        ESP_LOGI(TAG, "Sensor [%d]: ROM=%02X%02X%02X%02X%02X%02X%02X%02X, HW_ADDR=%02X",
                 i,
                 s_sensors[i].rom_id[0], s_sensors[i].rom_id[1],
                 s_sensors[i].rom_id[2], s_sensors[i].rom_id[3],
                 s_sensors[i].rom_id[4], s_sensors[i].rom_id[5],
                 s_sensors[i].rom_id[6], s_sensors[i].rom_id[7],
                 s_sensors[i].hw_addr);
    }
    
    // 如果找到传感器，执行初始读取
    for (int i = 0; i < s_sensor_count && i < MAX31850_SENSOR_COUNT; i++) {
        uint8_t data[9];
        float temp;
        
        // 重试几次
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
    
    // 创建轮询任务
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
    
    // 重试直到成功或超时
    while ((xTaskGetTickCount() - start_tick) < timeout) {
        if (max31850_read_data_frame(s_sensors[sensor_id].rom_id, data) == ESP_OK) {
            max31850_sensor_t temp_sensor;
            memcpy(temp_sensor.rom_id, s_sensors[sensor_id].rom_id, 8);
            
            max31850_err_t result = max31850_parse_data(data, &temp_sensor);
            
            if (result == MAX31850_OK) {
                *temp = temp_sensor.thermocouple_temp;
                
                // 更新缓存
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
    
    // 删除轮询任务
    if (s_poll_task_handle != NULL) {
        vTaskDelete(s_poll_task_handle);
        s_poll_task_handle = NULL;
    }
    
    // 删除互斥锁
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    // 复位GPIO
    gpio_reset_pin(s_gpio_num);
    s_gpio_num = GPIO_NUM_NC;
    
    s_initialized = false;
    s_sensor_count = 0;
    
    ESP_LOGI(TAG, "MAX31850 deinitialized");
}

//////////////////////////////////////////////////////////////
//////////////////////// 调试任务 ////////////////////////////
//////////////////////////////////////////////////////////////

void heating_print_task(void *pvParameters)
{
    float temp;
    max31850_err_t err;
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("HEATING", "Temperature print task started");
    
    while (1) {
        ESP_LOGI("HEATING", "========== Temperature Report ==========");
        for (uint8_t i = 0; i < MAX31850_SENSOR_COUNT; i++) {
            err = max31850_get_temperature(i, &temp);
            if (err == MAX31850_OK) {
                ESP_LOGI("HEATING", "Sensor %d (P%d): %.2f°C  [OK]", 
                         i, i + 1, temp);
            } else {
                ESP_LOGW("HEATING", "Sensor %d (P%d): %s  [%s]",
                         i, i + 1, max31850_err_to_string(err),
                         max31850_is_online(i) ? "ONLINE" : "OFFLINE");
            }
        }
        ESP_LOGI("HEATING", "=======================================");
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
