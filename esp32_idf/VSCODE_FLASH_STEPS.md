# VSCode + ESP-IDF 烧录详细步骤

## 准备工作

1. 打开 VSCode
2. 确认已安装 **ESP-IDF** 扩展（Espressif IDF）
3. 用 USB 线连接 ESP32-S3

---

## 步骤 1：打开 ESP-IDF 终端

### 方法 1：通过命令面板（推荐）

1. 按 `Ctrl+Shift+P` 打开命令面板
2. 输入 `ESP-IDF: Open ESP-IDF Terminal`
3. 按回车，等待终端初始化

### 方法 2：通过底部状态栏

1. 点击 VSCode 底部状态栏的 ESP-IDF 图标
2. 选择 "Open ESP-IDF Terminal"

### 方法 3：通过菜单

1. 菜单栏：View → Terminal
2. 在终端中手动执行 ESP-IDF 的 export 脚本

---

## 步骤 2：编译项目

在 ESP-IDF 终端中执行：

```bash
# 进入项目目录（如果不在）
cd E:/Platform_G2/esp32_idf

# 设置目标芯片（如果是第一次编译）
idf.py set-target esp32s3

# 编译项目
idf.py build
```

**编译过程**：
- 首次编译可能需要 5-10 分钟
- 后续编译约 1-2 分钟
- 看到 `Project build complete.` 表示成功

---

## 步骤 3：烧录到 ESP32-S3

### 3.1 进入下载模式

1. **按住** ESP32 开发板上的 **BOOT** 按钮
2. **按一下** **RESET** 按钮
3. **松开** **BOOT** 按钮
4. 设备进入下载模式（端口可能从 COM8 变为 COM9）

### 3.2 执行烧录命令

在 ESP-IDF 终端中执行：

```bash
# 自动检测端口烧录
idf.py flash

# 或指定端口
idf.py -p COM9 flash

# 或更高波特率加速烧录
idf.py -p COM9 -b 921600 flash
```

**烧录成功标志**：
```
Leaving...
Hard resetting via RTS pin...
Done
```

---

## 步骤 4：查看串口日志

### 方法 1：使用 idf.py monitor

```bash
idf.py monitor

# 或指定端口
idf.py -p COM9 monitor
```

**退出监控**：按 `Ctrl+]`

### 方法 2：组合命令（烧录+监控）

```bash
idf.py -p COM9 flash monitor
```

---

## 步骤 5：一键快捷操作（VSCode 任务）

### 配置 tasks.json（可选）

在项目根目录创建 `.vscode/tasks.json`：

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "ESP-IDF: Build",
            "type": "shell",
            "command": "idf.py build",
            "group": "build",
            "presentation": {
                "reveal": "always"
            }
        },
        {
            "label": "ESP-IDF: Flash",
            "type": "shell",
            "command": "idf.py flash",
            "group": "build",
            "presentation": {
                "reveal": "always"
            }
        },
        {
            "label": "ESP-IDF: Monitor",
            "type": "shell",
            "command": "idf.py monitor",
            "group": "build",
            "presentation": {
                "reveal": "always"
            }
        },
        {
            "label": "ESP-IDF: Flash and Monitor",
            "type": "shell",
            "command": "idf.py flash monitor",
            "group": "build",
            "presentation": {
                "reveal": "always"
            }
        }
    ]
}
```

### 使用快捷键

1. 按 `Ctrl+Shift+P`
2. 输入 `Tasks: Run Task`
3. 选择相应的任务（Build / Flash / Monitor）

---

## 完整测试流程

### 1. 烧录并启动

```bash
# 编译 + 烧录 + 监控
idf.py -p COM9 flash monitor
```

### 2. 观察 LED 状态

| 时间 | LED 状态 | 含义 |
|------|----------|------|
| 0-2s | 快速闪烁 | 系统启动，WiFi 连接中 |
| 2-5s | 慢速闪烁 | WiFi 成功，MQTT 连接中 |
| 5s+ | 常亮 | 全部连接成功 |

### 3. 查看串口日志

**成功连接的预期输出**：
```
I (234) ESP32S3_STATUS_LED: Status LED initialized on GPIO 2
I (234) ESP32S3_WIFI_EVENT: Begin to connect the AP
I (234) ESP32S3_STATUS_LED: LED mode changed to: 1
I (3234) ESP32S3_WIFI_EVENT: Got ip:192.168.233.xxx
I (3234) ESP32S3_WIFI_EVENT: Connected to ap!
I (3234) ESP32S3_STATUS_LED: LED mode changed to: 2
I (8234) ESP32S3_MQTT_EVENT: Connected to MQTT server.
I (8234) ESP32S3_STATUS_LED: LED mode changed to: 3
I (8234) ESP32S3_MQTT_EVENT: Subscribed to esp32_1/control
I (9234) ESP32S3_MQTT_EVENT: Published to esp32_1/heartbeat
```

### 4. Ping 测试

在电脑的命令提示符/PowerShell 中：

```powershell
# 从串口日志获取 ESP32 的 IP（如 192.168.233.101）
# 然后执行
ping 192.168.233.101

# 预期输出：
# Reply from 192.168.233.101: bytes=32 time=3ms TTL=64
# Reply from 192.168.233.101: bytes=32 time=2ms TTL=64
```

### 5. EMQX 管理界面验证

1. 打开浏览器访问：http://192.168.233.100:18083/
2. 登录（默认 admin/public）
3. 进入 Monitor → Clients
4. 查看是否有 Client ID 为 `ESP32_1` 的连接

---

## 常见问题

### Q1: 终端显示 "idf.py 不是内部或外部命令"

**解决**：
1. 确保使用 "ESP-IDF: Open ESP-IDF Terminal" 打开的终端
2. 检查 ESP-IDF 扩展是否正确安装
3. 重新配置 ESP-IDF：按 `Ctrl+Shift+P` → `ESP-IDF: Configure ESP-IDF Extension`

### Q2: 编译错误 "No such file or directory"

**解决**：
```bash
# 清理并重新配置
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

### Q3: 烧录失败 "Failed to connect"

**解决**：
1. 确认进入下载模式（BOOT+RESET）
2. 检查设备管理器中的 COM 端口
3. 尝试降低波特率：`idf.py -p COM9 -b 115200 flash`

### Q4: 串口监控显示乱码

**解决**：
- 确认波特率为 115200
- 检查 USB 数据线是否完好

---

**完成！** 🎉
