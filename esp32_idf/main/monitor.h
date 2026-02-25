#ifndef MONITOR_H
#define MONITOR_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

//////////////////////////////////////////////////////////////
//////////////////// MQTT Connection Monitor /////////////////
//////////////////////////////////////////////////////////////

// 监控配置参数
#define MONITOR_REPORT_INTERVAL_MS  (4 * 60 * 60 * 1000)  // 4小时报告间隔
#define MONITOR_MAX_DISCONNECT_LOG  100                   // 最大记录断开事件数

// 单次断开事件记录结构体
typedef struct {
    int64_t disconnect_time_ms;     // 断开时间戳 (毫秒)
    int64_t reconnect_time_ms;      // 重连时间戳 (毫秒，0表示尚未重连)
    int64_t disconnect_duration_ms; // 断开持续时长 (毫秒)
    const char* disconnect_reason;  // 断开原因描述
} disconnect_event_t;

// MQTT连接统计数据结构体
typedef struct {
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

// 全局统计变量声明
extern mqtt_connection_stats_t mqtt_stats;

// 监控函数声明
void monitor_init(void);
void monitor_task(void *pvParameters);
void monitor_record_connect(void);
void monitor_record_disconnect(const char* reason);
void monitor_report_statistics(void);
void monitor_reset_statistics(void);

#endif // MONITOR_H
