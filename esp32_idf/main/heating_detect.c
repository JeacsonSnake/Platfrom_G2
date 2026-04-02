#include "heating_detect.h"
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ROM 延时函数声明 (ESP32-S3 ROM 内置)
extern void esp_rom_delay_us(uint32_t us);

static const char *TAG = "MAX31850";

static gpio_num_t s_ow_gpio = GPIO_NUM_NC;
static max31850_sensor_t s_sensors[MAX31850_SENSOR_COUNT];
static SemaphoreHandle_t s_sensor_mutex = NULL;
static portMUX_TYPE s_ow_spinlock = portMUX_INITIALIZER_UNLOCKED;

//////////////////////////////////////////////////////////////
//////////////////// 调试开关 ////////////////////////////////
//////////////////////////////////////////////////////////////

#define OW_DEBUG_GPIO_DIAG          1
#define OW_DEBUG_RESET_PRESENCE     1
#define OW_DEBUG_ROM_SEARCH_BIT     1
#define OW_DEBUG_SCRATCHPAD_RAW     1
#define OW_DEBUG_BUS_LEVEL          1

//////////////////////////////////////////////////////////////
//////////////////// 底层 1-Wire Bit-Bang ////////////////////
//////////////////////////////////////////////////////////////

static inline void ow_set_low(void)
{
    gpio_set_level(s_ow_gpio, 0);
}

static inline void ow_set_release(void)
{
    gpio_set_level(s_ow_gpio, 1);
}

static inline int ow_read_level(void)
{
    return gpio_get_level(s_ow_gpio);
}

// 检查总线电平（调试用）
static void ow_check_bus_level(const char *label)
{
    if (!OW_DEBUG_BUS_LEVEL) return;
    int level = ow_read_level();
    ESP_LOGI(TAG, "[BUS_LEVEL] %s -> GPIO%d = %d", label, s_ow_gpio, level);
}

// 1-Wire Reset 时序，返回 presence 脉冲（带详细波形日志）
static uint8_t ow_reset(void)
{
    uint8_t presence = 0;

    if (OW_DEBUG_RESET_PRESENCE) {
        ESP_LOGI(TAG, "[OW_RESET] Start reset on GPIO%d", s_ow_gpio);
    }

    portENTER_CRITICAL_SAFE(&s_ow_spinlock);
    ow_set_low();
    if (OW_DEBUG_RESET_PRESENCE) ow_check_bus_level("After set low");
    esp_rom_delay_us(480);
    ow_set_release();
    if (OW_DEBUG_RESET_PRESENCE) ow_check_bus_level("After release (t=0)");
    portEXIT_CRITICAL_SAFE(&s_ow_spinlock);

    esp_rom_delay_us(70);
    int level_after_70us = ow_read_level();
    if (OW_DEBUG_RESET_PRESENCE) {
        ESP_LOGI(TAG, "[OW_RESET] After 70us level = %d", level_after_70us);
    }
    presence = (level_after_70us == 0);

    esp_rom_delay_us(410);
    int level_after_480us = ow_read_level();
    if (OW_DEBUG_RESET_PRESENCE) {
        ESP_LOGI(TAG, "[OW_RESET] After 480us level = %d (presence=%d)",
                 level_after_480us, presence);
    }
    return presence;
}

// 写 1 bit
static void ow_write_bit(uint8_t bit)
{
    portENTER_CRITICAL_SAFE(&s_ow_spinlock);
    ow_set_low();
    if (bit) {
        esp_rom_delay_us(5);
        ow_set_release();
        esp_rom_delay_us(55);
    } else {
        esp_rom_delay_us(60);
        ow_set_release();
        esp_rom_delay_us(10);
    }
    portEXIT_CRITICAL_SAFE(&s_ow_spinlock);
}

// 读 1 bit
static uint8_t ow_read_bit(void)
{
    uint8_t bit = 0;

    portENTER_CRITICAL_SAFE(&s_ow_spinlock);
    ow_set_low();
    esp_rom_delay_us(2);
    ow_set_release();
    esp_rom_delay_us(10);
    bit = ow_read_level();
    portEXIT_CRITICAL_SAFE(&s_ow_spinlock);

    esp_rom_delay_us(50);
    return bit;
}

// 写 1 byte (LSB First)
static void ow_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(data & 0x01);
        data >>= 1;
    }
}

// 读 1 byte (LSB First)
static uint8_t ow_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data >>= 1;
        if (ow_read_bit()) {
            data |= 0x80;
        }
    }
    return data;
}

//////////////////////////////////////////////////////////////
//////////////////// CRC8 校验 ///////////////////////////////
//////////////////////////////////////////////////////////////

// CRC8 多项式: X^8 + X^5 + X^4 + 1 (Maxim 1-Wire 标准)
static uint8_t ow_crc8_byte(uint8_t crc, uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        uint8_t mix = (crc ^ data) & 0x01;
        crc >>= 1;
        if (mix) {
            crc ^= 0x8C;
        }
        data >>= 1;
    }
    return crc;
}

//////////////////////////////////////////////////////////////
//////////////////// ROM Search 算法 /////////////////////////
//////////////////////////////////////////////////////////////

static int ow_search_rom(uint64_t *rom_codes, int max_devices)
{
    int last_discrepancy = 0;
    uint8_t rom_no[8] = {0};
    int num_devices = 0;
    int done = 0;
    int search_cycle = 0;

    if (OW_DEBUG_ROM_SEARCH_BIT) {
        ESP_LOGI(TAG, "[ROM_SEARCH] Start Search ROM...");
    }

    while (!done && num_devices < max_devices) {
        search_cycle++;
        if (!ow_reset()) {
            ESP_LOGW(TAG, "[ROM_SEARCH] No presence in cycle %d", search_cycle);
            break;
        }

        int last_zero = 0;
        ow_write_byte(OW_CMD_SEARCH_ROM);

        if (OW_DEBUG_ROM_SEARCH_BIT) {
            ESP_LOGI(TAG, "[ROM_SEARCH] Cycle %d, last_discrepancy=%d", search_cycle, last_discrepancy);
        }

        for (int i = 0; i < 64; i++) {
            int id_bit = ow_read_bit();
            int cmp_id_bit = ow_read_bit();
            int search_direction;

            if (OW_DEBUG_ROM_SEARCH_BIT && (i < 16 || (id_bit == 1 && cmp_id_bit == 1))) {
                ESP_LOGI(TAG, "[ROM_SEARCH] Bit[%02d] id=%d cmp=%d", i, id_bit, cmp_id_bit);
            }

            if (id_bit == 1 && cmp_id_bit == 1) {
                // 总线上无设备响应
                done = 1;
                break;
            }

            if (id_bit != cmp_id_bit) {
                search_direction = id_bit;
            } else {
                if (i == last_discrepancy) {
                    search_direction = 1;
                } else if (i > last_discrepancy) {
                    search_direction = 0;
                    last_zero = i;
                } else {
                    search_direction = (rom_no[i / 8] >> (i % 8)) & 1;
                    if (search_direction == 0) {
                        last_zero = i;
                    }
                }
            }

            if (search_direction) {
                rom_no[i / 8] |= (1 << (i % 8));
            } else {
                rom_no[i / 8] &= ~(1 << (i % 8));
            }
            ow_write_bit(search_direction);
        }

        last_discrepancy = last_zero;
        if (last_discrepancy == 0) {
            done = 1;
        }

        // CRC 校验 ROM ID
        uint8_t crc = 0;
        for (int i = 0; i < 7; i++) {
            crc = ow_crc8_byte(crc, rom_no[i]);
        }
        if (crc == rom_no[7]) {
            uint64_t rom_id = 0;
            for (int i = 0; i < 8; i++) {
                rom_id |= ((uint64_t)rom_no[i] << (i * 8));
            }
            if (rom_no[0] == MAX31850_FAMILY_CODE) {
                rom_codes[num_devices++] = rom_id;
                if (OW_DEBUG_ROM_SEARCH_BIT) {
                    ESP_LOGI(TAG, "[ROM_SEARCH] Found device #%d: ROM=0x%016llX", num_devices, rom_id);
                }
            } else {
                ESP_LOGW(TAG, "[ROM_SEARCH] Unknown family code 0x%02X", rom_no[0]);
            }
        } else {
            ESP_LOGW(TAG, "[ROM_SEARCH] CRC mismatch in cycle %d", search_cycle);
        }
    }

    if (OW_DEBUG_ROM_SEARCH_BIT) {
        ESP_LOGI(TAG, "[ROM_SEARCH] Done. Found %d device(s)", num_devices);
    }
    return num_devices;
}

//////////////////////////////////////////////////////////////
//////////////////// MAX31850 操作函数 ///////////////////////
//////////////////////////////////////////////////////////////

// 读取 Scratchpad (9 bytes)
static esp_err_t max31850_read_scratchpad(uint64_t rom_id, uint8_t *scratchpad)
{
    if (!ow_reset()) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    ow_write_byte(OW_CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        ow_write_byte((uint8_t)(rom_id >> (i * 8)));
    }
    ow_write_byte(MAX31850_CMD_READ_SCRATCHPAD);

    for (int i = 0; i < 9; i++) {
        scratchpad[i] = ow_read_byte();
    }

    if (OW_DEBUG_SCRATCHPAD_RAW) {
        ESP_LOGI(TAG, "[SCRATCHPAD_RAW] ROM=0x%016llX: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 rom_id,
                 scratchpad[0], scratchpad[1], scratchpad[2],
                 scratchpad[3], scratchpad[4], scratchpad[5],
                 scratchpad[6], scratchpad[7], scratchpad[8]);
    }

    // CRC 校验 (覆盖 Byte 0-7)
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        crc = ow_crc8_byte(crc, scratchpad[i]);
    }
    if (crc != scratchpad[8]) {
        ESP_LOGW(TAG, "[SCRATCHPAD_RAW] CRC mismatch: calc=0x%02X, recv=0x%02X", crc, scratchpad[8]);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

// 解析热电偶温度 (14-bit 有符号，分辨率 0.25°C，右移 2 位)
static float parse_thermocouple_temp(uint16_t raw)
{
    int16_t temp14 = ((int16_t)(raw & 0xFFFC)) >> 2;
    return temp14 * 0.25f;
}

// 解析冷端温度 (12-bit 有符号，分辨率 0.0625°C，右移 4 位)
static float parse_cold_junction_temp(uint16_t raw)
{
    int16_t temp12 = ((int16_t)(raw & 0xFFF0)) >> 4;
    return temp12 * 0.0625f;
}

//////////////////////////////////////////////////////////////
//////////////////// 状态机轮询任务 //////////////////////////
//////////////////////////////////////////////////////////////

static void max31850_poll_task(void *pvParameters)
{
    uint8_t scratchpad[9];
    (void)pvParameters;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
            if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
                continue;
            }

            if (!s_sensors[i].valid) {
                xSemaphoreGive(s_sensor_mutex);
                continue;
            }

            esp_err_t err = max31850_read_scratchpad(s_sensors[i].rom_id, scratchpad);

            float temp = 0.0f;
            float cj = 0.0f;
            uint8_t fault = 0;
            uint64_t rom_id = s_sensors[i].rom_id;
            uint8_t hw_addr = s_sensors[i].hw_addr;
            bool ok = false;

            if (err == ESP_OK) {
                uint16_t temp_raw = scratchpad[0] | ((uint16_t)scratchpad[1] << 8);
                uint16_t cj_raw = scratchpad[2] | ((uint16_t)scratchpad[3] << 8);

                temp = parse_thermocouple_temp(temp_raw);
                cj = parse_cold_junction_temp(cj_raw);
                // 故障位位于 Scratchpad Byte 2 的 Bit 0-2 (PDF 规范)
                fault = scratchpad[2] & 0x07;

                s_sensors[i].temperature = temp;
                s_sensors[i].cold_junction = cj;
                s_sensors[i].fault = fault;
                s_sensors[i].last_update = xTaskGetTickCount();
                ok = true;
            } else {
                ESP_LOGE(TAG, "Sensor [%d] read failed: %s", i, esp_err_to_name(err));
            }

            xSemaphoreGive(s_sensor_mutex);

            if (ok) {
                ESP_LOGI(TAG, "Sensor [%d]: ROM=0x%016llX, HW_ADDR=%02d, Temp=%.2f°C",
                         i, rom_id, hw_addr, temp);

                if (fault & MAX31850_FAULT_OPEN) {
                    ESP_LOGE(TAG, "Sensor [%d] FAULT: Thermocouple Open Circuit", i);
                }
                if (fault & MAX31850_FAULT_SHORT_GND) {
                    ESP_LOGE(TAG, "Sensor [%d] FAULT: Thermocouple Short to GND", i);
                }
                if (fault & MAX31850_FAULT_SHORT_VCC) {
                    ESP_LOGE(TAG, "Sensor [%d] FAULT: Thermocouple Short to VCC", i);
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////
//////////////////// 公共 API 实现 ///////////////////////////
//////////////////////////////////////////////////////////////

esp_err_t max31850_init(gpio_num_t gpio_num)
{
    s_ow_gpio = gpio_num;

    // 配置 GPIO 为开漏输出模式，禁止内部上下拉 (外部有 4.7K 上拉)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_ow_gpio),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(s_ow_gpio, 1); // 释放总线

    if (OW_DEBUG_GPIO_DIAG) {
        ESP_LOGI(TAG, "[GPIO_DIAG] Configured GPIO%d as INPUT_OUTPUT_OD", s_ow_gpio);
        ESP_LOGI(TAG, "[GPIO_DIAG] Internal pull-up/pull-down disabled, relying on external 4.7K pull-up");
        ow_check_bus_level("After init release");
    }

    s_sensor_mutex = xSemaphoreCreateMutex();
    if (s_sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 检测总线上是否有设备
    if (!ow_reset()) {
        ESP_LOGE(TAG, "No device detected on GPIO%d", s_ow_gpio);
        return ESP_ERR_NOT_FOUND;
    }

    // Search ROM 自动发现设备
    uint64_t rom_list[MAX31850_SENSOR_COUNT];
    int found = ow_search_rom(rom_list, MAX31850_SENSOR_COUNT);
    if (found != MAX31850_SENSOR_COUNT) {
        ESP_LOGE(TAG, "Expected %d sensors, found %d", MAX31850_SENSOR_COUNT, found);
        return ESP_ERR_NOT_FOUND;
    }

    // 初始化传感器表
    memset(s_sensors, 0, sizeof(s_sensors));

    uint8_t scratchpad[9];
    for (int i = 0; i < found; i++) {
        esp_err_t err = max31850_read_scratchpad(rom_list[i], scratchpad);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read scratchpad for ROM 0x%016llX: %s", rom_list[i], esp_err_to_name(err));
            return err;
        }

        // 硬件地址由 Configuration Register (Byte 4) 的 AD[1:0] 决定
        uint8_t hw_addr = scratchpad[4] & 0x03;
        if (OW_DEBUG_SCRATCHPAD_RAW) {
            ESP_LOGI(TAG, "[INIT_MAP] ROM=0x%016llX -> HW_ADDR=%d (cfg_reg=0x%02X)",
                     rom_list[i], hw_addr, scratchpad[4]);
        }
        if (hw_addr >= MAX31850_SENSOR_COUNT) {
            ESP_LOGE(TAG, "Invalid HW address %d for ROM 0x%016llX", hw_addr, rom_list[i]);
            return ESP_ERR_INVALID_ARG;
        }

        s_sensors[hw_addr].rom_id = rom_list[i];
        s_sensors[hw_addr].hw_addr = hw_addr;
        s_sensors[hw_addr].valid = true;
    }

    // 验证 4 个硬件地址是否全部映射
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        if (!s_sensors[i].valid) {
            ESP_LOGE(TAG, "Missing sensor at HW address %d", i);
            return ESP_ERR_NOT_FOUND;
        }
    }

    ESP_LOGI(TAG, "MAX31850 Init: Found 4 sensors on GPIO%d", s_ow_gpio);
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        ESP_LOGI(TAG, "Sensor [%d]: ROM=0x%016llX, HW_ADDR=%02d",
                 i, s_sensors[i].rom_id, s_sensors[i].hw_addr);
    }

    // 首次读取所有传感器，填充初始缓存
    for (int i = 0; i < MAX31850_SENSOR_COUNT; i++) {
        if (max31850_read_scratchpad(s_sensors[i].rom_id, scratchpad) == ESP_OK) {
            uint16_t temp_raw = scratchpad[0] | ((uint16_t)scratchpad[1] << 8);
            s_sensors[i].temperature = parse_thermocouple_temp(temp_raw);
            s_sensors[i].cold_junction = parse_cold_junction_temp(
                scratchpad[2] | ((uint16_t)scratchpad[3] << 8));
            s_sensors[i].fault = scratchpad[2] & 0x07;
            s_sensors[i].last_update = xTaskGetTickCount();
        }
    }

    // 创建状态机轮询任务，每秒轮询一次
    BaseType_t ret = xTaskCreate(max31850_poll_task, "MAX31850_POLL", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t max31850_get_temperature(uint8_t sensor_id, float *temp)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || temp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_OK;
    if (s_sensors[sensor_id].valid) {
        *temp = s_sensors[sensor_id].temperature;
    } else {
        err = ESP_ERR_INVALID_STATE;
    }

    xSemaphoreGive(s_sensor_mutex);
    return err;
}

esp_err_t max31850_force_update(uint8_t sensor_id, float *temp, TickType_t timeout)
{
    if (sensor_id >= MAX31850_SENSOR_COUNT || temp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_sensor_mutex, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_ERR_INVALID_STATE;
    uint8_t scratchpad[9];

    if (s_sensors[sensor_id].valid) {
        err = max31850_read_scratchpad(s_sensors[sensor_id].rom_id, scratchpad);
        if (err == ESP_OK) {
            uint16_t temp_raw = scratchpad[0] | ((uint16_t)scratchpad[1] << 8);
            s_sensors[sensor_id].temperature = parse_thermocouple_temp(temp_raw);
            s_sensors[sensor_id].cold_junction = parse_cold_junction_temp(
                scratchpad[2] | ((uint16_t)scratchpad[3] << 8));
            s_sensors[sensor_id].fault = scratchpad[2] & 0x07;
            s_sensors[sensor_id].last_update = xTaskGetTickCount();
            *temp = s_sensors[sensor_id].temperature;
        }
    }

    xSemaphoreGive(s_sensor_mutex);
    return err;
}
