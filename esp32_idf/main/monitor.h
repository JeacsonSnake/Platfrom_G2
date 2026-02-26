#ifndef MONITOR_H
#define MONITOR_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_sntp.h"

//////////////////////////////////////////////////////////////
//////////////////// MQTT Connection Monitor /////////////////
//////////////////////////////////////////////////////////////

// 监控配置参数
#define MONITOR_REPORT_INTERVAL_MS  (( 60 / 12 ) * 60 * 60 * 1000)  // 5分钟报告间隔
#define MONITOR_MAX_DISCONNECT_LOG  100                   // 最大记录断开事件数
// 国内NTP服务器配置（原配置，同步稳定性更好）
#define NTP_SERVER_PRIMARY   "cn.pool.ntp.org"             // 国内NTP服务器池（主）
#define NTP_SERVER_BACKUP    "ntp.aliyun.com"              // 阿里云NTP服务器（备）
#define NTP_SERVER_FALLBACK  "ntp.tencent.com"             // 腾讯云NTP服务器（备用）
#define TIME_SYNC_TIMEOUT_MS        30000                  // 时间同步超时时间（30秒）
#define TIME_SYNC_RETRY_MAX         3                      // 最大重试次数

// 单次断开事件记录结构体
typedef struct {
    int64_t disconnect_time_ms;     // 断开时间戳 (毫秒)
    int64_t reconnect_time_ms;      // 重连时间戳 (毫秒，0表示尚未重连)
    int64_t disconnect_duration_ms; // 断开持续时长 (毫秒)
    const char* disconnect_reason;  // 断开原因描述
} disconnect_event_t;

// MQTT连接统计数据结构体
typedef struct {
    // 时间同步相关
    bool time_synced;               // 时间是否已同步
    int64_t boot_time_ms;           // 开机时的系统时间（毫秒）
    time_t boot_real_time;          // 开机时的实际时间（UTC时间戳）
    int64_t time_sync_time_ms;      // 时间同步完成时的系统时间
    
    // 连接统计
    int total_connections;          // 总连接次数
    int total_disconnects;          // 总断开次数
    int64_t first_connect_time_ms;  // 首次连接时间
    int64_t current_session_start_ms; // 当前连接会话开始时间
    int64_t total_connected_time_ms; // 累计连接时长
    
    // 断开事件日志（循环缓冲区）
    disconnect_event_t disconnect_log[MONITOR_MAX_DISCONNECT_LOG];
    int log_index;                  // 当前日志索引
    int log_count;                  // 有效日志数量
    
    // 当前状态
    bool is_connected;              // 当前连接状态
    int64_t last_disconnect_time_ms; // 上次断开时间
} mqtt_connection_stats_t;

// 获取全局统计变量的函数声明（只读访问）
const mqtt_connection_stats_t* monitor_get_stats(void);

// 监控函数声明
void monitor_init(void);
void monitor_task(void *pvParameters);
void monitor_record_connect(void);
void monitor_record_disconnect(const char* reason);
void monitor_report_statistics(void);
void monitor_reset_statistics(void);

// 时间同步相关函数
void monitor_start_time_sync(void);
bool monitor_wait_time_sync(int timeout_ms);
bool monitor_is_time_synced(void);
void monitor_get_current_time_str(char* buffer, size_t buffer_size);
void monitor_get_elapsed_time_str(char* buffer, size_t buffer_size);

#endif // MONITOR_H
