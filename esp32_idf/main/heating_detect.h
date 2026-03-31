/**
 * @file heating_detect.h
 * @brief MAX31850KATB+ Temperature Sensor Driver (RMT-based 1-Wire)
 * 
 * 使用ESP32-S3 RMT外设实现1-Wire协议，驱动4通道MAX31850温度传感器
 * Hardware: GPIO14, 4x MAX31850KATB+, 4.7K pull-up per sensor
 */

#ifndef HEATING_DETECT_H
#define HEATING_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////
/////////////////////// 配置参数 /////////////////////////////
//////////////////////////////////////////////////////////////

#define MAX31850_SENSOR_COUNT       4       /**< 传感器数量 */
#define MAX31850_ONEWIRE_GPIO       GPIO_NUM_14  /**< 1-Wire总线GPIO */
#define MAX31850_CONVERSION_TIME_MS 100     /**< 温度转换时间(ms) */
#define MAX31850_MAX_RETRY          3       /**< 最大重试次数 */
#define MAX31850_OFFLINE_THRESHOLD  3       /**< 离线阈值(连续失败次数) */

//////////////////////////////////////////////////////////////
/////////////////////// 错误码定义 ///////////////////////////
//////////////////////////////////////////////////////////////

typedef enum {
    MAX31850_OK = 0,            /**< 成功 */
    MAX31850_ERR_OPEN,          /**< 热电偶开路 */
    MAX31850_ERR_SHORT_GND,     /**< 热电偶短路到GND */
    MAX31850_ERR_SHORT_VCC,     /**< 热电偶短路到VCC */
    MAX31850_ERR_CRC,           /**< CRC校验失败 */
    MAX31850_ERR_TIMEOUT,       /**< 通信超时 */
    MAX31850_ERR_OFFLINE,       /**< 传感器离线 */
    MAX31850_ERR_INVALID_ID,    /**< 无效的传感器ID */
    MAX31850_ERR_BUS_FAULT,     /**< 总线故障 */
    MAX31850_ERR_NOT_FOUND,     /**< 未找到设备 */
} max31850_err_t;

//////////////////////////////////////////////////////////////
/////////////////////// 数据结构 /////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 传感器状态结构
 */
typedef struct {
    uint8_t rom_id[8];          /**< 64-bit ROM ID (Family + Serial + CRC) */
    float temperature;          /**< 最后读取的温度值(°C) */
    int16_t raw_temp;           /**< 原始16位温度值 */
    uint8_t fault_reg;          /**< 故障寄存器 */
    max31850_err_t last_error;  /**< 上次错误码 */
    uint8_t fail_count;         /**< 连续失败计数 */
    bool online;                /**< 在线状态 */
    bool data_valid;            /**< 数据有效标志 */
    uint32_t last_read_time;    /**< 最后成功读取时间戳 */
} max31850_sensor_t;

//////////////////////////////////////////////////////////////
/////////////////////// API接口 //////////////////////////////
//////////////////////////////////////////////////////////////

/**
 * @brief 初始化MAX31850传感器驱动
 * 
 * 配置RMT外设，搜索总线上的设备，启动轮询任务
 * 
 * @param onewire_pin 1-Wire总线GPIO引脚（推荐GPIO_NUM_14）
 * @return ESP_OK 初始化成功
 * @return ESP_FAIL 初始化失败（如未找到设备）
 */
esp_err_t max31850_init(gpio_num_t onewire_pin);

/**
 * @brief 反初始化，释放RMT资源
 */
void max31850_deinit(void);

/**
 * @brief 获取指定传感器的温度值
 * 
 * 非阻塞方式，返回最近一次轮询的温度值
 * 
 * @param sensor_id 传感器ID (0-3，对应P1-P4)
 * @param temp_out 温度输出指针(°C)
 * @return MAX31850_OK 成功
 * @return MAX31850_ERR_INVALID_ID 无效ID
 * @return MAX31850_ERR_OFFLINE 传感器离线
 * @return MAX31850_ERR_* 其他错误
 */
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp_out);

/**
 * @brief 获取原始温度数据和故障寄存器
 * 
 * @param sensor_id 传感器ID (0-3)
 * @param raw_out 原始16位温度输出指针
 * @param fault_reg 故障寄存器输出指针(可为NULL)
 * @return MAX31850_OK 成功
 */
max31850_err_t max31850_get_raw_data(uint8_t sensor_id, int16_t *raw_out, uint8_t *fault_reg);

/**
 * @brief 强制立即更新指定传感器温度
 * 
 * 阻塞方式，绕过轮询周期，直接执行温度转换和读取
 * 
 * @param sensor_id 传感器ID (0-3)
 * @param temp_out 温度输出指针(°C)
 * @param timeout 超时时间(节拍)
 * @return MAX31850_OK 成功
 */
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp_out, TickType_t timeout);

/**
 * @brief 检查传感器是否在线
 * 
 * @param sensor_id 传感器ID (0-3)
 * @return true 在线
 * @return false 离线或无效ID
 */
bool max31850_is_online(uint8_t sensor_id);

/**
 * @brief 将错误码转换为字符串描述
 * 
 * @param err 错误码
 * @return const char* 错误描述字符串
 */
const char* max31850_err_to_string(max31850_err_t err);

/**
 * @brief 打印指定传感器的暂存器内容（调试用）
 * 
 * @param sensor_id 传感器ID (0-3)
 */
void max31850_dump_scratchpad(uint8_t sensor_id);

/**
 * @brief 获取所有传感器的当前状态（调试用）
 * 
 * @param sensors 传感器状态数组输出（需至少MAX31850_SENSOR_COUNT个元素）
 * @return uint8_t 实际获取的传感器数量
 */
uint8_t max31850_get_all_status(max31850_sensor_t *sensors);

//////////////////////////////////////////////////////////////
/////////////////////// 内部常量 /////////////////////////////
//////////////////////////////////////////////////////////////

/* 1-Wire命令 */
#define ONEWIRE_CMD_SEARCH_ROM      0xF0    /**< 搜索ROM */
#define ONEWIRE_CMD_READ_ROM        0x33    /**< 读取ROM */
#define ONEWIRE_CMD_MATCH_ROM       0x55    /**< 匹配ROM */
#define ONEWIRE_CMD_SKIP_ROM        0xCC    /**< 跳过ROM */
#define ONEWIRE_CMD_ALARM_SEARCH    0xEC    /**< 报警搜索 */

/* MAX31850特定命令 */
#define MAX31850_CMD_CONVERT_T      0x44    /**< 启动温度转换 */
#define MAX31850_CMD_READ_SCRATCH   0xBE    /**< 读取暂存器 */
#define MAX31850_CMD_WRITE_SCRATCH  0x4E    /**< 写入暂存器 */
#define MAX31850_CMD_COPY_SCRATCH   0x48    /**< 复制暂存器 */
#define MAX31850_CMD_RECALL_EE      0xB8    /**< 召回EEPROM */
#define MAX31850_CMD_READ_POWER     0xB4    /**< 读取供电方式 */

/* MAX31850常量 */
#define MAX31850_FAMILY_CODE        0x3B    /**< 家族码 */
#define MAX31850_SCRATCHPAD_LEN     9       /**< 暂存器长度 */

/* 故障寄存器位定义 */
#define MAX31850_FAULT_OPEN         0x01    /**< 开路故障 */
#define MAX31850_FAULT_SHORT_GND    0x02    /**< 短路到GND */
#define MAX31850_FAULT_SHORT_VCC    0x04    /**< 短路到VCC */

#ifdef __cplusplus
}
#endif

#endif /* HEATING_DETECT_H */
