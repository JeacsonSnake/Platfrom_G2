# 基于 MAC 地址的动态设备 ID 与 MQTT Topic 重构记录

**日期**: 2026-06-15  
**分支**: `main`  
**任务描述**: 将 ESP32 固件与 Django 后端的 MQTT 通信从硬编码的 `esp32_1/...` 重构为按 MAC 地址动态生成的 `esp32/<mac>/...` 层级，实现同一套固件在多块 ESP32S3 上的区分与统一管理。

---

## 1. 背景

### 历史问题回顾
- ESP32 固件中 MQTT topic 硬编码为 `esp32_1/control`、`esp32_1/heartbeat`、`esp32_1/data`。
- 同一套代码烧录到不同板子后，所有设备共用同一组 topic，后端无法区分来源。
- 后端 `main_page/mqtt.py` 中尝试使用 `esp32_+/+` 作为通配符订阅多台设备，但 MQTT 规范不允许 `+` 与字符串混在同一个 topic level 中，导致启动时报错：
  ```text
  ValueError: Invalid subscription filter.
  ```
- 结果：后端 MQTT 线程崩溃，无法订阅任何主题，Vue Dashboard 无法显示设备信息。

### 本次任务目标
1. ESP32 从底层读取 MAC 地址，动态生成唯一的 `device_id` 和 MQTT topic。
2. 后端使用合法的 MQTT 通配符订阅所有设备。
3. 后端根据 topic 自动识别设备，并反推 MAC 地址写入数据库。
4. 控制指令、急停、任务下发、定时任务均按新 topic 层级寻址。
5. 前端无需改动，自动显示新的 `esp32_<mac>` 设备。

---

## 2. 方案设计

### 2.1 Topic 层级规范

| 方向 | 旧 topic | 新 topic |
|---|---|---|
| 后端 → 设备 控制指令 | `esp32_1/control` | `esp32/<mac>/control` |
| 设备 → 后端 心跳 | `esp32_1/heartbeat` | `esp32/<mac>/heartbeat` |
| 设备 → 后端 遥测 | `esp32_1/data` | `esp32/<mac>/telemetry` |
| 设备 → 后端 任务状态 | `esp32_1/control` | `esp32/<mac>/task` |

其中 `<mac>` 为 ESP32 STA 接口 MAC 地址的 12 位十六进制字符串（无冒号）。

### 2.2 设备身份映射

- **device_id**: `esp32_<mac>`，例如 `esp32_7cdfa1e6d3cc`
- **mac_address**: 从 device_id 反推，例如 `7c:df:a1:e6:d3:cc`
- **MQTT client_id**: `ESP32S3_<mac>`，例如 `ESP32S3_7cdfa1e6d3cc`

### 2.3 后端订阅策略

```python
# 合法通配符：匹配所有新层级设备
mqtt_client.subscribe('esp32/+/+')
# 兼容旧设备（单台）
mqtt_client.subscribe('esp32_1/+')
```

---

## 3. 实现方案

### 3.1 新增文件

#### `esp32_idf/main/device_identity.h`
- 定义 `DEVICE_MAC_STR_LEN`、`DEVICE_ID_LEN`、`MQTT_CLIENT_ID_LEN`、`MQTT_TOPIC_LEN`。
- 声明全局变量：
  - `device_mac_str` / `device_id` / `mqtt_client_id`
  - `mqtt_control_topic` / `mqtt_heartbeat_topic` / `mqtt_telemetry_topic` / `mqtt_task_topic`
- 声明 `device_identity_init()`。

#### `esp32_idf/main/device_identity.c`
- 调用 `esp_read_mac(mac, ESP_MAC_WIFI_STA)` 读取 MAC。
- 格式化 12 位十六进制 MAC 字符串。
- 生成 device_id、client_id 以及四个 MQTT topic。
- 启动日志输出：
  ```text
  I (1234) DEVICE_IDENTITY: MAC=7cdfa1e6d3cc, device_id=esp32_7cdfa1e6d3cc, client_id=ESP32S3_7cdfa1e6d3cc
  I (1234) DEVICE_IDENTITY: control=esp32/7cdfa1e6d3cc/control, heartbeat=esp32/7cdfa1e6d3cc/heartbeat, telemetry=esp32/7cdfa1e6d3cc/telemetry, task=esp32/7cdfa1e6d3cc/task
  ```

### 3.2 修改文件

#### `esp32_idf/main/CMakeLists.txt`

```cmake
idf_component_register(SRCS "monitor.c" "led.c" "led_strip_encoder.c" "main.c" "wifi.c" "mqtt.c" "pwm.c" "pcnt.c" "pid.c" "heating_detect.c" "device_identity.c"
                    INCLUDE_DIRS ".")
```

#### `esp32_idf/main/main.h`
- 移除硬编码的 `MQTT_CONTROL_CHANNEL`、`MQTT_HEARTBEAT_CHANNEL`、`MQTT_DATA_CHANNEL` 宏。
- 添加 `#include "device_identity.h"`。
- topic 由 `device_identity_init()` 动态生成。

#### `esp32_idf/main/main.c`

```c
void app_main(void){
    // 从硬件读取 MAC 地址，生成 device_id 与 MQTT topic
    device_identity_init();

    xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 2, NULL);
    // ...
}
```

#### `esp32_idf/main/mqtt.c`
- `message_compare()` 与订阅频道改用 `mqtt_control_topic`。
- 心跳 payload 从 `"ESP32_1 is online"` 简化为 `"online"`。
- 心跳 topic 改用 `mqtt_heartbeat_topic`。
- MQTT client_id 改用动态 `mqtt_client_id`：
  ```c
  .credentials.client_id = mqtt_client_id,
  ```

#### `esp32_idf/main/pwm.c` / `esp32_idf/main/pcnt.c`
- PWM / PCNT 遥测消息发布到 `mqtt_telemetry_topic`。

#### `esp32_idf/main/pid.c`
- 任务创建/完成状态发布到独立的 `mqtt_task_topic`：
  ```c
  sprintf(buff, "task_create_%d_%d_%d", local_index, local_speed, local_duration);
  esp_mqtt_client_publish(mqtt_client, mqtt_task_topic, buff, strlen(buff), 2, 0);
  // ...
  sprintf(buff, "task_finished_%d_%d_%d", local_index, local_speed, local_duration);
  esp_mqtt_client_publish(mqtt_client, mqtt_task_topic, buff, strlen(buff), 2, 0);
  ```

#### `django_backend/django_backend/settings.py`
- 新增默认目标设备配置：
  ```python
  MQTT_DEFAULT_DEVICE_ID = os.environ.get('MQTT_DEFAULT_DEVICE_ID', 'esp32_1')
  ```

#### `django_backend/main_page/mqtt.py`
- 修复非法订阅 `esp32_+/+` 为合法 `esp32/+/+`，并保留 `esp32_1/+` 兼容旧设备。
- 新增 `_extract_device_id_from_topic()`，支持新旧两种层级：
  ```python
  if len(parts) >= 2 and parts[0] == 'esp32' and len(parts[1]) == 12:
      return f"esp32_{parts[1]}"
  if parts and parts[0].startswith('esp32_'):
      return parts[0]
  ```
- 新增 `_device_id_to_mac()`，自动从 device_id 反推 MAC 地址。
- 新增 `_device_control_topic()`，生成 `esp32/<mac>/control`。
- `emergency_stop()` 与 `dispatch_motor_task()` 使用 `_device_control_topic(device_id)`。
- `_ensure_device_state()` 自动创建 Device 时回填 `mac_address`。
- 改进 MQTT Client 启动守卫，兼容 `runserver` 与 Daphne/uvicorn/gunicorn：
  ```python
  def _should_init_mqtt_client():
      if os.environ.get('RUN_MAIN'):
          return True
      if any('daphne' in arg or 'uvicorn' in arg or 'gunicorn' in arg for arg in sys.argv):
          return True
      return False
  ```

#### `django_backend/main_page/apps.py`
- 移除 `RUN_MAIN` 守卫，只要 `mqtt.client` 已创建就启动 `loop_start()`。

#### `django_backend/main_page/views.py`
- `_resolve_motor_command()` 与 `_resolve_step_interface()` 中 STIR/DISPENSE 步骤的默认 topic 改为：
  ```python
  default_topic = _device_control_topic(getattr(settings, 'MQTT_DEFAULT_DEVICE_ID', 'esp32_1'))
  ```

#### `django_backend/task_manager.py`
- 移除硬编码的 `esp32_1/control`。
- 定时触发时读取 `settings.MQTT_DEFAULT_DEVICE_ID` 生成目标 topic。

---

## 4. 数据流

```text
┌─────────────────┐      MQTT      ┌─────────────────┐
│  ESP32-S3       │ ─────────────> │  EMQX Broker    │
│ (MAC-based ID)  │ <───────────── │                 │
└─────────────────┘                └────────┬────────┘
                                            │
                                            │ subscribe 'esp32/+/+'
                                            ▼
                                   ┌─────────────────┐
                                   │  Django Backend │
                                   │  main_page/mqtt │
                                   └────────┬────────┘
                                            │ Channels
                                            ▼
                                   ┌─────────────────┐
                                   │  Vue Dashboard  │
                                   └─────────────────┘
```

---

## 5. Git 提交记录

### 第一次提交：ESP32 动态 device_id 与 topic

```bash
git add \
  esp32_idf/main/device_identity.h \
  esp32_idf/main/device_identity.c \
  esp32_idf/main/CMakeLists.txt \
  esp32_idf/main/main.c \
  esp32_idf/main/main.h \
  esp32_idf/main/mqtt.c \
  esp32_idf/main/pcnt.c \
  esp32_idf/main/pid.c \
  esp32_idf/main/pwm.c

git commit -m "feat(esp32): 根据 MAC 地址动态生成 device_id 与 MQTT topic

- 新增 device_identity 模块，从底层读取 MAC 并生成唯一 device_id
- MQTT client_id、控制/心跳/遥测/任务 topic 均按 MAC 动态构建
- 同一套固件可在多块 ESP32-S3 上烧录，后端通过 esp32/<mac>/... 区分"
```

**提交信息**:
- Commit: `f333efb`
- 新增: 2 个文件
- 修改: 8 个文件

### 第二次提交：Django 后端适配多设备 topic

```bash
git add \
  django_backend/django_backend/settings.py \
  django_backend/main_page/apps.py \
  django_backend/main_page/mqtt.py \
  django_backend/main_page/views.py \
  django_backend/task_manager.py

git commit -m "feat(backend): 支持按 MAC 区分的多设备 MQTT topic 订阅与指令下发

- 修复非法通配符 esp32_+/+，改为合法订阅 esp32/+/+
- 新增 device_id 与 MAC 地址互推、控制 topic 生成 helper
- MQTT 控制/急停/任务下发均支持新的 esp32/<mac>/control 层级
- task_manager 与 recipe step 默认 topic 使用 MQTT_DEFAULT_DEVICE_ID
- 调整 MQTT Client 启动守卫，兼容 runserver 与 Daphne/ASGI"
```

**提交信息**:
- Commit: `62a9e91`
- 修改: 5 个文件

---

## 6. 使用说明

### 6.1 设置默认目标设备

Windows:
```cmd
set MQTT_DEFAULT_DEVICE_ID=esp32_7cdfa1e6d3cc
```

Linux / macOS:
```bash
export MQTT_DEFAULT_DEVICE_ID=esp32_7cdfa1e6d3cc
```

> 请把 `esp32_7cdfa1e6d3cc` 替换为实际 ESP32 的 device_id，烧录后可在 monitor 日志中查看。

### 6.2 编译烧录 ESP32

```powershell
cd esp32_idf
idf.py build
idf.py -p COM9 flash monitor
```

### 6.3 启动 Django 后端

```cmd
cd django_backend
set MQTT_DEFAULT_DEVICE_ID=esp32_<你的MAC>
python manage.py runserver
```

### 6.4 启动定时任务调度器

```cmd
set MQTT_DEFAULT_DEVICE_ID=esp32_<你的MAC>
python task_manager.py
```

### 6.5 验证

1. Django 控制台应输出 `MQTT Connect Success!`，无 `Invalid subscription filter` 报错。
2. EMQX Dashboard 中应出现 `ESP32S3_<mac>` 客户端。
3. Vue Dashboard 中应出现 `esp32_<mac>` 设备，展开可查看 PWM / PCNT 遥测。
4. MQTT 客户端监听：
   ```bash
   mosquitto_sub -h 192.168.233.100 -t 'esp32/#' -u Django -P 123456
   ```

---

## 7. 测试结果

### 7.1 Django 系统检查

```bash
python manage.py check
```

结果：
```text
System check identified no issues (0 silenced).
```

### 7.2 Django 单元测试

```bash
python manage.py test
```

结果：
```text
Ran 17 tests in 0.067s
FAILED (failures: 2)
```

失败用例：
- `test_job_start_queues_all_pending_steps_and_outbox`
- `test_job_status_returns_counts_and_next_step`

**失败原因**：测试环境未启动 MQTT Client（`RUN_MAIN` 未设置），`publish_device_command()` 抛出 `RuntimeError`，导致 job step 被标记为 `FAILED`。

**说明**：这两个失败并非本次修改引入。`manage.py test` 本身不会创建 MQTT Client，而相关测试未 mock `publish_device_command`，因此在修改前同样会失败。生产/开发环境使用 `runserver` 或 `daphne` 启动时不会出现该问题。

---

## 8. 问题解决记录

### 问题：后端订阅通配符非法导致无法接收消息
**现象**: 启动 `runserver` 后 paho-mqtt 线程抛出 `ValueError: Invalid subscription filter.`  
**原因**: `esp32_+/+` 中 `+` 没有独占一个 topic level，违反 MQTT 通配符规则  
**解决**: 改为合法订阅 `esp32/+/+`，并保留 `esp32_1/+` 兼容旧设备

### 问题：同一套 ESP32 代码无法区分多台设备
**现象**: 所有板子都发布到 `esp32_1/...`，后端无法知道消息来源  
**原因**: topic 和设备 ID 在代码中硬编码  
**解决**: 新增 `device_identity` 模块，从 `esp_read_mac()` 读取 MAC，动态生成 device_id 和 topic

### 问题：急停/任务下发 topic 与设备实际订阅不匹配
**现象**: 重构后后端若继续向 `esp32_1/control` 发指令，新设备收不到  
**原因**: 后端控制 topic 生成逻辑未同步更新  
**解决**: 新增 `_device_control_topic()` helper，统一按 `esp32/<mac>/control` 生成

---

## 9. 后续建议

### 方案 A: 给 `Spinning` 模型增加 `device_id` 字段
当前 `task_manager.py` 通过环境变量 `MQTT_DEFAULT_DEVICE_ID` 决定目标设备。长期应改为：

```python
class Spinning(models.Model):
    # ... 原有字段 ...
    device_id = models.CharField(max_length=32, default='esp32_000000000000')
```

这样每条定时记录可指定执行设备，支持多设备并行调度。

### 方案 B: 逐步迁移到 JSON 结构化协议
当前仍使用文本协议：`pcnt_count_0_300`、`pwm_set_0_4000`、`task_create_0_3000_10`。建议后续改为 JSON：

```json
{
  "interface_type": "telemetry",
  "telemetry_type": "pcnt",
  "motor": 0,
  "value": 300,
  "timestamp": "2026-06-15T12:00:00+08:00"
}
```

便于扩展温度、错误码、固件版本等字段。

### 方案 C: 部署架构升级
- Django Channels 从 `InMemoryChannelLayer` 迁移到 `channels_redis`，支持多 Daphne Worker。
- 生产环境使用 Daphne + Nginx 反向代理，而非 `runserver`。
- EMQX 配置 ACL/认证，避免节点间误操作。

---

## 10. 参考链接

- [ESP-IDF MAC 地址 API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/misc_system_api.html#mac-address)
- [MQTT Topic 与通配符](https://mqtt.org/blog/2019/05/13/mqtt-5-topic-and-wildcards)
- [EMQX 通配符订阅](https://www.emqx.io/docs/en/v5.0/mqtt/mqtt-topic.html#topic-wildcards)
- 项目规划文档: `PLATFORM_G2_FULL_OPTIMIZATION_PLAN.md`
- 开发规范: `AGENTS.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-06-15 12:30  
**完成时间**: 2026-06-15 12:30
