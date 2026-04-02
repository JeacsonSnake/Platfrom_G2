#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

//////////////////////////////////////////////////////////////
//////////////////// MAX31850 驱动配置 ////////////////////////
//////////////////////////////////////////////////////////////

// 传感器数量
#define MAX31850_SENSOR_COUNT       4

// MAX31850 家族码
#define MAX31850_FAMILY_CODE        0x3B

// 默认 1-Wire GPIO
#define MAX31850_GPIO_DEFAULT       GPIO_NUM_14

// 1-Wire ROM 命令
#define OW_CMD_SEARCH_ROM           0xF0
#define OW_CMD_READ_ROM             0x33
#define OW_CMD_MATCH_ROM            0x55
#define OW_CMD_SKIP_ROM             0xCC

// MAX31850 功能命令
// 注意：MAX31850KATB+ 为 Read-Only 设备，不支持 Write Scratchpad(0x4E)、Copy(0x48)
// 硬件上电后自动连续转换，无需发送 Convert T(0x44)
#define MAX31850_CMD_CONVERT_T          0x44
#define MAX31850_CMD_READ_SCRATCHPAD    0xBE
#define MAX31850_CMD_READ_POWER_SUPPLY  0xB4

// 故障标志位 (位于 Scratchpad Byte 2 低位，符合 PDF 规范)
#define MAX31850_FAULT_OPEN         0x01
#define MAX31850_FAULT_SHORT_GND    0x02
#define MAX31850_FAULT_SHORT_VCC    0x04

// 传感器数据结构
typedef struct {
    uint64_t rom_id;            // 64-bit ROM ID
    uint8_t hw_addr;            // 硬件地址 (0-3)
    float temperature;          // 热电偶温度 (°C)
    float cold_junction;        // 冷端温度 (°C)
    uint8_t fault;              // 故障状态
    bool valid;                 // 数据是否有效
    TickType_t last_update;     // 上次更新时间
} max31850_sensor_t;

//////////////////////////////////////////////////////////////
//////////////////// MAX31850 API ////////////////////////////
//////////////////////////////////////////////////////////////

// 初始化 1-Wire 总线与 MAX31850 传感器
// 执行 Search ROM，读取硬件地址并建立映射
esp_err_t max31850_init(gpio_num_t gpio_num);

// 非阻塞读取缓存温度
esp_err_t max31850_get_temperature(uint8_t sensor_id, float *temp);

// 阻塞强制更新指定传感器温度
esp_err_t max31850_force_update(uint8_t sensor_id, float *temp, TickType_t timeout);

#endif // HEATING_DETECT_H
