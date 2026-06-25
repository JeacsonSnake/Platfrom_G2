# 后端 MQTT 连接状态感知与设备任务生命周期重构实现记录

**日期**: 2026-06-25  
**分支**: `main`  
**任务描述**: 实现后端 MQTT 与 Broker 的连接状态感知、任务下发前置检查、任务完成/异常确认、手动重连，以及前端状态提示与连接状态栏重构。

---

## 1. 背景

### 1.1 历史问题回顾

在 2026-06-15 完成的前端模块化与 MAC-Based Topic 重构之后，Dashboard 已能实时展示设备状态并执行急停/恢复操作，但系统对“后端 ↔ MQTT Broker”这一段的连通性缺乏感知：

- 后端 MQTT 客户端断线时，前端无法及时获知，导致用户继续下发指令后出现静默失败。
- 任务下发入口（单电机任务、批量任务、`BatchJob`、Communication API、`task_manager.py`）没有统一检查设备是否在线或是否处于 busy/error/completed/estopped 状态。
- 设备任务进入 `completed` 或 `error` 后，没有“用户确认”机制，会阻塞后续任务下发，但界面上缺少明确引导。
- 前端使用 Bulma 内联横幅提示 MQTT 状态，难以复用，且中文提示与近期英文 UI 方向不一致。
- `ConnectionBar` 早期通过 `mqttAvailable` 与 `mqttConnected` 两个 prop 同时控制，状态语义不统一。

### 1.2 本次任务目标

1. 后端实时追踪 MQTT 与 Broker 的连接状态，并通过 WebSocket + REST 暴露给前端。
2. 所有任务下发入口统一进行设备在线与空闲状态前置检查。
3. 设备任务完成后进入 `completed`，异常/心跳超时时进入 `error`，必须经用户确认（acknowledge）后才能恢复为 `idle`。
4. 提供手动 MQTT 重连能力（REST 接口 + 前端刷新按钮）。
5. 使用 Element Plus `ElMessage` 重构 MQTT 状态提示，成功提示 5 秒自动关闭，失败提示持久显示并带重连按钮。
6. `ConnectionBar` 拆分为两个独立子项分别展示 WebSocket 与 MQTT 状态，统一使用英文用户文本。
7. 补充并更新测试与 `AGENTS.md` 文档。

---

## 2. 需求分析

| 需求项 | 说明 |
|--------|------|
| MQTT 连接状态感知 | 后端维护 `_mqtt_connection_state`，在 `on_connect`/`on_disconnect` 及看门狗线程中更新，变化时广播 `mqtt_connection_status`。 |
| 任务下发前置检查 | 所有下发入口调用 `can_dispatch_to_device()`，设备 offline/busy/estopped/error/completed 时拒绝下发并返回明确原因。 |
| 任务完成/异常确认 | 设备正常结束 → `completed`；心跳超时/异常中止 → `error`；用户调用 `acknowledge_device()` 后恢复 `idle`。 |
| 手动 MQTT 重连 | 新增 `POST /api/mqtt/reconnect/` 与 WebSocket `acknowledge_device`/`get_snapshot` 动作；重连最多等待 3 秒握手。 |
| 前端通知重构 | 新增 `mqttMessage.js` 服务，封装 `ElMessage`，失败提示带 `Reconnect` 按钮。 |
| 连接状态栏重构 | `ConnectionBar` 改为两个独立 `.connection-item`，分别展示 WebSocket 与 MQTT，样式按 `connected/connecting/disconnected` 区分。 |
| 国际化 | Dashboard、ConnectionBar、OperatorRail 等组件的用户可见文本改为英文。 |

---

## 3. 实现方案

### 3.1 新增文件

| 文件 | 说明 |
|------|------|
| `vue_frontend/src/services/mqttMessage.js` | Element Plus `ElMessage` 封装：成功自动关闭，失败持久显示刷新按钮。 |
| `vue_frontend/src/components/dashboard/OperatorRail.vue` | 右侧操作栏组件，新增 **Acknowledge Selected** 按钮。 |
| `vue_frontend/src/components/dashboard/DeviceStatusTable.vue` | 设备状态表格组件，支持复选、展开遥测、状态标签。 |
| `vue_frontend/src/__tests__/ui/ConnectionBar.spec.js` | ConnectionBar 单元测试。 |
| `vue_frontend/src/services/mqttMessage.test.js` | `mqttMessage.js` 单元测试。 |

### 3.2 修改文件

| 文件 | 主要变更 |
|------|----------|
| `django_backend/main_page/mqtt.py` | 新增 `_mqtt_connection_state`、`_set_mqtt_connection_state()`、`get_mqtt_connection_state()`、看门狗线程；新增 `is_device_online()`、`can_dispatch_to_device()`、`NON_DISPATCHABLE_STATUSES`、`_abort_device_task()`、`acknowledge_device()`、`reconnect_mqtt_client()`；`dispatch_motor_task()` 增加前置检查。 |
| `django_backend/main_page/views.py` | 新增 `mqtt_reconnect()`、`device_acknowledge()`；`device_list()` 返回 `mqtt_connected`；`device_dispatch_task()` / `device_dispatch_batch()` 按 offline/busy/error/completed/estopped 返回 503/409；`batch_job_start()` 每步检查设备可用性。 |
| `django_backend/django_backend/consumers.py` | 新增 `send_mqtt_connection_status()`、`handle_acknowledge_device()`；连接成功时推送快照 + MQTT 状态。 |
| `django_backend/task_manager.py` | 调度前查询 `main_page_device`，offline 或非 idle 时跳过该设备。 |
| `django_backend/main_page/models.py` | `Device.task_status` choices 增加 `error`、`completed`；`EmergencyStopLog` 增加确认相关字段。 |
| `django_backend/main_page/urls.py` / `urls_v1.py` | 增加 `^mqtt/reconnect/$`、`^devices/acknowledge/$` 路由。 |
| `django_backend/main_page/tests.py` | 增加设备在线 mock、MQTT 重连与任务生命周期断言，共 17 个测试。 |
| `vue_frontend/src/views/Dashboard.vue` | 接入 `mqtt_connection_status`、`acknowledgeDevices()`、`refreshMqttConnection()`，用户文本英文化。 |
| `vue_frontend/src/components/ui/ConnectionBar.vue` | 拆分为两个独立 `.connection-item`，移除 `mqttAvailable`，统一 `mqttConnected`/`mqttStatus`。 |
| `vue_frontend/src/services/websocket.js` | 连接后发送 `get_snapshot`，并每 25 秒发送一次作为心跳。 |
| `vue_frontend/src/services/api/devices.js` | 新增 `mqttReconnect()`、`acknowledge()`。 |
| `AGENTS.md` | 在 §11 中补充 MQTT 状态感知、生命周期检查、确认机制、手动重连、前端提示重构等说明。 |

---

## 4. 关键机制

### 4.1 后端 MQTT 连接状态追踪

```python
# django_backend/main_page/mqtt.py
_mqtt_connection_state = {'connected': False, 'since': None, 'reason': ''}
_mqtt_connection_lock = threading.Lock()

NON_DISPATCHABLE_STATUSES = ('busy', 'estopped', 'error', 'completed', 'offline')


def _set_mqtt_connection_state(connected, reason=''):
    """设置后端 MQTT 与 Broker 的连接状态，状态变化时向 WebSocket 广播。"""
    global _mqtt_connection_state
    with _mqtt_connection_lock:
        previous = _mqtt_connection_state.get('connected')
        if previous == connected:
            return
        now = timezone.now()
        _mqtt_connection_state = {
            'connected': connected,
            'since': now.isoformat(),
            'reason': reason,
        }
    _broadcast('mqtt_connection_status', {
        'connected': connected,
        'reason': reason,
        'since': _mqtt_connection_state['since'],
    })


def get_mqtt_connection_state():
    """返回后端 MQTT 与 Broker 的连接状态副本。"""
    with _mqtt_connection_lock:
        return dict(_mqtt_connection_state)
```

`on_connect()` 成功时调用 `_set_mqtt_connection_state(True)`，`on_disconnect()` 时调用 `_set_mqtt_connection_state(False, reason=...)`。 additionally，`_mqtt_watchdog()` 每 15 秒核对 `client.is_connected()` 与内存状态，出现不一致时自动修正并广播，避免 `paho` 回调遗漏导致的 stale 状态。

### 4.2 任务下发前置检查

```python
# django_backend/main_page/mqtt.py
def can_dispatch_to_device(device_id):
    """判断是否可以向指定 device_id 下发新任务。返回 (ok, reason)。"""
    state = _ensure_device_state(device_id)
    if state is None:
        return False, 'Unknown device'
    if not state.get('is_online', False):
        return False, 'Device is offline'
    task_status = state.get('task_status', 'idle')
    if task_status in NON_DISPATCHABLE_STATUSES:
        status_label = {
            'busy': 'Device is busy',
            'estopped': 'Device is in emergency stop state. Resume before dispatch.',
            'error': 'Device has an error. Acknowledge before dispatch.',
            'completed': 'Device has a completed task. Acknowledge before dispatch.',
            'offline': 'Device is offline',
        }
        return False, status_label.get(task_status, f'Device status is {task_status}')
    return True, ''
```

`dispatch_motor_task()` 在发布前先调用 `can_dispatch_to_device()`；失败时返回 `{success: False, error: reason}`，由视图层映射为 HTTP 503（offline / MQTT 不可用）或 409（busy / estopped / error / completed）。

`BatchJob` 启动时，每个 `BatchStepExecution` 在创建 `CommandOutbox` 之前同样调用 `can_dispatch_to_device()`，失败则标记该步骤为 `FAILED` 并记录原因。

### 4.3 任务完成与异常确认

设备状态流转：

```
idle → busy            (任务下发 / 收到 task_create)
busy → completed       (收到 task_finished)
busy → error           (心跳超时 / 异常中止 / abort)
any  → estopped        (急停)
completed / error / estopped → idle   (acknowledge_device)
```

```python
# django_backend/main_page/mqtt.py
def acknowledge_device(device_ids, acknowledged_by=''):
    """用户确认设备问题已清除或任务已验收，将状态从 error/completed/estopped 恢复为 idle。"""
    results = []
    for device_id in device_ids:
        state = _ensure_device_state(device_id)
        if state is None:
            results.append({'device_id': device_id, 'success': False, 'error': 'Unknown device'})
            continue

        task_status = state.get('task_status', 'idle')
        if task_status not in ('error', 'completed', 'estopped'):
            results.append({
                'device_id': device_id,
                'success': False,
                'error': f'Device status is {task_status}, no acknowledgement required.',
            })
            continue

        _set_device_task_status(device_id, 'idle', current_task={})
        results.append({'device_id': device_id, 'success': True, 'task_status': 'idle'})
    return results
```

### 4.4 手动 MQTT 重连

```python
# django_backend/main_page/mqtt.py
def reconnect_mqtt_client():
    """手动重连 MQTT Broker；供前端刷新按钮或管理接口调用。"""
    global client
    try:
        if client is not None and client.is_connected():
            return {'success': True, 'connected': True, 'message': 'Already connected'}

        if client is not None:
            client.reconnect()
        else:
            client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)
            client.on_connect = on_connect
            client.on_disconnect = on_disconnect
            client.on_message = on_message
            client.username_pw_set(settings.MQTT_USER, settings.MQTT_PASSWORD)
            client.connect(
                host=settings.MQTT_SERVER,
                port=settings.MQTT_PORT,
                keepalive=settings.MQTT_KEEPALIVE
            )
            client.loop_start()

        deadline = time.time() + 3
        while time.time() < deadline:
            if client.is_connected():
                break
            time.sleep(0.1)

        connected = client.is_connected()
        message = 'MQTT connected' if connected else 'MQTT reconnection in progress, please wait'
        return {'success': True, 'connected': connected, 'message': message}
    except Exception as exc:
        print(f'MQTT reconnect failed: {exc}')
        return {'success': False, 'connected': False, 'error': str(exc)}
```

```python
# django_backend/main_page/views.py
@api_view(['POST'])
def mqtt_reconnect(request):
    """手动触发后端 MQTT 客户端重连 Broker，并主动向所有 WebSocket 客户端广播当前状态。"""
    result = reconnect_mqtt_client()
    try:
        _broadcast('mqtt_connection_status', get_mqtt_connection_state())
    except Exception as exc:
        print(f'Broadcast mqtt_connection_status failed: {exc}')
    if not result.get('success'):
        return Response(result, status=status.HTTP_503_SERVICE_UNAVAILABLE)
    if result.get('connected'):
        return Response(result, status=status.HTTP_200_OK)
    return Response(result, status=status.HTTP_202_ACCEPTED)
```

### 4.5 前端 MQTT 状态提示服务

```javascript
// vue_frontend/src/services/mqttMessage.js
export function showMqttMessage({ connected, text, onRefresh }) {
  closeCurrent()

  if (connected) {
    currentMessageInstance = ElMessage({
      message: text,
      type: 'success',
      duration: 5000,
      showClose: true,
      center: true,
      offset: 16,
    })
    return
  }

  const content = h(
    'div',
    { class: 'mqtt-message-content is-flex is-align-items-center is-justify-content-space-between' },
    [
      h('span', { class: 'mqtt-message-text' }, text),
      h(
        'button',
        {
          class: 'button is-small is-outlined is-white ml-3',
          onClick: () => { if (typeof onRefresh === 'function') onRefresh() },
        },
        'Reconnect'
      ),
    ]
  )

  currentMessageInstance = ElMessage({
    message: content,
    type: 'error',
    duration: 0,
    showClose: false,
    center: true,
    offset: 16,
    customClass: 'mqtt-message--error',
  })
}
```

### 4.6 ConnectionBar 重构

```vue
<!-- vue_frontend/src/components/ui/ConnectionBar.vue -->
<template>
  <div class="connection-bar">
    <div class="connection-item" :class="wsItemClass">
      <span class="connection-dot"></span>
      <span class="connection-label">{{ wsLabel }}</span>
    </div>
    <div class="connection-item" :class="mqttItemClass">
      <span class="connection-dot"></span>
      <span class="connection-label">{{ mqttLabel }}</span>
    </div>
  </div>
</template>

<script>
export default {
  props: {
    status: { type: String, default: 'disconnected' },
    mqttConnected: { type: [Boolean, null], default: null },
    mqttStatus: { type: String, default: null }
  },
  // ...
}
</script>
```

使用方式：

```vue
<ConnectionBar :status="wsStatus" :mqtt-connected="backendMqttConnected" />
```

---

## 5. 接口与协议

### 5.1 REST API

| 方法 | Legacy 路径 | v1 路径 | 说明 |
|------|------------|---------|------|
| POST | `/api/mqtt/reconnect/` | `/api/v1/mqtt/reconnect/` | 手动触发后端 MQTT 重连 |
| GET  | `/api/device_list/` | `/api/v1/devices/` | 返回设备列表及 `mqtt_connected` |
| POST | `/api/devices/acknowledge/` | — | 确认设备，恢复为 `idle` |
| POST | `/api/devices/emergency_stop/` | — | 软急停 |
| POST | `/api/devices/resume/` | — | 恢复急停锁定 |
| POST | `/api/devices/dispatch_task/` | — | 单电机任务（带前置检查） |
| POST | `/api/devices/dispatch_batch/` | — | 批量任务（带前置检查） |

### 5.2 WebSocket 广播主题

| 主题 | 来源 | 载荷 |
|------|------|------|
| `mqtt_connection_status` | `mqtt._broadcast()` / `consumers.send_mqtt_connection_status()` | `{ connected, since, reason }` |
| `device_status` | `_set_device_task_status()` / 心跳超时 | `{ device_id, payload: { event, is_online, task_status, reason } }` |
| `device_snapshot` | `consumers.send_snapshot()` | 完整 `_device_states` 快照 |
| `acknowledge_result` | `consumers.handle_acknowledge_device()` | `{ topic: 'acknowledge_result', results: [...] }` |

### 5.3 WebSocket 客户端动作

| 动作 | 说明 |
|------|------|
| `emergency_stop` | 急停指定/全部设备 |
| `resume_device` | 恢复指定设备 |
| `dispatch_task` | 通过 WebSocket 下发电机任务 |
| `acknowledge_device` | 确认设备完成/异常 |
| `get_snapshot` | 请求当前设备快照与 MQTT 状态 |

---

## 6. 测试验证

### 6.1 后端测试

```bash
cd django_backend
python manage.py test
```

输出：

```text
Found 17 test(s).
Creating test database for alias 'default'...
System check identified no issues (0 silenced).
................
----------------------------------------------------------------------
Ran 17 tests in 0.063s

OK
Destroying test database for alias 'default'...
```

### 6.2 前端测试

```bash
cd vue_frontend
npm run test:run
```

输出：

```text
Test Files  14 passed (14)
     Tests  49 passed (49)
  Duration  2.16s
```

### 6.3 生产构建

```bash
cd vue_frontend
npm run build
```

构建成功，输出至 `dist/`。

---

## 7. Git 提交记录

```text
8935b32 refactor(frontend): ConnectionBar 使用独立子 div 分别展示 WebSocket 与 MQTT 状态
9f2f1e0 docs: 说明 ConnectionBar 移除 mqttAvailable 并统一使用 mqttConnected
d40e3a9 refactor(frontend): 统一 ConnectionBar 状态显示并将中文用户文本改为英文
41ffeda docs: 更新 MQTT 状态提示使用 element-plus ElMessage 的说明
896e1fd refactor(frontend): 使用 element-plus ElMessage 重构 MQTT 状态提示为可复用服务
c8807aa fix(frontend): MQTT 重连按钮根据接口响应立即更新状态并轮询兜底
4883fc5 fix(backend): MQTT 重连后主动广播状态并等待连接建立
2ad98d7 docs: 更新 AGENTS.md 说明 MQTT 手动重连与刷新按钮
57d76dd feat(backend): 新增 MQTT 手动重连接口 /api/mqtt/reconnect/
68b3aca feat(frontend): MQTT 断开横幅增加刷新连接按钮
796c9e5 docs: 在 AGENTS.md 中补充 MQTT 状态感知与任务生命周期变更记录
11120c6 feat(backend): 后端 MQTT 连接状态感知与设备任务生命周期前置检查
2f9834a feat(frontend): 增加 MQTT 连接状态横幅与设备任务确认交互
```

---

## 8. 使用说明

### 8.1 启动依赖

```bash
# 1. 启动 EMQX（确保 broker 在 192.168.233.100:1883 可达）

# 2. 启动 Django ASGI 服务器（支持 WebSocket）
cd django_backend
daphne -b 0.0.0.0 -p 8000 django_backend.asgi:application

# 3. 启动独立任务调度器
cd django_backend
python task_manager.py

# 4. 启动前端
cd vue_frontend
npm run dev
```

### 8.2 验证 MQTT 状态

1. 打开 Dashboard，顶部 `ConnectionBar` 应显示：
   - WebSocket Connected（绿色）
   - MQTT Connected（绿色）或 MQTT Disconnected（红色）
2. 断开 EMQX 网络，等待约 15 秒，看门狗会广播 `mqtt_connection_status`，顶部出现红色 `MQTT Disconnected` 提示，并显示 **Reconnect** 按钮。
3. 点击 **Reconnect**，后端调用 `reconnect_mqtt_client()`，最多等待 3 秒。成功后显示绿色 `MQTT reconnected` 提示，5 秒自动关闭。

### 8.3 验证任务确认流程

1. 向 `esp32_1` 下发一个 10 秒电机任务：`POST /api/devices/dispatch_task/`。
2. 任务执行期间设备状态为 `busy`；再次下发会返回 409 `Device is busy`。
3. 任务完成后，后端收到 `task_finished` 进入 `completed`；再次下发会返回 409 `Device has a completed task. Acknowledge before dispatch.`。
4. 在 Dashboard 选中该设备，点击 **Acknowledge Selected**，状态恢复为 `idle`，可继续下发。

---

## 9. 已知限制与后续建议

### 9.1 已知限制

- **`InMemoryChannelLayer` 限制**：WebSocket 广播与 `_device_states` 内存状态仅能在单 Daphne worker 内可靠工作。多 worker 部署时需迁移到 Redis Channel Layer。
- **`task_manager.py` 状态滞后**：独立调度器直接查询 SQLite 中的 `Device.is_online` / `task_status`，其状态刷新依赖后端 MQTT 进程写入数据库，可能存在秒级延迟。
- **仅支持手动重连**：Django 启动时若 MQTT Broker 不可达，`client` 会被置为 `None`，当前不会自动重试，需要用户点击刷新按钮。
- **多设备区分仍依赖固件**：ESP32 当前仍以 `esp32_1/...` 发布，多设备场景需要后续固件在 payload 中携带 MAC 地址或按 `esp32_N/...` 发布。
- **急停为软急停**：ESP32 固件没有硬件急停逻辑，当前通过发送 `cmd_<motor>_0_0` 停止电机，并在后端/前端锁定任务下发。

### 9.2 后续建议

| 方向 | 建议 |
|------|------|
| 部署架构 | Django Channels 切换到 `channels_redis`，支持多 ASGI worker。 |
| 调度器 | 将 `task_manager.py` 改为 Django Management Command 或 Celery Beat，统一使用内存状态。 |
| 自动重连 | 在 `_mqtt_watchdog()` 中检测断线后自动调用 `reconnect()`，并提供指数退避。 |
| 固件协议 | 设备在任务完成/异常时发送结构化 JSON 信封，后端减少超时推断。 |
| 安全 | 将 MQTT/EMQX 凭据、Django `SECRET_KEY`、JWT 密钥迁移至环境变量。 |

---

## 10. 参考链接

- [paho-mqtt 2.x Client API](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)
- [Django Channels - Channel Layers](https://channels.readthedocs.io/en/stable/topics/channel_layers.html)
- [Element Plus ElMessage](https://element-plus.org/en-US/component/message.html)
- 项目根目录 `AGENTS.md` §11 近期架构变更记录
- 同目录早期文档：
  - `2026-06-15_Frontend_Component_Refactor_README.md`
  - `2026-06-15_MAC-Based-Dynamic-Device-ID-and-MQTT-Topic-Refactor_README.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-06-25  
**完成时间**: 2026-06-25
