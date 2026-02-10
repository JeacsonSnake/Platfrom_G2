# ESP32-S3 烧录成功报告

> 烧录时间: 2026-02-10 14:41  
> 分支: wifi-emqx-test  
> 烧录方式: UART (COM9, 460800 baud)

---

## 1. 烧录结果

### 1.1 烧录状态: ✅ 成功

```
esptool.py v4.11.0
Serial port COM9
Connecting...
Chip is ESP32-S3 (QFN56) (revision v0.1)
Features: WiFi, BLE, Embedded PSRAM 2MB (AP_3v3)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: 7c:df:a1:e6:d3:cc
```

### 1.2 烧录文件详情

| 文件 | 地址 | 大小(压缩后) | 状态 |
|------|------|-------------|------|
| bootloader.bin | 0x00000000 | 21,056 → 13,389 bytes | ✅ 已验证 |
| test.bin | 0x00010000 | 906,448 → 565,885 bytes | ✅ 已验证 |
| partition-table.bin | 0x00008000 | 3,072 → 103 bytes | ✅ 已验证 |

**总烧录时间**: 约 11.3 秒

---

## 2. 设备信息

| 属性 | 值 |
|------|-----|
| 芯片型号 | ESP32-S3 (QFN56) |
| 芯片版本 | revision v0.1 |
| 特征 | WiFi, BLE, Embedded PSRAM 2MB |
| 晶振频率 | 40MHz |
| USB 模式 | USB-Serial/JTAG |
| MAC 地址 | 7c:df:a1:e6:d3:cc |

---

## 3. 接下来验证 WiFi/MQTT 连接

### 3.1 查看串口日志

在 VSCode ESP-IDF 终端中执行：

```bash
idf.py -p COM9 monitor
```

或点击底部状态栏的 **"ESP-IDF: Monitor Device"** 按钮。

### 3.2 预期日志输出

**正常启动流程**：
```
I (0) boot: ESP-IDF v5.5.2 2nd stage bootloader
...
I (234) ESP32S3_STATUS_LED: Status LED initialized on GPIO 2
I (234) ESP32S3_WIFI_EVENT: Begin to connect the AP
I (234) ESP32S3_STATUS_LED: LED mode changed to: 1
I (3234) ESP32S3_WIFI_EVENT: Got ip:192.168.233.xxx
I (3234) ESP32S3_WIFI_EVENT: Connected to ap!
I (3234) ESP32S3_STATUS_LED: LED mode changed to: 2
...
I (8234) ESP32S3_MQTT_EVENT: Connected to MQTT server.
I (8234) ESP32S3_STATUS_LED: LED mode changed to: 3
I (8234) ESP32S3_MQTT_EVENT: Subscribed to esp32_1/control
```

### 3.3 LED 状态指示

| 时间 | LED 状态 | 含义 |
|------|----------|------|
| 0-2s | 快速闪烁 (5Hz) | 系统启动，WiFi 连接中 |
| 2-5s | 慢速闪烁 (1Hz) | WiFi 成功，MQTT 连接中 |
| 5s+ | **常亮** ✅ | 全部连接成功 |

---

## 4. EMQX 验证步骤

### 4.1 打开 EMQX 管理界面

- URL: http://192.168.233.100:18083/
- 默认账号: admin
- 默认密码: public

### 4.2 验证客户端连接

**路径**: Monitor → Clients

预期看到：

| 字段 | 预期值 |
|------|--------|
| Client ID | ESP32_1 |
| Username | ESP32_1 |
| IP Address | 192.168.233.xxx |
| Connected | Yes |
| Protocol | MQTT v3.1.1 |
| Keepalive | 60s |

### 4.3 验证主题订阅

**路径**: Monitor → Subscriptions

预期看到：

| Client ID | Topic | QoS |
|-----------|-------|-----|
| ESP32_1 | esp32_1/control | 2 |

### 4.4 验证心跳消息

**路径**: Monitor → Topics

预期每秒收到消息：

| Topic | Payload | 频率 |
|-------|---------|------|
| esp32_1/heartbeat | ESP32_1 is online | 每秒 |

---

## 5. 网络连通性测试

### 5.1 从电脑 Ping ESP32

```powershell
# 从串口日志获取 ESP32 IP（如 192.168.233.101）
ping 192.168.233.101

# 预期输出：
Reply from 192.168.233.101: bytes=32 time=3ms TTL=64
Reply from 192.168.233.101: bytes=32 time=2ms TTL=64
```

### 5.2 使用网络测试脚本

```powershell
# 运行网络测试（替换为实际 IP）
.\network_test.ps1 -Esp32IP "192.168.233.101"
```

---

## 6. 故障排查

### 问题 1: LED 不亮

**排查**：
- 检查 GPIO2 是否连接 LED
- 检查固件是否正确烧录

### 问题 2: WiFi 连接失败

**排查**：
```
W (xx) ESP32S3_WIFI_EVENT: Waiting for WiFi connection... (x/30)
E (xx) ESP32S3_WIFI_EVENT: WiFi connection timeout!
```
- 确认 WiFi SSID: `去码头整点薯条`
- 确认 WiFi 密码: `Getfries0ndock`
- 确认电脑可以连接该 WiFi

### 问题 3: MQTT 连接失败

**排查**：
```
E (xx) ESP32S3_MQTT_EVENT: MQTT connection error
```
- 确认 EMQX 服务器运行: `ping 192.168.233.100`
- 确认 MQTT 端口 1883 开放
- 检查 EMQX 管理界面是否可访问

---

## 7. 测试清单

- [ ] 串口日志显示正常启动
- [ ] LED 快速闪烁（WiFi 连接中）
- [ ] LED 慢速闪烁（MQTT 连接中）
- [ ] LED 常亮（连接成功）
- [ ] 串口显示 IP 地址
- [ ] 电脑能 Ping 通 ESP32
- [ ] EMQX 管理界面显示客户端连接
- [ ] EMQX 显示主题订阅
- [ ] 心跳消息正常收发

---

*烧录完成时间: 2026-02-10 14:41*  
*固件版本: 8eb24bd-dirty*  
*分支: wifi-emqx-test*
