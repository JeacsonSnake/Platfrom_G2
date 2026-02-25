#include "main.h"
#include "monitor.h"

static const char* TAG = "MQTT_MONITOR";

// 全局统计变量定义
mqtt_connection_stats_t mqtt_stats = {
    .total_connections = 0,
    .total_disconnects = 0,
    .first_connect_time_ms = 0,
    .current_session_start_ms = 0,
    .total_connected_time_ms = 0,
    .log_index = 0,
    .log_count = 0,
    .is_connected = false,
    .last_disconnect_time_ms = 0
};

// 辅助函数：格式化毫秒时间为可读字符串
static void format_duration(int64_t ms, char* buffer, size_t buffer_size) {
    if (ms < 1000) {
        snprintf(buffer, buffer_size, "%lld ms", ms);
    } else if (ms < 60 * 1000) {
        snprintf(buffer, buffer_size, "%.1f s", ms / 1000.0);
    } else if (ms < 60 * 60 * 1000) {
        snprintf(buffer, buffer_size, "%.1f min", ms / (60.0 * 1000.0));
    } else {
        snprintf(buffer, buffer_size, "%.2f h", ms / (60.0 * 60.0 * 1000.0));
    }
}

// 辅助函数：获取当前时间字符串
static void get_current_time_str(char* buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

//////////////////////////////////////////////////////////////
//////////////////// 监控功能初始化 ///////////////////////////
//////////////////////////////////////////////////////////////

void monitor_init(void) {
    ESP_LOGI(TAG, "MQTT连接监控模块已初始化");
    ESP_LOGI(TAG, "统计报告间隔: %d 小时", MONITOR_REPORT_INTERVAL_MS / (60 * 60 * 1000));
    
    // 打印表头
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "      MQTT连接监控统计系统已启动");
    ESP_LOGI(TAG, "=================================================");
}

//////////////////////////////////////////////////////////////
//////////////////// 记录连接事件 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_record_connect(void) {
    int64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    // 更新统计
    mqtt_stats.total_connections++;
    mqtt_stats.is_connected = true;
    mqtt_stats.current_session_start_ms = current_time;
    
    // 记录首次连接时间
    if (mqtt_stats.first_connect_time_ms == 0) {
        mqtt_stats.first_connect_time_ms = current_time;
    }
    
    // 如果有上次断开记录，更新断开事件的恢复时间
    if (mqtt_stats.last_disconnect_time_ms > 0) {
        int idx = (mqtt_stats.log_index - 1 + MONITOR_MAX_DISCONNECT_LOG) % MONITOR_MAX_DISCONNECT_LOG;
        if (mqtt_stats.disconnect_log[idx].disconnect_time_ms == mqtt_stats.last_disconnect_time_ms) {
            mqtt_stats.disconnect_log[idx].reconnect_time_ms = current_time;
            mqtt_stats.disconnect_log[idx].disconnect_duration_ms = 
                current_time - mqtt_stats.last_disconnect_time_ms;
        }
        mqtt_stats.last_disconnect_time_ms = 0;
    }
    
    char time_str[32];
    get_current_time_str(time_str, sizeof(time_str));
    
    ESP_LOGI(TAG, "[连接事件] #%d | 时间: %s", mqtt_stats.total_connections, time_str);
}

//////////////////////////////////////////////////////////////
//////////////////// 记录断开事件 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_record_disconnect(const char* reason) {
    int64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    // 更新统计
    mqtt_stats.total_disconnects++;
    mqtt_stats.is_connected = false;
    mqtt_stats.last_disconnect_time_ms = current_time;
    
    // 累加本次连接时长
    if (mqtt_stats.current_session_start_ms > 0) {
        mqtt_stats.total_connected_time_ms += (current_time - mqtt_stats.current_session_start_ms);
        mqtt_stats.current_session_start_ms = 0;
    }
    
    // 记录到断开日志
    disconnect_event_t* event = &mqtt_stats.disconnect_log[mqtt_stats.log_index];
    event->disconnect_time_ms = current_time;
    event->reconnect_time_ms = 0;
    event->disconnect_duration_ms = 0;
    event->disconnect_reason = reason ? reason : "Unknown";
    
    mqtt_stats.log_index = (mqtt_stats.log_index + 1) % MONITOR_MAX_DISCONNECT_LOG;
    if (mqtt_stats.log_count < MONITOR_MAX_DISCONNECT_LOG) {
        mqtt_stats.log_count++;
    }
    
    char time_str[32];
    get_current_time_str(time_str, sizeof(time_str));
    
    ESP_LOGW(TAG, "[断开事件] #%d | 时间: %s | 原因: %s", 
             mqtt_stats.total_disconnects, time_str, reason ? reason : "Unknown");
}

//////////////////////////////////////////////////////////////
//////////////////// 输出统计报告 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_report_statistics(void) {
    int64_t current_time = esp_timer_get_time() / 1000;
    
    // 计算总运行时间
    int64_t total_runtime = current_time - mqtt_stats.first_connect_time_ms;
    
    // 计算当前连接时长（如果已连接）
    int64_t current_session_duration = 0;
    if (mqtt_stats.is_connected && mqtt_stats.current_session_start_ms > 0) {
        current_session_duration = current_time - mqtt_stats.current_session_start_ms;
    }
    
    // 计算总连接时长（包含当前会话）
    int64_t total_connected = mqtt_stats.total_connected_time_ms + current_session_duration;
    
    // 计算连接率
    float connection_rate = (total_runtime > 0) ? 
        ((float)total_connected / total_runtime * 100.0f) : 0.0f;
    
    // 计算平均连接时长
    float avg_session_duration = (mqtt_stats.total_connections > 0) ? 
        ((float)total_connected / mqtt_stats.total_connections / 1000.0f) : 0.0f;
    
    // 格式化时间显示
    char runtime_str[32], connected_str[32], avg_session_str[32];
    format_duration(total_runtime, runtime_str, sizeof(runtime_str));
    format_duration(total_connected, connected_str, sizeof(connected_str));
    format_duration((int64_t)(avg_session_duration * 1000), avg_session_str, sizeof(avg_session_str));
    
    char time_str[32];
    get_current_time_str(time_str, sizeof(time_str));
    
    // 打印统计报告
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "           MQTT连接统计报告 [%s]", time_str);
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "总运行时间:     %s", runtime_str);
    ESP_LOGI(TAG, "总连接次数:     %d", mqtt_stats.total_connections);
    ESP_LOGI(TAG, "总断开次数:     %d", mqtt_stats.total_disconnects);
    ESP_LOGI(TAG, "累计连接时长:   %s", connected_str);
    ESP_LOGI(TAG, "平均连接时长:   %s/次", avg_session_str);
    ESP_LOGI(TAG, "连接保持率:     %.2f%%", connection_rate);
    ESP_LOGI(TAG, "当前连接状态:   %s", mqtt_stats.is_connected ? "已连接" : "未连接");
    
    // 打印最近断开事件
    if (mqtt_stats.log_count > 0) {
        ESP_LOGI(TAG, "-------------------------------------------------");
        ESP_LOGI(TAG, "最近 %d 次断开事件:", 
                 (mqtt_stats.log_count > 10) ? 10 : mqtt_stats.log_count);
        ESP_LOGI(TAG, "%-5s %-20s %-15s %s", "序号", "断开时间", "恢复耗时", "原因");
        
        int count = (mqtt_stats.log_count > 10) ? 10 : mqtt_stats.log_count;
        for (int i = 0; i < count; i++) {
            int idx = (mqtt_stats.log_index - count + i + MONITOR_MAX_DISCONNECT_LOG) % MONITOR_MAX_DISCONNECT_LOG;
            disconnect_event_t* event = &mqtt_stats.disconnect_log[idx];
            
            char duration_str[16];
            if (event->reconnect_time_ms > 0) {
                format_duration(event->disconnect_duration_ms, duration_str, sizeof(duration_str));
            } else {
                snprintf(duration_str, sizeof(duration_str), "未恢复");
            }
            
            char disconnect_time_str[20];
            time_t t = event->disconnect_time_ms / 1000;
            struct tm* tm_info = localtime(&t);
            strftime(disconnect_time_str, sizeof(disconnect_time_str), "%H:%M:%S", tm_info);
            
            ESP_LOGI(TAG, "#%-4d %-20s %-15s %s", 
                     i + 1, disconnect_time_str, duration_str, event->disconnect_reason);
        }
    }
    
    ESP_LOGI(TAG, "=================================================");
}

//////////////////////////////////////////////////////////////
//////////////////// 重置统计数据 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_reset_statistics(void) {
    memset(&mqtt_stats, 0, sizeof(mqtt_stats));
    ESP_LOGI(TAG, "统计数据已重置");
}

//////////////////////////////////////////////////////////////
//////////////////// 监控任务 /////////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_task(void *pvParameters) {
    // 初始化监控模块
    monitor_init();
    
    // 等待首次连接
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    while (1) {
        // 每4小时输出一次统计报告
        vTaskDelay(pdMS_TO_TICKS(MONITOR_REPORT_INTERVAL_MS));
        
        // 输出统计报告
        monitor_report_statistics();
    }
}
