/**
 * @file heating_detect.h
 * @brief MAX31850KATB+ 温度传感器驱动模块头文件
 * 
 * 基于ESP32-S3 RMT外设实现1-Wire协议，支持4个MAX31850传感器并联
 * 硬件配置：GPIO14，4.7KΩ上拉，非寄生电源模式
 * 
 * @author Kimi Code CLI
 * @date 2026-03-27
 */

#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdio.h>
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
//////////////////////// 常量定义 /////////////////////////////
//////////////////////////////////////////////////////////////

/** 1-Wire总线GPIO引脚 */
#define MAX31850_ONEWIRE_GPIO       GPIO_NUM_14

/** 传感器数量 */
#define MAX31850_SENSOR_COUNT       4

/** 温度转换时间（毫秒）- MAX31850需要≥100ms */
#define MAX31850_CONVERSION_TIME_MS 150  // 增加转换时间，确保转换完成（MAX31850需要~100ms）

/** 轮询周期（毫秒）- 每秒读取一次 */
#define MAX31850_POLL_INTERVAL_MS   1000

/** 连续失败次数阈值 - 超过则标记离线 */
#define MAX31850_MAX_FAIL_COUNT     3

/** ROM ID长度（字节） */
#define MAX31850_ROM_ID_LEN         8

/** 暂存器长度（字节） */
#define MAX31850_SCRATCHPAD_LEN     9

//////////////////////////////////////////////////////////////
//////////////////////// 1-Wire命令 ///////////////////////////
//////////////////////////////////////////////////////////////

#define ONEWIRE_CMD_READ_ROM        0x33    /**< 读取ROM（仅单设备） */
#define ONEWIRE_CMD_MATCH_ROM       0x55    /**< 匹配ROM（选择特定设备） */
#define ONEWIRE_CMD_SKIP_ROM        0xCC    /**< 跳过ROM（广播，不推荐使用） */
#define ONEWIRE_CMD_SEARCH_ROM      0xF0    /**< 搜索ROM（自动发现） */
#define ONEWIRE_CMD_ALARM_SEARCH    0xEC    /**< 报警搜索 */

//////////////////////////////////////////////////////////////
//////////////////////// MAX31850功能命令 //////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_CMD_CONVERT_T      0x44    /**< 启动温度转换 */
#define MAX31850_CMD_READ_SCRATCH   0xBE    /**< 读取暂存器 */
#define MAX31850_CMD_WRITE_SCRATCH  0x4E    /**< 写暂存器 */
#define MAX31850_CMD_COPY_SCRATCH   0x48    /**< 复制暂存器到EEPROM */
#define MAX31850_CMD_RECALL_EE      0xB8    /**< 从EEPROM恢复 */
#define MAX31850_CMD_READ_PWR       0xB4    /**< 读取电源模式 */

//////////////////////////////////////////////////////////////
//////////////////////// 故障寄存器位定义 //////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_FAULT_OPEN         0x01    /**< 热电偶开路 */
#define MAX31850_FAULT_SHORT_GND    0x02    /**< 热电偶短路到GND */
#define MAX31850_FAULT_SHORT_VCC    0x04    /**< 热电偶短路到VCC */

//////////////////////////////////////////////////////////////
//////////////////////// 错误码定义 ///////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief MAX31850错误码
 */
typedef enum {
    MAX31850_OK = 0,            /**< 成功 */
    MAX31850_ERR_OPEN,          /**< 热电偶开路 */
    MAX31850_ERR_SHORT_GND,     /**< 热电偶短路到GND */
    MAX31850_ERR_SHORT_VCC,     /**< 热电偶短路到VCC */
    MAX31850_ERR_CRC,           /**< CRC校验错误 */
    MAX31850_ERR_TIMEOUT,       /**< 通信超时 */
    MAX31850_ERR_OFFLINE,       /**< 传感器离线 */
    MAX31850_ERR_INVALID_ID,    /**< 无效的传感器ID */
    MAX31850_ERR_BUS_FAULT,     /**< 总线故障 */
} max31850_err_t;

//////////////////////////////////////////////////////////////
//////////////////////// 数据结构 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 传感器状态结构
 */
typedef struct {
    uint8_t rom_id[MAX31850_ROM_ID_LEN];    /**< 64位ROM ID */
    float temperature;                       /**< 最后一次读取的温度（°C） */
    int16_t raw_temp;                        /**< 原始16位温度值 */
    uint8_t fault_reg;                       /**< 故障寄存器值 */
    max31850_err_t last_error;               /**< 最后一次错误码 */
    uint8_t fail_count;                      /**< 连续失败计数 */
    bool online;                             /**< 在线状态 */
    bool data_valid;                         /**< 数据有效标志 */
    uint32_t last_read_time;                 /**< 最后读取时间戳 */
} max31850_sensor_t;

//////////////////////////////////////////////////////////////
//////////////////////// API函数声明 //////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化MAX31850温度传感器模块
 * 
 * 配置RMT外设，搜索总线上的设备并初始化轮询任务
 * 
 * @param onewire_pin 1-Wire总线GPIO引脚（应为GPIO_NUM_14）
 * @return esp_err_t ESP_OK成功，其他表示错误
 */
esp_err_t max31850_init(gpio_num_t onewire_pin);

/**
 * @brief 获取指定传感器的温度
 * 
 * 从内部缓冲区读取最近一次轮询的温度值（非阻塞）
 * 
 * @param sensor_id 传感器ID（0-3，对应原理图P1-P4）
 * @param temp_out 输出温度值（°C），有效范围-270~+1372
 * @return max31850_err_t 错误码，MAX31850_OK表示成功
 */
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out);

/**
 * @brief 获取原始16位温度值和故障寄存器（用于调试）
 * 
 * @param sensor_id 传感器ID（0-3）
 * @param raw_out 输出原始16位温度值
 * @param fault_reg 输出故障寄存器值
 * @return max31850_err_t 错误码
 */
max31850_err_t max31850_get_raw_data(uint8_t sensor_id, int16_t *raw_out, uint8_t *fault_reg);

/**
 * @brief 强制立即更新温度（阻塞式）
 * 
 * 绕过轮询周期，立即执行温度转换并读取（用于紧急检查）
 * 此函数会阻塞等待转换完成，总耗时约100ms+通信时间
 * 
 * @param sensor_id 传感器ID（0-3）
 * @param temp_out 输出温度值（°C）
 * @param timeout 超时时间（FreeRTOS tick）
 * @return max31850_err_t 错误码
 */
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout);

/**
 * @brief 检查传感器是否在线
 * 
 * @param sensor_id 传感器ID（0-3）
 * @return true 传感器在线且工作正常
 * @return false 传感器离线或ID无效
 */
bool max31850_is_online(uint8_t sensor_id);

/**
 * @brief 获取传感器最后一次错误
 * 
 * @param sensor_id 传感器ID（0-3）
 * @return max31850_err_t 最后一次错误码，无效ID返回MAX31850_ERR_INVALID_ID
 */
max31850_err_t max31850_get_last_error(uint8_t sensor_id);

/**
 * @brief 获取传感器ROM ID（64位唯一标识）
 * 
 * @param sensor_id 传感器ID（0-3）
 * @param rom_out 输出缓冲区（8字节），用于存储ROM ID
 * @return esp_err_t ESP_OK成功，其他表示错误
 */
esp_err_t max31850_get_rom_id(uint8_t sensor_id, uint8_t *rom_out);

/**
 * @brief 反初始化，释放所有资源
 * 
 * 停止轮询任务，释放RMT通道和内存
 */
void max31850_deinit(void);

/**
 * @brief 打印暂存器原始数据（用于调试）
 * 
 * 输出9字节暂存器的十六进制值到串口
 * 
 * @param sensor_id 传感器ID（0-3）
 */
void max31850_dump_scratchpad(uint8_t sensor_id);

/**
 * @brief 打印所有传感器的ROM ID（初始化后自动调用）
 * 
 * 输出所有发现的传感器64位ROM ID到串口
 */
void max31850_print_rom_ids(void);

/**
 * @brief 将错误码转换为可读字符串
 * 
 * @param err 错误码
 * @return const char* 错误描述字符串
 */
const char* max31850_err_to_string(max31850_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HEATING_DETECT_H */
