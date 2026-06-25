# 后端 MQTT 自动重连与前端状态同步实现记录

**日期**: 2026-06-25  
**分支**: `main`  
**任务描述**: 在 2026-06-25 已完成的手动重连与任务生命周期检查基础上，进一步实现 Django 后端与 MQTT Broker 之间的自动重连，并修复后端自动重连后前端 MQTT 状态提示未实时更新的 Bug。

---

## 1. 背景

### 1.1 历史问题回顾

- 后端 MQTT 断线后，只有手动 `/api/mqtt/reconnect/` 才能恢复；若操作员未及时发现，设备任务可能长时间无法下发。
- 2026-06-25 的实现已提供手动重连、连接状态广播与任务生命周期检查，但缺少自动恢复机制。
- 在开发自动重连过程中发现：后端重连成功后，前端仍持久显示 `MQTT Disconnected`，只有 F5 刷新页面才能恢复，原因是前端对 `mqtt_connection_status` 广播消息的字段解析错误。

### 1.2 本次任务目标

1. 后端 MQTT 客户端在断线后按指数退避自动重试恢复连接。
2. 保留手动重连入口，并确保手动触发时优先立即尝试。
3. 避免重新创建 `mqtt.Client` 对象，防止其它模块持有的 client 引用失效。
4. 修复前端 WebSocket 消息解析，使自动重连成功后 UI 状态实时同步。
5. 更新 `AGENTS.md` 文档。

---

## 2. 需求分析

| 需求项 | 说明 |
|--------|------|
| 自动重连范围 | 仅 Django 主进程 MQTT 客户端（与手动重连范围一致）。 |
| 退避策略 | 指数退避：2s → 4s → 8s → 16s → 32s → 64s → 90s，上限 90s。 |
| paho 内置重连 | 保留作为第一层恢复，限制其退避为 1~30s。 |
| 手动/自动协作 | 手动点击 Reconnect 时重置退避并立即尝试；连接成功后重置退避计数器。 |
| 单例保护 | 启动时即使 Broker 不可达也不销毁 `mqtt.Client` 对象，由 `loop_start()` 继续后台重试。 |
| 前端同步 | WebSocket `mqtt_connection_status` 消息到达时，实时更新 `ConnectionBar` 与 ElMessage 提示。 |

---

## 3. 实现方案

### 3.1 修改文件

#### `django_backend/main_page/mqtt.py`

- 新增自动重连配置与状态：
  - `MQTT_AUTO_RECONNECT_MIN_DELAY`
  - `MQTT_AUTO_RECONNECT_MAX_DELAY`
  - `MQTT_AUTO_RECONNECT_MULTIPLIER`
  - `_auto_reconnect_state`
- 新增内部函数：
  - `_reset_auto_reconnect()`：重置退避计数器。
  - `_schedule_next_auto_reconnect()`：按指数退避安排下一次尝试。
  - `_do_auto_reconnect()`：执行一次自动重连，失败则继续安排。
  - `_perform_reconnect()`：手动与自动重连共用的实际重连逻辑。
- `_set_mqtt_connection_state()`：断线时启动退避，连通时重置退避。
- `_mqtt_connection_watchdog()`：每 5 秒校验状态，必要时触发自动重连。
- `reconnect_mqtt_client()`：手动重连入口，调用前重置退避。
- 初始 `Client` 创建时设置 `reconnect_on_failure=True` 与 `reconnect_delay_set(min_delay=1, max_delay=30)`。
- 启动失败时不再将 `client` 置为 `None`，保留对象供 `apps.py` 启动 `loop_start()`。

#### `django_backend/django_backend/settings.py`

```python
# MQTT 自动重连退避配置（秒）
MQTT_AUTO_RECONNECT_MIN_DELAY = 2
MQTT_AUTO_RECONNECT_MAX_DELAY = 90
MQTT_AUTO_RECONNECT_MULTIPLIER = 2
```

#### `vue_frontend/src/views/Dashboard.vue`

```js
handleMqttConnectionStatus(payload) {
    const statusPayload = payload.payload && typeof payload.payload === 'object'
        ? payload.payload
        : payload
    const connected = statusPayload.connected
    if (typeof connected !== 'boolean') return
    this.backendMqttConnected = connected
    this.updateMqttBanner(connected)
}
```

- 修复从 `payload.payload.connected` 读取的问题。
- 同时兼容后端广播（顶层 `connected`）与 WebSocket 连接建立时的直接推送（嵌套 `payload`）。

#### `AGENTS.md`

- 在 §11 新增 **11.6 后端 MQTT 自动重连**，记录实现细节与单例保护。

---

## 4. 关键机制

### 4.1 后端自动重连流程

```
MQTT 断线
  ├─ paho 内置 loop_start 自动重连（1~30s 退避）
  └─ 自定义 watchdog 指数退避重试
       成功 → _set_mqtt_connection_state(True) → 重置退避并广播
       失败 → _schedule_next_auto_reconnect() → 继续等待
```

### 4.2 单例保护

```python
if _should_init_mqtt_client():
    client = mqtt.Client(...)   # 创建对象
    client.on_connect = ...
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    try:
        client.connect(...)
    except (OSError, TimeoutError, socket.error) as exc:
        # 不再 client = None，保留对象由 loop_start 继续重试
        _set_mqtt_connection_state(False, reason=f'startup_failed_{exc}')
```

### 4.3 前端状态同步

- 后端通过 `channel_layer` 广播 `mqtt_connection_status` 时，`connected` 位于消息顶层。
- `Dashboard.vue` 订阅该 topic 后，根据 `connected` 实时调用 `updateMqttBanner()`：
  - `true` → 关闭旧错误提示，显示绿色 `MQTT connection restored`。
  - `false` → 显示红色持久错误提示，带 **Reconnect** 按钮。

---

## 5. 接口与协议

### 5.1 REST API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/mqtt/reconnect/` / `/api/v1/mqtt/reconnect/` | 手动触发后端 MQTT 重连，同时重置自动重连退避。 |

### 5.2 WebSocket 广播主题

| 主题 | 载荷 |
|------|------|
| `mqtt_connection_status` | `{ connected: boolean, since: string, reason: string }` |

---

## 6. 测试验证

### 6.1 后端测试

```bash
cd django_backend
python manage.py test
```

```text
Found 17 test(s).
Ran 17 tests in 0.060s
OK
```

### 6.2 前端测试

```bash
cd vue_frontend
npm run test:run
```

```text
Test Files  14 passed (14)
     Tests  49 passed (49)
```

### 6.3 生产构建

```bash
cd vue_frontend
npm run build
```

构建成功，输出至 `dist/`。

---

## 7. Git 提交记录

### 第一次提交：后端 MQTT 自动重连

```bash
git add django_backend/main_page/mqtt.py django_backend/django_backend/settings.py
git commit -m "feat(backend): 实现后端 MQTT 自动重连（指数退避 2s~90s）"
```

**提交信息**:

- Commit: `c2fa03d`
- 2 files changed, 115 insertions(+), 12 deletions(-)

### 第二次提交：前端 MQTT 状态同步修复

```bash
git add vue_frontend/src/views/Dashboard.vue
git commit -m "fix(frontend): 修复 MQTT 连接状态广播解析，支持自动重连后 UI 同步"
```

**提交信息**:

- Commit: `2573ca7`
- 1 file changed, 5 insertions(+), 1 deletion(-)

### 第三次提交：文档更新

```bash
git add AGENTS.md
git commit -m "docs: 更新 AGENTS.md 后端 MQTT 自动重连说明"
```

**提交信息**:

- Commit: `f8b5f84`
- 1 file changed, 11 insertions(+)

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

### 8.2 验证自动重连

1. Dashboard 顶部 `ConnectionBar` 显示 **MQTT Connected**。
2. 断开 EMQX 网络或重启 Broker，等待后端感知断线：
   - 顶部出现红色 `MQTT Disconnected` 提示，带 **Reconnect** 按钮。
3. 恢复 Broker 网络：
   - 后端自动重连成功后，红色提示自动关闭，显示绿色 `MQTT connection restored`。
   - `ConnectionBar` 同步变为 **MQTT Connected**，无需 F5。

---

## 9. 问题解决记录

### 问题：后端自动重连成功后前端仍显示 Disconnected

**现象**: 断线后后端自动恢复连接，但前端 `ConnectionBar` 与 ElMessage 一直显示 `MQTT Disconnected`，只有 F5 刷新后才正常。  
**原因**: `Dashboard.vue` 的 `handleMqttConnectionStatus` 使用 `payload.payload.connected` 读取状态，但后端通过 `channel_layer` 广播时 `connected` 位于消息顶层，导致该值始终为 `undefined`。  
**解决**: 同时兼容两种消息结构：

```js
const statusPayload = payload.payload && typeof payload.payload === 'object'
    ? payload.payload
    : payload
const connected = statusPayload.connected
```

---

## 10. 后续建议

| 方向 | 建议 |
|------|------|
| 部署架构 | Django Channels 切换到 `channels_redis`，支持多 ASGI worker 共享 `_device_states` 与 MQTT 连接状态。 |
| 调度器 | 将 `task_manager.py` 改为 Django Management Command 或 Celery Beat，统一使用内存状态并共享 MQTT 客户端。 |
| 固件协议 | 设备在任务完成/异常时发送结构化 JSON 信封，后端减少超时推断。 |
| 安全 | 将 MQTT/EMQX 凭据、Django `SECRET_KEY`、JWT 密钥迁移至环境变量。 |

---

## 11. 参考链接

- [paho-mqtt 2.x Client API](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)
- [Django Channels - Channel Layers](https://channels.readthedocs.io/en/stable/topics/channel_layers.html)
- [Element Plus ElMessage](https://element-plus.org/en-US/component/message.html)
- 项目根目录 `AGENTS.md` §11 近期架构变更记录
- 同目录早期文档：
  - `2026-06-25_MQTT-Connection-Status-Awareness-and-Task-Lifecycle-Refactor_README.md`
  - `2026-06-15_Frontend_Component_Refactor_README.md`
  - `2026-06-15_MAC-Based-Dynamic-Device-ID-and-MQTT-Topic-Refactor_README.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-06-25 18:15  
**完成时间**: 2026-06-25 18:15
