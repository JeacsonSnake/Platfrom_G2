# ESP32S3 MQTT 断连问题修复与错误诊断优化记录

**日期**: 2026-02-26  
**分支**: `fix/mqtt-connection`  
**任务描述**: 修复 MQTT 断连问题，优化连接稳定性，实现详细错误分类诊断，区分 WiFi 问题与 MQTT 服务器问题

---

## 1. 背景

### 历史问题回顾
根据之前的测试记录，ESP32S3 存在严重的 MQTT 连接不稳定问题：
- 频繁断开，日志显示 `No PING_RESP` 错误
- 断开原因不明确，难以区分是 WiFi 问题还是 MQTT 服务器问题
- 重连机制被动，依赖 MQTT 客户端内部重连
- 心跳发送与 MQTT 事件处理在同一线程，可能相互阻塞

### 本次任务目标
1. 修复 MQTT 断连的根本原因
2. 分离心跳发送任务，避免阻塞事件处理
3. 实现详细错误分类诊断，精确定位问题
4. 优化重连机制，提高连接恢复速度
5. 区分 WiFi 问题与 MQTT 服务器问题

---

## 2. 问题分析与修复方案

### 2.1 核心问题分析

| 问题 | 原因 | 影响 |
|------|------|------|
| 心跳阻塞事件处理 | `mqtt_init()` 中包含阻塞式心跳循环 | MQTT 协议层无法及时处理 PING 响应 |
| 缺乏主动重连 | 仅依赖 MQTT 客户端内部重连机制 | 断开后恢复速度慢 |
| 连接状态竞争 | `connect_flag` 无互斥锁保护 | 多任务环境下状态不一致 |
| 错误信息笼统 | 所有断开都显示 `No PING_RESP` | 无法定位根本原因 |
| WiFi/MQTT 混淆 | MQTT 错误时无法判断 WiFi 状态 | 诊断困难 |

### 2.2 修复策略

1. **任务分离**: 将心跳发送、健康检查独立为单独任务
2. **互斥锁保护**: 使用互斥锁保护连接状态变量
3. **主动重连**: 健康检查任务主动检测并重连
4. **错误分类**: 根据 `error_handle` 详细分类错误类型
5. **WiFi 状态检查**: MQTT 检查前优先确认 WiFi 状态

---

## 3. 实现方案

### 3.1 新增功能

#### 心跳发送任务 (`mqtt_heartbeat_task`)
```c
void mqtt_heartbeat_task(void *pvParameters)
{
    while (1) {
        if (get_connect_flag()) {
            esp_mqtt_client_publish(mqtt_client, MQTT_HEARTBEAT_CHANNEL, 
                                   "ESP32_1 is online", ..., 1, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30秒间隔
    }
}
```

#### 连接健康检查任务 (`mqtt_health_check_task`)
```c
void mqtt_health_check_task(void *pvParameters)
{
    while (1) {
        // 优先检查 WiFi 状态
        if (!wifi_is_connected()) {
            ESP_LOGW(TAG, "WiFi未连接，跳过MQTT重连检查");
            continue;
        }
        
        // 检查 MQTT 连接状态
        if (!get_connect_flag()) {
            // 连续3次未连接则强制重连
            // 指数退避策略：1s, 2s, 4s, 8s, 16s, 30s...
        }
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10秒检查间隔
    }
}
```

#### 错误分类诊断 (`get_error_type_string`)
```c
static const char* get_error_type_string(...)
{
    switch (error_type) {
        case MQTT_ERROR_TYPE_TCP_TRANSPORT:
            if (esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CANNOT_CONNECT) {
                if (!wifi_is_connected()) {
                    return "WIFI_NOT_CONNECTED";
                }
                return "TLS_CANNOT_CONNECT";
            }
            // ... 其他错误类型判断
    }
}
```

### 3.2 修改文件

#### `main/mqtt.c` - 核心重构
```c
// 新增：互斥锁保护连接状态
static SemaphoreHandle_t connect_flag_mutex = NULL;
static SemaphoreHandle_t subscribe_flag_mutex = NULL;

// 新增：连接状态安全访问
static bool get_connect_flag(void) { ... }
static void set_connect_flag(bool flag) { ... }

// 新增：错误统计变量
static int error_count_transport_timeout = 0;
static int error_count_ping_timeout = 0;
static int error_count_connection_reset = 0;
static int error_count_connect_failed = 0;

// 修改：MQTT 事件处理，支持详细错误诊断
case MQTT_EVENT_ERROR:
case MQTT_EVENT_DISCONNECTED:
    // 获取详细错误信息
    if (client_event->error_handle) {
        error_reason = get_error_type_string(...);
        ESP_LOGE(TAG, "错误详情: %s (type=%d, tls_err=%d, ...)", ...);
    }
    monitor_record_disconnect(error_reason);
    break;
```

#### `main/mqtt.c` - MQTT 配置优化
```c
esp_mqtt_client_config_t cfg = {
    .session = {
        .keepalive = 120,              // 从60秒增加到120秒
        .disable_clean_session = true, // 启用清理会话
    },
    .network = {
        .reconnect_timeout_ms = 5000,  // 5秒重连间隔
        .timeout_ms = 20000,           // 20秒网络超时
    },
    .buffer = {
        .size = 4096,                  // 增加到4KB
        .out_size = 4096,
    },
    .task = {
        .priority = 5,                 // 提高优先级
        .stack_size = 8192,            // 8KB栈
    }
};
```

#### `main/wifi.c` - 新增 WiFi 状态跟踪
```c
// 新增：WiFi 连接状态标志
static bool wifi_connected = false;

// 新增：获取 WiFi 连接状态（供 MQTT 模块使用）
bool wifi_is_connected(void)
{
    return wifi_connected;
}

// 修改：事件处理中更新状态
static void event_handler(...)
{
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
    }
    else if (event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
    }
}
```

#### `main/main.c` - 任务创建调整
```c
// 修改前：单一 MQTT 任务
xTaskCreate(mqtt_init, "MQTT_TASK", 4096, NULL, 1, NULL);

// 修改后：分离为多个任务
xTaskCreate(mqtt_init, "MQTT_INIT", 8192, NULL, 2, NULL);
xTaskCreate(mqtt_heartbeat_task, "MQTT_HB", 4096, NULL, 1, NULL);
xTaskCreate(mqtt_health_check_task, "MQTT_CHK", 4096, NULL, 1, NULL);
xTaskCreate(mqtt_error_report_task, "MQTT_ERR", 2048, NULL, 1, NULL);
```

#### `main/main.h` - 新增声明
```c
// MQTT 任务声明
void mqtt_heartbeat_task(void *pvParameters);
void mqtt_health_check_task(void *pvParameters);
void mqtt_error_report_task(void *pvParameters);

// WiFi 状态查询
bool wifi_is_connected(void);
```

---

## 4. 错误类型诊断体系

### 4.1 错误分类表

| 错误类型 | 触发条件 | 说明 |
|---------|---------|------|
| `WIFI_NOT_CONNECTED` | `tls_err=32772` 且 WiFi 未连接 | WiFi 未连接导致无法连接 MQTT |
| `TLS_CANNOT_CONNECT` | `tls_err=32772` 且 WiFi 已连接 | TLS 层无法建立连接 |
| `TCP_CONNECTION_REFUSED` | `errno=ECONNREFUSED` | 服务器拒绝连接 |
| `TCP_CONNECT_TIMEOUT` | `errno=ETIMEDOUT` | TCP 连接超时 |
| `TCP_CONNECT_IN_PROGRESS` | `errno=EINPROGRESS` | 异步连接超时 |
| `TCP_CONNECTION_RESET` | `errno=ECONNRESET` | 连接被重置 |
| `NETWORK_UNREACHABLE` | `errno=ENETUNREACH` | 网络不可达 |
| `HOST_UNREACHABLE` | `errno=EHOSTUNREACH` | 主机不可达 |
| `CONN_REFUSE_PROTOCOL` | MQTT 返回码 0x01 | 协议版本不支持 |
| `CONN_REFUSE_ID_REJECTED` | MQTT 返回码 0x02 | Client ID 被拒绝 |
| `CONN_REFUSE_BAD_CREDENTIALS` | MQTT 返回码 0x04 | 认证失败 |

### 4.2 错误统计报告

新增 `mqtt_error_report_task`，每5分钟输出错误统计：
```
=================================================
           MQTT错误类型统计报告
=================================================
传输层超时:        X
PING响应超时:      X
连接被重置:        X
写入超时:          X
连接失败:          X
=================================================
```

---

## 5. Git 提交记录

### 第一次提交：修复 MQTT 断连问题，优化连接稳定性

```bash
git add main/mqtt.c main/main.c main/main.h
git commit -m "fix(mqtt): 修复 MQTT 断连问题，优化连接稳定性

- 分离心跳发送任务：创建独立的 mqtt_heartbeat_task
- 添加连接健康检查任务：每10秒检查连接状态，主动重连
- 添加互斥锁保护 connect_flag：防止多任务竞争
- 优化 MQTT 配置：
  - 缓冲区从 1KB 增加到 2KB
  - MQTT 内部任务优先级提升到 5
  - MQTT 内部任务栈增加到 8KB
- 调整任务创建逻辑：
  - mqtt_init 任务栈从 4KB 增加到 8KB
  - 新增 MQTT_HB 和 MQTT_CHK 任务"
```

**提交信息**:  
- Commit: `1305748`  
- 修改: 3 个文件，141 行新增，21 行删除

---

### 第二次提交：优化重连机制和订阅管理

```bash
git add main/mqtt.c
git commit -m "fix(mqtt): 优化重连机制和订阅管理

- 添加订阅状态跟踪：使用 subscribe_flag 避免重复订阅
- 实现指数退避重连策略：1s, 2s, 4s, 8s, 16s, 30s...
- 添加重连计数器：跟踪总重连次数
- 订阅 QoS 从 2 降级为 1，减少网络负担
- 断开时重置订阅状态，确保重连后正确重新订阅"
```

**提交信息**:  
- Commit: `a801650`  
- 修改: 1 个文件，74 行新增，12 行删除

---

### 第三次提交：删除未使用的常量

```bash
git add main/mqtt.c
git commit -m "fix(mqtt): 删除未使用的常量 MAX_RECONNECT_ATTEMPTS

- 修复编译错误：未使用变量导致 -Werror 报错"
```

**提交信息**:  
- Commit: `5cb7331`  
- 修改: 1 个文件，1 行删除

---

### 第四次提交：优化 MQTT 配置和错误处理

```bash
git add main/mqtt.c
git commit -m "fix(mqtt): 优化 MQTT 配置和错误处理，添加总断开计数器

- 修复 MQTT_EVENT_ERROR 未递增计数器的问题
- 添加 total_disconnect_count 总计数器
- 优化 MQTT 配置参数：
  - KeepAlive 从 60 秒增加到 120 秒
  - 启用清理会话 (disable_clean_session = true)
  - 重连间隔从 5 秒缩短到 3 秒
  - 网络超时从 10 秒增加到 15 秒"
```

**提交信息**:  
- Commit: `e270eee`  
- 修改: 1 个文件，14 行新增，8 行删除

---

### 第五次提交：实现详细错误分类诊断

```bash
git add main/mqtt.c main/main.c main/main.h
git commit -m "feat(mqtt): 实现详细错误分类诊断和优化网络配置

- 新增 get_error_type_string() 函数，详细分类错误类型
- 支持错误类型：TCP_CONNECT_TIMEOUT, CONNECTION_RESET, PING_TIMEOUT 等
- 新增错误统计变量和 mqtt_error_report_task 任务
- 缓冲区从 2KB 增加到 4KB
- 网络超时增加到 20 秒
- 优化日志输出，显示详细错误信息"
```

**提交信息**:  
- Commit: `c5fb4e0`  
- 修改: 3 个文件，128 行新增，8 行删除

---

### 第六次提交：修复编译错误

```bash
git add main/mqtt.c
git commit -m "fix(mqtt): 修复编译错误，移除未定义的宏引用

- 移除 ESP_ERR_MBEDTLS_SSL_SETUP_FAILED 等未定义宏
- 移除 MQTT_ERROR_TYPE_PUBLISH_FAILED 未定义枚举
- 使用 errno 值进行错误分类
- 简化 TLS 错误判断"
```

**提交信息**:  
- Commit: `5b87933`  
- 修改: 1 个文件，19 行新增，16 行删除

---

### 第七次提交：优化错误诊断和 WiFi 状态检查

```bash
git add main/mqtt.c main/wifi.c main/main.h
git commit -m "feat(mqtt/wifi): 优化错误诊断和 WiFi 状态检查

- 新增 ESP_ERR_ESP_TLS_CANNOT_CONNECT (32772) 错误码识别
- 新增 'WIFI_NOT_CONNECTED' 和 'TLS_CANNOT_CONNECT' 错误类型
- wifi.c 新增 wifi_connected 状态变量和 wifi_is_connected() 函数
- MQTT 健康检查任务优先确认 WiFi 状态，避免无效重连
- 添加 'WiFi未连接，跳过MQTT重连检查' 日志提示"
```

**提交信息**:  
- Commit: `d3adeca`  
- 修改: 3 个文件，45 行新增，1 行删除

---

## 6. 关键代码详解

### 6.1 互斥锁保护机制

```c
static SemaphoreHandle_t connect_flag_mutex = NULL;
static bool connect_flag = false;

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

static void set_connect_flag(bool flag) {
    if (connect_flag_mutex != NULL) {
        xSemaphoreTake(connect_flag_mutex, portMAX_DELAY);
    }
    connect_flag = flag;
    if (connect_flag_mutex != NULL) {
        xSemaphoreGive(connect_flag_mutex);
    }
}
```

### 6.2 指数退避重连策略

```c
// 计算指数退避延迟（最多30秒）
int backoff_delay = (reconnect_attempts < 6) ? (1 << reconnect_attempts) * 1000 : 30000;
if (backoff_delay > 30000) backoff_delay = 30000;

// 延迟序列：1s, 2s, 4s, 8s, 16s, 30s, 30s...
vTaskDelay(pdMS_TO_TICKS(backoff_delay));
```

### 6.3 错误诊断示例

```c
// 日志输出示例：
E (65658) ESP32S3_MQTT_EVENT: 错误详情: WIFI_NOT_CONNECTED 
    (type=1, tls_err=32772, stack_err=0, ret_code=0)
    
// 这明确表示：WiFi 未连接导致 MQTT 无法连接服务器
```

---

## 7. 使用说明

### 编译烧录
```powershell
# 清理并重新构建
Remove-Item -Recurse -Force build
idf.py build
idf.py -p COM9 flash monitor
```

### 监控日志分析

1. **观察 WiFi 连接状态**：
   ```
   I (xxx) ESP32S3_WIFI_EVENT: Got ip:192.168.xxx.xxx
   ```

2. **观察 MQTT 连接状态**：
   ```
   I (xxx) ESP32S3_MQTT_EVENT: Connected to MQTT server.
   I (xxx) ESP32S3_MQTT_EVENT: 已订阅控制频道 'esp32_1/control'
   ```

3. **观察错误诊断信息**：
   ```
   E (xxx) ESP32S3_MQTT_EVENT: 错误详情: WIFI_NOT_CONNECTED (type=1, tls_err=32772, ...)
   ```

4. **每5分钟查看错误统计**：
   ```
   I (xxx) ESP32S3_MQTT_EVENT: =================================================
   I (xxx) ESP32S3_MQTT_EVENT:            MQTT错误类型统计报告
   I (xxx) ESP32S3_MQTT_EVENT: =================================================
   ```

---

## 8. 问题解决记录

### 问题：心跳发送阻塞 MQTT 事件处理
**现象**: MQTT 协议层无法及时处理 PING 响应，导致频繁断开  
**原因**: `mqtt_init()` 中包含阻塞式心跳循环  
**解决**: 分离心跳发送任务，创建独立的 `mqtt_heartbeat_task`

---

### 问题：连接状态竞争访问
**现象**: 多任务环境下连接状态不一致  
**原因**: `connect_flag` 全局变量无同步机制  
**解决**: 添加 FreeRTOS 互斥锁保护，`get_connect_flag()` / `set_connect_flag()`

---

### 问题：错误信息笼统无法诊断
**现象**: 所有断开都显示 `No PING_RESP`，无法定位根本原因  
**原因**: 未利用 `esp_mqtt_event_t->error_handle` 中的详细错误信息  
**解决**: 实现 `get_error_type_string()` 函数，根据错误类型、errno、TLS 错误码详细分类

---

### 问题：WiFi 与 MQTT 问题混淆
**现象**: MQTT 连接失败时无法判断是 WiFi 问题还是服务器问题  
**原因**: 缺乏 WiFi 状态查询机制  
**解决**: 
1. `wifi.c` 新增 `wifi_connected` 状态变量
2. 新增 `wifi_is_connected()` 查询函数
3. MQTT 错误诊断时优先检查 WiFi 状态
4. 新增 `WIFI_NOT_CONNECTED` 错误类型

---

### 问题：频繁无效重连尝试
**现象**: WiFi 未连接时 MQTT 仍不断尝试重连，浪费资源  
**原因**: MQTT 健康检查任务未检查 WiFi 状态  
**解决**: 
1. 健康检查任务优先确认 `wifi_is_connected()`
2. WiFi 未连接时跳过重连逻辑
3. 添加明确日志：`WiFi未连接，跳过MQTT重连检查`

---

## 9. 测试验证

### 测试场景 1：WiFi 正常，MQTT 服务器正常
**预期结果**: 
- 连接成功，心跳正常发送
- LED 绿色常亮
- 无错误日志

### 测试场景 2：WiFi 断开
**预期结果**:
- MQTT 检测到 `WIFI_NOT_CONNECTED`
- 跳过重连尝试
- 日志显示：`WiFi未连接，跳过MQTT重连检查`

### 测试场景 3：WiFi 正常，MQTT 服务器断开
**预期结果**:
- 检测到 `TCP_CONNECTION_REFUSED` 或 `TCP_CONNECT_TIMEOUT`
- 执行指数退避重连
- 恢复后自动重新订阅

---

## 10. 后续建议

### 方案 A: 进一步优化重连策略
- 实现更智能的退避策略（添加随机抖动）
- WiFi 信号强度低于阈值时主动告警
- 记录历史连接质量，预测性重连

### 方案 B: 网络层增强
- 实现 DNS 缓存，避免重复解析
- 支持 MQTT over WebSocket 作为备选
- 添加网络质量探测功能

### 方案 C: 诊断工具增强
- 记录每次连接尝试的详细时间线
- 支持远程诊断命令
- 导出连接质量报告

---

## 11. 参考链接

- [ESP-IDF MQTT Client 配置文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [ESP-IDF WiFi 驱动文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [MQTT 协议错误码定义](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718035)
- [FreeRTOS 互斥锁文档](https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html)
- 历史修复记录: `2026_02_25_and_02_26_wifi_test/MQTT_Connection_Monitoring_Implementation.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-02-26 18:30  
**完成时间**: 2026-02-26 18:30
