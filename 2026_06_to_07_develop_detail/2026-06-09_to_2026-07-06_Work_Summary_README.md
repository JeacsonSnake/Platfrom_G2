# Platform G2 开发工作总结

> **时间范围**: 2026年6月9日 - 2026年7月6日  
> **起始 Commit**: `bb395cbb8496bf79b9cd2c4dd10ba504a3af9a83`  
> **结束 Commit**: `4d8373a42384bf26ee2d437a78795dc205a46d2b`  
> **项目**: Platform G2（Robotic Platform Service）  
> **状态**: ✅ 已完成，代码已通过测试与构建验证

---

## 目录

1. [项目概述](#项目概述)
2. [工作阶段总览](#工作阶段总览)
3. [详细工作内容](#详细工作内容)
4. [关键成果](#关键成果)
5. [问题与解决方案](#问题与解决方案)
6. [Git 提交记录清单](#git-提交记录清单)
7. [文档索引](#文档索引)
8. [总结与后续建议](#总结与后续建议)

---

## 项目概述

本阶段工作围绕 **Platform G2** 的通信链路、设备管理、前端工程化与系统可观测性展开，目标是解决单台 ESP32-S3 与后端通信中的硬编码 topic、后端 MQTT 断线不可感知、任务下发缺乏前置检查、前端视图文件过大等核心问题，为后续多设备统一管理、自动化实验流程与生产部署奠定基础。

### 技术栈

| 组件 | 技术 |
|------|------|
| 前端 | Vue 3.5.22 + Vite 5 + Bulma CSS + vxe-table + Element Plus |
| 后端 | Django 4.2.24 + Django REST Framework + Django Channels + paho-mqtt 2.x |
| 嵌入式 | ESP32-S3-DevKitC-1 + ESP-IDF v5.5.2 + FreeRTOS |
| 消息代理 | EMQX Broker |
| 测试 | Django `APITestCase` + Vitest + `@vue/test-utils` + jsdom |

---

## 工作阶段总览

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                    2026年6月9日 - 7月6日 工作历程                              │
└─────────────────────────────────────────────────────────────────────────────┘

【第一阶段】文档与架构规划 (6月9日-10日)
    ├── AGENTS.md 初始化与项目指南建立
    ├── 方案 B 完整优化文档编写
    └── MQTT-WebSocket 链路重构启动

【第二阶段】MQTT-WebSocket 链路重构 (6月10日)
    ├── 后端 Device 注册表与急停 API
    ├── 内存设备状态追踪与 WebSocket 广播
    └── Dashboard 实时设备面板与事件流

【第三阶段】MAC-Based 动态设备 ID 与 Topic 重构 (6月15日)
    ├── ESP32 固件读取 MAC 动态生成 device_id / topic
    ├── Django 后端合法通配符订阅与多设备预留
    └── Vue 前端模块化组件重构

【第四阶段】MQTT 连接状态感知与任务生命周期 (6月25日)
    ├── 后端 MQTT 与 Broker 连接状态可感知
    ├── 设备在线/空闲前置检查
    ├── 任务完成/异常确认（acknowledge）机制
    ├── 手动重连接口 /api/mqtt/reconnect/
    └── 前端 ElMessage 状态提示与 ConnectionBar 重构

【第五阶段】后端 MQTT 自动重连与状态同步 (6月25日)
    ├── 指数退避自动重连（2s~90s）
    ├── 保留 mqtt.Client 单例避免引用失效
    └── 修复前端 mqtt_connection_status 解析 Bug

【第六阶段】计划文档沉淀 (7月6日)
    └── agent_generated_plan 归档
```

---

## 详细工作内容

### 第一阶段：文档与架构规划（6月9日-10日）

#### 1.1 AGENTS.md 初始化

**Commit**: `da999ab` - `docs: agent file init`

- 新增项目根目录 `AGENTS.md`，为后续 AI 编程助手与协作者提供统一的项目背景、目录结构、构建命令、开发规范、安全红线与近期架构变更记录。
- 明确项目三大核心组件（Vue 前端 / Django 后端 / ESP32-IDF）及外设脚本、EMQX 总线的运行时架构。

#### 1.2 方案 B 完整优化文档

**Commit**: `0243826` - `docs: 新增方案B完整优化文档并更新AGENTS.md架构变更记录`

- 新增 `PLATFORM_G2_FULL_OPTIMIZATION_PLAN.md`，将 6 月初中期评估中的“方案 A：最小修复”与“方案 B：完整优化”进行系统沉淀。
- 在 `AGENTS.md` §10 中补充 2026-06-10 架构变更记录，包括 `Device` 模型、MQTT 客户端重构、WebSocket Consumer 升级、REST API 新增、`task_manager.py` 配置统一等内容。

---

### 第二阶段：MQTT-WebSocket 链路重构（6月10日）

**Commits**: `70e2270`、`b5d415a`

#### 2.1 后端：Device 注册表、急停 API 与多设备预留

**文件**: `django_backend/main_page/models.py`、`django_backend/main_page/mqtt.py`、`django_backend/main_page/views.py`、`django_backend/django_backend/consumers.py`

- **新增 `Device` 模型**：字段包括 `device_id`、`client_id`、`mac_address`、`label`、`is_registered`、`is_online`、`last_heartbeat`、`task_status`、`current_task`（JSON）、`telemetry`（JSON）。
- **新增 `EmergencyStopLog` 模型**：用于审计急停操作。
- **MQTT Topic 订阅调整**：由 `esp32_1/+` 改为 `esp32_+/+`（预留多设备，后于 6 月 15 日修正为合法通配符）。
- **消息标准化**：Legacy 文本协议消息统一包装为结构化字典（含 `topic`、`device_id`、`timestamp`、`payload`）。
- **内存设备状态追踪**：`_device_states` 实时追踪每台设备在线状态、任务状态、最新遥测；后台线程每 15 秒扫描，超时 90 秒未心跳则标记离线。
- **WebSocket 广播增强**：新增 `heartbeat`、`telemetry`、`task_status`、`device_status`、`device_reply` 等 topic 标准化广播。
- **急停接口**：新增 `emergency_stop()` / `resume_devices()` / `dispatch_motor_task()`，当前实现为软急停（向所有电机发送 `cmd_X_0_0`），并标记设备为 `estopped` 阻止后续任务下发。
- **paho-mqtt 2.x 兼容**：`mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)`。

#### 2.2 前端：Dashboard 实时数据接入

**文件**: `vue_frontend/src/views/Dashboard.vue`、`vue_frontend/src/services/websocket.js`

- 顶部增加 WebSocket 连接状态条。
- 设备表格增加复选框（支持多选）、展开按钮（显示 4 路电机 PWM/PCNT、任务进度条、温度预留字段）。
- 摘要栏增加 Busy / E-Stopped 统计。
- 右侧操作面板增加 **Stop Selected / Stop All** 急停按钮（带二次确认）与 **Resume Selected** 恢复按钮。
- 底部增加实时事件流面板，显示心跳、遥测、任务状态、急停等事件。
- 任务剩余时间由前端根据 `started_at + duration_sec` 自动倒计时。

---

### 第三阶段：MAC-Based 动态设备 ID 与 Topic 重构（6月15日）

**Commits**: `f333efb`、`62a9e91`、`5ccc68e`、`92727f9`、`6559f91`

#### 3.1 ESP32 固件：基于 MAC 动态生成 device_id 与 MQTT topic

**新增文件**: `esp32_idf/main/device_identity.h`、`esp32_idf/main/device_identity.c`

**修改文件**: `esp32_idf/main/main.c`、`esp32_idf/main/main.h`、`esp32_idf/main/mqtt.c`、`esp32_idf/main/pwm.c`、`esp32_idf/main/pcnt.c`、`esp32_idf/main/pid.c`、`esp32_idf/main/CMakeLists.txt`

- 调用 `esp_read_mac(mac, ESP_MAC_WIFI_STA)` 读取 STA 接口 MAC 地址。
- 生成 12 位十六进制 MAC 字符串，构建：
  - `device_id`：`esp32_<mac>`
  - `mqtt_client_id`：`ESP32S3_<mac>`
  - 控制 topic：`esp32/<mac>/control`
  - 心跳 topic：`esp32/<mac>/heartbeat`
  - 遥测 topic：`esp32/<mac>/telemetry`
  - 任务 topic：`esp32/<mac>/task`
- 移除 `main.h` 中硬编码的 `MQTT_CONTROL_CHANNEL`、`MQTT_HEARTBEAT_CHANNEL`、`MQTT_DATA_CHANNEL`。
- `pid.c` 中任务创建/完成状态发布到独立的 `mqtt_task_topic`。

#### 3.2 Django 后端：合法通配符订阅与多设备指令下发

**修改文件**: `django_backend/main_page/mqtt.py`、`django_backend/main_page/views.py`、`django_backend/main_page/apps.py`、`django_backend/task_manager.py`、`django_backend/django_backend/settings.py`

- 修复非法订阅 `esp32_+/+` 为合法 `esp32/+/+`，并保留 `esp32_1/+` 兼容旧设备。
- 新增 `_extract_device_id_from_topic()`，支持新旧两种 topic 层级解析。
- 新增 `_device_id_to_mac()` 自动从 `device_id` 反推 MAC 地址。
- 新增 `_device_control_topic(device_id)`，统一按 `esp32/<mac>/control` 生成控制 topic。
- `emergency_stop()`、`dispatch_motor_task()`、Recipe Step 默认 topic、`task_manager.py` 均迁移到新 topic 层级。
- 新增 `MQTT_DEFAULT_DEVICE_ID` 环境变量配置，默认 `esp32_1`。
- 调整 MQTT Client 启动守卫，兼容 `runserver`、Daphne、uvicorn、gunicorn。
- `apps.py` 移除 `RUN_MAIN` 守卫，只要 `mqtt.client` 已创建就启动 `loop_start()`。

#### 3.3 Vue 前端：模块化组件重构

**新增文件**: `vue_frontend/src/components/ui/*`、`vue_frontend/src/components/dashboard/*`、`vue_frontend/src/components/spinning/*`、`vue_frontend/src/components/recipe/*`、`vue_frontend/src/services/api/*`、`vue_frontend/src/__tests__/*`、`vue_frontend/vitest.config.js`

**修改文件**: `vue_frontend/src/views/Dashboard.vue`、`vue_frontend/src/views/Dashboard/Spinning.vue`、`vue_frontend/src/views/Dashboard/RecipeDemo.vue`、`vue_frontend/package.json`

- 建立 `src/components/ui/`、`src/components/dashboard/`、`src/components/spinning/`、`src/components/recipe/` 组件目录。
- 将 `Dashboard.vue` 从约 1,700 行降至约 700 行，`Spinning.vue` 从约 720 行降至约 230 行，`RecipeDemo.vue` 从约 1,300 行降至约 470 行。
- Dashboard 设备表格迁移至 `vxe-table`，支持复选、展开行。
- 按业务域拆分 axios 调用至 `src/services/api/`（`auth.js`、`devices.js`、`motors.js`、`materials.js`、`jobs.js`）。
- 引入 Vitest + `@vue/test-utils` + jsdom 单元测试，新增 13 个测试文件、43 个测试用例全部通过。
- 统一 UI 基础组件：`PanelHeader`、`ConsoleHeader`、`MetricCard`、`StatusChip`、`ConnectionBar`、`LiveEventStream`。

#### 3.4 数据库同步

**Commit**: `6559f91` - `sync: sql sync`

- 执行 `python manage.py migrate`，同步 `Device`、`EmergencyStopLog` 等模型变更到 `db.sqlite3`。

---

### 第四阶段：后端 MQTT 连接状态感知与设备任务生命周期（6月25日）

**Commits**: `2f9834a`、`11120c6`、`796c9e5`、`68b3aca`、`57d76dd`、`2ad98d7`、`4883fc5`、`c8807aa`、`896e1fd`、`41ffeda`、`d40e3a9`、`9f2f1e0`、`8935b32`

#### 4.1 后端 MQTT 连接状态可感知

**文件**: `django_backend/main_page/mqtt.py`、`django_backend/django_backend/consumers.py`、`django_backend/main_page/views.py`

- 新增 `_mqtt_connection_state` 内存状态，由 `on_connect` / `on_disconnect` 回调与 5 秒 watchdog 线程共同维护。
- WebSocket 新增广播 topic `mqtt_connection_status`，在连接/断开时推送给所有在线前端；`connect()` 时立即补发一次当前状态。
- `/api/device_list/` 返回体新增 `mqtt_connected` 字段，供 REST 轮询兜底。
- 新增 `POST /api/mqtt/reconnect/` 端点（同时注册在 v1 路由 `mqtt/reconnect/`），用于手动触发后端 MQTT 客户端重连 Broker。

#### 4.2 设备任务生命周期前置检查

**文件**: `django_backend/main_page/mqtt.py`、`django_backend/main_page/views.py`、`django_backend/task_manager.py`、`django_backend/main_page/models.py`

- 新增 `is_device_online(device_id)` 与 `can_dispatch_to_device(device_id)`，统一判断设备是否在线、是否空闲。
- `Device.task_status` 新增 `error`（异常待确认）与 `completed`（完成待验收）。
- 所有下发入口统一前置检查：
  - `dispatch_motor_task()` / `/api/devices/dispatch_task/` / `/api/devices/dispatch_batch/`
  - `batch_job_start()`
  - Communication API 的 `_queue_transport_message()`（当请求显式携带 `device` 时）
  - `task_manager.py` 定时任务（通过查询 SQLite `main_page_device` 表）
- 设备在任务期间掉线，`_offline_detector` 自动调用 `_abort_device_task()` 将其置为 `error`。
- 任务完成后设备进入 `completed`，需用户确认后方可继续下发。
- 新增 `acknowledge_device()` 函数与 WebSocket 动作 `acknowledge_device`、REST 端点 `POST /api/devices/acknowledge/`，用于将 `error` / `completed` / `estopped` 恢复为 `idle`。
- 修复 `_offline_detector` 持锁调用 `_mark_device_offline()` 导致的潜在死锁；将数据库写操作移到 `_device_states_lock` 外，避免阻塞 MQTT 消息处理线程。

#### 4.3 前端 MQTT 状态提示与任务确认

**文件**: `vue_frontend/src/services/mqttMessage.js`、`vue_frontend/src/views/Dashboard.vue`、`vue_frontend/src/components/ui/ConnectionBar.vue`、`vue_frontend/src/components/dashboard/OperatorRail.vue`

- 使用 Element Plus `ElMessage` 实现 `mqttMessage` 服务：
  - MQTT 断开：红色错误消息，不自动关闭、无关闭按钮，右侧带 **Reconnect** 按钮，调用 `POST /api/mqtt/reconnect/`。
  - MQTT 恢复：绿色成功消息，显示 5 秒后自动关闭。
- `ConnectionBar` 组件移除冗余 `mqttAvailable` prop，统一使用 WebSocket 实时推送的 MQTT 连接状态；拆分为两个独立子 div 分别展示 WebSocket 与 MQTT 状态；用户可见文本改为英文。
- `OperatorRail` 新增 **Acknowledge Selected** 按钮，用于确认选中设备的完成/异常/急停状态。
- 下发任务前前端先做在线/空闲拦截；下发失败时顶部显示 5 秒错误提示。

#### 4.4 测试补充

**文件**: `django_backend/main_page/tests.py`

- `RecipeAndJobApiTests` 的 `setUp` 中增加 `_set_default_device_online()` 辅助函数。
- 增加 `publish_device_command` 与 `mqtt_client_available` 的 Mock。
- 断言修正为步骤 `RUNNING`、Outbox `SENT`。
- 后端 17 个测试全部通过。

---

### 第五阶段：后端 MQTT 自动重连与前端状态同步（6月25日）

**Commits**: `c2fa03d`、`2573ca7`、`f8b5f84`

#### 5.1 后端 MQTT 自动重连

**文件**: `django_backend/main_page/mqtt.py`、`django_backend/django_backend/settings.py`

- 在 `_mqtt_connection_watchdog` 中增加自动重连逻辑：断线后按指数退避持续尝试恢复连接（最小间隔 2s，最大间隔 90s，倍数 2）。
- 复用已有 `reconnect_mqtt_client()` 的重连逻辑，抽取内部 `_perform_reconnect()`；手动重连调用时先重置退避。
- 保留 paho-mqtt 内置自动重连作为第一层恢复（`reconnect_on_failure=True`），自定义自动重连作为补充。
- 启动时即使初始连接失败也保留同一个 `mqtt.Client` 实例，由 `apps.py` 启动 `loop_start()`，避免重新创建 client 对象导致其它地方持有的引用失效。
- 自动重连仅在 `_should_init_mqtt_client()` 为 `True` 的进程启用，避免测试、迁移等进程产生无意义网络请求。
- `settings.py` 新增 `MQTT_AUTO_RECONNECT_MIN_DELAY`、`MQTT_AUTO_RECONNECT_MAX_DELAY`、`MQTT_AUTO_RECONNECT_MULTIPLIER` 配置项。

#### 5.2 前端 MQTT 状态同步修复

**文件**: `vue_frontend/src/views/Dashboard.vue`

- 修复 `handleMqttConnectionStatus` 中错误地从 `payload.payload.connected` 读取状态的问题。
- 同时兼容后端广播（顶层 `connected`）与 WebSocket 连接建立时的直接推送（嵌套 `payload`）。
- 自动重连成功后前端无需 F5 即可实时更新 `ConnectionBar` 与 ElMessage 提示。

---

### 第六阶段：计划文档沉淀（7月6日）

**Commit**: `4d8373a` - `docs: agent_generated_plan`

- 将本阶段实施过程中产生的计划文件（`plan/luke-cage-medusa-karnak.md`、`plan/winter-soldier-echo-flash.md`）及相关变更归档，作为 agent 生成计划的记录。

---

## 关键成果

### 功能实现清单

| 功能模块 | 实现状态 | 说明 |
|----------|----------|------|
| AGENTS.md 项目指南 | ✅ 已实现 | 统一项目背景、架构、规范、安全红线 |
| 方案 B 完整优化文档 | ✅ 已实现 | 沉淀最小修复与完整优化两条路线 |
| Device 注册表模型 | ✅ 已实现 | 支持设备注册、在线状态、任务快照、遥测快照 |
| 急停 / 恢复 API | ✅ 已实现 | 软急停 + 状态锁定，前端带二次确认 |
| WebSocket 实时广播 | ✅ 已实现 | 设备快照、心跳、遥测、任务状态、MQTT 状态 |
| MAC-Based 动态 device_id | ✅ 已实现 | ESP32 固件按 MAC 生成唯一 ID 与 topic |
| 合法 MQTT 通配符订阅 | ✅ 已实现 | `esp32/+/+` 订阅所有新设备，保留 `esp32_1/+` 兼容 |
| 多设备 topic 层级 | ✅ 已实现 | `esp32/<mac>/control|heartbeat|telemetry|task` |
| Vue 前端组件化 | ✅ 已实现 | 拆分 Dashboard / Spinning / RecipeDemo，建立 components/ui/、dashboard/、spinning/、recipe/ |
| vxe-table 设备表格 | ✅ 已实现 | Dashboard 设备表格支持复选、展开、刷新 |
| API 服务收敛 | ✅ 已实现 | `services/api/` 按业务域拆分 axios 调用 |
| Vitest 单元测试 | ✅ 已实现 | 前端 14 个测试文件、49 个测试用例通过 |
| MQTT 连接状态感知 | ✅ 已实现 | 后端 `_mqtt_connection_state` + WebSocket 广播 |
| 任务生命周期前置检查 | ✅ 已实现 | `can_dispatch_to_device()` 统一拦截 |
| 任务完成/异常确认 | ✅ 已实现 | `completed` / `error` 需 `acknowledge_device()` 恢复 |
| 手动 MQTT 重连 | ✅ 已实现 | `POST /api/mqtt/reconnect/` + 前端 Reconnect 按钮 |
| 后端 MQTT 自动重连 | ✅ 已实现 | 指数退避 2s~90s，保留 Client 单例 |
| 前端状态实时同步 | ✅ 已实现 | 自动重连后 UI 无需刷新即可更新 |

### 性能与质量指标

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| ESP32 topic 唯一性 | 所有板子共用 `esp32_1/...` | 按 MAC 自动生成唯一 topic |
| 后端 MQTT 订阅合法性 | `esp32_+/+` 启动报错 | `esp32/+/+` 合法订阅 |
| Dashboard.vue 代码行数 | ~1,700 行 | ~700 行 |
| Spinning.vue 代码行数 | ~720 行 | ~230 行 |
| RecipeDemo.vue 代码行数 | ~1,300 行 | ~470 行 |
| 前端测试覆盖 | 0 个测试 | 49 个测试通过 |
| 后端测试 | 部分失败（未 mock MQTT） | 17 个测试全部通过 |
| MQTT 断线感知 | 无感知，静默失败 | WebSocket 实时广播 + 前端提示 |
| 任务下发安全 | 无前置检查 | 在线/空闲/状态机统一拦截 |
| MQTT 恢复 | 需手动重启后端或 F5 | 自动重连 + 前端实时同步 |

---

## 问题与解决方案

### 主要问题汇总

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 后端订阅 topic 报错 `Invalid subscription filter` | `esp32_+/+` 中 `+` 未独占 topic level | 改为合法订阅 `esp32/+/+`，保留 `esp32_1/+` 兼容旧设备 |
| 同一套 ESP32 固件无法区分多台设备 | topic 与 device_id 硬编码为 `esp32_1` | 新增 `device_identity` 模块，从 `esp_read_mac()` 读取 MAC 动态生成 |
| 后端 MQTT 断线前端无法感知 | 缺少连接状态追踪与广播 | 新增 `_mqtt_connection_state`、watchdog、WebSocket `mqtt_connection_status` |
| 任务下发可能静默失败 | 无统一在线/空闲检查 | 新增 `can_dispatch_to_device()`，覆盖所有下发入口 |
| 任务完成后无法再次下发 | 旧逻辑自动回到 `idle`，缺少验收确认 | 完成后进入 `completed`，新增 `acknowledge_device()` |
| 前端 MQTT 提示不可复用 | 使用 Bulma 内联横幅 | 使用 Element Plus `ElMessage` 封装为 `mqttMessage` 服务 |
| MQTT 断线后需手动恢复 | 无自动重连机制 | 指数退避自动重连 + 手动重连按钮 |
| 自动重连后前端仍显示 Disconnected | `Dashboard.vue` 解析 `payload.payload.connected` 错误 | 兼容顶层与嵌套两种消息结构 |
| `_offline_detector` 潜在死锁 | 持锁调用 `_mark_device_offline()` | 收集 ID 后释放锁再逐个调用 |

---

## Git 提交记录清单

本阶段共 **30 个 commit**，涉及 **83 个文件**，**+18,887 / -5,251** 行变更。

| 日期 | Commit | 类型 | 说明 |
|------|--------|------|------|
| 2026-06-09 | `c8b8097` | sync | 数据库同步，`python manage.py migrate` |
| 2026-06-09 | `da999ab` | docs | AGENTS.md 初始化 |
| 2026-06-10 | `0243826` | docs | 新增方案 B 完整优化文档，更新 AGENTS.md 架构变更记录 |
| 2026-06-10 | `70e2270` | feat(backend) | 重构 MQTT-WebSocket 链路，新增 Device 注册表、急停 API 与多设备预留接口 |
| 2026-06-10 | `b5d415a` | feat(frontend) | Dashboard 接入 WebSocket 实时数据，新增遥测展开面板、急停控制与事件流 |
| 2026-06-15 | `f333efb` | feat(esp32) | ESP32 根据 MAC 地址动态生成 device_id 与 MQTT topic |
| 2026-06-15 | `62a9e91` | feat(backend) | Django 支持按 MAC 区分的多设备 MQTT topic 订阅与指令下发 |
| 2026-06-16 | `5ccc68e` | feat(frontend) | Vue 前端模块化组件重构 |
| 2026-06-16 | `92727f9` | docs | 添加 MAC-Based Dynamic Device ID 与 MQTT Topic 重构说明文档 |
| 2026-06-16 | `6559f91` | sync | SQL 同步 |
| 2026-06-25 | `7ab2d6c` | docs | 添加 Vue 前端模块化组件重构实现记录 |
| 2026-06-25 | `2f9834a` | feat(frontend) | 增加 MQTT 连接状态横幅与设备任务确认交互 |
| 2026-06-25 | `11120c6` | feat(backend) | 后端 MQTT 连接状态感知与设备任务生命周期前置检查 |
| 2026-06-25 | `796c9e5` | docs | 在 AGENTS.md 中补充 MQTT 状态感知与任务生命周期变更记录 |
| 2026-06-25 | `68b3aca` | feat(frontend) | MQTT 断开横幅增加刷新连接按钮 |
| 2026-06-25 | `57d76dd` | feat(backend) | 新增 MQTT 手动重连接口 `/api/mqtt/reconnect/` |
| 2026-06-25 | `2ad98d7` | docs | 更新 AGENTS.md 说明 MQTT 手动重连与刷新按钮 |
| 2026-06-25 | `4883fc5` | fix(backend) | MQTT 重连后主动广播状态并等待连接建立 |
| 2026-06-25 | `c8807aa` | fix(frontend) | MQTT 重连按钮根据接口响应立即更新状态并轮询兜底 |
| 2026-06-25 | `896e1fd` | refactor(frontend) | 使用 element-plus ElMessage 重构 MQTT 状态提示为可复用服务 |
| 2026-06-25 | `41ffeda` | docs | 更新 MQTT 状态提示使用 element-plus ElMessage 的说明 |
| 2026-06-25 | `d40e3a9` | refactor(frontend) | 统一 ConnectionBar 状态显示并将中文用户文本改为英文 |
| 2026-06-25 | `9f2f1e0` | docs | 说明 ConnectionBar 移除 mqttAvailable 并统一使用 mqttConnected |
| 2026-06-25 | `8935b32` | refactor(frontend) | ConnectionBar 使用独立子 div 分别展示 WebSocket 与 MQTT 状态 |
| 2026-06-25 | `70eadd2` | docs | 添加后端 MQTT 连接状态感知与设备任务生命周期重构实现记录 |
| 2026-06-25 | `c2fa03d` | feat(backend) | 实现后端 MQTT 自动重连（指数退避 2s~90s） |
| 2026-06-25 | `2573ca7` | fix(frontend) | 修复 MQTT 连接状态广播解析，支持自动重连后 UI 同步 |
| 2026-06-25 | `f8b5f84` | docs | 更新 AGENTS.md 后端 MQTT 自动重连说明 |
| 2026-06-25 | `8645867` | docs | 添加后端 MQTT 自动重连与前端状态同步实现记录 |
| 2026-07-06 | `4d8373a` | docs | agent_generated_plan |

---

## 文档索引

### 核心开发记录

| 文档 | 日期 | 内容 |
|------|------|------|
| `2026-06-15_MAC-Based-Dynamic-Device-ID-and-MQTT-Topic-Refactor_README.md` | 2026-06-15 | ESP32 MAC-Based device_id 与 MQTT topic 重构 |
| `2026-06-15_Frontend_Component_Refactor_README.md` | 2026-06-15 | Vue 前端模块化组件重构 |
| `2026-06-25_MQTT-Connection-Status-Awareness-and-Task-Lifecycle-Refactor_README.md` | 2026-06-25 | 后端 MQTT 状态感知与设备任务生命周期 |
| `2026-06-25_Backend-MQTT-Auto-Reconnect-and-Frontend-Status-Sync_README.md` | 2026-06-25 | 后端 MQTT 自动重连与前端状态同步 |
| `2026-06-09_to_2026-07-06_Work_Summary_README.md` | 2026-07-07 | 本总结文档 |

### 计划文档

| 文档 | 内容 |
|------|------|
| `plan/luke-cage-medusa-karnak.md` | MQTT 连接状态感知 + 设备任务生命周期前置检查实施计划 |
| `plan/winter-soldier-echo-flash.md` | 前端模块化组件重构计划（MVP） |

### 项目级参考文档

| 文档 | 内容 |
|------|------|
| `AGENTS.md` | 项目指南、架构、开发规范、近期变更记录 |
| `PLATFORM_G2_FULL_OPTIMIZATION_PLAN.md` | 方案 B 完整优化计划 |
| `README.md` | 项目快速开始与人文说明 |

---

## 总结与后续建议

### 阶段总结

本阶段历时约四周，完成了 Platform G2 从“单设备硬编码通信”向“多设备可管理、可观测、可自动恢复”的关键演进。核心成果包括：

1. **ESP32 固件**：从硬编码 `esp32_1/...` 演进为按 MAC 地址动态生成 device_id 与 MQTT topic，为同一套固件在多块板子上统一部署扫清障碍。
2. **Django 后端**：建立 Device 注册表、内存状态追踪、MQTT 连接状态感知、任务生命周期状态机与自动重连机制，显著提升系统鲁棒性。
3. **Vue 前端**：完成 Dashboard / Spinning / RecipeDemo 三大视图的组件化拆分，引入 vxe-table 与 Vitest 单元测试，代码可维护性大幅增强。
4. **通信链路**：实现前后端、后端与 Broker、后端与 ESP32 之间的状态实时同步，断线可感知、可手动/自动恢复。

### 已知限制

- **`InMemoryChannelLayer` 限制**：WebSocket 广播与 `_device_states`、`_mqtt_connection_state` 内存状态仅能在单 Daphne worker 内可靠工作。
- **`task_manager.py` 状态滞后**：独立调度器通过 SQLite 轮询 `Device.is_online` / `task_status`，与 Django 主进程内存状态存在秒级延迟。
- **急停为软急停**：ESP32 固件没有硬件急停逻辑，当前通过发送 `cmd_X_0_0` 停止电机并锁定任务下发。
- **温度数据不可用**：MAX31850 当前硬件因总线电容过大无法稳定通信，Dashboard UI 中显示为 `N/A`。
- **安全与部署**：`SECRET_KEY`、MQTT/EMQX 凭据、`db.sqlite3` 被 Git 追踪等生产部署问题仍需后续处理。

### 后续建议

| 方向 | 建议 |
|------|------|
| 部署架构 | Django Channels 切换到 `channels_redis`，支持多 ASGI worker 共享状态 |
| 调度器 | 将 `task_manager.py` 改为 Django Management Command 或 Celery Beat，统一使用内存状态 |
| 多设备调度 | 给 `Spinning` 模型增加 `device_id` 字段，实现按记录指定执行设备 |
| 固件协议 | 设备任务完成/异常时发送结构化 JSON 信封，减少后端超时推断 |
| 安全 | 将 `SECRET_KEY`、JWT 密钥、MQTT/EMQX 凭据迁移至环境变量或 KMS；将 `db.sqlite3` 加入 `.gitignore` 并迁移到 PostgreSQL/MySQL |
| 生产部署 | 关闭 `DEBUG`，设置正确的 `ALLOWED_HOSTS`，限制 CORS，配置 Nginx 反向代理与静态文件收集 |
| 前端优化 | 生产构建产物超过 1.4 MB，建议评估路由级动态导入进行代码分割 |
| 硬件 | 等待 PCB 改版解决 MAX31850 总线电容问题，恢复温度数据采集 |

---

**记录人**: Kimi Code CLI  
**生成时间**: 2026-07-07  
**覆盖 Commit**: `bb395cbb...` ~ `4d8373a4...`
