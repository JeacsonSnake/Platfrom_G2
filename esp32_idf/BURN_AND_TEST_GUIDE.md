# ESP32-S3 烧录与测试完整指南

> 目标：将代码烧录到 ESP32-S3 并验证 WiFi/MQTT 连接  
> EMQX 管理界面：http://192.168.233.100:18083/

---

## 📋 准备工作

### 1. 确认硬件连接

1. 使用 USB 线连接 ESP32-S3 到电脑
2. 确认设备管理器中显示 COM 端口（如 COM8 或 COM9）
3. 记下端口号，后续使用

### 2. 确认网络环境

| 检查项 | 命令/方法 | 预期结果 |
|--------|----------|----------|
| 电脑能否访问 EMQX | `ping 192.168.233.100` | 有响应 |
| EMQX 管理界面 | 浏览器访问 http://192.168.233.100:18083/ | 显示登录页 |
| 目标 WiFi 可用 | 电脑连接 `去码头整点薯条` | 连接成功 |

---

## 🔧 方法一：通过 VSCode + ESP-IDF 扩展烧录

### 步骤 1：打开 VSCode

1. 打开 VSCode
2. 打开项目文件夹：`E:\Platform_G2\esp32_idf`
3. 确保已安装 ESP-IDF 扩展

### 步骤 2：配置 ESP-IDF 环境

在 VSCode 中按 `Ctrl+`` 打开终端，执行：

```powershell
# 方式 1：使用 ESP-IDF 终端（推荐）
# 在 VSCode 命令面板中 (Ctrl+Shift+P) 输入：
# "ESP-IDF: Open ESP-IDF Terminal"

# 方式 2：手动设置环境变量（如果知道安装路径）
$env:IDF_PATH = "C:\Users\<用户名>\esp\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Users\<用户名>\.espressif"
# 然后运行 export.ps1
. "$env:IDF_PATH\export.ps1"
```

### 步骤 3：编译项目

在 ESP-IDF 终端中执行：

```bash
# 清理旧构建（可选）
idf.py fullclean

# 编译项目
idf.py build
```

**预期输出**：
```
[1/8] Performing build step for 'bootloader'
...
[8/8] Generating binary image from built executable
Generated E:/Platform_G2/esp32_idf/build/test.bin
```

### 步骤 4：烧录到 ESP32-S3

```bash
# 烧录（自动检测端口）
idf.py flash

# 或指定端口
idf.py -p COM8 flash

# 或波特率更高的快速烧录
idf.py -p COM8 -b 921600 flash
```

**烧录过程**：
```
esptool.py v4.x
Serial port COM8
Connecting....
Chip is ESP32-S3 (revision v0.1)
Features: Wi-Fi, BLE
...
Writing at 0x00010000... (50%)
Writing at 0x00020000... (100%)
Leaving...
Hard resetting via RTS pin...
Done
```

### 步骤 5：查看串口日志

```bash
# 监控串口输出
idf.py monitor

# 或指定端口
idf.py -p COM8 monitor

# 退出监控：Ctrl+]
```

---

## 🔧 方法二：使用 esptool 直接烧录

如果已安装 esptool，可以直接烧录：

### 步骤 1：进入下载模式

1. 按住 **BOOT** 按钮
2. 按一下 **RESET** 按钮
3. 松开 **BOOT** 按钮
4. 端口会从 COM8 变为 COM9

### 步骤 2：烧录各分区

```powershell
# 烧录 Bootloader (0x0)
python -m esptool --port COM9 --chip esp32s3 write_flash 0x0 build/bootloader/bootloader.bin

# 烧录分区表 (0x8000)
python -m esptool --port COM9 --chip esp32s3 write_flash 0x8000 build/partition_table/partition-table.bin

# 烧录应用程序 (0x10000)
python -m esptool --port COM9 --chip esp32s3 write_flash 0x10000 build/test.bin

# 或使用合并烧录命令
python -m esptool --port COM9 --chip esp32s3 write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/test.bin
```

---

## 🧪 测试方法

### 测试 1：串口日志验证

烧录完成后，打开串口监视器：

```bash
idf.py monitor
# 或
python -m serial.tools.miniterm COM8 115200
```

**预期日志输出**：

```
I (0) boot: ESP-IDF v5.5.2 2nd stage bootloader
...
I (234) ESP32S3_STATUS_LED: Status LED initialized on GPIO 2
I (234) ESP32S3_WIFI_EVENT: Begin to connect the AP
I (234) ESP32S3_STATUS_LED: LED mode changed to: 1  <-- 快速闪烁
I (3234) ESP32S3_WIFI_EVENT: Got ip:192.168.233.xxx
I (3234) ESP32S3_STATUS_LED: LED mode changed to: 2  <-- 慢速闪烁
...
I (8234) ESP32S3_MQTT_EVENT: Connected to MQTT server.
I (8234) ESP32S3_STATUS_LED: LED mode changed to: 3  <-- 常亮
I (8234) ESP32S3_MQTT_EVENT: Subscribed to esp32_1/control
I (9234) ESP32S3_MQTT_EVENT: Published to esp32_1/heartbeat
```

### 测试 2：LED 状态观察

观察开发板上的 LED（GPIO2）：

| 时间 | LED 状态 | 含义 |
|------|----------|------|
| 0-2s | 快速闪烁 (5Hz) | WiFi 连接中 |
| 2-5s | 慢速闪烁 (1Hz) | WiFi 成功，MQTT 连接中 |
| 5s+ | 常亮 | 全部连接成功 |

### 测试 3：Ping 测试（验证 WiFi 连接）

#### 方法 A：从电脑 Ping ESP32

```bash
# 1. 从串口日志获取 ESP32 的 IP（如 192.168.233.101）
# 2. 在电脑上执行
ping 192.168.233.xxx

# 预期：有响应，延迟 < 10ms
Reply from 192.168.233.xxx: bytes=32 time=3ms TTL=64
```

#### 方法 B：从 ESP32 Ping 网关（代码中添加）

如需在 ESP32 端测试网络连通性，可在 `main.c` 中添加：

```c
#include "esp_ping.h"

// 在 wifi_init() 之后添加
void test_ping(void)
{
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr.u_addr.ip4.addr = ipaddr_addr("192.168.233.100"); // EMQX IP
    ping_config.count = 5;  // Ping 5 次
    
    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, NULL, &ping);
    esp_ping_start(ping);
}
```

### 测试 4：EMQX 管理界面验证 MQTT 连接

1. **打开 EMQX 管理界面**
   - 浏览器访问：http://192.168.233.100:18083/
   - 默认账号：admin
   - 默认密码：public（或根据实际配置）

2. **查看客户端连接**
   
   路径：Monitor → Clients
   
   | 字段 | 预期值 |
   |------|--------|
   | Client ID | `ESP32_1` |
   | Username | `ESP32_1` |
   | IP Address | `192.168.233.xxx` |
   | Connected | Yes |
   | Protocol | MQTT v3.1.1 |

   ![预期截图位置]

3. **查看订阅主题**
   
   路径：Monitor → Subscriptions
   
   | Client ID | Topic | QoS |
   |-----------|-------|-----|
   | ESP32_1 | `esp32_1/control` | 2 |

4. **查看消息收发**
   
   路径：Monitor → Topics
   
   - `esp32_1/heartbeat` 应每秒收到消息 "ESP32_1 is online"

5. **发送测试消息**
   
   路径：Tools → WebSocket
   
   ```
   - 连接到 ws://192.168.233.100:8083/mqtt
   - 订阅主题：esp32_1/heartbeat
   - 发送主题：esp32_1/control
   - 消息内容：Hello there
   - 预期回复：Hello to you too
   ```

---

## 🔍 故障排查

### 问题 1：无法编译

**现象**：`idf.py build` 报错

**解决**：
```bash
# 清理并重新配置
idf.py fullclean
idf.py set-target esp32s3
idf.py menuconfig  # 检查配置
idf.py build
```

### 问题 2：烧录失败

**现象**：`Failed to connect to ESP32-S3`

**解决**：
1. 确认进入下载模式（BOOT+RESET）
2. 检查端口是否正确（设备管理器）
3. 降低波特率：`idf.py -p COM9 -b 115200 flash`

### 问题 3：WiFi 连接失败

**现象**：串口显示 "WiFi connection timeout"

**排查**：
```bash
# 1. 确认 WiFi SSID 和密码
# 检查 main/main.h
#define WIFI_SSID "去码头整点薯条"
#define WIFI_PASS "Getfries0ndock"

# 2. 确认电脑可以连接同一 WiFi

# 3. 检查信号强度
```

### 问题 4：MQTT 连接失败

**现象**：WiFi 成功但 MQTT 无法连接

**排查**：
1. 确认 EMQX 服务器运行：`ping 192.168.233.100`
2. 检查 EMQX 端口 1883 是否开放：
   ```bash
   telnet 192.168.233.100 1883
   ```
3. 检查防火墙设置
4. 查看 EMQX 管理界面是否有连接记录

### 问题 5：LED 不亮

**现象**：LED 无反应

**排查**：
1. 确认使用的是 GPIO2（部分开发板可能不同）
2. 检查 LED 是否损坏（短接 GPIO2 和 3.3V 测试）
3. 查看串口日志是否有 LED 初始化信息

---

## 📊 测试清单

### 烧录前检查

- [ ] ESP32-S3 通过 USB 连接电脑
- [ ] 设备管理器识别到 COM 端口
- [ ] VSCode ESP-IDF 环境可用
- [ ] 代码已保存并编译通过

### 烧录过程检查

- [ ] 成功进入下载模式
- [ ] 烧录无错误
- [ ] 烧录后自动复位

### 功能验证检查

- [ ] 串口显示启动日志
- [ ] LED 快速闪烁（WiFi 连接中）
- [ ] LED 慢速闪烁（MQTT 连接中）
- [ ] LED 常亮（连接成功）
- [ ] 串口显示 IP 地址
- [ ] 电脑能 Ping 通 ESP32
- [ ] EMQX 管理界面显示客户端连接
- [ ] EMQX 显示主题订阅
- [ ] 心跳消息正常收发

---

## 📝 快速命令参考

```bash
# 完整流程（VSCode ESP-IDF 终端）

# 1. 编译
idf.py build

# 2. 烧录
idf.py -p COM8 flash

# 3. 监控
idf.py monitor

# 4. 编译+烧录+监控
idf.py -p COM8 flash monitor

# 5. 仅擦除 Flash
idf.py -p COM8 erase-flash
```

---

**祝烧录顺利！** 🚀
