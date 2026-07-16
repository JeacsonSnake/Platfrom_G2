# Multi-Device Spinning Dashboard and Motor State Management Fixes

**日期**: 2026-07-16  
**分支**: `main`  
**任务描述**: 改造 `Spinning.vue` 控制台以支持多台 ESP32S3 设备（按 MAC 区分）的电机状态分页显示与任务注册；修复任务下发后设备/电机状态卡死、Actual RPM 不归零、WebSocket 序列化失败等问题；实现任务运行期间的动态刷新频率。

---

## 1. 背景

### 1.1 系统概述

项目使用 ESP32-S3-DevKitC-1 控制 4 路 CHB-BLDC2418 无刷直流电机，Django 后端通过 MQTT 与设备通信，Vue 前端提供 `Spinning.vue` 操作界面：

```
Spinning.vue (Vue 3)
    ├── Motor Status Board   (电机实时状态)
    ├── Register Spin Task   (预约/立即下发任务)
    └── Registration List    (任务历史与状态)
```

- **控制命令**: `cmd_<motor_index>_<speed_rpm>_<duration_sec>`，例如 `cmd_2_800_10`
- **任务事件**: `task_create_<motor>_<speed>_<duration>`、`task_finished_<motor>_<speed>_<duration>`
- **状态缓存**: 后端使用内存 `_device_states` 维护设备在线/任务/遥测状态
- **心跳/遥测**: `esp32/<mac>/heartbeat`、`esp32/<mac>/telemetry`

### 1.2 历史问题

截至 2026-07-16 之前，代码存在以下主要问题：

| # | 问题 | 现象 |
|---|------|------|
| 1 | 单设备视角 | `Motor Status Board` 只显示自动解析出的单台设备电机，无法区分多台 ESP32S3 |
| 2 | 设备级 Availability | 任一台电机 busy 即整台设备所有电机显示 `Unavailable` |
| 3 | 任务完成状态死锁 | 未收到 `task_finished` 时设备永远 `busy`，必须重启后端 |
| 4 | 状态不重置 | 任务结束后 `status` 仍显示 `Running`，`Actual RPM` 不归零 |
| 5 | WebSocket 崩溃 | `_device_states` 中的 `set` 类型无法 JSON 序列化，导致 WebSocket 连接断开 |
| 6 | 多电机调度失败 | 调度器向同一设备连续下发多个电机时，第二个电机被 `can_dispatch` 拦截 |
| 7 | 错误提示不统一 | 使用原生 DOM 块显示错误，未使用 Element Plus Message |
| 8 | 刷新不及时 | 固定 5 秒轮询，无法及时观察电机启动/停止瞬态 |

---

## 2. 需求确认

通过与用户多轮确认，最终确定以下需求：

| 需求项 | 说明 |
|--------|------|
| 多设备支持 | `Motor Status Board` 按 Device 分页，标签显示 `esp32_<mac>` 格式 |
| 电机级可用性 | 一台设备上部分电机运行时，其他空闲电机仍显示 `Available` |
| 多电机调度 | `Register Spin Task` 支持选择同一 Device 的多个电机，单条 `Spinning` 记录存储电机列表 |
| 手动刷新 | 点击刷新立即获取当前选中 Device 的电机状态 |
| 动态刷新 | 无任务时 5 秒；任务运行时按阶段使用 10Hz/1Hz/10Hz |
| 状态恢复 | 任务结束（收到 `task_finished` 或超时释放）后，`status`、`Actual RPM` 恢复 |
| 错误提示 | 统一使用 `ElMessage.error` / `ElMessage.success` |

---

## 3. 实现方案

### 3.1 新增/修改文件

#### 后端

| 文件 | 说明 |
|------|------|
| `django_backend/main_page/models.py` | `Spinning` 新增 `motor_names` JSONField，兼容旧版 `motor_name` |
| `django_backend/main_page/serializer.py` | `SpinningSerializer` 允许 `motor_name` 可选、`motor_names` 可写 |
| `django_backend/main_page/migrations/0034_spinning_motor_names.py` | 新增字段迁移 |
| `django_backend/main_page/views.py` | `get_motors` 支持 `device_id`；`spinning` 支持 `device_id` + `motor_names` |
| `django_backend/main_page/scheduler.py` | 多电机循环下发；首个电机校验，后续直接发布 |
| `django_backend/main_page/mqtt.py` | 电机级可用性、active_motors 跟踪、状态自动释放、遥测清零、WebSocket 序列化兼容 |

#### 前端

| 文件 | 说明 |
|------|------|
| `vue_frontend/src/services/api/motors.js` | `getList` 支持 `device_id`；`createSchedule` 支持新 payload |
| `vue_frontend/src/components/spinning/MotorStatusBoard.vue` | Device 分页标签 + 手动刷新按钮 + 设备摘要 |
| `vue_frontend/src/components/spinning/ScheduleForm.vue` | Device 选择 + 多电机复选框 + 电机索引排序 |
| `vue_frontend/src/components/spinning/ScheduleQueue.vue` | 显示 Device 与 Motors 列 |
| `vue_frontend/src/views/Dashboard/Spinning.vue` | 设备选择、状态缓存、动态轮询、ElMessage 错误提示 |
| `vue_frontend/src/__tests__/api/motors.spec.js` | 更新 API 测试 |

#### 文档

| 文件 | 说明 |
|------|------|
| `2026_06_to_07_develop_detail/07_16/plan/2026_07_16_multi_device_spinning_dashboard_plan.md` | 实施计划副本 |

---

## 4. Phase 1: Multi-Device Dashboard Foundation

### 4.1 数据模型调整

```python
class Spinning(models.Model):
    motor_name = models.CharField(max_length=128, null=False)
    motor_names = models.JSONField(default=list, blank=True)
    device_id = models.CharField(max_length=32, default='esp32_1')
    ...

    def effective_motor_names(self):
        if self.motor_names:
            return list(self.motor_names)
        return [self.motor_name] if self.motor_name else []
```

### 4.2 后端 API 调整

- `POST /api/get_motors/` 增加可选 `device_id` 参数，返回指定设备的 4 路电机状态。
- `POST /api/spinning/` 创建任务时接收 `device_id` + `motor_names`，校验电机存在性。
- 列表接口补充 `mac_address`、`device_label`、`motor_display`。

### 4.3 前端重构

- `Spinning.vue` 新增 `devices`、`selectedDeviceId`、`deviceMotors` 状态。
- `MotorStatusBoard.vue` 渲染 Excel 式底部标签页，MAC 格式化为 `esp32_7cdfa1e6d3cc`。
- `ScheduleForm.vue` 增加 Device 下拉框与 4 路电机多选。
- `ScheduleQueue.vue` 增加 Device/Motors 列。

---

## 5. Phase 2: Per-Motor Availability and State Lifecycle

### 5.1 电机级可用性

新增 `can_dispatch_motor(device_id, motor_index)`：

```python
def can_dispatch_motor(device_id, motor_index):
    ...
    if task_status == 'busy':
        active_motors = state.get('active_motors', set())
        if motor_index in active_motors:
            return False, f'Motor {motor_index} is busy'
    return True, ''
```

- `get_motors()` 对每个电机单独判断 `avaliable`。
- `dispatch_motor_task()` 使用电机级校验。
- `resolve_dispatchable_device_id()` 支持传入 `motor_index`。

### 5.2 active_motors 跟踪

设备状态增加 `active_motors: set()`：

- `task_create`：加入集合。
- `task_finished`：移除集合；为空时恢复 `idle`。
- 下发命令前乐观加入，发布失败时移除。
- 急停/确认/离线时清空。

### 5.3 状态自动释放

新增 `_auto_release_stale_busy_state(device_id, grace_seconds=5)`：

- 当 `current_task.expected_finished_at + grace_seconds` 过后仍未收到 `task_finished`，自动释放。
- 清零所有电机 `rpm`、`health_status`、`zero_samples`。
- 将仍处于 `SENT`/`RUNNING` 的 `Spinning` 记录置为 `FINISHED`。
- 用户已将宽限期调整为 **5 秒**。

---

## 6. Phase 3: Bug Fixes and Polish

### 6.1 WebSocket 序列化

`get_device_states()` 返回前将 `active_motors` 从 `set` 转换为排序后的 `list`：

```python
if isinstance(copy.get('active_motors'), set):
    copy['active_motors'] = sorted(copy['active_motors'])
```

### 6.2 Actual RPM 与 status 不归零

- 修复 `_handle_device_event` 中 `task_finished` 分支漏掉 `_update_device_task('finished')` 的回归。
- `_update_device_task('finished')` 中清零该电机 `rpm`、`health_status`。
- `_update_motor_health()` 对非 active 电机收到转速时强制 `rpm = 0`。
- `_mark_device_offline()` 清零所有电机遥测。

### 6.3 多电机连续下发

调度器首个电机校验设备可用，后续电机 `check_dispatch=False` 直接发布：

```python
for index, motor in enumerate(motors):
    result = dispatch_motor_task(
        device_id, motor.motor_index, task.motor_speed, task.duration_sec,
        check_dispatch=(index == 0)
    )
```

### 6.4 动态刷新频率

`Spinning.vue` 根据 `current_task` 动态计算轮询间隔：

| 条件 | 间隔 |
|------|------|
| 无任务 | 5000 ms |
| 总时长 < 15 s | 100 ms |
| 开始后 5 s 内 | 100 ms |
| 结束前 5 s 内 | 100 ms |
| 中间时段 | 1000 ms |

使用递归 `setTimeout` 而非固定 `setInterval`。

### 6.5 错误提示与标签格式

- 移除 `ScheduleForm.vue` 的 `console-message` 块，统一使用 `ElMessage.error`。
- Device 标签统一为 `esp32_<mac>`，不再显示带冒号的 MAC。
- 选中电机按索引从小到大排序。

---

## 7. 测试与验证

| 检查项 | 命令 | 结果 |
|--------|------|------|
| Django 系统检查 | `python manage.py check` | 通过 |
| Python 语法检查 | `python -m py_compile ...` | 通过 |
| 前端单元测试 | `npm test -- --run` | 50/50 通过 |
| 前端生产构建 | `npm run build` | 成功 |

---

## 8. Git 提交记录

```text
0c1979a  feat(Spinning): task lifecycle states, past-time guard, live motor status board, remove QuickControl
        (前期已完成的基础工作，来自上下文压缩)
d640e11  feat(spinning): multi-device motor status board and task registration
d2bb919  fix(spinning): device label format, motor sorting, availability bug, ElMessage errors
97d368c  fix(mqtt): WebSocket serialization, multi-motor dispatch, stale busy state
57030dd  fix(mqtt): per-motor availability, clear RPM on finish/offline
c32fd12  fix(spinning): task finish state reset and dynamic motor polling
b664f3c  fix(mqtt): clear telemetry on stale busy auto-release
7e3bf09  fix(mqtt): finish stale Spinning records on auto-release
```

> 所有提交均排除 `django_backend/db.sqlite3`。

---

## 9. 附：同日的 esp32_serial_logger.py 修复

在 2026-07-16 上午的会话中，还针对 `e:\Platform_G2\esp32_serial_logger.py` 进行了排查：

- 问题：驱动重装后（Silicon Labs CP210x，COM9）无串口输出。
- 排查：文件位于 `e:\Platform_G2\esp32_serial_logger.py`；环境缺少 `pyserial`。
- 结论：需确认端口描述符/波特率/编码是否与驱动变更后一致；若后续需要正式修复，将另开计划文档。

---

## 10. 备注

- `2026_06_to_07_develop_detail/07_16/log/` 下的会话导出仅用于汇总，未提交 git。
- 当前 `grace_seconds` 已设置为 **5 秒**。
- 后续如继续扩展多设备能力，可考虑：
  - 在 `Spinning.vue` 中为每个 Device 缓存独立的 `current_task` 以支持多设备同时高频率刷新。
  - 为 `Registration List` 增加设备筛选或分页。
