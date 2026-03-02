# ESP32S3 MQTT 监控模块代码修复与分支合并记录

**日期**: 2026-02-26  
**分支**: `feature/mqtt-connection-monitor` → `main`, `fix/mqtt-connection`  
**任务描述**: 修复监控模块时间同步问题，统一时间显示格式，完成 Git 分支合并与同步

---

## 1. 背景

### 问题回顾
根据 ESP32S3 实际运行日志，发现以下问题：

1. **时间同步时机错误**: `monitor_task` 在 `wifi_init()` 之前创建，导致 NTP 同步在 WiFi 未连接时就开始尝试，造成 60 秒超时
2. **时间显示格式不统一**: 
   - 未同步时显示 "未同步" 而非系统时间格式
   - 同步后缺少运行时长信息
3. **NTP 服务器选择**: 香港服务器 (`hk.pool.ntp.org`) 在实际环境中同步速度不如国内服务器稳定

### 本次任务目标
修复上述问题，完成代码提交和分支合并操作。

---

## 2. 问题分析与修复

### 2.1 问题①: 时间同步时机错误

**现象**:
```
W (60448) MQTT_MONITOR: 仍在等待时间同步... (60/60 秒)
W (60448) MQTT_MONITOR: 时间同步超时，将继续使用系统时间进行监控
W (60618) ESP32S3_WIFI_EVENT: Waiting for WiFi connection... (60/60)
E (60618) ESP32S3_WIFI_EVENT: WiFi connection timeout! Check SSID and password.
I (61138) ESP32S3_WIFI_EVENT: Got ip:192.168.110.180
```

**原因分析**:
- `monitor_task` 在 `main.c` 中先于 `wifi_init()` 创建
- 监控任务立即开始等待 NTP 同步，但此时 WiFi 尚未连接
- 60 秒后 WiFi 才获取到 IP 地址，NTP 同步已超时

**解决方案**:
```c
// 修改前 (main.c)
xTaskCreate(monitor_task, "MONITOR_TASK", 4096, NULL, 2, NULL);
wifi_init();

// 修改后 (main.c)
wifi_init();  // 先初始化 WiFi，等待连接完成
vTaskDelay(5000 / portTICK_PERIOD_MS);
xTaskCreate(monitor_task, "MONITOR_TASK", 4096, NULL, 2, NULL);  // 后创建监控任务
```

---

### 2.2 问题②: 时间显示格式不统一

**现象**:
- 未同步时显示: `时间: 未同步 [运行 00:01:15]`
- 同步后显示: `时间: 2026-02-26 15:16:04`
- 同步后缺少运行时长信息

**解决方案**: 统一格式为 `时间 [运行 xx:xx:xx]`

```c
// monitor.c - monitor_get_current_time_str()
// 格式: 同步后 "2026-02-26 15:16:04 [运行 00:01:45]"
//       未同步 "1970-01-01 00:00:45 [运行 00:01:45]"

void monitor_get_current_time_str(char* buffer, size_t buffer_size)
{
    // 计算系统运行时间
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    int64_t uptime_sec = uptime_ms / 1000;
    int run_hours = uptime_sec / 3600;
    int run_minutes = (uptime_sec % 3600) / 60;
    int run_seconds = uptime_sec % 60;
    
    if (get_time_synced()) {
        // 已同步：显示实际时间 + 运行时长
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        snprintf(buffer, buffer_size, "%s [运行 %02d:%02d:%02d]", 
                 time_str, run_hours, run_minutes, run_seconds);
    } else {
        // 未同步：显示1970年基准时间 + 运行时长
        time_t sys_time = uptime_sec;
        struct tm* tm_info = localtime(&sys_time);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        snprintf(buffer, buffer_size, "%s [运行 %02d:%02d:%02d]", 
                 time_str, run_hours, run_minutes, run_seconds);
    }
}
```

---

### 2.3 问题③: NTP 服务器优化

**尝试方案**: 使用香港 NTP 服务器
```c
#define NTP_SERVER_PRIMARY   "hk.pool.ntp.org"
#define NTP_SERVER_BACKUP    "asia.pool.ntp.org"
#define NTP_SERVER_FALLBACK  "time.hko.hk"
```

**实际结果**: 同步时间反而比国内服务器更长（约 7-8 分钟）

**最终方案**: 换回国内 NTP 服务器
```c
// monitor.h
#define NTP_SERVER_PRIMARY   "cn.pool.ntp.org"      // 国内NTP服务器池（主）
#define NTP_SERVER_BACKUP    "ntp.aliyun.com"       // 阿里云NTP服务器（备）
#define NTP_SERVER_FALLBACK  "ntp.tencent.com"      // 腾讯云NTP服务器（备用）
```

**SNTP 优化配置**:
```c
// 缩短首次同步轮询间隔，加快首次同步速度
esp_sntp_set_sync_interval(1000);  // 首次同步间隔1秒

// 同步完成后恢复为正常轮询间隔
esp_sntp_set_sync_interval(3600 * 1000);  // 正常轮询：1小时
```

---

## 3. Git 提交记录

### 第一次提交：优化 NTP 服务器和统一时间格式

```bash
git add main/monitor.c main/monitor.h
git commit -m "fix(monitor): 优化NTP服务器为香港地区，统一时间显示格式

- 更换NTP服务器为hk.pool.ntp.org等香港地区服务器
- 统一时间显示格式：实际时间 [运行 xx:xx:xx]
- 未同步时显示1970-01-01基准时间而非'未同步'
- 优化SNTP轮询间隔，首次同步1秒后恢复1小时间隔"
```

**提交信息**:
- Commit: `c5283b7`
- 修改: 68 行（2 个文件）

---

### 第二次提交：修复 NTP 同步时机问题

```bash
git add main/main.c main/monitor.c main/monitor.h
git commit -m "fix(monitor): 修复NTP同步时机问题，换回国内NTP服务器

- 调整任务创建顺序：monitor_task在wifi_init之后创建
  - 确保WiFi已连接后才启动NTP同步，避免同步超时
  - 原顺序导致监控任务等待60秒后WiFi才连接
- 换回国内NTP服务器（cn.pool.ntp.org, ntp.aliyun.com）
  - 香港服务器同步速度不如国内服务器稳定
  - 新增腾讯云NTP作为第三备用服务器"
```

**提交信息**:
- Commit: `19bd5be`
- 修改: 13 行（3 个文件）

---

## 4. Git 分支合并操作

### 4.1 将 feature/mqtt-connection-monitor 合并到 main

```bash
git checkout main
git merge feature/mqtt-connection-monitor -m "Merge branch 'feature/mqtt-connection-monitor' into main

功能: MQTT连接长时间监控与统计
- 实现4小时间隔的统计报告
- SNTP时间同步功能
- 统一的时间显示格式 [运行 xx:xx:xx]
- 互斥锁保护共享变量"
```

**合并结果**:
```
Updating cb21db6..19bd5be
Fast-forward
10 files changed, 1602 insertions(+), 3 deletions(-)
```

---

### 4.2 将 fix/mqtt-connection 合并到 main

```bash
git checkout main
git merge fix/mqtt-connection -m "Merge branch 'fix/mqtt-connection' into main"
```

**合并结果**:
```
Already up to date.
```
说明 fix/mqtt-connection 的提交已包含在 main 中。

---

### 4.3 同步 fix/mqtt-connection 与 main

```bash
git checkout fix/mqtt-connection
git merge main -m "Merge branch 'main' into fix/mqtt-connection

同步 main 分支的最新更改：
- MQTT 连接监控统计功能
- SNTP 时间同步功能
- 统一时间显示格式"
```

**同步结果**:
```
Updating 7f793e4..19bd5be
Fast-forward
8 files changed, 1069 insertions(+), 1 deletion(-)
```

---

## 5. 最终分支状态

| 分支名 | 最新提交 | 状态 |
|--------|----------|------|
| `main` | `19bd5be` | ✅ 主分支，已包含所有更改 |
| `feature/mqtt-connection-monitor` | `19bd5be` | ✅ 已合并到 main |
| `fix/mqtt-connection` | `19bd5be` | ✅ 已与 main 同步 |
| `wifi-emqx-test` | `fca331d` | 历史分支 |
| `list` | `a676f65` | 独立分支 |

---

## 6. 修改后的预期日志输出

### 时间同步阶段
```
I (61138) MQTT_MONITOR: 正在启动 SNTP 时间同步...
I (61148) MQTT_MONITOR: NTP服务器1: cn.pool.ntp.org (国内)
I (61148) MQTT_MONITOR: NTP服务器2: ntp.aliyun.com (阿里云)
I (61158) MQTT_MONITOR: NTP服务器3: ntp.tencent.com (腾讯云)
...
I (473788) MQTT_MONITOR: 时间同步完成！
I (473788) MQTT_MONITOR: 当前实际时间: 2026-02-26 15:35:55 [运行 00:07:53]
```

### 连接/断开事件
```
I (383588) MQTT_MONITOR: [连接事件] #2 | 时间: 1970-01-01 08:06:23 [运行 00:06:23]
W (375598) MQTT_MONITOR: [断开事件] #32 | 时间: 1970-01-01 08:06:15 [运行 00:06:15] | 原因: No PING_RESP / Connection reset

// 同步后
I (519388) MQTT_MONITOR: [连接事件] #4 | 时间: 2026-02-26 15:36:40 [运行 00:08:39]
W (510178) MQTT_MONITOR: [断开事件] #34 | 时间: 2026-02-26 15:36:31 [运行 00:08:29] | 原因: No PING_RESP / Connection reset
```

---

## 7. 总结

本次工作完成了以下任务：

1. ✅ **修复时间同步时机问题**: 调整任务创建顺序，确保 WiFi 连接后再启动 NTP 同步
2. ✅ **统一时间显示格式**: 所有事件时间统一为 `时间 [运行 xx:xx:xx]` 格式
3. ✅ **优化 NTP 服务器配置**: 换回国内服务器，提高同步稳定性
4. ✅ **完成 Git 分支管理**: 
   - feature/mqtt-connection-monitor → main (合并)
   - fix/mqtt-connection → main (合并)
   - fix/mqtt-connection 与 main 同步

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-02-26 15:30  
**完成时间**: 2026-02-26 15:30
