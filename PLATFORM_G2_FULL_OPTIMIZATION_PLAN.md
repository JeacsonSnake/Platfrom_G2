# Platform G2 后端-MQTT-前端链路 完整优化方案（方案 B）

> 本文档为方案 B：完整优化。当前实施阶段为**方案 A（最小修复）**，但所有接口与数据结构均按本文档预留扩展能力。

---

## 1. 目标与范围

### 1.1 目标
- 支持 **N 台 ESP32-S3** 同时接入，Dashboard 统一展示与批量管控。
- 实现**真正的实时数据流**：MQTT → Django Channels → WebSocket → Vue，替代 HTTP 轮询。
- 建立**设备注册与发现机制**：Django 维护设备白名单，与 EMQX 在线状态做比对。
- 实现**任务全生命周期追踪**：下发 → 确认 → 进度 → 完成/失败/急停。
- 提供**急停与恢复**的安全操作闭环。
- `task_manager.py` 最终整合进 Django 生态（Celery / Management Command）。

### 1.2 范围
| 组件 | 改动范围 |
|---|---|
| Django Backend | 模型扩展、MQTT 客户端重构、Channels Consumer 升级、API 新增 |
| Vue Frontend | WebSocket 服务封装、Dashboard 重构、任务下发界面 |
| ESP32 Firmware | **后续统一迭代**，当前方案预留接口 |
| EMQX Broker | 配置规则引擎（可选，用于多设备消息标记） |
| 部署架构 | Redis Channel Layer、Daphne 多 Worker、Nginx 反向代理 |

---

## 2. 架构设计

### 2.1 多设备 Topic 规范（预留）

当前固件使用硬编码 Topic：
```
esp32_1/control    # 订阅
ces32_1/heartbeat  # 发布
ces32_1/data       # 发布
```

**未来多设备 Topic 规范（推荐）**：
```
esp32_1/control    →  esp32/{device_id}/control
esp32_1/heartbeat  →  esp32/{device_id}/heartbeat
esp32_1/data       →  esp32/{device_id}/telemetry
esp32_1/task       →  esp32/{device_id}/task/status   # 新增：任务状态专属 Topic
```

**过渡期兼容策略**：
- 后端订阅 `esp32_+/+`（通配符），从 Topic 提取 `device_id`。
- 当固件未更新时，所有设备共用 `esp32_1/...`，后端通过 Payload 中的 `mac_address` 字段二次区分（需固件配合）。
- 单台设备阶段，`device_id = "esp32_1"` 即唯一标识。

### 2.2 设备身份识别体系

| 字段 | 来源 | 说明 |
|---|---|---|
| `device_id` | Topic 名 / 配置 | 逻辑标识，如 `esp32_1` |
| `client_id` | MQTT 连接层 | EMQX 分配的 Client ID，如 `ESP32S3_xxx` |
| `mac_address` | ESP32 固件上报 | 硬件唯一标识，用于最终区分多台设备 |
| `node_label` | Django 注册表 | 用户自定义别名，如 "反应釜 A" |

**设备发现流程**：
```
1. ESP32 连接 EMQX → 后端通过 EMQX API 或 MQTT 系统 Topic 发现新 Client ID
2. 后端比对 Device 注册表 → 若未注册，标记为 "未注册设备" 或自动注册
3. Dashboard 显示所有注册设备，离线的也显示（Offline）
```

### 2.3 数据流完整链路

```
┌─────────────┐     MQTT (esp32_1/+)     ┌─────────────────┐
│  ESP32-S3   │ ────────────────────────>│  Django Backend │
│  (Firmware) │ <────────────────────────│  (MQTT Client)  │
└─────────────┘   MQTT (esp32_1/control) └────────┬────────┘
                                                   │
                          ┌────────────────────────┘
                          │ channel_layer (Redis)
                          ↓
                   ┌─────────────┐
                   │   Daphne    │ ← 多 Worker 部署
                   │   (ASGI)    │
                   └──────┬──────┘
                          │ WebSocket
                          ↓
                   ┌─────────────┐
                   │  Vue Front  │
                   │  Dashboard  │
                   └─────────────┘
```

### 2.4 后端核心模块职责

| 模块 | 职责 |
|---|---|
| `models.py` | `Device`（设备注册表）、`DeviceTelemetry`（遥测历史）、`EmergencyStopLog`（急停记录） |
| `mqtt.py` | 多 Topic 订阅、消息解析标准化、设备心跳追踪、自动离线检测 |
| `consumers.py` | WebSocket 连接管理、心跳检测、消息广播、前端指令接收（如下发任务） |
| `views.py` | REST API：设备 CRUD、急停/恢复、任务下发、遥测查询 |
| `task_manager.py` | **整合为 Django Management Command 或 Celery Beat**，复用 Django ORM 与统一 MQTT Client |

---

## 3. 模型设计（预留）

### 3.1 Device（设备注册表）

```python
class Device(models.Model):
    device_id = models.CharField(max_length=32, unique=True)      # esp32_1
    client_id = models.CharField(max_length=64, blank=True)       # ESP32S3_xxx
    mac_address = models.CharField(max_length=17, blank=True)     # AA:BB:CC:DD:EE:FF
    label = models.CharField(max_length=64, blank=True)           # 用户自定义名称
    is_registered = models.BooleanField(default=False)
    is_online = models.BooleanField(default=False)
    last_heartbeat = models.DateTimeField(null=True, blank=True)
    task_status = models.CharField(max_length=16, default='idle') # idle/busy/paused/estopped
    current_task_id = models.CharField(max_length=64, blank=True)
    firmware_version = models.CharField(max_length=32, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)
```

### 3.2 DeviceTelemetry（遥测快照）

```python
class DeviceTelemetry(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    timestamp = models.DateTimeField(auto_now_add=True)
    motor_0_pwm = models.IntegerField(null=True)
    motor_0_pcnt = models.IntegerField(null=True)
    motor_1_pwm = models.IntegerField(null=True)
    motor_1_pcnt = models.IntegerField(null=True)
    motor_2_pwm = models.IntegerField(null=True)
    motor_2_pcnt = models.IntegerField(null=True)
    motor_3_pwm = models.IntegerField(null=True)
    motor_3_pcnt = models.IntegerField(null=True)
    temperature_0 = models.FloatField(null=True)  # 预留
    temperature_1 = models.FloatField(null=True)  # 预留
    # ... 扩展字段
```

> **方案 A 阶段**：`Device` 模型已创建，`DeviceTelemetry` 仅预留接口，暂不存储历史数据（避免 SQLite 膨胀）。

### 3.3 EmergencyStopLog（急停审计）

```python
class EmergencyStopLog(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    triggered_at = models.DateTimeField(auto_now_add=True)
    triggered_by = models.CharField(max_length=64)      # user_email / system / batch
    scope = models.CharField(max_length=16)             # single / multi / broadcast
    reason = models.TextField(blank=True)
    acknowledged_at = models.DateTimeField(null=True, blank=True)
    acknowledged_by = models.CharField(max_length=64, blank=True)
```

---

## 4. MQTT 客户端重构规划

### 4.1 订阅策略

```python
# 当前（方案 A）：
client.subscribe('esp32_+/+')    # 通配符订阅所有设备

# 未来（方案 B 完整版）：
client.subscribe('esp32/+/control')      # 设备回复（暂不订阅自己发布的）
client.subscribe('esp32/+/heartbeat')
client.subscribe('esp32/+/telemetry')
client.subscribe('esp32/+/task/status')
client.subscribe('$SYS/brokers/+/clients/+/connected')    # EMQX 系统 Topic（设备上线）
client.subscribe('$SYS/brokers/+/clients/+/disconnected') # EMQX 系统 Topic（设备离线）
```

### 4.2 消息解析标准化

所有 Legacy 文本消息统一解析为结构化字典：

```python
{
    "type": "mqtt_msg_broadcast",
    "topic": "telemetry",           # heartbeat / telemetry / task_status / cmd_reply
    "device_id": "esp32_1",
    "client_id": "ESP32S3_xxx",     # 预留，需固件配合或 EMQX 规则引擎
    "mac_address": "",              # 预留
    "timestamp": "2026-06-10T10:58:29+08:00",
    "payload": {
        # 根据 topic 不同填充不同字段
    }
}
```

### 4.3 设备心跳与离线检测

- 收到 `heartbeat` → 更新 `Device.last_heartbeat = now()`，`is_online = True`
- 后台定时任务（Celery / 线程）每 60 秒扫描：
  - `last_heartbeat < now() - 90s` → `is_online = False`
  - 向前端广播 `device_offline` 事件

---

## 5. WebSocket 服务升级规划

### 5.1 Channel Layer：Redis（必须）

```python
# settings.py
CHANNEL_LAYERS = {
    "default": {
        "BACKEND": "channels_redis.core.RedisChannelLayer",
        "CONFIG": {
            "hosts": [("127.0.0.1", 6379)],
        },
    },
}
```

### 5.2 Consumer 升级

```python
class DashboardConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        await self.channel_layer.group_add("mqtt_group", self.channel_name)
        await self.channel_layer.group_add("device_status", self.channel_name)
        await self.accept()
        # 发送当前全量设备状态快照
        await self.send_device_snapshot()

    async def receive(self, text_data):
        # 接收前端指令：急停、任务下发、设备解锁
        data = json.loads(text_data)
        action = data.get("action")
        if action == "emergency_stop":
            await self.handle_estop(data)
        elif action == "resume_device":
            await self.handle_resume(data)
        elif action == "dispatch_task":
            await self.handle_task_dispatch(data)
```

### 5.3 前端 WebSocket 服务封装

```javascript
// services/websocket.js
class WebSocketService {
    constructor(url) {
        this.url = url;
        this.reconnectInterval = 3000;
        this.maxReconnectInterval = 30000;
        this.listeners = new Map();
        this.connect();
    }
    
    connect() { /* 自动重连 + 指数退避 */ }
    
    subscribe(eventType, callback) {
        this.listeners.set(eventType, callback);
    }
    
    send(action, payload) { /* 向前端发送指令 */ }
}
```

---

## 6. API 设计（预留）

### 6.1 设备管理

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/devices/` | 设备列表（含在线状态） |
| POST | `/api/v1/devices/` | 注册新设备 |
| GET | `/api/v1/devices/{id}/` | 设备详情 |
| PATCH | `/api/v1/devices/{id}/` | 更新设备别名、注册状态 |
| DELETE | `/api/v1/devices/{id}/` | 注销设备 |

### 6.2 急停与恢复

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/devices/emergency_stop/` | 急停（body: `{device_ids: [], scope: "single"}`） |
| POST | `/api/v1/devices/resume/` | 恢复设备任务下发（body: `{device_ids: []}`） |
| GET | `/api/v1/devices/emergency_stop_log/` | 急停历史记录 |

### 6.3 任务下发

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/devices/dispatch_task/` | 向指定设备下发电机任务 |
| POST | `/api/v1/devices/dispatch_batch/` | 批量任务（相同参数发给多台设备） |

### 6.4 遥测查询

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/devices/{id}/telemetry/latest/` | 最新遥测快照 |
| GET | `/api/v1/devices/{id}/telemetry/history/` | 遥测历史（分页） |

---

## 7. 前端 Dashboard 重构规划

### 7.1 布局

```
┌─────────────────────────────────────────────────────────────┐
│  Header: 全局统计 (Total / Online / Busy / E-Stopped)       │
├──────────────────────────────┬──────────────────────────────┤
│                              │  STOP Operator Rail          │
│  Device Status Board         │  ┌────────────────────────┐  │
│  ┌────────────────────────┐  │  │ [急停选中]  [全部急停]  │  │
│  │ ☑ esp32_1  在线  空闲  │  │  └────────────────────────┘  │
│  │    ▼ 展开：电机0 转速   │  │                              │
│  │            电机1 转速   │  │  Runbook & Context          │
│  │            温度: N/A    │  │                              │
│  ├────────────────────────┤  │                              │
│  │ ☐ esp32_2  离线  --    │  │                              │
│  ├────────────────────────┤  │                              │
│  │ ☑ esp32_3  在线  任务中 │  │                              │
│  │    ▼ 展开：进度条 剩余   │  │                              │
│  └────────────────────────┘  │                              │
│                              │                              │
├──────────────────────────────┴──────────────────────────────┤
│  Realtime Event Stream (可折叠)                             │
│  [10:58:29] esp32_1 任务开始  Motor0 3000rpm 10s            │
│  [10:58:30] esp32_1 遥测    Motor0 PCNT=2985                │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 交互细节

- **表格行展开**：点击设备行展开遥测面板，显示 4 路电机 PWM/PCNT、任务进度条、温度（预留）。
- **多选复选框**：表格首列复选框，选中后右侧操作面板激活「急停选中」/「全部急停」。
- **急停确认**：点击急停后按钮变为加载状态，等待后端返回成功/失败。
- **设备锁定状态**：E-Stopped 的设备行显示红色边框，任务下发按钮对该设备禁用，直到手动恢复。
- **WebSocket 状态条**：页面顶部显示 WebSocket 连接状态（绿色/红色）和 MQTT Broker 连接状态。

---

## 8. 急停与恢复闭环设计

### 8.1 软急停（不改固件阶段）

```
用户点击急停
    ↓
前端发送 WebSocket 指令 {action: "emergency_stop", device_ids: ["esp32_1", "esp32_2"]}
    ↓
Django Consumer 接收 → 调用 mqtt.py 的 emergency_stop()
    ↓
向每个设备发送：cmd_0_0_0, cmd_1_0_0, cmd_2_0_0, cmd_3_0_0（所有电机 speed=0）
    ↓
更新 Device.task_status = "estopped", 记录 EmergencyStopLog
    ↓
向前端广播 {type: "device_estopped", device_id: "esp32_1", timestamp: "..."}
    ↓
前端锁定该设备的任务下发按钮，显示红色警告
```

### 8.2 恢复运行

```
用户点击恢复
    ↓
前端发送 WebSocket 指令 {action: "resume_device", device_ids: ["esp32_1"]}
    ↓
后端更新 Device.task_status = "idle"
    ↓
向前端广播 {type: "device_resumed", device_id: "esp32_1"}
    ↓
前端解锁该设备的任务下发按钮
```

### 8.3 硬急停（未来固件升级后）

```
后端向 esp32_1/control 发送：{"action": "estop", "source": "dashboard", "reason": "..."}
    ↓
ESP32 解析 JSON：
  - 立即将所有 motor_speed_list[i] = 0
  - pwm_set_duty(8191, i) 对所有电机
  - 停止加热器（若存在）
  - 发布 estop_ack 到 esp32_1/estop/status
    ↓
后端收到 estop_ack → 更新状态 → 通知前端
```

---

## 9. 部署架构演进

### 9.1 当前（方案 A）

```
单进程 Daphne + InMemoryChannelLayer
SQLite3
独立 task_manager.py 进程
```

### 9.2 未来（方案 B 完整版）

```
Nginx (80/443)
  ├─ /api/ → Daphne Worker 1-N（负载均衡）
  ├─ /ws/  → Daphne（WebSocket 长连接）
  └─ /static/ → Nginx 本地文件

Daphne Workers → Redis Channel Layer
Django ORM → PostgreSQL / MySQL
Celery Beat → 替代 task_manager.py
Celery Worker → 异步任务队列
Redis → Channel Layer + Celery Broker + 缓存
```

### 9.3 环境变量化

```bash
# .env
SECRET_KEY=xxx
DEBUG=False
ALLOWED_HOSTS=platform-g2.lab.local
MQTT_SERVER=192.168.233.100
MQTT_PORT=1883
MQTT_USER=Django
MQTT_PASSWORD=xxx
REDIS_URL=redis://127.0.0.1:6379/0
DATABASE_URL=postgresql://user:pass@localhost/django
```

---

## 10. 与方案 A 的接口对照

| 能力 | 方案 A（当前实施） | 方案 B（本文档） |
|---|---|---|
| 设备模型 | ✅ `Device` 已创建，基础字段 | 扩展 `DeviceTelemetry`、`EmergencyStopLog` |
| Topic 订阅 | `esp32_+/+`（通配符） | `esp32/{id}/+` + EMQX 系统 Topic |
| 多设备区分 | Topic 名提取 `device_id` | Topic + MAC 地址双重确认 |
| 心跳检测 | 内存字典记录最后心跳 | Celery 定时任务 + 数据库状态持久化 |
| Channel Layer | `InMemoryChannelLayer` | `RedisChannelLayer` |
| WebSocket 服务 | 页面内原生 WebSocket | 全局 `services/websocket.js` 单例 |
| 急停 | 软急停（speed=0） | 软急停 + 硬急停（JSON 协议） |
| 任务进度 | 前端倒计时 | 前端倒计时 + 固件真实进度上报 |
| 温度显示 | 预留 UI，显示 N/A | 真实数据（固件上报 + 后端存储） |
| task_manager.py | 保持独立进程 | 整合为 Celery Beat / Management Command |
| 部署 | 单进程 Daphne | Nginx + 多 Daphne + PostgreSQL + Redis |

---

## 11. 实施路线图建议

```
Phase 1（当前）: 方案 A 最小修复
  ├── 统一 Broker 配置
  ├── 创建 Device 模型 + 注册表 API
  ├── Dashboard.vue 接入 WebSocket + 展开面板 + 急停按钮
  ├── 软急停 API + 设备锁定机制
  └── 前端 WebSocket 自动重连

Phase 2: 多设备适配
  ├── ESP32 固件更新：动态 device_id、Payload 携带 MAC
  ├── 后端：MAC 地址解析、设备自动发现
  └── Dashboard：批量勾选、分组下发

Phase 3: 协议升级
  ├── ESP32 固件：JSON 结构化协议（急停、进度、温度）
  ├── 后端：Legacy 解析器逐步退役
  └── 前端：遥测曲线图、历史回放

Phase 4: 部署与运维
  ├── 切换 Redis Channel Layer
  ├── 数据库迁移至 PostgreSQL
  ├── task_manager.py → Celery
  └── Nginx + HTTPS + 环境变量化
```

---

*文档生成时间：2026-06-10*
*对应代码库版本：Platform G2 当前 HEAD*
