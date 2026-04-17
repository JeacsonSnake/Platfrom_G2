# ESP32S3 MQTT 连接不稳定问题分析与修复记录

**日期**: 2026-02-10  
**分支**: `fix/mqtt-connection`  
**问题描述**: ESP32S3 不定时断联 EMQX MQTT Broker，连接保持时间极短（10-60秒不等）

---

## 1. 问题现象

### 初始错误日志
```
E (117302) mqtt_client: Writing didn't complete in specified timeout: errno=119
E (117312) ESP32S3_MQTT_EVENT: Disconnected from MQTT server.

E (39062) mqtt_client: No PING_RESP, disconnected

E (516262) transport_base: poll_write select error 104, errno = Connection reset by peer
```

### 关键特征
- 断开间隔**极其规律**（约 30秒、60秒）
- 错误类型交替出现：`No PING_RESP`、`Connection reset by peer`、`Writing timeout`
- MQTTX 客户端（PC）连接同一服务器正常，排除 EMQX 服务器本身问题

---

## 2. 排查过程

### 2.1 初步推测：KeepAlive 配置不匹配

**分析**: 断开间隔规律，疑似 MQTT KeepAlive 机制导致

**尝试修改**:
| 尝试 | ESP32 KeepAlive | EMQX 配置 | 结果 |
|------|----------------|-----------|------|
| 1 | 默认(120s) | 倍数=1 | 30秒断开 |
| 2 | 60s | 倍数=1.5 | 60秒断开 |
| 3 | 30s | 倍数=1.5 | 30秒断开 |
| 4 | 20s | 倍数=1.5 | 20秒断开 |
| 5 | 10s | 倍数=1.5 | 10秒断开 |
| 6 | 5s | 倍数=1.5 | 5秒断开 |
| 7 | 3s | 倍数=1.5 | 仍断开 |

**结论**: KeepAlive 值与断开时间严格对应，但 EMQX 配置修改未生效，实际使用倍数=1

### 2.2 网络层排查：VMware NAT 嫌疑

**分析**: 使用 VMware NAT 端口映射访问 EMQX

**检查项**:
- Windows 防火墙规则: EMQX MQTT 已允许入站 ✅
- VMware NAT 注册表超时: 未找到自定义配置
- MQTTX 测试: **正常保持连接** ✅

**结论**: 网络层不是主因，问题在 ESP32 端

### 2.3 关键发现：WiFi 省电模式

**日志线索**:
```
I (762) wifi:pm start, type: 1
```

**分析**: ESP32 默认启用 WiFi 省电模式（Power Save），会周期性休眠 WiFi 射频，导致 MQTT 心跳包无法及时收发

**对比验证**:
| 客户端 | WiFi 省电 | KeepAlive | 结果 |
|--------|-----------|-----------|------|
| MQTTX | 无 (PC) | 60s | ✅ 正常 |
| ESP32 | **启用** | 60s | ❌ 断开 |

---

## 3. 最终修复方案

### 3.1 代码修改

#### `main/wifi.c`
```c
// 禁用 WiFi 省电模式，确保 MQTT 连接稳定
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

// 延长 WiFi 连接超时至 60秒
const int MAX_RETRY = 60;
```

#### `main/mqtt.c`
```c
// 固定唯一 ClientID，避免重连冲突
.credentials.client_id = "ESP32S3_7cdfa1e6d3cc",

// 配置 KeepAlive 和会话
.session = {
    .keepalive = 60,               // 60秒 KeepAlive
    .disable_keepalive = false,    // 启用 KeepAlive
    .disable_clean_session = false,// 保留会话
},

// 网络超时配置
.network = {
    .reconnect_timeout_ms = 5000,  // 5秒重连间隔
    .timeout_ms = 10000,           // 10秒操作超时
},

// 增大缓冲区
.buffer = {
    .size = 1024,
    .out_size = 1024,
},

// 应用层心跳改为 30秒（减轻网络负担）
vTaskDelay(pdMS_TO_TICKS(30000));
```

### 3.2 EMQX 端配置建议

| 配置项 | 建议值 | 说明 |
|--------|--------|------|
| Keep Alive 倍数 | 1.5 或 2.0 | 给予充足缓冲 |
| 空闲超时 | infinity (禁用) | 避免额外限制 |
| 服务端 Keep Alive | 300s | 允许的最大值 |

---

## 4. 修复效果

### 改善后日志
```
I (16112) ESP32S3_MQTT_EVENT: Connected to MQTT server.
... (持续 78秒)
E (90232) transport_base: tcp_read error, errno=Connection reset by peer

I (95742) ESP32S3_MQTT_EVENT: Connected to MQTT server.
... (持续 180秒)
E (286712) mqtt_client: No PING_RESP, disconnected
```

### 关键指标
- **连接保持时间**: 从 10-30秒 提升至 **60-180秒**
- **断开后重连**: 3-5秒内自动恢复
- **稳定性**: 显著提升，偶尔仍有断开但频次大幅降低

---

## 5. 遗留问题与后续建议

### 5.1 仍存在的问题
1. **EMQX KeepAlive 倍数配置未生效**: 实际使用倍数=1，配置修改后未正确应用
2. **偶尔 No PING_RESP**: 可能与 WiFi 信号质量 (RSSI -75dBm) 或网络延迟有关
3. **前期连接波动**: 首次连接成功率有待提升

### 5.2 后续优化方向

#### 方案 A: 降低应用层心跳频率（当前 30秒 → 10秒）
```c
vTaskDelay(pdMS_TO_TICKS(10000));  // 更频繁地发送应用层消息
```

#### 方案 B: 检查 EMQX 配置生效方式
- 确认 Dashboard 修改后是否重启服务
- 检查是否存在 Zone 级别配置覆盖全局配置
- 查看 EMQX 版本文档确认 KeepAlive 倍数配置项名称

#### 方案 C: 改善网络环境
- 增强 WiFi 信号（当前 -75dBm 偏弱）
- 使用桥接模式替代 NAT，或给虚拟机配置双网卡
- 物理机部署 EMQX 进行对比测试

#### 方案 D: 代码层面增强
- 添加连接状态统计日志
- 实现指数退避重连策略
- 增加 LWT (Last Will and Testament) 遗嘱消息

---

## 6. Git 提交记录

```bash
git checkout -b fix/mqtt-connection
# 修改代码...
git add main/mqtt.c main/wifi.c
git commit -m "fix(mqtt): 优化 MQTT 连接稳定性配置

- 添加固定 ClientID 避免连接冲突
- 启用 MQTT KeepAlive (60s) 和 Clean Session
- 配置网络超时和重连间隔
- 增大 MQTT 缓冲区
- 禁用 WiFi 省电模式 (WIFI_PS_NONE)
- 延长 WiFi 连接超时至 60s
- 应用层心跳改为 30s 减轻网络负担"
```

---

## 7. 参考链接

- [ESP-IDF MQTT Client 配置文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [EMQX KeepAlive 机制](https://www.emqx.io/docs/en/v5.0/mqtt/mqtt-keepalive.html)
- [ESP32 WiFi 省电模式](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html#_CPPv418esp_wifi_set_ps13wifi_ps_type_t)

---

**记录人**: Kimi Code CLI  
**完成时间**: 2026-02-10 18:20+
