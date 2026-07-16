# 计划：前端 → 后端 → MQTT 定时电机旋转

## 目标
实现用户在前端 `Spinning.vue` 预约“指定时间 + 指定电机 + 指定转速 + 指定时长”的旋转任务，后端在指定时间通过 MQTT 向 ESP32-S3 下发标准指令 `cmd_<motor_index>_<speed_rpm>_<duration_sec>`，并能在前端查看任务状态与取消未执行任务。

## 已确认的需求
1. **调度执行方式**：将调度逻辑集成进 Django 主进程，不再需要单独运行 `task_manager.py`。
2. **电机索引映射**：给 `Motor` 模型增加 `motor_index` 字段（0–3），通过迁移写入当前 `Motor 1/2/3` 对应的索引 `0/1/2`。
3. **命令格式**：使用 `Spinning.duration_sec` 作为 MQTT 命令第三个参数；前端传来的 `scheduled_time` 按 `Asia/Hong_Kong`（东八区）本地时间理解。
4. **状态管理**：给 `Spinning` 模型增加状态字段，前端队列显示状态并支持取消未执行的预约。

## 关键设计

### 1. 后端模型变更（`django_backend/main_page/models.py`）
- `Motor`：新增 `motor_index = PositiveSmallIntegerField(default=0, validators=[MinValueValidator(0), MaxValueValidator(3)])`。
- `Spinning`：新增字段：
  - `status = CharField(max_length=16, choices=SPINNING_STATUS_CHOICES, default='PENDING')`
  - `device_id = CharField(max_length=32, default=settings.MQTT_DEFAULT_DEVICE_ID)`
  - `dispatched_at = DateTimeField(null=True, blank=True)`
  - `completed_at = DateTimeField(null=True, blank=True)`
  - `error_message = CharField(max_length=256, null=True, blank=True)`
  - `created_at / updated_at`（可选，便于审计）
- 状态选项：`PENDING` / `SENT` / `FAILED` / `COMPLETED` / `CANCELLED`。

### 2. 迁移文件
- `0030_motor_motor_index.py`：添加字段并写入现有数据 `motor_index = id - 1`。
- `0031_spinning_status_and_metadata.py`：添加状态及元数据字段，默认现有记录状态为 `PENDING`。

### 3. 序列化器（`django_backend/main_page/serializer.py`）
- `MotorSerializer`：暴露 `motor_index`。
- `SpinningSerializer`：暴露新字段；`scheduled_time` 保持 ISO-8601 输出。

### 4. 调度器（新建 `django_backend/main_page/scheduler.py`）
- `SpinningScheduler` 守护线程：
  - 启动间隔 500ms 轮询。
  - 查询 `status='PENDING'` 且 `scheduled_time <= timezone.now()` 的记录。
  - 对每条记录：
    1. 查找对应 `Motor`，获取 `motor_index`；找不到则标记 `FAILED`。
    2. 取 `device_id`（默认 `settings.MQTT_DEFAULT_DEVICE_ID`）。
    3. 使用 `mqtt.can_dispatch_to_device(device_id)` 检查设备在线且空闲。
    4. 调用 `mqtt.dispatch_motor_task(device_id, motor_index, speed, duration)` 下发命令。
    5. 成功：更新 `status='SENT'`, `dispatched_at=now`。
    6. 失败：更新 `status='FAILED'`, `error_message=reason`。
  - 同时检查 `status='SENT'` 且 `scheduled_time + duration_sec <= now` 的记录，标记为 `COMPLETED`。
- 线程安全：使用 `select_for_update()` 或原子地将状态改为 `SENDING` 再下发，避免多进程/多线程重复触发。

### 5. Django 启动（`django_backend/main_page/apps.py`）
- 在 `ready()` 中，若 `mqtt._should_init_mqtt_client()` 为真，则在 `client.loop_start()` 之后启动 `SpinningScheduler`。

### 6. 视图与接口（`django_backend/main_page/views.py` + `urls.py`）
- `spinning` 视图：
  - 创建时：将前端无时区字符串 `YYYY-MM-DDTHH:MM:SS` 按 `Asia/Hong_Kong` 解析为 aware datetime 再保存。
  - 列表时：将 `scheduled_time` 转换为本地时间返回。
- 新增 `POST /api/spinning/cancel/`：接收 `{id}`，仅允许取消 `PENDING` 状态记录，更新为 `CANCELLED`。
- `urls_v1.py` 同步增加 `path('spinning-jobs/<int:job_id>/cancel/', ...)` 或等效路径（保持 v1 一致）。

### 7. 前端（`vue_frontend/src/views/Dashboard/Spinning.vue` 等）
- `motors.js`：增加 `cancelSchedule(token, id)`。
- `ScheduleQueue.vue`：
  - 增加 “Status” 列。
  - 对 `PENDING` 记录显示 “Cancel” 按钮，调用取消接口并刷新列表。
- `Spinning.vue`：
  - 创建成功后刷新列表。
  - 增加定时刷新（如每 5s）以便观察 `PENDING → SENT → COMPLETED/FAILED`。

### 8. 时区处理
- 前端 `ScheduleForm` 使用浏览器本地时间（已在中国区运行），输出无偏移字符串。
- 后端使用 `zoneinfo.ZoneInfo('Asia/Hong_Kong')` 将其 aware 化；数据库统一存 UTC；比较时用 `timezone.now()`。

### 9. 代码文件清单
新增：
- `django_backend/main_page/scheduler.py`
- `django_backend/main_page/migrations/0030_motor_motor_index.py`
- `django_backend/main_page/migrations/0031_spinning_status_and_metadata.py`

修改：
- `django_backend/main_page/models.py`
- `django_backend/main_page/serializer.py`
- `django_backend/main_page/views.py`
- `django_backend/main_page/urls.py`
- `django_backend/main_page/urls_v1.py`
- `django_backend/main_page/apps.py`
- `vue_frontend/src/services/api/motors.js`
- `vue_frontend/src/components/spinning/ScheduleQueue.vue`
- `vue_frontend/src/views/Dashboard/Spinning.vue`

## 验证步骤
1. `cd /e/Platform_G2/django_backend && python manage.py makemigrations && python manage.py migrate`
2. 启动 Django（`python manage.py runserver` 或 daphne），确认日志出现调度器启动信息。
3. 前端创建预约任务，确认数据库记录 `status=PENDING`，`motor_index` 正确。
4. 到达指定时间，确认 MQTT 发布 `cmd_<index>_<speed>_<duration>`，记录变为 `SENT`。
5. 设备执行完成后，记录变为 `COMPLETED`；若设备离线/忙碌，记录变为 `FAILED` 并带原因。
6. 取消未执行的 `PENDING` 任务，确认状态变为 `CANCELLED` 且不会被触发。

## 提交
- 排除 `django_backend/db.sqlite3`。
- Commit message 建议：`feat: integrate Spinning scheduler into Django with motor_index, duration and status`

## 计划副本
批准后，本计划将复制一份到 `2026_06_to_07_develop_detail/07_16/plan.md`。
