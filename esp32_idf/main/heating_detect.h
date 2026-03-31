/**
 * @file heating_detect.h
 * @brief MAX31850KATB+ 温度传感器驱动头文件
 * 
 * 基于 ESP32-S3 (240MHz) 的 GPIO Bit-Bang 1-Wire 驱动
 * 支持 4 个 MAX31850 传感器并联于 GPIO14
 * 
 * @note MAX31850 是 Read-Only 设备，自动连续转换，无需发送 Convert T 命令
 */

#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////
///////////////////// 配置常量 ///////////////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_SENSOR_COUNT       4           /**< 传感器数量 */
#define MAX31850_ONEWIRE_GPIO       GPIO_NUM_14 /**< 1-Wire 总线 GPIO */
#define MAX31850_PULLUP_RESISTOR    4700        /**< 上拉电阻 4.7KΩ */

/* 1-Wire 命令 */
#define ONEWIRE_CMD_SEARCH_ROM      0xF0        /**< Search ROM 命令 */
#define ONEWIRE_CMD_READ_ROM        0x33        /**< Read ROM 命令（单设备） */
#define ONEWIRE_CMD_MATCH_ROM       0x55        /**< Match ROM 命令 */
#define ONEWIRE_CMD_SKIP_ROM        0xCC        /**< Skip ROM 命令 */

/* MAX31850 特定命令 - 注意：MAX31850 是 Read-Only，不支持写操作 */
#define MAX31850_CMD_READ_SCRATCH   0xBE        /**< 读取暂存器（兼容性） */

/* 故障寄存器位定义 */
#define MAX31850_FAULT_OPEN         0x01        /**< Bit 0: 热电偶开路 */
#define MAX31850_FAULT_SHORT_GND    0x02        /**< Bit 1: 短路到 GND */
#define MAX31850_FAULT_SHORT_VCC    0x04        /**< Bit 2: 短路到 VCC */

/* 任务配置 */
#define MAX31850_TASK_STACK_SIZE    4096        /**< 轮询任务栈大小 */
#define MAX31850_TASK_PRIORITY      2           /**< 轮询任务优先级 */
#define MAX31850_POLL_INTERVAL_MS   250         /**< 每个传感器轮询间隔 250ms，总周期 1s */

/* 故障检测阈值 */
#define MAX31850_MAX_FAIL_COUNT     3           /**< 最大连续失败次数 */
#define MAX31850_OFFLINE_RETRY_MS   10000       /**< 离线传感器重试间隔 10s */

//////////////////////////////////////////////////////////////
///////////////////// 数据类型 ///////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 错误码定义
 */
typedef enum {
    MAX31850_OK = 0,            /**< 成功 */
    MAX31850_ERR_OPEN,          /**< 热电偶开路（Byte 4 Bit 0） */
    MAX31850_ERR_SHORT_GND,     /**< 短路到 GND（Byte 4 Bit 1） */
    MAX31850_ERR_SHORT_VCC,     /**< 短路到 VCC（Byte 4 Bit 2） */
    MAX31850_ERR_CRC,           /**< Scratchpad CRC 校验失败 */
    MAX31850_ERR_TIMEOUT,       /**< 总线无响应（无 Presence） */
    MAX31850_ERR_BUS_FAULT,     /**< 总线短路到 GND */
    MAX31850_ERR_OFFLINE,       /**< 设备离线 */
    MAX31850_ERR_INVALID_ID     /**< 无效的传感器 ID */
} max31850_err_t;

/**
 * @brief 轮询状态机状态
 */
typedef enum {
    POLL_IDLE = 0,              /**< 空闲状态 */
    POLL_SELECT_SENSOR,         /**< 选择当前传感器 */
    POLL_MATCH_ROM,             /**< 发送 Match ROM 命令 */
    POLL_READ_DATA,             /**< 读取 9 字节数据 */
    POLL_PARSE,                 /**< 解析数据 */
    POLL_NEXT,                  /**< 切换到下一个传感器 */
    POLL_DELAY                  /**< 等待下一次轮询 */
} poll_state_t;

/**
 * @brief 传感器数据结构
 */
typedef struct {
    uint8_t rom_id[8];          /**< 64-bit 唯一序列号 */
    uint8_t hw_addr;            /**< 硬件地址 (0-3) */
    bool online;                /**< 在线状态 */
    bool data_valid;            /**< 数据有效标志 */
    uint8_t fail_count;         /**< 连续失败计数 */
    float thermocouple_temp;    /**< 热电偶温度（°C） */
    float cold_junction_temp;   /**< 冷端温度（°C） */
    uint8_t fault_reg;          /**< 原始故障寄存器 */
    uint8_t scratchpad[9];      /**< 原始 9 字节数据 */
    TickType_t last_update;     /**< 上次更新时间戳 */
} max31850_sensor_t;

//////////////////////////////////////////////////////////////
///////////////////// API 接口 ///////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化 MAX31850 驱动
 * 
 * 配置 GPIO14，执行 Search ROM 发现所有设备，初始化传感器实例
 * 
 * @param gpio_num 固定为 GPIO_NUM_14
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 未找到足够设备，ESP_ERR_INVALID_STATE 总线故障
 */
esp_err_t max31850_init(gpio_num_t gpio_num);

/**
 * @brief 反初始化，释放资源
 */
void max31850_deinit(void);

/**
 * @brief 获取指定传感器温度（非阻塞，读取缓存值）
 * 
 * @param sensor_id 传感器 ID (0-3 对应硬件位置 00, 01, 10, 11)
 * @param temp_out 输出温度值（°C）
 * @return MAX31850_OK 成功，其他错误码
 */
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out);

/**
 * @brief 获取传感器详细数据
 * 
 * @param sensor_id 传感器 ID (0-3)
 * @param data_out 输出数据结构
 * @return MAX31850_OK 成功，其他错误码
 */
max31850_err_t max31850_get_data(uint8_t sensor_id, max31850_sensor_t *data_out);

/**
 * @brief 强制立即更新指定传感器（阻塞式）
 * 
 * 用于初始化校准或故障恢复，流程：Match ROM → Read 9 Bytes → Parse
 * 
 * @param sensor_id 传感器 ID (0-3)
 * @param temp_out 输出温度值（°C），可为 NULL
 * @param timeout 超时时间（FreeRTOS ticks）
 * @return MAX31850_OK 成功，其他错误码
 */
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout);

/**
 * @brief 检查传感器是否在线
 * 
 * @param sensor_id 传感器 ID (0-3)
 * @return true 在线，false 离线
 */
bool max31850_is_online(uint8_t sensor_id);

/**
 * @brief 调试：打印指定传感器的 9 字节原始数据
 * 
 * @param sensor_id 传感器 ID (0-3)
 */
void max31850_dump_scratchpad(uint8_t sensor_id);

/**
 * @brief 将错误码转换为中文描述字符串
 * 
 * @param err 错误码
 * @return 错误描述字符串
 */
const char* max31850_err_to_string(max31850_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HEATING_DETECT_H */
