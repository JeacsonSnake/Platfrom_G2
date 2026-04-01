/**
 * @file heating_detect.h
 * @brief MAX31850KATB+ Temperature Sensor Driver for ESP32-S3
 * 
 * 4-channel K-type thermocouple digitizer with cold-junction compensation
 * 1-Wire protocol on GPIO14, GPIO bit-bang implementation with critical section
 * 
 * Hardware: ESP32-S3-DevKitC-1 @ 240MHz
 * Sensors: 4× MAX31850KATB+ (Read-Only 1-Wire device)
 * Bus: GPIO14, Open-Drain mode, 4.7KΩ pull-up per sensor
 * 
 * @note MAX31850 is Read-Only: No Write Scratchpad(0x4E), Copy(0x48), Convert T(0x44)
 * @note Auto-continuous conversion: ~100ms cycle after power-on
 * 
 * @version 3.0
 * @date 2026-04-01
 */

#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////
//////////////////////// 宏定义 //////////////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_SENSOR_COUNT       4           /**< 传感器数量 */
#define MAX31850_ONEWIRE_GPIO       GPIO_NUM_14 /**< 1-Wire总线GPIO */

/** @name 1-Wire命令 */
#define ONEWIRE_CMD_SEARCH_ROM      0xF0        /**< Search ROM命令 */
#define ONEWIRE_CMD_READ_ROM        0x33        /**< Read ROM命令（单设备） */
#define ONEWIRE_CMD_MATCH_ROM       0x55        /**< Match ROM命令 */
#define ONEWIRE_CMD_SKIP_ROM        0xCC        /**< Skip ROM命令 */

/** @name MAX31850暂存器命令（Read-Only设备） */
#define MAX31850_CMD_READ_DATA      0xBE        /**< 读取9字节数据帧 */

/** @name MAX31850故障寄存器位定义 */
#define MAX31850_FAULT_OPEN         0x01        /**< Bit0: 热电偶开路 */
#define MAX31850_FAULT_SHORT_GND    0x02        /**< Bit1: 短接到GND */
#define MAX31850_FAULT_SHORT_VCC    0x04        /**< Bit2: 短接到VCC */

/** @name 1-Wire时序参数（ESP32-S3 @ 240MHz，单位：微秒） */
#define ONEWIRE_RESET_LOW_US        480         /**< Reset低电平时间 */
#define ONEWIRE_PRESENCE_WAIT_US    70          /**< 等待Presence响应 */
#define ONEWIRE_RESET_RECOVERY_US   410         /**< Reset恢复时间 */
#define ONEWIRE_WRITE1_LOW_US       5           /**< 写1低电平时间 */
#define ONEWIRE_WRITE1_RECOVERY_US  55          /**< 写1恢复时间 */
#define ONEWIRE_WRITE0_LOW_US       70          /**< 写0低电平时间 */
#define ONEWIRE_WRITE0_RECOVERY_US  5           /**< 写0恢复时间 */
#define ONEWIRE_READ_INIT_US        3           /**< 读初始化低电平 */
#define ONEWIRE_READ_SAMPLE_US      10          /**< 读到采样点延迟 */
#define ONEWIRE_READ_RECOVERY_US    50          /**< 读恢复时间 */
#define ONEWIRE_BIT_INTERVAL_US     2           /**< 位间间隔（额外裕量） */

/** @name 系统参数 */
#define MAX31850_POLL_INTERVAL_MS   250         /**< 单个传感器轮询间隔 */
#define MAX31850_TASK_STACK_SIZE    4096        /**< 轮询任务栈大小 */
#define MAX31850_TASK_PRIORITY      2           /**< 轮询任务优先级 */
#define MAX31850_MAX_RETRY          3           /**< 最大重试次数 */

//////////////////////////////////////////////////////////////
//////////////////////// 数据类型 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief MAX31850错误代码
 */
typedef enum {
    MAX31850_OK = 0,                /**< 成功 */
    MAX31850_ERR_OPEN,              /**< 热电偶开路 */
    MAX31850_ERR_SHORT_GND,         /**< 短接到GND */
    MAX31850_ERR_SHORT_VCC,         /**< 短接到VCC */
    MAX31850_ERR_CRC,               /**< CRC校验失败 */
    MAX31850_ERR_TIMEOUT,           /**< 通信超时 */
    MAX31850_ERR_BUS_FAULT,         /**< 总线故障（短路到GND） */
    MAX31850_ERR_OFFLINE,           /**< 设备离线 */
    MAX31850_ERR_INVALID_ID,        /**< 无效的传感器ID */
    MAX31850_ERR_NOT_FOUND,         /**< 未找到设备 */
} max31850_err_t;

/**
 * @brief 传感器状态结构体
 */
typedef struct {
    uint8_t rom_id[8];              /**< 64-bit ROM ID（Family + Serial + CRC） */
    uint8_t hw_addr;                /**< 硬件地址（0-3，由AD0/AD1决定） */
    bool online;                    /**< 在线状态 */
    bool data_valid;                /**< 数据有效标志 */
    uint8_t fail_count;             /**< 连续失败计数 */
    max31850_err_t last_error;      /**< 最后错误代码 */
    float thermocouple_temp;        /**< 热电偶温度（°C） */
    float cold_junction_temp;       /**< 冷端温度（°C） */
    uint8_t fault_reg;              /**< 故障寄存器 */
    uint8_t scratchpad[9];          /**< 原始9字节数据 */
    TickType_t last_update;         /**< 最后成功更新时间戳 */
} max31850_sensor_t;

//////////////////////////////////////////////////////////////
//////////////////////// API函数 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化MAX31850温度传感器模块
 * 
 * @param gpio_num 1-Wire总线GPIO引脚（必须为GPIO14）
 * @return esp_err_t ESP_OK成功，其他失败
 * 
 * @note 该函数会执行ROM Search自动发现4个传感器
 */
esp_err_t max31850_init(gpio_num_t gpio_num);

/**
 * @brief 获取指定传感器的温度（非阻塞，读取缓存）
 * 
 * @param sensor_id 传感器索引（0-3）
 * @param temp 输出温度值（°C）
 * @return max31850_err_t 错误代码
 */
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp);

/**
 * @brief 获取指定传感器的详细数据（非阻塞，读取缓存）
 * 
 * @param sensor_id 传感器索引（0-3）
 * @param data 输出数据指针
 * @return max31850_err_t 错误代码
 */
max31850_err_t max31850_get_data(uint8_t sensor_id, max31850_sensor_t *data);

/**
 * @brief 强制更新指定传感器的温度（阻塞式）
 * 
 * @param sensor_id 传感器索引（0-3）
 * @param temp 输出温度值（°C）
 * @param timeout 超时时间（tick）
 * @return max31850_err_t 错误代码
 * 
 * @note MAX31850不支持Convert T命令，此函数直接读取最新数据
 */
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp, TickType_t timeout);

/**
 * @brief 检查传感器是否在线
 * 
 * @param sensor_id 传感器索引（0-3）
 * @return true 在线
 * @return false 离线或无效ID
 */
bool max31850_is_online(uint8_t sensor_id);

/**
 * @brief 打印指定传感器的原始暂存器数据（调试）
 * 
 * @param sensor_id 传感器索引（0-3）
 */
void max31850_dump_scratchpad(uint8_t sensor_id);

/**
 * @brief 将错误代码转换为字符串
 * 
 * @param err 错误代码
 * @return const char* 错误描述字符串
 */
const char* max31850_err_to_string(max31850_err_t err);

/**
 * @brief 反初始化，释放资源
 */
void max31850_deinit(void);

//////////////////////////////////////////////////////////////
//////////////////////// 调试任务 ////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 温度打印任务（示例）
 * 
 * @param pvParameters FreeRTOS任务参数（未使用）
 * 
 * @note 每2秒打印一次所有传感器的温度
 */
void heating_print_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* HEATING_DETECT_H */
