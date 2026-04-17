# ESP32S3 MQTT 监控修复与 WiFi 连接优化记录

**日期**: 2026-02-27  
**分支**: `fix/mqtt-connection`  
**任务描述**: 修复 MQTT 连接统计报告间隔问题，优化 MQTT 连接稳定性，解决 WiFi 连接超时问题

---

## 1. 背景

### 问题回顾
根据最新烧录测试的监控日志，发现以下问题：

1. **MQTT 连接统计报告未正常显示**: 仅显示 `mqtt.c` 的 MQTT错误类型统计报告，推测是上一轮 coding 时相关代码被覆盖
2. **报告输出时间重叠**: `monitor.c` 和 `mqtt.c` 的报告间隔相同（5分钟），导致日志输出重叠在一起，影响可读性
3. **WiFi 连接超时后又快速连接**: 多次初始化时出现 `Waiting for WiFi connection... (60/60)` 超时，但超时后又能很快连接上 WiFi
4. **MQTT 频繁断开**: 运行约7分钟，断开100次，连接保持率 0.00%，主要是 `TCP_TRANSPORT_ERROR` (tls_err=32774)
5. **MQTT_ERR 任务栈溢出风险**: 栈大小仅 2048 字节，长时间运行后可能崩溃

### 本次任务目标
1. 恢复 `monitor.c` 的 MQTT连接统计报告 功能
2. 调整报告间隔，避免两个报告同时输出
3. 修复 MQTT_ERR 任务栈溢出问题
4. 优化 MQTT 连接配置，提高弱信号环境（-87dBm）下的稳定性
5. 解决 WiFi 连接超时问题

---

## 2. 问题分析与修复方案

### 2.1 问题①: 统计报告间隔错误

**现象**:
- `monitor.c` 的 MQTT连接统计报告 未正常显示
- 日志中仅看到 `mqtt.c` 的 MQTT错误类型统计报告

**原因分析**:
- `MONITOR_REPORT_INTERVAL_MS` 计算错误：`(60 / 12) * 60 * 60 * 1000` 实际为 **5小时**（18,000,000ms）
- 注释说是"5分钟"，但计算结果为5小时
- `mqtt.c` 的错误报告是每5分钟，所以能看到

**解决方案**:
```c
// monitor.h
// 修改前
#define MONITOR_REPORT_INTERVAL_MS  (( 60 / 12 ) * 60 * 60 * 1000)  // 实际是5小时

// 修改后
#define MONITOR_REPORT_INTERVAL_MS  (8 * 60 * 1000)  // 8分钟（480000毫秒）
```

**进一步调整**:
- 为避免与 `mqtt.c` 的5分钟报告重叠，将 `monitor.c` 报告间隔改为 **8分钟**

---

### 2.2 问题②: MQTT_ERR 任务栈溢出

**现象**:
- 根据上次测试记录（2026-02-26），运行约4小时后发生栈溢出
- 任务 `MQTT_ERR` 检测到栈溢出，设备自动重启

**原因分析**:
- `mqtt_error_report_task` 任务栈仅分配 **2048 字节**
- `ESP_LOGI` 系列函数在格式化长字符串时需要较多栈空间
- 运行一段时间后，错误统计变量累积，打印日志时占用栈空间增加

**解决方案**:
```c
// main.c
// 修改前
xTaskCreate(mqtt_error_report_task, "MQTT_ERR", 2048, NULL, 1, NULL);

// 修改后
xTaskCreate(mqtt_error_report_task, "MQTT_ERR", 4096, NULL, 1, NULL);
```

---

### 2.3 问题③: MQTT 连接不稳定（弱信号环境）

**现象**:
- WiFi 信号强度：-87 dBm（非常弱，正常应 > -70 dBm）
- 频繁 `select() timeout` (error 32774) 和 `TLS_CANNOT_CONNECT` (error 32772)
- 连接保持率 0.00%

**原因分析**:
- VMware NAT 网络延迟：ESP32 → VMware NAT → EMQX，NAT 层连接状态表导致连接建立延迟
- WiFi 信号极弱：-87 dBm 接近临界值，容易丢包
- MQTT 配置不够激进：KeepAlive 120秒过长，网络超时 20秒过长

**解决方案**:
```c
// mqtt.c - MQTT 配置优化
.session = {
    .keepalive = 60,               // 120秒 -> 60秒（更快检测连接问题）
    .disable_keepalive = false,
    .disable_clean_session = false, // true -> false（启用会话恢复）
},
.network = {
    .reconnect_timeout_ms = 3000,  // 5000ms -> 3000ms（更快重试）
    .timeout_ms = 10000,           // 20000ms -> 10000ms（避免长时间阻塞）
},
```

**指数退避策略优化**:
```c
// 修改前: 1s, 2s, 4s, 8s, 16s, 30s...
int backoff_delay = (reconnect_attempts < 6) ? (1 << reconnect_attempts) * 1000 : 30000;

// 修改后: 2s, 4s, 8s, 15s, 15s, 15s...（更激进，避免过长等待）
if (reconnect_attempts < 2) {
    backoff_delay = 2000;  // 前2次：2秒
} else if (reconnect_attempts < 4) {
    backoff_delay = 4000;  // 第3-4次：4秒
} else if (reconnect_attempts < 6) {
    backoff_delay = 8000;  // 第5-6次：8秒
} else {
    backoff_delay = 15000; // 之后：15秒（避免过长等待）
}
```

---

### 2.4 问题④: WiFi 连接超时后又快速连接

**现象**:
- `Waiting for WiFi connection... (60/60)` 超时
- 超时后又能很快连接上 WiFi

**原因分析**:
- `LED_TASK` 优先级为 **4**，是最高优先级任务
- LED 启动测试（3次白色闪烁，每次300ms亮+300ms灭）占用约 **1.8秒**
- WiFi 事件处理在回调函数 `event_handler` 中，可能被 LED 任务延迟
- WiFi 连接信号量释放延迟，导致 `wifi_init()` 等待超时

**解决方案**:
```c
// main.c
// 修改前
xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 4, NULL);

// 修改后
xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 2, NULL);
```

**原理**:
- LED 是视觉反馈，不需要实时性
- WiFi 连接是网络基础，需要优先处理
- 降低 LED 任务优先级后，WiFi 事件处理更及时

---

## 3. 实现方案

### 3.1 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `main/monitor.h` | 修正报告间隔为 8分钟 |
| `main/main.c` | LED_TASK 优先级 4→2，MQTT_ERR 栈 2048→4096 |
| `main/mqtt.c` | 优化 MQTT 配置参数，优化指数退避策略 |

### 3.2 详细修改

#### `main/monitor.h` - 修正报告间隔
```c
// 监控配置参数
#define MONITOR_REPORT_INTERVAL_MS  (8 * 60 * 1000)  // 8分钟报告间隔（480000毫秒）
```

#### `main/main.c` - 任务优先级和栈大小调整
```c
void app_main(void){
    // 创建LED状态指示任务（优先级2，低于WiFi初始化）
    xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 2, NULL);
    
    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    xTaskCreate(monitor_task, "MONITOR_TASK", 4096, NULL, 3, NULL);
    xTaskCreate(mqtt_init, "MQTT_INIT", 4096, NULL, 2, NULL);
    xTaskCreate(mqtt_heartbeat_task, "MQTT_HB", 4096, NULL, 1, NULL);
    xTaskCreate(mqtt_health_check_task, "MQTT_CHK", 4096, NULL, 1, NULL);
    // 创建MQTT错误统计报告任务（栈大小4096防止栈溢出）
    xTaskCreate(mqtt_error_report_task, "MQTT_ERR", 4096, NULL, 1, NULL);
    // ...
}
```

#### `main/mqtt.c` - MQTT 配置优化
```c
esp_mqtt_client_config_t cfg = {
    .broker.address = {
        .uri = "mqtt://192.168.110.31",
        .port = 1883,
    },
    .credentials = {
        .client_id = "ESP32S3_7cdfa1e6d3cc",
        .username = "ESP32_1",
    },
    .credentials.authentication = {
        .password = "123456",
    },
    // 添加会话和网络配置以改善连接稳定性（针对弱信号环境优化）
    .session = {
        .keepalive = 60,               // 60秒 KeepAlive 间隔
        .disable_keepalive = false,
        .disable_clean_session = false, // 禁用清理会话，启用会话恢复
    },
    .network = {
        .reconnect_timeout_ms = 3000,  // 3秒重连间隔
        .timeout_ms = 10000,           // 网络操作超时10秒
    },
    .buffer = {
        .size = 4096,
        .out_size = 4096,
    },
    .task = {
        .priority = 5,
        .stack_size = 8192,
    }
};
```

---

## 4. Git 提交记录

### 第一次提交：修复统计报告间隔和优化 MQTT 连接稳定性

```bash
git add main/main.c main/monitor.h main/mqtt.c
git commit -m "fix(monitor/mqtt): 修复统计报告间隔和优化MQTT连接稳定性

1. 修复 monitor.c 统计报告间隔:
   - 从 5小时 修正为 5分钟 (MONITOR_REPORT_INTERVAL_MS)
   - 现在能正常显示 MQTT连接统计报告

2. 修复 MQTT_ERR 任务栈溢出问题:
   - 栈大小从 2048 增加到 4096 字节
   - 防止长时间运行后栈溢出崩溃

3. 优化 MQTT 连接配置（针对弱信号环境 -87dBm）:
   - KeepAlive: 120秒 -> 60秒（更快检测连接问题）
   - disable_clean_session: true -> false（启用会话恢复）
   - reconnect_timeout_ms: 5000ms -> 3000ms（更快重试）
   - timeout_ms: 20000ms -> 10000ms（避免长时间阻塞）

4. 优化指数退避重连策略:
   - 新策略: 2s, 4s, 8s, 15s, 15s...（避免过长等待）
   - 增加退避级别日志，便于调试"
```

**提交信息**:
- Commit: `3ce9733`
- 修改: 3 个文件，26 行新增，17 行删除

---

### 第二次提交：调整统计报告间隔避免重叠

```bash
git add main/monitor.h
git commit -m "fix(monitor): 调整统计报告间隔为8分钟，避免与错误报告重叠

- MONITOR_REPORT_INTERVAL_MS: 5分钟 -> 8分钟
- 避免与 mqtt.c 的 MQTT错误类型统计报告（5分钟间隔）同时输出
- 日志显示两个报告重叠在一起，影响可读性"
```

**提交信息**:
- Commit: `cd44dab`
- 修改: 1 个文件，1 行修改

---

### 第三次提交：降低 LED 任务优先级优化 WiFi 连接

```bash
git add main/main.c
git commit -m "fix(main): 降低 LED_TASK 优先级，优化 WiFi 连接稳定性

- LED_TASK 优先级: 4 -> 2
- 避免 LED 启动测试（3次闪烁约1.8秒）阻塞 WiFi 事件处理
- WiFi 初始化现在可以更及时地处理连接事件
- 解决 'Waiting for WiFi connection... (60/60)' 超时后又快速连上的问题"
```

**提交信息**:
- Commit: `2e960ef`
- 修改: 1 个文件，2 行修改

---

## 5. 关键代码详解

### 5.1 任务优先级分布（修改后）

| 任务 | 优先级 | 说明 |
|------|--------|------|
| `MONITOR_TASK` | 3 | MQTT 连接监控 |
| `MQTT_INIT` | 2 | MQTT 初始化 |
| `LED_TASK` | **2** | LED 状态指示（已降低） |
| `MQTT_HB` | 1 | 心跳发送 |
| `MQTT_CHK` | 1 | 健康检查 |
| `MQTT_ERR` | 1 | 错误统计报告（栈4096） |

### 5.2 指数退避重连策略

```c
// 计算指数退避延迟（针对弱信号优化）
if (reconnect_attempts < 2) {
    backoff_delay = 2000;  // 前2次：2秒
} else if (reconnect_attempts < 4) {
    backoff_delay = 4000;  // 第3-4次：4秒
} else if (reconnect_attempts < 6) {
    backoff_delay = 8000;  // 第5-6次：8秒
} else {
    backoff_delay = 15000; // 之后：15秒（上限）
}
```

**优势**:
- 初始重连更快（2秒 vs 1秒，但避免过快导致网络冲击）
- 最大延迟缩短（15秒 vs 30秒，避免过长等待）
- 适合弱信号环境下频繁断开的场景

---

## 6. 使用说明

### 编译烧录

```powershell
# 清理并重新构建
Remove-Item -Recurse -Force build
idf.py build
idf.py -p COM9 flash monitor
```

### 监控日志分析

1. **观察 WiFi 连接**:
   ```
   I (xxx) ESP32S3_WIFI_EVENT: Got ip:192.168.xxx.xxx
   ```
   - 确认是否还有 `(60/60)` 超时现象

2. **观察 MQTT 连接统计报告**（每8分钟）:
   ```
   I (xxx) MQTT_MONITOR: =================================================
   I (xxx) MQTT_MONITOR:            MQTT连接统计报告 [时间]
   I (xxx) MQTT_MONITOR: =================================================
   I (xxx) MQTT_MONITOR: 总连接次数:     X
   I (xxx) MQTT_MONITOR: 总断开次数:     X
   I (xxx) MQTT_MONITOR: 连接保持率:     XX.XX%
   ```

3. **观察 MQTT 错误类型统计报告**（每5分钟）:
   ```
   I (xxx) ESP32S3_MQTT_EVENT: =================================================
   I (xxx) ESP32S3_MQTT_EVENT:            MQTT错误类型统计报告
   I (xxx) ESP32S3_MQTT_EVENT: =================================================
   ```

4. **验证两个报告不再重叠**:
   - 两个报告应间隔约3分钟输出，不会同时出现

---

## 7. 问题解决记录

### 问题：MQTT连接统计报告未显示

**现象**: 仅显示 `mqtt.c` 的 MQTT错误类型统计报告，看不到 `monitor.c` 的 MQTT连接统计报告  
**原因**: `MONITOR_REPORT_INTERVAL_MS` 计算错误，实际为5小时而非5分钟  
**解决**: 修正计算为 `(8 * 60 * 1000)`，8分钟报告一次

---

### 问题：两个报告同时输出导致日志重叠

**现象**: 两个报告的日志交错在一起，难以阅读  
**原因**: 两个报告间隔相同（5分钟），同时触发  
**解决**: 将 `monitor.c` 报告间隔改为 **8分钟**，与 `mqtt.c` 的5分钟错开

---

### 问题：MQTT_ERR 任务栈溢出

**现象**: 运行约4小时后崩溃，`***ERROR*** A stack overflow in task MQTT_ERR has been detected`  
**原因**: 栈仅 2048 字节，格式化日志时需要更多空间  
**解决**: 栈大小增加到 **4096 字节**

---

### 问题：WiFi 连接超时后又快速连接

**现象**: `Waiting for WiFi connection... (60/60)` 超时后又能很快连接  
**原因**: `LED_TASK` 优先级过高（4），启动测试占用 CPU 1.8秒，延迟 WiFi 事件处理  
**解决**: LED_TASK 优先级降至 **2**，低于 WiFi 初始化

---

### 问题：MQTT 频繁断开（弱信号环境）

**现象**: -87dBm 信号下，7分钟断开100次，连接保持率 0%  
**原因**: 
- KeepAlive 120秒过长，无法及时检测连接问题
- 网络超时 20秒过长，阻塞时间太久
- 指数退避最大30秒，等待过久

**解决**:
- KeepAlive: 60秒
- 网络超时: 10秒
- 退避策略: 2,4,8,15秒...
- 启用会话恢复（disable_clean_session = false）

---

## 8. 后续建议

### 方案 A: 进一步优化 LED 启动测试（如 WiFi 问题仍存在）

```c
// led.c
// 移除或缩短启动测试时间
void status_led_init(void)
{
    // ... 初始化代码 ...
    
    // 方案 A1: 完全删除启动测试
    // 方案 A2: 缩短测试时间（300ms -> 100ms）
    for (int i = 0; i < 3; i++) {
        set_led_color(&COLOR_WHITE);
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms instead of 300ms
        set_led_color(&COLOR_OFF);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 方案 B: 优化 WiFi 等待机制

```c
// wifi.c
// 更频繁地检查连接状态
while (retry_count < MAX_RETRY)
{
    // 每 500ms 检查一次（更灵敏）
    if (xSemaphoreTake(sem, pdMS_TO_TICKS(500)) == pdPASS)
    {
        connected = true;
        break;
    }
    retry_count++;
    // 每2次循环打印一次日志（避免刷屏）
    if (retry_count % 2 == 0) {
        ESP_LOGW(TAG, "Waiting... (%d/%d)", retry_count/2, MAX_RETRY/2);
    }
}
```

### 方案 C: 网络环境优化（需硬件配合）

1. **改善 WiFi 信号**:
   - 将 ESP32 靠近路由器（当前 -87dBm 太弱）
   - 使用外置天线

2. **优化 VMware NAT 配置**:
   - 调整 NAT 端口映射的超时设置
   - 考虑改为桥接模式（如果网络允许）

3. **部署本地 EMQX**:
   - 在物理机上部署 EMQX，减少虚拟化层延迟

### 方案 D: 代码层面的进一步改进

1. **添加 WiFi 信号强度监测**:
   ```c
   // 定期检查 RSSI，低于阈值时告警
   int8_t rssi = wifi_get_rssi();
   if (rssi < -80) {
       ESP_LOGW(TAG, "WiFi信号弱: %d dBm，建议靠近路由器", rssi);
   }
   ```

2. **实现连接质量评分**:
   - 记录连接成功率、平均连接时长
   - 低质量时自动调整重连策略

3. **添加 LWT (Last Will and Testament)**:
   - MQTT 遗嘱消息，离线时通知服务器

---

## 9. 参考链接

- [ESP-IDF MQTT Client 配置文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [ESP-IDF WiFi 驱动文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [FreeRTOS 任务优先级](https://www.freertos.org/RTOS-task-priorities.html)
- [ESP32 WiFi 信号强度与 RSSI](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html#wi-fi-rssi)
- 历史修复记录: `2026_02_25_and_02_26_wifi_test/MQTT_Connection_Monitoring_Implementation.md`
- 历史修复记录: `2026_02_25_and_02_26_wifi_test/2026-02-26-MQTT_disconnect_repair_and_error_detector_optimization.md`

---

**记录人**: Kimi Code CLI  
**记录时间**: 2026-02-27 17:30  
**完成时间**: 2026-02-27 17:30
