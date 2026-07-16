# 计划：Spinning 任务状态、时间保护、Motor Status Board 与界面精简

## 目标
仅修改前后端代码，解决以下四个问题：
1. **过去时间保护**：`Scheduled Time` 早于当前时间时，自动设为当前时间再执行。
2. **状态跟随 ESP32 实际生命周期**：`PENDING → SENT → RUNNING → FINISHED`。
3. **Motor Status Board 展示实时运行数据**：显示可用性、idle/running/fault 状态、目标转速、实际转速；移除 Description 列。
4. **删除 Spinning.vue 的 Operating Information 栏**：其功能已并入 Register Spin Task。

## 已确认的需求
- 状态流转保留 `SENT` 中间状态。
- Motor Status Board 字段：运行状态（available/unavailable、idle/running/fault）、目标转速、实际转速；不显示 PWM。
- 目标转速非零但实际转速连续为 0 时，状态标记为 `fault/stall`。

## 关键设计

### 1. 过去时间保护（后端 `main_page/views.py`）
- `spinning` 视图创建任务时：
  ```python
  if scheduled_time < timezone.now():
      scheduled_time = timezone.now()
  ```
- 这样即使前端默认“立即执行”的时间略有滞后，也不会因“已过期”导致调度器跳过或误判。

### 2. 任务状态跟随 ESP32 生命周期

#### 模型扩展（`main_page/models.py`）
- `SPINNING_STATUS_CHOICES` 增加 `('RUNNING', 'Running')`、`('FINISHED', 'Finished')`。
- `Spinning` 增加 `started_at`、`finished_at`（DateTimeField, null=True, blank=True）。
- 生成迁移 `0033_spinning_status_running_finished.py`。

#### 调度器与 MQTT 回调（`main_page/scheduler.py`、`main_page/mqtt.py`）
- 调度器成功下发后，状态设为 `SENT`（已有逻辑）。
- `mqtt.py` 的 `on_message` 中：
  - 解析到 `task_create_<motor>_<speed>_<duration>` 时，调用 `_match_spinning_record(device_id, motor, speed, duration, expected_status='SENT')`，找到最新匹配的记录，更新为 `RUNNING` 并设置 `started_at`。
  - 解析到 `task_finished_<motor>_<speed>_<duration>` 时，调用 `_match_spinning_record(..., expected_status='RUNNING')`，更新为 `FINISHED` 并设置 `finished_at`。
- 匹配规则：按 `device_id`、`motor_name` 对应的 `motor_index`、`motor_speed`、`duration_sec`、期望状态过滤，取 `scheduled_time` 最新的一条。避免纯按 ID 找不到（因为 ESP32 不返回任务 ID）。

#### 前端状态展示（`ScheduleQueue.vue`）
- 增加 `RUNNING`、`FINISHED` 的 status-badge 样式。

### 3. Motor Status Board 实时数据

#### 后端实时数据（`main_page/mqtt.py`、`main_page/views.py`）
- `mqtt.py` 的 `on_message` 新增解析 `pcnt_rpm_<motor>_<rpm>`，调用 `_update_device_telemetry(device_id, motor, 'rpm', rpm)`。
- 新增 `_update_motor_health(device_id, motor, rpm)`：
  - 若设备 `current_task.motor == motor` 且 `speed > 0`：
    - `rpm == 0` 则累加 `zero_samples`；达到 3 次（约 600ms）后状态为 `fault`。
    - `rpm > 0` 则重置 `zero_samples`，状态为 `running`。
  - 否则状态为 `idle`。
  - 将 `health_status` 和 `zero_samples` 写入该电机的 telemetry。
- `get_motors` 视图返回每个电机的：
  - `avaliable`：设备是否在线且可下发。
  - `status`：`idle` / `running` / `fault` / `offline`。
  - `target_speed`：来自 `current_task.speed`（若当前任务是该电机）。
  - `actual_speed`：来自 telemetry 的 `rpm`。

#### 前端展示（`MotorStatusBoard.vue`）
- 移除 Description 列。
- 列改为：ID、Name、Availability、Status、Target RPM、Actual RPM。
- 根据状态显示不同 pill 颜色。

### 4. 删除 Operating Information 栏
- `vue_frontend/src/views/Dashboard/Spinning.vue`：
  - 移除 `QuickControl` 组件引用、import、模板中的 Operating Information panel。
  - 移除 `real_speed`、`target_speed`、`listen_started`、`listener` 等不再使用的 data 和 `set_speed`、`get_speed` 方法。

## 文件清单
修改：
- `django_backend/main_page/models.py`
- `django_backend/main_page/views.py`
- `django_backend/main_page/mqtt.py`
- `django_backend/main_page/scheduler.py`（小调整，新增 started/finished 字段写入）
- `vue_frontend/src/views/Dashboard/Spinning.vue`
- `vue_frontend/src/components/spinning/MotorStatusBoard.vue`
- `vue_frontend/src/components/spinning/ScheduleQueue.vue`

新增：
- `django_backend/main_page/migrations/0033_spinning_status_running_finished.py`

## 验证
1. `python manage.py makemigrations && python manage.py migrate`
2. `python manage.py check`
3. `npm run build`
4. 手动验证：
   - 选择过去时间创建任务，数据库中 `scheduled_time` 被重置为当前时间。
   - 下发后状态 `SENT`；收到 `task_create` 后变为 `RUNNING`；收到 `task_finished` 后变为 `FINISHED`。
   - Motor Status Board 显示目标/实际转速和状态。

## 提交
- 排除 `django_backend/db.sqlite3`。
- Commit message 建议：`feat(Spinning): task lifecycle states, past-time guard, live motor status board`

## 计划副本
批准后，本计划将复制一份到 `2026_06_to_07_develop_detail/07_16/plan.md`。
