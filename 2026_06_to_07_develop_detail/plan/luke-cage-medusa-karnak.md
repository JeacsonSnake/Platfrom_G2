# 实施计划：MQTT 连接状态感知 + 设备任务生命周期前置检查

## 1. 目标与范围

基于已确认的需求，本次修订围绕两个核心目标：

1. **后端 MQTT 与 Broker 的连接状态可感知、可推送**
   - 后端在 MQTT 上线/掉线时，通过 WebSocket 向所有在线前端广播。
   - 前端使用现有 Bulma 样式实现非阻塞顶部横幅：
     - 掉线：持久显示 `is-danger` 警告横幅。
     - 上线：自动消失或 3 秒后自动关闭的 `is-success` 提示横幅。

2. **任务下发前完成“设备在线 / 空闲”检查，并在任务执行期间监测异常**
   - 下发前：检查目标设备是否在线、是否空闲（无进行中任务）。
   - 下发后：持续监测心跳；若设备在任务期间掉线，则中止任务并进入待确认状态。
   - 异常后：设备状态变为 `error`，必须用户确认问题清除后才恢复 `idle`。
   - 任务完成：设备状态变为 `completed`，必须用户验收确认后才恢复 `idle`。

范围覆盖 `task_manager.py` 定时任务、`device_dispatch_task` / `device_dispatch_batch`、`batch_job_start`、Communication API（topic/service/action）。

## 2. 方案概述

采用**最小侵入、单进程内存状态为主**的方案：

- 后端在 `mqtt.py` 中新增一个全局 `_mqtt_connection_state`，由 `on_connect` / `on_disconnect` /  watchdog 线程共同维护，并通过 `_broadcast('mqtt_connection_status', ...)` 推送。
- 复用现有的 `_device_states` 内存表，新增 `is_device_online(device_id)` 与 `can_dispatch_to_device(device_id)` 两个工具函数，作为所有下发入口的统一判据。
- 扩展 `Device.task_status` 枚举，新增 `error`（异常待确认）和 `completed`（完成待验收）。
- 任务完成时不再自动回到 `idle`，而是进入 `completed`；设备掉线/异常时进入 `error`；均通过新增 WebSocket 动作 `acknowledge_device` 恢复为 `idle`。
- 同步顺手修复 `_offline_detector` 当前已存在的“持锁调用 `_mark_device_offline`”潜在死锁问题。

## 3. 后端改动

### 3.1 `django_backend/main_page/mqtt.py`

#### A. 后端 MQTT 自身连接状态

- 新增全局变量与锁：
  ```python
  _mqtt_connection_state = {'connected': False, 'since': None}
  _mqtt_connection_lock = threading.Lock()
  ```
- 新增 `_set_mqtt_connection_state(connected: bool, reason='')`：
  - 更新 `_mqtt_connection_state`。
  - 当状态变化时，调用 `_broadcast('mqtt_connection_status', {'connected': connected, 'reason': reason, 'since': ...})`。
- 新增 `on_disconnect(client, userdata, rc)`：调用 `_set_mqtt_connection_state(False, reason=f'disconnect_rc_{rc}')`。
- 修改 `on_connect`：在订阅 topic 成功后调用 `_set_mqtt_connection_state(True)`。
- 新增 `_mqtt_watchdog()` 守护线程（每 5 秒）：
  - 比对 `client.is_connected()` 与内存状态，发现不一致时触发状态更新与广播，作为回调的兜底。
- 新增导出函数 `get_mqtt_connection_state()`，供 WebSocket `connect()` 与 REST API 使用。

#### B. 设备在线 / 可下发检查工具函数

- 新增 `is_device_online(device_id)`：
  - 读取 `_device_states`，返回 `state.get('is_online', False)`。
- 新增 `can_dispatch_to_device(device_id)`：
  - 返回 `(ok: bool, reason: str)`。
  - 检查顺序：设备是否存在 → 是否在线 → `task_status` 是否为 `idle`。
  - 若状态为 `busy` / `estopped` / `error` / `completed` / `offline`，返回对应 reason。

#### C. 任务状态扩展与异常处理

- 修改 `dispatch_motor_task(device_id, ...)`：
  1. 调用 `can_dispatch_to_device(device_id)`，不满足直接返回 `{'success': False, 'error': reason}`。
  2. 满足后设置 `state['task_status'] = 'busy'`、`state['current_task'] = {...}` 并同步数据库。
  3. 发布 MQTT 指令。
  4. 返回 `success: True`。
- 新增 `_set_device_task_status(device_id, new_status, current_task=None, reason='')`：
  - 统一更新内存与数据库 `Device.task_status` / `current_task`，并广播 `device_status` 事件。
- 新增 `_abort_device_task(device_id, reason, triggered_by='system')`：
  - 将状态设为 `error`，清空 `current_task`，广播 `device_status` 事件 `event: 'aborted'`。
  - 向设备发送四路电机停止指令（软停止）。
- 新增 `acknowledge_device(device_ids, acknowledged_by='')`：
  - 仅当 `task_status in ('error', 'completed', 'estopped')` 时，恢复为 `idle`。
  - 更新数据库并广播 `device_status` 事件 `event: 'acknowledged'`。
- 修改 `on_message` 中的 `task_finished` 分支：
  - 不再直接设为 `idle`，而是调用 `_set_device_task_status(device_id, 'completed', {})`。
  - 广播 `task_status` 事件 `event: 'task_completed_pending_ack'`。
- 修改 `_offline_detector`：
  - 修复潜在死锁：遍历 `_device_states` 时收集需标记离线的设备 ID，释放锁后再逐个调用 `_mark_device_offline`。
  - 当发现 `is_online=True` 设备超时时，除标记离线外，若其 `task_status == 'busy'`，额外调用 `_abort_device_task(..., reason='heartbeat_timeout')`。
- 顺手将 `_update_device_heartbeat` / `_mark_device_offline` 中的数据库写操作尽量移到锁外，避免阻塞 MQTT 消息线程。

### 3.2 `django_backend/django_backend/consumers.py`

- 导入新增函数：
  ```python
  from main_page.mqtt import (
      emergency_stop, resume_devices, dispatch_motor_task, get_device_states,
      get_mqtt_connection_state, acknowledge_device,
  )
  ```
- `connect()`：在 `send_snapshot()` 之后，立即发送一次 `mqtt_connection_status` 消息：
  ```python
  await self.send(text_data=json.dumps({
      'type': 'mqtt_msg_broadcast',
      'topic': 'mqtt_connection_status',
      'payload': get_mqtt_connection_state(),
  }))
  ```
- `receive()` 增加 `acknowledge_device` 分支：
  - 读取 `device_ids`、`acknowledged_by`，调用 `sync_to_async(acknowledge_device, ...)`。
  - 向前端返回 `topic: 'acknowledge_result'`。

### 3.3 `django_backend/main_page/views.py`

- `device_list()`：
  - 在返回体中增加 `mqtt_connected` 字段（使用 `get_mqtt_connection_state()` 或 `mqtt_client_available()`）。
- `device_dispatch_task()` / `device_dispatch_batch()`：
  - 继续调用 `dispatch_motor_task()`，由后者统一完成在线/空闲检查。
  - 对错误返回合适的 HTTP 状态：离线/忙 → `409 CONFLICT` 或 `503 SERVICE UNAVAILABLE`。
- `_queue_transport_message(..., device=None, ...)`：
  - 若 `device` 参数包含 `device_id`（或 `id`），调用 `can_dispatch_to_device(device_id)`。
  - 若不可下发，将 `outbox.status` 设为 `FAILED`，`dispatch_error` 写入原因，不再调用 `publish_device_command`。
- `batch_job_start()`：
  - 对每个 `step_execution` 解析出目标 `device_id`（从 `dispatch['device']` 或默认 `MQTT_DEFAULT_DEVICE_ID`）。
  - 调用 `can_dispatch_to_device(device_id)`，不满足时将该 step 标记为 `FAILED` 并记录错误，继续处理后续 step（与现有失败处理一致）。
- 新增 REST 端点（可选，作为 WebSocket 的兜底）：
  - `POST /api/devices/acknowledge/`：接受 `device_ids`、`acknowledged_by`，调用 `acknowledge_device()`。

### 3.4 `django_backend/task_manager.py`

- 在 `timer_thread()` 下发指令前增加检查：
  1. `self.mqtt_client.is_connected()` 是否为真。
  2. 通过已打开的 `sqlite3` 连接查询 `main_page_device` 表：
     - `SELECT is_online, task_status FROM main_page_device WHERE device_id = ?`
  3. 仅当 `is_online = 1` 且 `task_status = 'idle'` 时才下发；否则打印错误并发布到 `task_manager` topic，跳过本次触发。
- 在 `mqtt_init()` 中增加 `on_disconnect` 回调，掉线时打印/发布告警。

### 3.5 `django_backend/main_page/models.py`

- 扩展 `DEVICE_STATUS_CHOICES`：
  ```python
  DEVICE_STATUS_CHOICES = (
      ('idle', 'Idle'),
      ('busy', 'Busy'),
      ('estopped', 'E-Stopped'),
      ('offline', 'Offline'),
      ('error', 'Error'),
      ('completed', 'Completed'),
  )
  ```
- 由于仅修改 choices 不影响数据库 schema，通常无需生成迁移；若 Django 提示需迁移，执行 `makemigrations`。

## 4. 前端改动

### 4.1 `vue_frontend/src/views/Dashboard.vue`

#### A. MQTT 连接状态横幅

- 新增 data：`backendMqttConnected: null`、`showMqttBanner: false`、`mqttBannerType: 'danger'`、`mqttBannerText: ''`。
- 在模板 `<ConnectionBar ... />` 下方新增 Bulma 通知横幅：
  ```html
  <div v-if="showMqttBanner" class="notification" :class="mqttBannerType">
      <button class="delete" @click="showMqttBanner = false"></button>
      {{ mqttBannerText }}
  </div>
  ```
- 在 `initWebSocket()` 中订阅 `mqtt_connection_status`：
  - `connected: true`：显示 `is-success` 横幅“MQTT 已恢复连接”，3 秒后自动隐藏；更新 `backendMqttConnected = true`。
  - `connected: false`：显示 `is-danger` 横幅“MQTT 已断开，命令将无法下发”，持久显示；更新 `backendMqttConnected = false`。
- `getDeviceList()` 后使用 `result.mqtt_connected` 初始化横幅状态。

#### B. 任务完成确认与异常确认

- 在 `handleTaskStatus()` 中处理 `event: 'task_completed_pending_ack'`：
  - 更新设备 `taskStatus = 'Completed'`。
  - 在事件流中显示“任务完成，等待验收确认”。
- 在 `handleDeviceStatus()` 中处理 `event: 'aborted'`：
  - 更新设备 `taskStatus = 'Error'`、`connectionStatus = 'Offline'`。
  - 显示错误提示“设备异常，任务已中止，请确认问题后恢复”。
- 在设备表格/操作面板为 `Completed` / `Error` / `E-Stopped` 状态增加“确认恢复”按钮：
  - 点击后通过 WebSocket 发送 `{ action: 'acknowledge_device', device_ids: [...], acknowledged_by: ... }`。
- 新增 `acknowledgeDevices()` 方法，复用现有选中设备逻辑。

#### C. 任务下发失败提示

- `dispatch_result` 事件订阅中，失败时：
  - 设置 `errorMessage` 并在 5 秒后自动清空；或调用 Bulma notification 临时显示。
  - 记录到 `LiveEventStream`。
- `dispatchTaskToSelected()` 可在发送前做简单前端拦截：若选中设备 `connectionStatus !== 'Online'` 或 `taskStatus !== 'Idle'`，直接 `alert` 拒绝。

### 4.2 `vue_frontend/src/components/ui/ConnectionBar.vue`

- 新增 prop `mqttConnected: [Boolean, null]`。
- 在现有 sublabel 中扩展：显示 `MQTT 已连接 / 已断开 / 未知`。

### 4.3 状态显示映射

- 更新 `mapTaskStatus()`：
  - `'error'` → `'Error'`（红色）。
  - `'completed'` → `'Completed'`（蓝色/紫色）。
- 在 `DeviceStatusTable` 或相关样式中为 Error / Completed 增加对应颜色类（若不存在，用内联 class）。

## 5. 数据流与状态机

```
空闲 idle
  ↓ 下发任务成功
busy
  ├─ 任务正常结束 → completed ── 用户确认 ──→ idle
  ├─ 设备掉线 / 异常 → error ── 用户确认 ──→ idle
  └─ 急停触发 → estopped ── 用户恢复 ──→ idle
```

## 6. 风险与已知限制

- **多进程部署限制**：`_device_states` 与 `_mqtt_connection_state` 仍为单进程内存。若后续使用多个 Daphne worker，需要迁移到 Redis Channel Layer 或共享存储。本次方案在单 worker 场景下工作。
- **task_manager 独立进程**：它通过 SQLite 轮询 `Device.is_online`，可能滞后于 Django 主进程的内存状态，但足以避免向明显离线的设备发指令。
- **前端任务完成确认**：需要用户在 Dashboard 页面手动点击确认；若用户未操作，设备将一直停留在 `completed` 状态，阻止新任务下发。
- **异常判定目前仅依赖心跳超时**：更细粒度的设备内部错误上报（如电机堵转）需后续 ESP32 固件配合。

## 7. 测试计划

- 后端：
  - 新增/更新 `main_page/tests.py`：
    - 模拟 `_device_states`，验证 `can_dispatch_to_device()` 各分支。
    - 验证 `dispatch_motor_task()` 对离线/忙设备返回错误。
    - 验证 `_offline_detector` 标记离线时，busy 设备会进入 `error`。
- 集成（手动）：
  - 启动 Django + Daphne，关闭 EMQX，观察前端顶部横幅变为红色；重启 EMQX，观察横幅变绿并在 3 秒后消失。
  - 启动 ESP32 或模拟器，任务下发后断开设备网络，观察 Dashboard 显示 Error；点击确认后恢复 Idle。
- task_manager：
  - 将 `Device.is_online` 手动改为 0，验证定时任务跳过下发并打印日志。

## 8. 实施顺序

建议按以下顺序分步实施与验证：

1. **后端 MQTT 状态 + 前端横幅**：改动最小，先验证连接状态感知链路。
2. **设备在线/空闲检查 + 下发入口拦截**：统一工具函数 + 改造所有下发入口。
3. **任务完成/异常确认流程**：扩展 task_status、新增 acknowledge 动作、前端按钮。
4. **task_manager 适配与回归测试**。
5. **文档更新**：在 `AGENTS.md` 中补充新的 MQTT 状态广播 topic 与设备状态机说明。
