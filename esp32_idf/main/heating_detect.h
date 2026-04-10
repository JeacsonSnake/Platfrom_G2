/**
 * @file heating_detect.h
 * @brief MAX31850KATB+ 1-Wire 温度传感器驱动头文件
 * 
 * 支持4个MAX31850KATB+传感器并联在单一1-Wire总线上
 * 使用GPIO14作为1-Wire总线
 * 
 * @author ESP32-S3 Motor Control IoT Project
 * @version 1.0.0
 */

#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

//////////////////////////////////////////////////////////////
//////////////////////// DEBUG CONFIG ////////////////////////
//////////////////////////////////////////////////////////////

/** @brief 启用详细调试日志 */
#define MAX31850_DEBUG_ENABLE           1

/** @brief 调试日志级别 */
#define MAX31850_LOG_LEVEL              ESP_LOG_INFO

/** @brief GPIO诊断功能 */
#define MAX31850_DEBUG_GPIO             1

/** @brief 1-Wire波形日志 */
#define MAX31850_DEBUG_WAVEFORM         1

/** @brief ROM搜索位级调试 */
#define MAX31850_DEBUG_ROM_SEARCH       1

/** @brief 暂存器数据打印 */
#define MAX31850_DEBUG_SCRATCHPAD       1

/** @brief 总线电平检查 */
#define MAX31850_DEBUG_BUS_LEVEL        1

//////////////////////////////////////////////////////////////
//////////////////////// CONFIGURATION ///////////////////////
//////////////////////////////////////////////////////////////

/** @brief 1-Wire总线GPIO引脚 */
#define ONE_WIRE_GPIO           GPIO_NUM_14

/** @brief 传感器数量 */
#define MAX31850_SENSOR_COUNT   4

/** @brief 温度转换时间（毫秒），MAX=100ms */
#define CONVERSION_TIME_MS      100

/** @brief 轮询间隔（毫秒） */
#define POLLING_INTERVAL_MS             1000

/** @brief 离线传感器重试间隔（毫秒），比正常轮询间隔更长以减少总线占用 */
#define OFFLINE_RETRY_INTERVAL_MS       5000

/** @brief 离线判定阈值，连续失败多少次后标记为离线 */
#define OFFLINE_THRESHOLD               3

/** @brief 离线传感器最大重试间隔（毫秒），指数退避上限 */
#define OFFLINE_RETRY_MAX_MS            30000

/** @brief ROM搜索重试间隔（毫秒），用于定期重新搜索未发现的设备 */
#define ROM_SEARCH_RETRY_INTERVAL_MS    10000

/** @brief 已知传感器总数（硬件固定4个） */
#define EXPECTED_SENSOR_COUNT           4

/** @brief 允许CRC失败的设备（在已知硬件配置下使用Family Code验证） */
#define ALLOW_CRC_FAILURE_DEVICES       1

/** @brief 任务栈大小 */
#define HEATING_TASK_STACK_SIZE 4096

/** @brief 任务优先级 */
#define HEATING_TASK_PRIORITY   1

//////////////////////////////////////////////////////////////
//////////////////////// 1-WIRE COMMANDS /////////////////////
//////////////////////////////////////////////////////////////

/** @brief 1-Wire ROM命令 */
#define ONE_WIRE_CMD_SEARCH_ROM     0xF0
#define ONE_WIRE_CMD_READ_ROM       0x33
#define ONE_WIRE_CMD_MATCH_ROM      0x55
#define ONE_WIRE_CMD_SKIP_ROM       0xCC
#define ONE_WIRE_CMD_ALARM_SEARCH   0xEC

/** @brief MAX31850功能命令 */
#define MAX31850_CMD_CONVERT_T      0x44
#define MAX31850_CMD_READ_SCRATCH   0xBE
#define MAX31850_CMD_WRITE_SCRATCH  0x4E
#define MAX31850_CMD_COPY_SCRATCH   0x48
#define MAX31850_CMD_RECALL_E2      0xB8
#define MAX31850_CMD_READ_PWR       0xB4

//////////////////////////////////////////////////////////////
//////////////////////// DATA STRUCTURES /////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 传感器ROM ID结构（64-bit）
 */
typedef union {
    uint8_t bytes[8];
    struct {
        uint8_t family_code;        // Byte 0: 家族码 (MAX31850 = 0x3B)
        uint8_t serial[6];          // Byte 1-6: 序列号
        uint8_t crc;                // Byte 7: ROM CRC
    };
} max31850_rom_id_t;

/**
 * @brief Scratchpad数据结构（9字节）
 */
typedef union {
    uint8_t bytes[9];
    struct {
        uint8_t temp_lsb;           // Byte 0: 温度LSB (Bit0=Fault, Bit1=保留)
        uint8_t temp_msb;           // Byte 1: 温度MSB (14-bit有符号)
        uint8_t cj_lsb;             // Byte 2: 冷端温度LSB (Bit0-2=故障位)
        uint8_t cj_msb;             // Byte 3: 冷端温度MSB (12-bit有符号)
        uint8_t config;             // Byte 4: 配置寄存器 (AD3-AD0硬件地址)
        uint8_t reserved[3];        // Byte 5-7: 保留 (0xFF)
        uint8_t crc;                // Byte 8: Scratchpad CRC
    };
} max31850_scratchpad_t;

/**
 * @brief 故障类型定义
 */
typedef enum {
    MAX31850_FAULT_NONE = 0,
    MAX31850_FAULT_OC   = 0x01,     // 热电偶开路 (Bit0)
    MAX31850_FAULT_SCG  = 0x02,     // 热电偶对地短路 (Bit1)
    MAX31850_FAULT_SCV  = 0x04,     // 热电偶对电源短路 (Bit2)
} max31850_fault_t;

/**
 * @brief 传感器状态结构
 */
typedef struct {
    max31850_rom_id_t rom_id;       // 64-bit ROM ID
    uint8_t hw_addr;                // 硬件地址 (0-3)，从Config寄存器读取
    bool present;                   // 设备是否存在
    
    float thermocouple_temp;        // 热电偶温度 (°C)
    float cold_junction_temp;       // 冷端温度 (°C)
    max31850_fault_t fault;         // 故障状态
    
    uint32_t last_update_tick;      // 最后更新时间戳
    uint32_t crc_error_count;       // CRC错误计数
    uint32_t fault_count;           // 故障计数
    uint8_t  consecutive_failures;  // 连续失败计数
    bool     offline;               // 离线状态标志
    uint32_t offline_retry_interval_ms;  // 当前离线重试间隔（动态调整）
    bool     crc_valid;             // ROM CRC是否有效
} max31850_sensor_t;

/**
 * @brief 驱动状态结构
 */
typedef struct {
    gpio_num_t gpio_num;            // GPIO引脚号
    bool initialized;               // 是否已初始化
    uint8_t sensor_count;           // 发现的传感器数量
    max31850_sensor_t sensors[MAX31850_SENSOR_COUNT];  // 传感器数组（按硬件地址索引）
    SemaphoreHandle_t mutex;        // 互斥锁
    TaskHandle_t polling_task;      // 轮询任务句柄
} max31850_driver_t;

//////////////////////////////////////////////////////////////
//////////////////////// PUBLIC API //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化MAX31850驱动
 * 
 * 初始化GPIO，执行Search ROM发现设备，验证4个传感器存在，建立ROM ID与硬件地址映射表
 * 
 * @param gpio_num 1-Wire总线GPIO引脚
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_init(gpio_num_t gpio_num);

/**
 * @brief 反初始化MAX31850驱动
 * 
 * 停止轮询任务，释放资源
 * 
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_deinit(void);

/**
 * @brief 非阻塞读取缓存温度
 * 
 * 从内部缓存读取上次更新的温度值，不触发新的转换
 * 
 * @param hw_addr 硬件地址 (0-3)
 * @param temp 输出参数，热电偶温度 (°C)
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数错误，ESP_ERR_NOT_FOUND 传感器不存在
 */
esp_err_t max31850_get_temperature(uint8_t hw_addr, float *temp);

/**
 * @brief 非阻塞读取缓存温度和冷端温度
 * 
 * @param hw_addr 硬件地址 (0-3)
 * @param thermocouple_temp 输出参数，热电偶温度 (°C)，可为NULL
 * @param cold_junction_temp 输出参数，冷端温度 (°C)，可为NULL
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_get_temperature_full(uint8_t hw_addr, float *thermocouple_temp, float *cold_junction_temp);

/**
 * @brief 阻塞式强制更新单个传感器温度
 * 
 * 触发转换并等待完成，然后读取温度
 * 
 * @param hw_addr 硬件地址 (0-3)
 * @param temp 输出参数，热电偶温度 (°C)，可为NULL
 * @param timeout 超时时间（滴答）
 * @return ESP_OK 成功，ESP_ERR_TIMEOUT 超时，其他失败
 */
esp_err_t max31850_force_update(uint8_t hw_addr, float *temp, TickType_t timeout);

/**
 * @brief 获取传感器故障状态
 * 
 * @param hw_addr 硬件地址 (0-3)
 * @param fault 输出参数，故障状态
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_get_fault_status(uint8_t hw_addr, max31850_fault_t *fault);

/**
 * @brief 获取发现的传感器数量
 * 
 * @return 传感器数量 (0-4)
 */
uint8_t max31850_get_sensor_count(void);

/**
 * @brief 检查传感器是否存在
 * 
 * @param hw_addr 硬件地址 (0-3)
 * @return true 存在，false 不存在
 */
bool max31850_sensor_exists(uint8_t hw_addr);

/**
 * @brief 打印所有传感器信息（调试用）
 */
void max31850_print_sensor_info(void);

/**
 * @brief 启动温度轮询任务
 * 
 * 创建后台任务，每秒轮询所有传感器
 * 
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_start_polling(void);

/**
 * @brief 停止温度轮询任务
 * 
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_stop_polling(void);

/**
 * @brief 手动触发一次所有传感器转换（用于同步采集）
 * 
 * 发送Skip ROM + Convert T命令，所有设备同时开始转换
 * 
 * @return ESP_OK 成功，其他失败
 */
esp_err_t max31850_trigger_all_conversion(void);

/**
 * @brief 字符串化故障信息
 * 
 * @param fault 故障状态
 * @return 故障描述字符串
 */
const char* max31850_fault_to_string(max31850_fault_t fault);

#endif /* HEATING_DETECT_H */
