# ESP32S3 MQTT 连接长时间监控功能实现记录

**日期**: 2026-02-25  
**分支**: `feature/mqtt-connection-monitor`  
**任务描述**: 实现 MQTT 连接长时间监控与统计功能，用于统计特定时间（如四小时）内的断连次数，分析 WiFi 信号偏弱 (-72dBm) 导致的偶发包丢失问题

---

## 1. 背景

### 历史问题回顾
根据 2026-02-10 的修复记录，ESP32S3 存在 MQTT 连接不稳定问题：
- 断开间隔不规律（10-180秒）
- 错误类型：`No PING_RESP`、`Connection reset by peer`
- 已实施的修复（禁用 WiFi 省电模式、优化 MQTT 配置）收效有限
- 推测原因：WiFi 信号偏弱 (-72dBm) 导致偶发包丢失

### 本次任务目标
创建专门的监控分支，实现长时间监控功能，统计4小时内的断连次数，为后续分析提供数据支撑。

---

## 2. 监控功能设计

### 2.1 需求分析

| 需求项 | 说明 |
|--------|------|
| 记录内容 | 每次连接/断开事件的时间戳、断开原因 |
| 统计维度 | 总运行时间、连接次数、断开次数、连接保持率 |
| 报告频率 | 每4小时输出一次完整统计报告 |
| 存储限制 | 最近100次断开事件，循环覆盖 |
| **时间同步** | **通过 SNTP 同步实际时间，确保记录准确** |

### 2.2 数据结构

```c
// 单次断开事件记录
typedef struct {
    int64_t disconnect_time_ms;     // 断开时间戳
    int64_t reconnect_time_ms;      // 重连时间戳
    int64_t disconnect_duration_ms; // 断开持续时长
    const char* disconnect_reason;  // 断开原因
} disconnect_event_t;

// MQTT连接统计数据结构体
typedef struct {
    // 时间同步相关（新增）
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
    disconnect_event_t disconnect_log[100];
    int log_index;                  // 当前日志索引
    int log_count;                  // 有效日志数量
    
    // 当前状态
    bool is_connected;              // 当前连接状态
    int64_t last_disconnect_time_ms; // 上次断开时间
} mqtt_connection_stats_t;
```

---

## 3. 实现方案

### 3.1 新增文件

#### `main/monitor.h`
- 监控模块头文件
- 定义数据结构体和配置参数
- 声明监控函数接口
- **新增 SNTP 配置：**
  - `NTP_SERVER_PRIMARY`: `cn.pool.ntp.org`（国内NTP服务器）
  - `NTP_SERVER_BACKUP`: `ntp.aliyun.com`（阿里云NTP服务器）

#### `main/monitor.c`
- 监控模块实现
- 事件记录函数：`monitor_record_connect()` / `monitor_record_disconnect()`
- 统计报告函数：`monitor_report_statistics()`
- 监控任务：`monitor_task()` (每4小时输出报告)
- **新增 SNTP 同步功能：**
  - `monitor_start_time_sync()`: 启动 SNTP 同步
  - `monitor_wait_time_sync()`: 等待同步完成
  - `monitor_is_time_synced()`: 检查同步状态
  - `monitor_get_current_time_str()`: 获取实际时间字符串
  - `monitor_get_elapsed_time_str()`: 获取运行时长
- **新增互斥锁保护：**
  - `stats_mutex`: 保护共享变量的互斥锁
  - `set_time_synced()` / `get_time_synced()`: 安全访问同步状态

### 3.2 修改文件

#### `main/main.c`
```c
// 创建MQTT连接监控任务
xTaskCreate(monitor_task, "MONITOR_TASK", 4096, NULL, 2, NULL);
```

#### `main/mqtt.c`
在 MQTT 事件处理回调中添加监控调用：
- `MQTT_EVENT_CONNECTED`: 调用 `monitor_record_connect()`
- `MQTT_EVENT_DISCONNECTED`: 调用 `monitor_record_disconnect("No PING_RESP / Connection reset")`
- `MQTT_EVENT_ERROR`: 调用 `monitor_record_disconnect("MQTT_EVENT_ERROR")`

#### `main/wifi.c`（新增 SNTP 启动）
```c
else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    status_led_set_mode(LED_BLINK_SLOW);
    
    // 启动NTP时间同步（新增）
    monitor_start_time_sync();
    
    xSemaphoreGive(sem);
}
```

#### `main/main.h`
- 添加 `#include "monitor.h"`
- 添加 `#include "esp_timer.h"`
- **添加 `#include "esp_sntp.h"`**
- **添加函数声明：`void monitor_start_time_sync(void);`**

#### `main/CMakeLists.txt`
- 添加 `monitor.c` 到编译列表

---

## 4. SNTP 时间同步实现

### 4.1 同步流程

```
WiFi 连接成功 → 启动 SNTP → 等待 NTP 响应 → 同步完成 → 开始记录事件
```

### 4.2 关键代码

```c
// SNTP 配置
#define NTP_SERVER_PRIMARY   "cn.pool.ntp.org"  // 国内NTP服务器
#define NTP_SERVER_BACKUP    "ntp.aliyun.com"   // 阿里云NTP服务器
#define TIME_SYNC_TIMEOUT_MS 30000

// 启动时间同步
void monitor_start_time_sync(void)
{
    // 记录开机系统时间
    mqtt_stats.boot_time_ms = esp_timer_get_time() / 1000;
    
    // 设置中国时区 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 配置 SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_PRIMARY);
    esp_sntp_setservername(1, NTP_SERVER_BACKUP);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    esp_sntp_init();
}

// 同步完成回调
static void sntp_sync_notification_cb(struct timeval *tv)
{
    mqtt_stats.time_sync_time_ms = esp_timer_get_time() / 1000;
    mqtt_stats.time_synced = true;
    
    // 计算实际开机时间
    time_t now = time(NULL);
    mqtt_stats.boot_real_time = now - (mqtt_stats.time_sync_time_ms / 1000);
}
```

### 4.3 时间显示逻辑

| 状态 | 时间显示格式 |
|------|-------------|
| 未同步 | `未同步 [运行 00:15:32]` |
| 已同步 | `2026-02-25 17:45:32` |

---

## 5. 统计报告格式

监控任务每4小时自动输出以下格式的统计报告：

```
=================================================
           MQTT连接统计报告 [2026-02-25 17:45:32]
=================================================
实际开机时间:   2026-02-25 17:08:12    ← 新增
系统运行时长:   2.62 h
-------------------------------------------------
总连接次数:     5
总断开次数:     4
累计连接时长:   2.45 h
平均连接时长:   29.4 min/次
连接保持率:     93.51%
当前连接状态:   已连接
时间同步状态:   已同步                  ← 新增
-------------------------------------------------
最近 4 次断开事件:
序号  断开时间              恢复耗时         原因
#1    17:15:32              12.3 s           No PING_RESP / Connection reset
#2    17:23:18              8.5 s            No PING_RESP / Connection reset
...
=================================================
```

---

## 6. 实时事件日志

监控模块会在 MQTT 连接状态变化时实时输出日志：

```
// 时间同步成功
I (31216) MQTT_MONITOR: ==============================================
I (31216) MQTT_MONITOR: 时间同步成功！
I (31216) MQTT_MONITOR: 当前时间: 2026-02-25 17:08:12
I (31216) MQTT_MONITOR: 开机时间戳: 14231 ms
I (31216) MQTT_MONITOR: 实际开机时间: 2026-02-25 17:08:00
I (31216) MQTT_MONITOR: ==============================================

// 连接事件（同步后显示实际时间）
I (6942) MQTT_MONITOR: [连接事件] #1 | 时间: 2026-02-25 17:08:12

// 断开事件
W (438552) MQTT_MONITOR: [断开事件] #1 | 时间: 2026-02-25 17:15:32 | 原因: No PING_RESP / Connection reset
```

---

## 7. Git 提交记录

### 第一次提交：基础监控功能

```bash
# 创建并切换到监控分支
git checkout -b feature/mqtt-connection-monitor

# 新增监控模块文件
git add main/monitor.c main/monitor.h

# 修改现有文件
git add main/main.c main/main.h main/mqtt.c main/CMakeLists.txt

# 提交
git commit -m "feat(monitor): 添加 MQTT 连接监控统计功能

- 新增 monitor.c/monitor.h 模块，实现连接断开事件的记录与统计
- 记录每次连接/断开事件，包含时间戳和断开原因
- 每4小时自动输出统计报告，包含：
  - 总运行时间、总连接/断开次数
  - 累计连接时长、平均连接时长
  - 连接保持率
  - 最近10次断开事件的详细记录
- 在 MQTT 连接成功和断开时自动调用监控函数
- 监控任务优先级为2，堆栈4096"
```

**提交信息**:  
- Commit: `c48269d`  
- 新增: 299 行（6个文件）

### 第二次提交：SNTP 时间同步修复

```bash
# 修改时间同步相关文件
git add main/monitor.c main/monitor.h main/main.h main/wifi.c

# 提交
git commit -m "fix(monitor): 添加 SNTP 时间同步功能

- 新增 SNTP 时间同步，使用 pool.ntp.org 服务器
- WiFi 连接成功后自动启动时间同步
- 记录开机实际时间，用于统计实际运行时长
- 时间未同步前显示系统运行时间，同步后显示实际时间
- 统计报告中显示实际开机时间和时间同步状态
- 断开事件日志显示实际时间戳"
```

**提交信息**:  
- Commit: `9e470a9`  
- 修改: 185 行（4个文件）

---

### 第三次提交：互斥锁保护时间同步状态

```bash
# 修改监控模块
git add main/monitor.c main/monitor.h

# 提交
git commit -m "fix(monitor): 添加互斥锁保护时间同步状态

- 添加互斥锁保护 mqtt_stats.time_synced 变量
- 使用 getter/setter 函数安全访问同步状态
- 防止多任务竞争导致的状态不一致"
```

**提交信息**:  
- Commit: `d2e7a07`  
- 修改: 60 行（2个文件）

---

### 第四次提交：优化 NTP 服务器配置

```bash
# 修改 NTP 配置
git add main/monitor.c main/monitor.h main/wifi.c

# 提交
git commit -m "fix(monitor): 优化 NTP 时间同步配置

- 使用国内 NTP 服务器: cn.pool.ntp.org, ntp.aliyun.com
- 添加中国时区设置 CST-8 (UTC+8)
- WiFi 连接成功后等待 NTP 同步完成再返回"
```

**提交信息**:  
- Commit: `2fdcb99`  
- 修改: 23 行（3个文件）

---

## 8. 使用说明

### 编译烧录
```powershell
# 清理并重新构建
Remove-Item -Recurse -Force build
idf.py build
idf.py -p COM9 flash monitor
```

### 监控数据分析
1. **观察时间同步日志**：
   - 查找 `时间同步成功！` 确认 NTP 同步完成
   - 记录实际开机时间

2. **观察实时事件日志**（标签 `MQTT_MONITOR`）：
   - `[连接事件]`: 每次成功连接
   - `[断开事件]`: 每次断开及原因

3. **每4小时查看统计报告**：
   - **连接保持率**: 目标 >95%
   - **平均连接时长**: 反映连接稳定性
   - **断开频次**: 统计周期内断开次数
   - **恢复耗时**: 重连速度

### 预期结果
运行4小时后，将获得完整的数据：
- 实际开机时间和总运行时长
- 在特定 WiFi 信号强度 (-72dBm) 下的连接稳定性表现
- 断开事件的分布规律（带实际时间戳）
- 重连时间统计

---

## 9. 问题解决记录

### 问题：时间显示不正确
**现象**: 日志显示时间为 `1970-01-01 00:44:14`，而非实际时间  
**原因**: ESP32 没有 RTC 电池，上电后时间从 Unix 纪元（1970年）开始计数  
**解决**: 通过 SNTP 同步网络时间，WiFi 连接成功后自动启动同步

### SNTP 同步实现细节
- ~~使用 `pool.ntp.org` 作为 NTP 服务器~~ → 改用国内服务器 `cn.pool.ntp.org` 和 `ntp.aliyun.com`
- 同步超时时间：30秒
- 同步模式：`SNTP_SYNC_MODE_IMMED`（立即同步）
- 运行模式：`SNTP_OPMODE_POLL`（轮询模式）
- 通过回调函数 `sntp_sync_notification_cb` 通知同步完成
- 时区设置：`CST-8` (UTC+8，中国标准时间)

---

### 问题：NTP 服务器无法访问
**现象**: SNTP 启动后回调从未被触发，日志一直显示 `时间: 未同步 [运行 ...]`  
**原因**: `pool.ntp.org` 在国内网络环境下无法访问或响应超时  
**解决**: 
1. 更换为国内 NTP 服务器：`cn.pool.ntp.org` 和 `ntp.aliyun.com`
2. 添加中国时区设置 `setenv("TZ", "CST-8", 1)`
3. 在 `wifi.c` 中 WiFi 连接成功后等待 NTP 同步完成

---

### 问题：时间同步状态竞争访问
**现象**: SNTP 回调已触发（日志显示 `NTP时间同步完成`），但后续事件仍显示 `未同步`  
**原因**: `time_synced` 变量在多任务环境中被竞争访问，SNTP 回调在中断上下文执行，与 MQTT 任务中的读取操作无同步机制  
**解决**: 添加 FreeRTOS 互斥锁保护：
- `stats_mutex`: 互斥锁保护统计变量
- `set_time_synced()`: 安全设置同步状态
- `get_time_synced()`: 安全获取同步状态

---

## 10. 后续建议

### 方案 A: 信号强度改善
- 将 ESP32S3 靠近 WiFi 路由器
- 使用外置天线
- 对比不同信号强度下的连接稳定性

### 方案 B: 网络环境优化
- VMware NAT 改为桥接模式
- 物理机部署 EMQX 进行对比测试
- 检查路由器 QoS 设置

### 方案 C: 代码层面增强（基于监控数据分析后）
- 实现指数退避重连策略
- 添加 LWT (Last Will and Testament) 遗嘱消息
- WiFi 信号强度低于阈值时主动告警
- **优化：SNTP 同步失败时的重试机制**

---

## 11. 参考链接

- [ESP-IDF MQTT Client 配置文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [ESP32 WiFi 信号强度与稳定性](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html#wi-fi-rssi)
- [EMQX KeepAlive 机制](https://www.emqx.io/docs/en/v5.0/mqtt/mqtt-keepalive.html)
- [ESP-IDF SNTP 时间同步](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html#sntp-time-synchronization)
- 历史修复记录: `2026_02_10_wifi_test/MQTT_Connection_Issue_Analysis_and_Fix.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-02-25 18:15
**完成时间**: 2026-02-25 18:00
