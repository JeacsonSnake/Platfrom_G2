#include "main.h"
#include "monitor.h"
#include "errno.h"
#include "lwip/sockets.h"

static char* TAG = "ESP32S3_MQTT_EVENT";
static bool connect_flag = false; // 定义一个连接上mqtt服务器的flag
static bool subscribe_flag = false; // 定义一个已订阅频道的flag
static SemaphoreHandle_t connect_flag_mutex = NULL; // 互斥锁保护connect_flag
static SemaphoreHandle_t subscribe_flag_mutex = NULL; // 互斥锁保护subscribe_flag
static int reconnect_attempts = 0; // 当前会话重连尝试计数
static int total_disconnect_count = 0; // 总断开次数计数器（累计所有会话）

// 错误类型统计
static int error_count_transport_timeout = 0;      // 传输层超时
static int error_count_ping_timeout = 0;           // PING超时
static int error_count_connection_reset = 0;       // 连接被重置
static int error_count_write_timeout = 0;          // 写入超时
static int error_count_connect_failed = 0;         // 连接失败

// 安全地获取连接状态
static bool get_connect_flag(void) {
    bool flag = false;
    if (connect_flag_mutex != NULL) {
        xSemaphoreTake(connect_flag_mutex, portMAX_DELAY);
    }
    flag = connect_flag;
    if (connect_flag_mutex != NULL) {
        xSemaphoreGive(connect_flag_mutex);
    }
    return flag;
}

// 安全地设置连接状态
static void set_connect_flag(bool flag) {
    if (connect_flag_mutex != NULL) {
        xSemaphoreTake(connect_flag_mutex, portMAX_DELAY);
    }
    connect_flag = flag;
    if (connect_flag_mutex != NULL) {
        xSemaphoreGive(connect_flag_mutex);
    }
}

// 安全地获取订阅状态
static bool get_subscribe_flag(void) {
    bool flag = false;
    if (subscribe_flag_mutex != NULL) {
        xSemaphoreTake(subscribe_flag_mutex, portMAX_DELAY);
    }
    flag = subscribe_flag;
    if (subscribe_flag_mutex != NULL) {
        xSemaphoreGive(subscribe_flag_mutex);
    }
    return flag;
}

// 安全地设置订阅状态
static void set_subscribe_flag(bool flag) {
    if (subscribe_flag_mutex != NULL) {
        xSemaphoreTake(subscribe_flag_mutex, portMAX_DELAY);
    }
    subscribe_flag = flag;
    if (subscribe_flag_mutex != NULL) {
        xSemaphoreGive(subscribe_flag_mutex);
    }
}

// 获取错误类型字符串并更新统计
static const char* get_error_type_string(esp_mqtt_error_type_t error_type, int esp_tls_last_esp_err, 
                                         int esp_tls_stack_err, int esp_tls_cert_verify_flags, 
                                         int connect_return_code)
{
    switch (error_type) {
        case MQTT_ERROR_TYPE_TCP_TRANSPORT:
            error_count_transport_timeout++;
            if (esp_tls_last_esp_err == ESP_ERR_MBEDTLS_SSL_SETUP_FAILED) {
                return "TLS_SSL_SETUP_FAILED";
            } else if (esp_tls_last_esp_err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) {
                return "TLS_HANDSHAKE_FAILED";
            } else if (esp_tls_stack_err == MBEDTLS_ERR_SSL_TIMEOUT) {
                return "TLS_TIMEOUT";
            } else if (connect_return_code == ECONNREFUSED) {
                return "TCP_CONNECTION_REFUSED";
            } else if (connect_return_code == ETIMEDOUT || connect_return_code == EINPROGRESS) {
                return "TCP_CONNECT_TIMEOUT";
            } else if (connect_return_code == ECONNRESET) {
                error_count_connection_reset++;
                return "TCP_CONNECTION_RESET";
            } else if (connect_return_code == ENETUNREACH) {
                return "NETWORK_UNREACHABLE";
            }
            return "TCP_TRANSPORT_ERROR";
            
        case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
            switch (connect_return_code) {
                case 0x01: return "CONN_REFUSE_PROTOCOL";
                case 0x02: return "CONN_REFUSE_ID_REJECTED";
                case 0x03: return "CONN_REFUSE_SERVER_UNAVAILABLE";
                case 0x04: return "CONN_REFUSE_BAD_CREDENTIALS";
                case 0x05: return "CONN_REFUSE_NOT_AUTHORIZED";
                default: return "CONN_REFUSE_UNKNOWN";
            }
            
        case MQTT_ERROR_TYPE_SUBSCRIBE_FAILED:
            return "SUBSCRIBE_FAILED";
            
        case MQTT_ERROR_TYPE_PUBLISH_FAILED:
            return "PUBLISH_FAILED";
            
        case MQTT_ERROR_TYPE_PING_TIMEOUT:
            error_count_ping_timeout++;
            return "PING_RESPONSE_TIMEOUT";
            
        default:
            return "UNKNOWN_ERROR";
    }
}

// MQTT信息处理
void message_compare(char *msg)
{
    if(strcmp(msg, "Hello there") == 0)
    {
        char buff[64];
        sprintf(buff, "Hello to you too");
        esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    }
    else if(strncmp(msg, "cmd_", 4) == 0)
    {
        int index, speed, duration;
        sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
        cmd_params params = {speed, duration, index};
        xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)&params, 1, NULL);

    }
}

// MQTT事件处理回调函数
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *event_data)
{
    // 获取MQTT客户端信息
    esp_mqtt_event_t *client_event = event_data;
    esp_mqtt_event_id_t client_id = id;

    // 判断MQTT具体事件
    switch (client_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT server.");
            set_connect_flag(true);
            reconnect_attempts = 0; // 重置重连计数器
            status_led_set_mode(LED_ON);  // MQTT 连接成功 - LED 常亮
            // 记录连接事件
            monitor_record_connect();
            // 订阅MQTT 控制频道来接收指令（仅在未订阅时订阅）
            if (!get_subscribe_flag()) {
                int msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_CONTROL_CHANNEL, 1);
                if (msg_id >= 0) {
                    ESP_LOGI(TAG, "已订阅控制频道 '%s' (msg_id=%d)", MQTT_CONTROL_CHANNEL, msg_id);
                    set_subscribe_flag(true);
                } else {
                    ESP_LOGE(TAG, "订阅控制频道失败");
                }
            } else {
                ESP_LOGI(TAG, "会话已恢复，跳过重复订阅");
            }
            break;

        case MQTT_EVENT_DISCONNECTED: {
            ESP_LOGI(TAG, "Disconnected from MQTT server.");
            set_connect_flag(false);
            set_subscribe_flag(false); // 重置订阅状态
            reconnect_attempts++;
            total_disconnect_count++;
            ESP_LOGW(TAG, "MQTT断开次数: 当前会话=%d, 总计=%d", reconnect_attempts, total_disconnect_count);
            status_led_set_mode(LED_BLINK_SLOW);  // MQTT 断开，回到慢速闪烁
            
            // 获取详细错误信息
            const char* error_reason = "No PING_RESP / Connection reset";
            if (client_event->error_handle) {
                esp_mqtt_error_type_t error_type = client_event->error_handle->error_type;
                int esp_tls_last_esp_err = client_event->error_handle->esp_tls_last_esp_err;
                int esp_tls_stack_err = client_event->error_handle->esp_tls_stack_err;
                int connect_return_code = client_event->error_handle->connect_return_code;
                
                error_reason = get_error_type_string(error_type, esp_tls_last_esp_err, 
                                                      esp_tls_stack_err, 0, connect_return_code);
                ESP_LOGW(TAG, "断开原因: %s (type=%d, tls_err=%d, stack_err=%d, ret_code=%d)",
                         error_reason, error_type, esp_tls_last_esp_err, 
                         esp_tls_stack_err, connect_return_code);
            }
            
            // 记录断开事件
            monitor_record_disconnect(error_reason);
            break;
        }

        case MQTT_EVENT_DATA:
            {
                // 接收外部传入的信息，分别存储topic和data
                char topic[128] = {};
                char data[512] = {};
                memcpy(topic, client_event->topic, client_event->topic_len);
                memcpy(data, client_event->data, client_event->data_len);
                // 进行信息处理，目前只传入了data而没有传入topic
                message_compare(data);
            }
            break;
            
        case MQTT_EVENT_ERROR: {
            ESP_LOGE(TAG, "MQTT connection error");
            set_connect_flag(false);
            set_subscribe_flag(false); // 重置订阅状态
            reconnect_attempts++;
            total_disconnect_count++;
            ESP_LOGW(TAG, "MQTT错误次数: 当前会话=%d, 总计=%d", reconnect_attempts, total_disconnect_count);
            status_led_set_mode(LED_BLINK_SLOW);  // MQTT 错误，回到慢速闪烁
            
            // 获取详细错误信息
            const char* error_reason = "MQTT_EVENT_ERROR";
            if (client_event->error_handle) {
                esp_mqtt_error_type_t error_type = client_event->error_handle->error_type;
                int esp_tls_last_esp_err = client_event->error_handle->esp_tls_last_esp_err;
                int esp_tls_stack_err = client_event->error_handle->esp_tls_stack_err;
                int connect_return_code = client_event->error_handle->connect_return_code;
                
                error_reason = get_error_type_string(error_type, esp_tls_last_esp_err, 
                                                      esp_tls_stack_err, 0, connect_return_code);
                ESP_LOGE(TAG, "错误详情: %s (type=%d, tls_err=%d, stack_err=%d, ret_code=%d)",
                         error_reason, error_type, esp_tls_last_esp_err, 
                         esp_tls_stack_err, connect_return_code);
            }
            
            // 记录断开事件
            monitor_record_disconnect(error_reason);
            break;
        }

        default:
            break;
    }
}

// 心跳发送任务 - 独立于MQTT事件处理
void mqtt_heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT心跳任务已启动");
    
    // 等待初始连接建立
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    while (1)
    {
        // 使用安全的方式获取连接状态
        if (get_connect_flag() == true)
        {
            char buff[64] = "ESP32_1 is online";
            // 向mqtt服务器发布主题为heartbeat，payload为buff的数据
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_HEARTBEAT_CHANNEL, buff, strlen(buff), 1, 0);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "心跳发送失败，可能连接已断开");
            } else {
                ESP_LOGD(TAG, "心跳已发送 (msg_id=%d)", msg_id);
            }
        }
        else
        {
            ESP_LOGD(TAG, "MQTT未连接，跳过本次心跳发送");
        }

        vTaskDelay(pdMS_TO_TICKS(30000));  // 应用层心跳改为30秒，减轻网络负担
    }
}

// 连接健康检查任务 - 监控连接状态并主动触发重连
void mqtt_health_check_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT连接健康检查任务已启动");
    
    int disconnect_count = 0;
    const int MAX_DISCONNECT_BEFORE_RECONNECT = 3; // 连续3次检查未连接则强制重连
    
    // 初始延迟，等待MQTT连接建立
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    while (1)
    {
        // 每10秒检查一次连接状态
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        if (!get_connect_flag()) {
            disconnect_count++;
            ESP_LOGW(TAG, "MQTT连接检查: 未连接 (计数=%d/%d, 会话重连=%d, 总计=%d)", 
                     disconnect_count, MAX_DISCONNECT_BEFORE_RECONNECT, reconnect_attempts, total_disconnect_count);
            
            if (disconnect_count >= MAX_DISCONNECT_BEFORE_RECONNECT) {
                // 计算指数退避延迟（最多30秒）
                int backoff_delay = (reconnect_attempts < 6) ? (1 << reconnect_attempts) * 1000 : 30000;
                if (backoff_delay > 30000) backoff_delay = 30000;
                
                ESP_LOGW(TAG, "MQTT连接检查: 连续%d次未连接，%dms后尝试强制重连...", 
                         MAX_DISCONNECT_BEFORE_RECONNECT, backoff_delay);
                
                vTaskDelay(pdMS_TO_TICKS(backoff_delay));
                
                // 再次检查是否仍未连接
                if (!get_connect_flag()) {
                    // 尝试停止并重新启动MQTT客户端
                    esp_mqtt_client_stop(mqtt_client);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_err_t err = esp_mqtt_client_start(mqtt_client);
                    
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "MQTT客户端已重新启动，等待连接...");
                    } else {
                        ESP_LOGE(TAG, "MQTT客户端重启失败: %s", esp_err_to_name(err));
                    }
                }
                
                disconnect_count = 0; // 重置计数器
            }
        } else {
            if (disconnect_count > 0) {
                ESP_LOGI(TAG, "MQTT连接检查: 连接已恢复");
            }
            disconnect_count = 0; // 连接正常，重置计数器
        }
    }
}

// 错误统计报告任务 - 定期输出错误统计
void mqtt_error_report_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(60000)); // 等待1分钟后开始报告
    
    while (1)
    {
        ESP_LOGI(TAG, "=================================================");
        ESP_LOGI(TAG, "           MQTT错误类型统计报告");
        ESP_LOGI(TAG, "=================================================");
        ESP_LOGI(TAG, "传输层超时:        %d", error_count_transport_timeout);
        ESP_LOGI(TAG, "PING响应超时:      %d", error_count_ping_timeout);
        ESP_LOGI(TAG, "连接被重置:        %d", error_count_connection_reset);
        ESP_LOGI(TAG, "写入超时:          %d", error_count_write_timeout);
        ESP_LOGI(TAG, "连接失败:          %d", error_count_connect_failed);
        ESP_LOGI(TAG, "=================================================");
        
        vTaskDelay(pdMS_TO_TICKS(300000)); // 每5分钟报告一次
    }
}

// 初始化MQTT线程
void mqtt_init()
{
    // 初始化互斥锁
    connect_flag_mutex = xSemaphoreCreateMutex();
    if (connect_flag_mutex == NULL) {
        ESP_LOGE(TAG, "创建connect_flag互斥锁失败");
        return;
    }
    
    subscribe_flag_mutex = xSemaphoreCreateMutex();
    if (subscribe_flag_mutex == NULL) {
        ESP_LOGE(TAG, "创建subscribe_flag互斥锁失败");
        // 清理已创建的资源
        vSemaphoreDelete(connect_flag_mutex);
        connect_flag_mutex = NULL;
        return;
    }
    
    // mqtt服务器的配置信息
    esp_mqtt_client_config_t cfg = {
        .broker.address = {
            // EMQX 服务器主机IP地址 (通过VMware NAT端口映射访问)
            .uri = "mqtt://192.168.110.31",
            .port = 1883,
            
        },
        .credentials = {
            .client_id = "ESP32S3_7cdfa1e6d3cc",  // 固定唯一ClientID（使用MAC地址后6位）
            .username = "ESP32_1",
            
        },
        .credentials.authentication = {
            .password = "123456",
        },
        // 添加会话和网络配置以改善连接稳定性
        .session = {
            .keepalive = 120,              // 120秒 KeepAlive 间隔，减少频繁 PING 带来的网络负担
            .disable_keepalive = false,    // 启用 KeepAlive
            .disable_clean_session = true, // 启用清理会话，避免服务器残留状态导致问题
        },
        .network = {
            .reconnect_timeout_ms = 5000,  // 恢复为 5秒重连间隔
            .timeout_ms = 20000,           // 网络操作超时增加到 20秒，适应不稳定网络
        },
        .buffer = {
            .size = 4096,                  // 增加发送缓冲区到4KB
            .out_size = 4096,              // 增加接收缓冲区到4KB
        },
        .task = {
            .priority = 5,                 // 提高MQTT内部任务优先级
            .stack_size = 8192,            // 增加MQTT内部任务栈大小到8KB
        }
    };

    // 定义MQTT客户端
    mqtt_client = esp_mqtt_client_init(&cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端初始化失败");
        return;
    }

    //注册MQTT状态机事件处理回调函数
    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);

    // 开始MQTT客户端
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT客户端启动失败: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "MQTT客户端已启动");
    
    // 此任务完成使命，删除自身
    // 心跳和健康检查由单独的任务处理
    vTaskDelete(NULL);
}
