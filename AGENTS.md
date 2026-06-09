# AGENTS.md — Platform G2 项目指南

> 本文档供 AI 编程助手阅读。假设读者对本项目一无所知，所有信息均基于当前代码库的实际内容，不做臆测。

---

## 1. 项目概述

**Platform G2**（又称 Robotic Platform Service）是一个面向化学实验流程自动化的机器人平台。目标是实现从材料配方、实验执行到数据采集的全自动工作流，并预留 AI 算法集成接口用于材料筛选。

项目由三大核心组件及若干外设控制脚本组成：

- **Vue 前端** (`vue_frontend/`)：实验交互界面与设备监控台。
- **Django 后端** (`django_backend/`)：REST API、WebSocket 网关、MQTT 桥接、任务调度与数据持久化。
- **ESP32-IDF 客户端** (`esp32_idf/`)：基于 ESP32-S3 的嵌入式固件，负责电机闭环控制、传感器采集与 MQTT 通信。
- **外设脚本** (`peripherals/`)：独立于后端的 PC 端硬件驱动（注射泵、转台、机械臂）。
- **EMQX MQTT 消息服务器**：作为前后端与设备间的消息总线。

> **语言环境**：代码标识符使用英文，但注释、文档字符串、提交提示和开发日志大量使用**中文**。本文件使用中文撰写以符合项目惯例。

---

## 2. 技术栈与运行时架构

### 2.1 前端

- **Vue 3.5.22**（选项式 API 为主，少量组合式 API 混入）
- **构建工具**：Vite 5
- **路由**：`vue-router` 4（History 模式）
- **状态管理**：`vuex` 4 + `localStorage`（auth 状态）
- **HTTP 客户端**：Axios（`baseURL` 硬编码为 `http://127.0.0.1:8000`）
- **UI 框架**：Bulma CSS 0.9.4（主样式）+ Tailwind CSS 3.4.18（已安装但主视图几乎未使用）
- **表格库**：`vxe-table` + `vxe-pc-ui`
- **实时通信**：浏览器原生 `WebSocket`（非 `vue-websocket` 包），地址 `ws://127.0.0.1:8000/websocket/`

### 2.2 后端

- **Django 4.2.24** + **Django REST Framework 3.16.1**
- **ASGI / WebSocket**：Django Channels 4 + Daphne 4.2.1
- **API 文档**：`drf-yasg`（Swagger UI `/swagger/`，ReDoc `/redoc/`）
- **跨域**：`django-cors-headers`（当前配置为完全开放 `CORS_ORIGIN_ALLOW_ALL = True`）
- **数据库**：SQLite3（`db.sqlite3`）。`settings.py` 中有 MySQL 配置但被注释。
- **MQTT 客户端**：`paho-mqtt` 2.1.0
- **认证**：未使用 Django 内置 `User`，采用自定义 `User` 模型 + `python-jose` / PyJWT（HS256），Token 有效期 1 小时。
- **时区**：`Asia/Hong_Kong`

### 2.3 嵌入式（ESP32）

- **芯片**：ESP32-S3-DevKitC-1
- **框架**：ESP-IDF v5.5.2
- **RTOS**：FreeRTOS（双核，100Hz Tick）
- **网络**：WiFi STA（关闭 Power Save）、MQTT v3.1.1
- **外设**：LEDC PWM、PCNT 脉冲计数、RMT（WS2812 RGB 灯）、自定义 1-Wire（MAX31850 温度传感器）

### 2.4 通信架构概览

```
Vue Frontend  <--REST/API/WebSocket-->  Django Backend  <--MQTT (EMQX)-->  ESP32 / 外设
                                              ^
                                              | (SQLite)
                                       task_manager.py (独立进程)
```

- **前端 ↔ 后端**：HTTP REST（`/api/v1/`） + WebSocket。后端通过 Channels 将 MQTT 消息广播到 WebSocket 客户端。
- **后端 ↔ ESP32**：MQTT。后端订阅 `esp32_1/+` 接收遥测，向 `control` 等主题发布指令。
- **后端 ↔ 外设**：无直接耦合。外设脚本 (`peripherals/`) 作为独立进程运行，自行连接串口/MQTT/以太网。
- **定时任务**：`task_manager.py` 是一个独立 Python 脚本（非 Django Management Command），直接轮询 `db.sqlite3` 的 `main_page_spinning` 表，到期后通过 MQTT 发布电机指令。

---

## 3. 目录结构与模块划分

```
Platform_G2/
├── django_backend/           # Django 后端
│   ├── django_backend/       # 项目级配置 (settings, urls, asgi, wsgi, consumers, routing, middleware)
│   ├── main_page/            # 唯一应用：模型、视图、序列化器、URL、测试、MQTT 客户端
│   ├── db.sqlite3            # SQLite 数据库（被 Git 追踪）
│   ├── manage.py
│   ├── requirements.txt
│   └── task_manager.py       # 独立的定时任务调度脚本
├── vue_frontend/             # Vue 3 + Vite 前端
│   ├── src/
│   │   ├── views/            # 页面级视图（无 src/components/ 目录）
│   │   ├── router/           # vue-router 配置
│   │   ├── store/            # Vuex store
│   │   ├── App.vue
│   │   └── main.js
│   ├── package.json
│   └── vite.config.js
├── esp32_idf/                # ESP-IDF 项目
│   ├── main/                 # 所有源码（单体式，未拆分为自定义组件）
│   │   ├── main.c            # app_main()：创建 FreeRTOS 任务
│   │   ├── wifi.c/h          # WiFi STA 连接与事件处理
│   │   ├── mqtt.c/h          # MQTT 客户端、心跳、健康检查、错误上报
│   │   ├── pwm.c/h           # LEDC PWM 电机输出（GPIO 1/4/6/8）
│   │   ├── pcnt.c/h          # 脉冲计数输入（GPIO 2/5/7/9）
│   │   ├── pid.c/h           # 4 路电机 PID 闭环控制
│   │   ├── led.c/h           # WS2812 状态灯（GPIO 48）
│   │   ├── heating_detect.c/h# MAX31850 1-Wire 温度传感器驱动
│   │   └── ...
│   ├── components/           # 空目录（无自定义组件）
│   ├── 2026_02_to_04_develop_detail/  # 开发日志与测试报告（中文）
│   ├── hardware_info/        # 硬件原理图、电机手册、Datasheet
│   └── network_connect_log/  # ESP32 串口日志（历史测试记录）
├── peripherals/              # PC 端外设驱动脚本（独立运行）
│   ├── KSY30_pump_config.py
│   ├── KSY30_syringe_pump_testing.py  # 注射泵 RS-232 驱动
│   ├── Syringe_Model.py      # 注射泵线性回归校准脚本
│   ├── Turntable_host_drive.py        # 转台 Modbus RTU 底层
│   ├── motor_driver.py       # 转台高层驱动（串口 + MQTT）
│   ├── motor_driver_config.py
│   ├── robo_arm_config.py
│   ├── robo_arm_driver.py    # 机械臂 TCP 驱动（pymycobot）
│   └── RoticArm_Gripper_test.py
├── network_connect_log/      # 根目录日志副本
├── esp32_serial_logger.py    # ESP32 串口日志记录与分析工具
└── test.py                   # 空测试占位文件
```

### 3.1 Django `main_page` 应用核心模型

项目目前只有 `main_page` 一个应用，包含 18 个模型，主要围绕任务、电机控制、用户认证、实验流程与配方执行：

- **任务与用户**：`Task`, `User`（自定义）, `LoginRecord`
- **电机相关**：`Motor`, `MotorControl`, `Spinning`（定时任务）, `MotorEvent`, `MotorData`
- **实验与配方**：`ExperimentProcess`, `CharacterizationResult`, `MaterialType`, `MaterialRecipe`, `RecipeStep`
- **作业执行**：`BatchJob`（状态机：PENDING → RUNNING → DONE/FAILED/ABORTED）, `BatchStepExecution`
- **设备通信**：`CommandOutbox`（Outbox 模式）, `TelemetryIngest`

视图采用 **DRF 函数视图（FBV）**，非类视图。URL 分为 Legacy（`/api/`）和 v1（`/api/v1/`）两套，Legacy 已被自定义中间件标记为废弃。

### 3.2 ESP32 主要 FreeRTOS 任务

| 任务 | 功能 | 优先级 |
|---|---|---|
| `LED_TASK` | WS2812 状态灯（未启动/连WiFi/连MQTT/正常） | 2 |
| `MONITOR_TASK` | 连接统计与 NTP 同步等待 | 3 |
| `MQTT_INIT` | 一次性 MQTT 初始化 | 2 |
| `MQTT_HB` | 30 秒心跳发布到 `esp32_1/heartbeat` | 1 |
| `MQTT_CHK` | 健康检查与强制重连 | 1 |
| `MQTT_ERR` | 5 分钟错误统计上报 | 1 |
| `PCNT_TASK` ×4 | 每路电机脉冲计数（200ms 采样） | — |
| `PID_TASK` ×4 | 每路电机 PID 闭环 | — |
| `CMD_TASK` | 电机指令执行（速度 + 时长） | — |
| `max31850_poll` | 温度传感器轮询（1s，当前硬件阻塞） | — |

---

## 4. 构建与运行命令

### 4.1 Vue 前端

```bash
cd vue_frontend
npm install      # 或 pnpm install
npm run dev      # Vite 开发服务器（默认 localhost:5173）
npm run build    # 生产构建（输出到 dist/）
npm run preview  # 预览生产构建
```

### 4.2 Django 后端

```bash
cd django_backend
pip install -r requirements.txt
python manage.py migrate
python manage.py runserver            # 开发服务器（不支持 WebSocket）
# 或启动 ASGI 服务器以支持 WebSocket：
daphne -b 0.0.0.0 -p 8000 django_backend.asgi:application
```

**独立任务调度器**（必须单独启动，否则定时电机任务不会执行）：
```bash
cd django_backend
python task_manager.py
```

**运行测试**：
```bash
cd django_backend
python manage.py test
```

### 4.3 ESP32-IDF

```bash
cd esp32_idf
idf.py set-target esp32s3
idf.py build
idf.py -p COM9 flash
idf.py -p COM9 monitor
```

项目根目录下 `esp32_idf_backup/build/` 有旧构建产物，但应以 `esp32_idf/build/` 为准。

### 4.4 辅助工具与外设脚本

- **ESP32 串口日志监控**：
  ```bash
  python esp32_serial_logger.py --port COM9 --baud 115200
  ```
- **外设脚本**：均为独立运行，直接执行对应 `.py` 文件即可。注意串口号（COM6/COM7）和 MQTT Broker IP 在代码中硬编码。

---

## 5. 开发规范与代码风格

### 5.1 双语代码库

- 所有变量、类名、函数名使用**英文**（PascalCase 类名、snake_case 函数/变量）。
- 注释、文档字符串、打印日志、UI 提示、开发报告大量使用**中文**。
- 修改代码时，**保持中文注释风格**，尤其是 ESP32 固件和 Django 视图中的中文日志与注释。

### 5.2 Python 后端

- `requirements.txt` 包含 `flake8==3.9.0` 和 `yapf==0.31.0`，但项目中**未发现** `.flake8`、`setup.cfg` 或 `pyproject.toml` 配置，因此使用默认规则。
- 模型与视图文件较大（`views.py` 约 30KB），以功能为导向组织，未按模块进一步拆分应用。

### 5.3 Vue 前端

- **无组件化目录**：`src/views/` 即所有页面，没有 `src/components/`。复杂 UI（如 `RecipeDemo.vue`、`Spinning.vue`）为单一大文件，内含模板、脚本与大量 `<style scoped>`。
- **状态持久化**：Auth 信息同时存于 Vuex 和 `localStorage`，`App.vue` 在 `beforeCreate()` 中校验 Token。
- **路由守卫**：`router/index.js` 中定义了 `meta: { requireLogin: true }`，但 `beforeEach` 守卫**已被注释掉**，当前不强制登录跳转。

### 5.4 Git 提交规范

前端配置了 `commitizen` + `cz-git`，提示语言为中文。提交类型遵循 Angular 规范：

`feat` / `fix` / `docs` / `style` / `refactor` / `perf` / `test` / `build` / `ci` / `revert` / `chore`

配置位于 `vue_frontend/.commitlintrc.cjs`。

### 5.5 API 版本约定

- **v1 API** (`/api/v1/`)：RESTful 风格，使用 `path()` 路由，支持 Recipe、Job、Communication 等新功能。
- **Legacy API** (`/api/`)：旧版扁平路由，使用 regex。已被 `LegacyApiDeprecationMiddleware` 标记废弃，响应头带 `X-API-Deprecated: true` 和 `Warning`。
- 开发新功能时，**优先在 v1 路由中实现**。

---

## 6. 测试策略

### 6.1 Django 后端测试

`main_page/tests.py` 包含相对完整的 `APITestCase` 套件，覆盖：

- `ExperimentProcess` 的增删改查
- `MaterialRecipe` / `BatchJob` 的创建、启动、状态流转
- Communication API（Topic 发布、Service 调用、Action Goal）的 Mock MQTT 测试
- 设备回复包（ack / progress / result / error）处理与数据库状态同步
- Legacy API 废弃响应头校验

运行命令：`python manage.py test`

### 6.2 ESP32 固件测试

- **无单元测试框架**。验证方式以硬件在环（HIL）和长时间稳定性测试为主。
- `network_connect_log/` 目录保存了历次长时间运行的串口日志，用于事后分析断连模式。
- `esp32_serial_logger.py` 是配套工具，支持实时错误统计、连接率计算与安全退出。
- 开发日志目录 `2026_02_to_04_develop_detail/` 中有详细的阶段测试报告（WiFi 稳定性、电机闭环、温度传感器信号完整性）。

### 6.3 前端与外设

- **前端**：未发现 Jest、Vitest 或 E2E 测试配置。
- **外设脚本**：无自动化测试，通过手动运行并观察硬件动作验证。

---

## 7. 安全与部署注意事项

> ⚠️ 当前代码库包含多项仅适用于开发的硬编码配置，**不能直接部署到生产环境**。

### 7.1 Django 后端安全红线

1. **`DEBUG = True`** 且 **`ALLOWED_HOSTS = ['*']`**。
2. **`SECRET_KEY`** 在 `settings.py` 中硬编码。
3. **JWT 密钥** `MuCSL` 在 `main_page/token.py` 中硬编码。
4. **CORS 完全开放**：`CORS_ORIGIN_ALLOW_ALL = True`，且 `CORS_ALLOW_HEADERS = ('*')`。
5. **EMQX API 凭据** 硬编码在 `views.py` 的 `device_list` 中。
6. **MQTT 凭据** 硬编码在 `settings.py` 与 `task_manager.py` 中。
7. **`db.sqlite3` 被 Git 追踪**：`django_backend/.gitignore` 未排除它，协作时易冲突且可能泄漏数据。
8. **Channel Layer**：使用 `InMemoryChannelLayer`，仅支持单进程。若使用多 Daphne Worker，WebSocket 广播将失效，需替换为 Redis Channel Layer。

### 7.2 ESP32 固件安全红线

1. **WiFi SSID/密码** (`WeShare-6148` / `1234567890`) 和 **MQTT 凭据** (`ESP32_1` / `123456`) 以 `#define` 形式硬编码在 `main.h` 中，无 NVS 配网机制。
2. **无 TLS**：MQTT 使用明文 `mqtt://`（1883 端口）。`sdkconfig` 虽已编译 `mbedtls`，但未在 MQTT 客户端配置中启用。
3. **无 OTA**：没有空中升级逻辑。

### 7.3 部署前必须事项（Checklist）

- [ ] 将 `SECRET_KEY`、JWT 密钥、MQTT/EMQX 凭据迁移到环境变量或密钥管理系统。
- [ ] 关闭 `DEBUG`，设置正确的 `ALLOWED_HOSTS`。
- [ ] 限制 CORS 为前端实际域名。
- [ ] 将 `db.sqlite3` 加入 `.gitignore`，并迁移到 PostgreSQL / MySQL。
- [ ] Django Channels 替换为 Redis Channel Layer（`channels_redis`）。
- [ ] 配置静态文件收集（`python manage.py collectstatic`）并配合 Nginx/CDN。
- [ ] ESP32：将网络凭据移至 NVS 或使用 WiFi Provisioning；按需启用 TLS；评估 OTA 方案。

---

## 8. 关键通信协议与数据格式

### 8.1 MQTT 主题约定

| 方向 | 主题 | 说明 |
|---|---|---|
| 后端 ← ESP32 | `esp32_1/+` | 后端订阅，接收遥测与心跳 |
| 后端 → ESP32 | `esp32_1/control` | 控制指令 |
| ESP32 → 后端 | `esp32_1/heartbeat` | 30 秒在线心跳 |
| ESP32 → 后端 | `esp32_1/data` | PCNT/PWM 实时数据 |
| 后端 / 调度器 | `control` / `task_manager` | 旧版电机调度与任务管理 |
| 转台 | `spintable_1/control` | 转台电机 MQTT 控制 |

### 8.2 Legacy 文本协议（电机控制）

- 指令格式：`cmd_<motor>_<speed>_<duration>`（例如 `cmd_motor1_3000_10`）
- PCNT 反馈：`pcnt_count_motor_count`
- PWM 设置：`pwm_set_motor_value`

### 8.3 新结构化协议（Recipe / Job 系统）

后端与设备间支持 JSON 信封：

```json
{
  "interface_type": "service" | "action" | "topic",
  "message_type": "ack" | "progress" | "result" | "error",
  "correlation": {
    "job_id": 1,
    "step_execution_id": 2,
    "outbox_id": 3
  },
  "payload": { ... }
}
```

后端 `mqtt.py` 中的 `process_device_reply_envelope()` 负责解析此格式并同步 `CommandOutbox`、`BatchStepExecution` 与 `BatchJob` 状态。

### 8.4 硬件现状提醒

- **温度传感器**：`heating_detect.c` 中 MAX31850 驱动代码完整（含 CRC、ROM 搜索、故障检测），但因 PCB 上 4 路传感器共用单一大阻值上拉导致总线电容过大，**当前硬件无法稳定通信**，等待硬件改版。
- **电机 PWM**：采用反相逻辑（duty=8191 为停止，0 为全速），PID 参数 `Kp=8, Ki=0.02, Kd=0.01`，带软启动（启动 2 秒内限制 3000）。

---

## 9. 常见问题与排查提示

- **Django 开发服务器重启导致重复 MQTT 客户端**：代码中已通过 `if os.environ.get('RUN_MAIN')` 在 `apps.py` 和 `mqtt.py` 中做了防护，但使用 `runserver` 时仍需注意。
- **WebSocket 消息不同步**：若启动多个 Daphne 进程，InMemoryChannelLayer 无法跨进程广播，需切到 Redis。
- **ESP32 串口日志乱码**：`esp32_serial_logger.py` 已内置 `utf-8` / `gbk` / `latin-1` 降级解码。
- **任务调度器未启动**：`task_manager.py` 不会随 Django 自动启动，忘记运行它会导致 `Spinning` 定时任务失效。
- **外设脚本 Broker IP 不一致**：`motor_driver.py` 连接 `192.168.31.74`，`task_manager.py` 连接 `192.168.31.18`，ESP32 连接 `192.168.110.31`，注意区分不同网段或历史配置残留。
