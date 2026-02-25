#include "main.h"
#include "monitor.h"

static const char* TAG = "MQTT_MONITOR";

// 全局统计变量定义
static mqtt_connection_stats_t mqtt_stats = {
    .time_synced = false,
    .boot_time_ms = 0,
    .boot_real_time = 0,
    .time_sync_time_ms = 0,
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

// 同步信号量
static SemaphoreHandle_t sntp_sync_sem = NULL;
static bool sntp_initialized = false;
static SemaphoreHandle_t stats_mutex = NULL;  // 互斥锁保护统计变量

// 内部函数：初始化互斥锁
static void init_stats_mutex(void) {
    if (stats_mutex == NULL) {
        stats_mutex = xSemaphoreCreateMutex();
    }
}

// 获取全局统计变量（只读访问）
const mqtt_connection_stats_t* monitor_get_stats(void) {
    return &mqtt_stats;
}

// 内部函数：安全更新时间同步状态
static void set_time_synced(bool synced) {
    if (stats_mutex != NULL) {
        xSemaphoreTake(stats_mutex, portMAX_DELAY);
    }
    mqtt_stats.time_synced = synced;
    if (stats_mutex != NULL) {
        xSemaphoreGive(stats_mutex);
    }
}

// 内部函数：安全获取时间同步状态
static bool get_time_synced(void) {
    bool synced = false;
    if (stats_mutex != NULL) {
        xSemaphoreTake(stats_mutex, portMAX_DELAY);
    }
    synced = mqtt_stats.time_synced;
    if (stats_mutex != NULL) {
        xSemaphoreGive(stats_mutex);
    }
    return synced;
}

//////////////////////////////////////////////////////////////
//////////////////// 时间同步回调 /////////////////////////////
//////////////////////////////////////////////////////////////

// SNTP 同步完成回调函数
static void sntp_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "NTP时间同步回调被触发！");
    
    if (sntp_sync_sem != NULL) {
        xSemaphoreGive(sntp_sync_sem);
        ESP_LOGI(TAG, "已发送同步完成信号");
    }
    
    // 立即更新同步状态（使用互斥锁保护）
    int64_t current_time = esp_timer_get_time() / 1000;
    mqtt_stats.time_sync_time_ms = current_time;
    
    // 获取当前实际时间作为开机参考时间
    time_t now = time(NULL);
    mqtt_stats.boot_real_time = now - (current_time / 1000);
    
    // 使用互斥锁安全设置同步标志
    set_time_synced(true);
    
    struct tm* tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    ESP_LOGI(TAG, "时间同步完成！");
    ESP_LOGI(TAG, "当前实际时间: %s", time_str);
    ESP_LOGI(TAG, "开机时间戳: %lld ms", mqtt_stats.boot_time_ms);
    ESP_LOGI(TAG, "实际开机时间: %s", ctime(&mqtt_stats.boot_real_time));
    ESP_LOGI(TAG, "==============================================");
}

//////////////////////////////////////////////////////////////
//////////////////// 时间同步功能 /////////////////////////////
//////////////////////////////////////////////////////////////

// 启动时间同步
void monitor_start_time_sync(void)
{
    // 防止重复初始化
    if (sntp_initialized) {
        ESP_LOGW(TAG, "SNTP 已经初始化，跳过");
        return;
    }
    
    // 初始化互斥锁
    init_stats_mutex();
    
    if (sntp_sync_sem == NULL) {
        sntp_sync_sem = xSemaphoreCreateBinary();
    }
    
    // 记录开机时间（系统启动时间）
    mqtt_stats.boot_time_ms = esp_timer_get_time() / 1000;
    // 重置时间同步状态
    mqtt_stats.time_synced = false;
    
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "正在启动 SNTP 时间同步...");
    ESP_LOGI(TAG, "NTP服务器: %s", NTP_SERVER);
    ESP_LOGI(TAG, "当前系统时间: %lld ms", mqtt_stats.boot_time_ms);
    
    // 配置 SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    esp_sntp_init();
    sntp_initialized = true;
    
    ESP_LOGI(TAG, "SNTP 初始化完成，等待时间同步...");
    ESP_LOGI(TAG, "==============================================");
}

// 等待时间同步完成
bool monitor_wait_time_sync(int timeout_ms)
{
    if (sntp_sync_sem == NULL) {
        return false;
    }
    
    // 等待同步完成信号
    if (xSemaphoreTake(sntp_sync_sem, pdMS_TO_TICKS(timeout_ms)) == pdPASS) {
        // 记录同步完成时间
        mqtt_stats.time_sync_time_ms = esp_timer_get_time() / 1000;
        mqtt_stats.time_synced = true;
        
        // 获取当前实际时间作为开机参考时间
        time_t now = time(NULL);
        mqtt_stats.boot_real_time = now - (mqtt_stats.time_sync_time_ms / 1000);
        
        struct tm* tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        ESP_LOGI(TAG, "==============================================");
        ESP_LOGI(TAG, "时间同步成功！");
        ESP_LOGI(TAG, "当前时间: %s", time_str);
        ESP_LOGI(TAG, "开机时间戳: %lld ms", mqtt_stats.boot_time_ms);
        ESP_LOGI(TAG, "实际开机时间: %s", ctime(&mqtt_stats.boot_real_time));
        ESP_LOGI(TAG, "==============================================");
        
        return true;
    }
    
    ESP_LOGW(TAG, "时间同步超时（%d ms），将继续使用系统时间", timeout_ms);
    return false;
}

// 检查时间是否已同步
bool monitor_is_time_synced(void)
{
    return get_time_synced();
}

//////////////////////////////////////////////////////////////
//////////////////// 时间格式化辅助函数 ///////////////////////
//////////////////////////////////////////////////////////////

// 获取带偏移的当前实际时间字符串（考虑开机时间）
void monitor_get_current_time_str(char* buffer, size_t buffer_size)
{
    // 使用 getter 函数安全获取同步状态
    if (get_time_synced()) {
        // 使用实际时间
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        // 未同步时使用系统运行时间
        int64_t uptime_ms = esp_timer_get_time() / 1000;
        int64_t uptime_sec = uptime_ms / 1000;
        int hours = uptime_sec / 3600;
        int minutes = (uptime_sec % 3600) / 60;
        int seconds = uptime_sec % 60;
        snprintf(buffer, buffer_size, "未同步 [运行 %02d:%02d:%02d]", hours, minutes, seconds);
    }
}

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

// 获取已运行时间字符串
void monitor_get_elapsed_time_str(char* buffer, size_t buffer_size)
{
    int64_t current_time = esp_timer_get_time() / 1000;
    int64_t elapsed_ms = current_time - mqtt_stats.boot_time_ms;
    format_duration(elapsed_ms, buffer, buffer_size);
}

//////////////////////////////////////////////////////////////
//////////////////// 监控功能初始化 ///////////////////////////
//////////////////////////////////////////////////////////////

void monitor_init(void) {
    // 初始化互斥锁
    init_stats_mutex();
    
    // 记录初始开机时间
    mqtt_stats.boot_time_ms = esp_timer_get_time() / 1000;
    
    ESP_LOGI(TAG, "MQTT连接监控模块已初始化");
    ESP_LOGI(TAG, "统计报告间隔: %d 小时", MONITOR_REPORT_INTERVAL_MS / (60 * 60 * 1000));
    
    // 打印表头
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "      MQTT连接监控统计系统已启动");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "提示: 时间将在WiFi连接后通过NTP同步");
}

//////////////////////////////////////////////////////////////
//////////////////// 记录连接事件 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_record_connect(void) {
    int64_t current_time = esp_timer_get_time() / 1000;
    
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
    
    char time_str[64];
    monitor_get_current_time_str(time_str, sizeof(time_str));
    
    ESP_LOGI(TAG, "[连接事件] #%d | 时间: %s", mqtt_stats.total_connections, time_str);
}

//////////////////////////////////////////////////////////////
//////////////////// 记录断开事件 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_record_disconnect(const char* reason) {
    int64_t current_time = esp_timer_get_time() / 1000;
    
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
    
    char time_str[64];
    monitor_get_current_time_str(time_str, sizeof(time_str));
    
    ESP_LOGW(TAG, "[断开事件] #%d | 时间: %s | 原因: %s", 
             mqtt_stats.total_disconnects, time_str, reason ? reason : "Unknown");
}

//////////////////////////////////////////////////////////////
//////////////////// 输出统计报告 /////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_report_statistics(void) {
    int64_t current_time = esp_timer_get_time() / 1000;
    
    // 计算总运行时间
    int64_t total_runtime = current_time - mqtt_stats.boot_time_ms;
    
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
    
    char time_str[64];
    monitor_get_current_time_str(time_str, sizeof(time_str));
    
    char elapsed_str[32];
    monitor_get_elapsed_time_str(elapsed_str, sizeof(elapsed_str));
    
    // 打印统计报告
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "           MQTT连接统计报告 [%s]", time_str);
    ESP_LOGI(TAG, "=================================================");
    
    // 显示开机时间信息
    if (mqtt_stats.time_synced) {
        char boot_time_str[32];
        struct tm* tm_info = localtime(&mqtt_stats.boot_real_time);
        strftime(boot_time_str, sizeof(boot_time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        ESP_LOGI(TAG, "实际开机时间:   %s", boot_time_str);
    }
    ESP_LOGI(TAG, "系统运行时长:   %s", elapsed_str);
    ESP_LOGI(TAG, "-------------------------------------------------");
    ESP_LOGI(TAG, "总连接次数:     %d", mqtt_stats.total_connections);
    ESP_LOGI(TAG, "总断开次数:     %d", mqtt_stats.total_disconnects);
    ESP_LOGI(TAG, "累计连接时长:   %s", connected_str);
    ESP_LOGI(TAG, "平均连接时长:   %s/次", avg_session_str);
    ESP_LOGI(TAG, "连接保持率:     %.2f%%", connection_rate);
    ESP_LOGI(TAG, "当前连接状态:   %s", mqtt_stats.is_connected ? "已连接" : "未连接");
    ESP_LOGI(TAG, "时间同步状态:   %s", get_time_synced() ? "已同步" : "未同步");
    
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
            
            // 显示断开时间
            char disconnect_time_str[32];
            if (get_time_synced()) {
                // 计算实际断开时间
                time_t disconnect_real_time = mqtt_stats.boot_real_time + 
                    (event->disconnect_time_ms / 1000);
                struct tm* tm_info = localtime(&disconnect_real_time);
                strftime(disconnect_time_str, sizeof(disconnect_time_str), "%H:%M:%S", tm_info);
            } else {
                int64_t uptime_sec = event->disconnect_time_ms / 1000;
                int hours = uptime_sec / 3600;
                int minutes = (uptime_sec % 3600) / 60;
                int seconds = uptime_sec % 60;
                snprintf(disconnect_time_str, sizeof(disconnect_time_str), "%02d:%02d:%02d", 
                         hours, minutes, seconds);
            }
            
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
    mqtt_stats.boot_time_ms = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "统计数据已重置");
}

//////////////////////////////////////////////////////////////
//////////////////// 监控任务 /////////////////////////////////
//////////////////////////////////////////////////////////////

void monitor_task(void *pvParameters) {
    // 初始化监控模块
    monitor_init();
    
    // 等待 SNTP 同步完成（最多等待60秒）
    ESP_LOGI(TAG, "等待 SNTP 时间同步完成...");
    bool sync_success = false;
    int wait_count = 0;
    const int MAX_WAIT = 60; // 最多等待60秒
    
    while (!sync_success && wait_count < MAX_WAIT) {
        if (monitor_is_time_synced()) {
            sync_success = true;
            ESP_LOGI(TAG, "时间同步已完成，开始监控任务");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_count++;
        if (wait_count % 10 == 0) {
            ESP_LOGW(TAG, "仍在等待时间同步... (%d/%d 秒)", wait_count, MAX_WAIT);
        }
    }
    
    if (!sync_success) {
        ESP_LOGW(TAG, "时间同步超时，将继续使用系统时间进行监控");
    }
    
    // 主循环：每4小时输出一次统计报告
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_REPORT_INTERVAL_MS));
        
        // 输出统计报告
        monitor_report_statistics();
    }
}
