# 前端模块化组件重构计划（MVP）

> 目标：在**最小影响**前提下，将 `vue_frontend` 前端进行**合理组件化拆分**，完整保留当前所有旧功能，不删除任何现有页面或接口。后续再做统一的功能删减或升级。

---

## 1. 背景与目标

### 1.1 当前痛点

- **无组件目录**：`src/components/` 不存在，所有 UI 写在 `src/views/` 内。
- **视图文件过大**：
  - `Dashboard.vue` 1,698 行
  - `RecipeDemo.vue` 1,295 行
  - `Spinning.vue` 719 行
- **重复样式**：`.panel-header`、`.console-header`、`.metric-row`、`.status-chip`、表格 CSS 在多个视图中重复。
- **表格手写**：`vxe-table` 已全局注册但未被使用，当前表格均为 CSS grid 手写实现。
- **axios 调用分散**：各视图直接 `import axios`，URL 与 token 组装逻辑重复。
- **无测试**：项目没有测试配置。

### 1.2 本次 MVP 目标

1. **基础层建设**：建立 `src/components/ui/`、`src/components/dashboard/`、`src/components/spinning/`、`src/services/api/`、`src/__tests__/`。
2. **Dashboard 设备表格 vxe-table 化**：仅替换 Dashboard 中的设备表格为 `DeviceStatusTable.vue`（基于 `vxe-table`），其余部分保持原样。
3. **Spinning.vue 完整拆分**：将 719 行的 Spinning 页面拆分为多个业务组件，视图仅保留编排逻辑。
4. **axios 收敛**：按业务域拆分 API 模块。
5. **引入单元测试**：覆盖 `components/ui/` 和 `services/`，使用 Vitest + `@vue/test-utils` + `jsdom`。
6. **功能无损**：所有现有功能、交互、样式、路由保持不变，仅做物理拆分与抽象。

---

## 2. 设计原则

| 原则 | 说明 |
|---|---|
| **选项式 API** | 新组件统一使用 `export default { ... }` 选项式 API，与现有代码风格一致。 |
| **功能零删减** | 不删除任何现有页面、路由、API 调用、按钮或提示文案。 |
| **最小侵入** | 不修改后端接口、Vuex Store、Router、WebSocket 服务核心逻辑。 |
| **样式就近** | 每个组件的 `<style scoped>` 只保留该组件需要的样式；公共基础样式沉淀到 `components/ui/`。 |
| **props 向下、events 向上** | 业务组件通过 props 接收数据，通过 `$emit` 通知父视图执行动作。 |
| **vxe-table 逐步替换** | 本次只在 Dashboard 设备表格试点使用 `vxe-table`，其余手写表格保留。 |

---

## 3. 目录结构（MVP 后）

```
vue_frontend/src/
├── App.vue
├── main.js
├── router/
│   └── index.js              # 不变
├── store/
│   └── index.js              # 不变
├── services/
│   ├── websocket.js          # 不变（已存在）
│   └── api/
│       ├── client.js         # axios 实例 + 请求/响应拦截
│       ├── auth.js           # 登录/注册/Token 校验/改密码
│       ├── devices.js        # 设备列表、急停、恢复、下发
│       ├── motors.js         # 电机列表、Spinning 调度、mqtt_msg
│       ├── materials.js      # 物料/配方（为 RecipeDemo 预留）
│       └── jobs.js           # Job 相关（为 RecipeDemo 预留）
├── components/
│   ├── ui/                   # 纯 UI 组件，无业务逻辑
│   │   ├── PanelHeader.vue
│   │   ├── ConsoleHeader.vue
│   │   ├── MetricCard.vue
│   │   ├── StatusChip.vue
│   │   ├── ConnectionBar.vue
│   │   └── LiveEventStream.vue
│   ├── dashboard/            # Dashboard 业务组件
│   │   ├── FleetSummary.vue
│   │   ├── DeviceStatusTable.vue
│   │   ├── DeviceDetailRow.vue
│   │   └── OperatorRail.vue
│   └── spinning/             # Spinning 业务组件
│       ├── MotorStatusBoard.vue
│       ├── ScheduleForm.vue
│       ├── ScheduleQueue.vue
│       └── QuickControl.vue
├── __tests__/                # Vitest 测试目录
│   ├── ui/
│   ├── api/
│   └── setup.js
└── views/
    ├── Dashboard.vue         # 仅保留编排，设备表格替换为组件
    ├── Dashboard/
    │   ├── Spinning.vue      # 仅保留编排
    │   ├── RecipeDemo.vue    # 本次不变
    │   └── Websocket.vue     # 本次不变
    └── ...                   # 其他视图本次不变
```

---

## 4. 组件设计详述

### 4.1 UI 组件（`components/ui/`）

#### `PanelHeader.vue`

统一 `.panel-header` + `.panel-kicker` + `.panel-title` + `.panel-badge`。

```vue
<PanelHeader kicker="Fleet" title="Motor Status Board" badge="Inventory">
  <!-- slot：右侧额外操作 -->
</PanelHeader>
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `kicker` | `String` | `''` | 小标题 |
| `title` | `String` | `''` | 主标题 |
| `badge` | `String` / `Boolean` | `''` | 右上角标签，传 `false` 隐藏 |

- **Slot**：`default` — 标题右侧的自定义内容（如刷新按钮、清除按钮）。
- **样式来源**：聚合 `Dashboard.vue`、`RecipeDemo.vue`、`Spinning.vue` 中重复的 `.panel-header` 样式。

#### `ConsoleHeader.vue`

统一 `.console-header` + `.eyebrow` + `.console-title` + `.console-copy` + 右侧状态芯片。

```vue
<ConsoleHeader
  eyebrow="Motor Control Console"
  title="Spinning Operations"
  copy="Schedule spin tasks..."
  :status-items="[{ label: 'Motors', value: motors.length }, { label: 'Jobs', value: records.length }]"
/>
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `eyebrow` | `String` | `''` | 顶部小字 |
| `title` | `String` | `''` | 页面标题 |
| `copy` | `String` | `''` | 描述文案 |
| `statusItems` | `Array<{label, value}>` | `[]` | 右侧状态芯片列表 |

#### `MetricCard.vue`

统一 `.metric-card`。

```vue
<MetricCard label="Selected Motor" :value="motor_selected || 'Not selected'" />
<MetricCard label="Schedule Queue" :value="records.length" accent />
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `label` | `String` | `''` | 标签 |
| `value` | `String` / `Number` | `''` | 值 |
| `accent` | `Boolean` | `false` | 是否高亮左边框 |

#### `StatusChip.vue`

统一 `.status-pill`、`.availability-pill`、Dashboard 中的状态标签。

```vue
<StatusChip label="Online" value="Responsive" variant="success" />
<StatusChip label="E-Stopped" value="2" variant="danger" />
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `label` | `String` | `''` | 小字标签 |
| `value` | `String` / `Number` | `''` | 主值 |
| `variant` | `String` | `'default'` | `success` / `danger` / `warning` / `info` / `default` |

#### `ConnectionBar.vue`

Dashboard 顶部 WebSocket 连接状态条。

```vue
<ConnectionBar :status="wsStatus" :mqtt-available="mqttAvailable" />
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `status` | `String` | `'disconnected'` | `connected` / `connecting` / `disconnected` |
| `mqttAvailable` | `Boolean` / `null` | `null` | MQTT 是否可用 |

#### `LiveEventStream.vue`

实时事件流面板。

```vue
<LiveEventStream :events="liveEvents" :max-events="50" @clear="clearEvents" />
```

| Prop | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `events` | `Array` | `[]` | 事件列表 |
| `maxEvents` | `Number` | `50` | 最大保留条数（由父视图控制，组件只展示） |

- **Events**：
  - `clear` — 用户点击 "Clear" 按钮时触发。

---

### 4.2 Dashboard 业务组件（`components/dashboard/`）

#### `FleetSummary.vue`

Dashboard 顶部 6 个统计卡。

```vue
<FleetSummary :summary="summary" />
```

| Prop | 类型 | 说明 |
|---|---|---|
| `summary` | `Object` | `{ total, online, busy, estopped, idle, offline }` |

- **内部实现**：6 个 `MetricCard` 组合，保留当前 6 列 grid 布局。

#### `DeviceStatusTable.vue`

基于 `vxe-table` 的设备表格，替代 Dashboard 当前手写 CSS grid 表格。

```vue
<DeviceStatusTable
  :devices="devices"
  :selected-ids="selectedDeviceIds"
  :expanded-ids="expandedDeviceIds"
  :loading="loading"
  @update:selected-ids="selectedDeviceIds = $event"
  @update:expanded-ids="expandedDeviceIds = $event"
  @refresh="getDeviceList"
/>
```

| Prop | 类型 | 说明 |
|---|---|---|
| `devices` | `Array` | 规范化后的设备列表 |
| `selectedIds` | `Array` | 当前选中的设备 ID 列表 |
| `expandedIds` | `Array` | 当前展开的设备 ID 列表 |
| `loading` | `Boolean` | 是否正在加载 |

- **Events**：
  - `update:selected-ids(ids)` — 选中变化时同步给父视图。
  - `update:expanded-ids(ids)` — 展开行变化时同步给父视图。
  - `refresh` — 用户点击刷新按钮。

- **表格列**：
  - 复选框列（`type="checkbox"`）
  - Device（`label` + `deviceId`）
  - Status（`taskStatus`，用 `StatusChip` 渲染）
  - Task（当前任务电机/速度/剩余时间）
  - Connection（`connectionStatus`）
  - Last Seen（`lastSeenText`）
  - Expand（`type="expand"`）

- **展开行**：内嵌 `DeviceDetailRow` 组件。

#### `DeviceDetailRow.vue`

表格展开行的内容：Telemetry、Current Task、Temperature。

```vue
<DeviceDetailRow :device="device" />
```

| Prop | 类型 | 说明 |
|---|---|---|
| `device` | `Object` | 单个规范化后的设备对象 |

- **保留逻辑**：
  - 4 路电机 PWM / PCNT 展示
  - Temperature N/A 占位
  - Current Task 进度条与倒计时

#### `OperatorRail.vue`

右侧操作面板：急停、恢复、跳转入口、Runbook、Platform Context。

```vue
<OperatorRail
  :selected-count="selectedDeviceIds.length"
  :dispatch-form="dispatchForm"
  @emergency-stop="emergencyStop"
  @resume="resumeDevices"
  @dispatch-task="dispatchTaskToSelected"
/>
```

| Prop | 类型 | 说明 |
|---|---|---|
| `selectedCount` | `Number` | 选中设备数量 |
| `dispatchForm` | `Object` | `{ motor, speed, duration }`，用于任务下发表单 |

- **Events**：
  - `emergency-stop(scope)` — `scope` 为 `'single'` 或 `'broadcast'`。
  - `resume` — 恢复选中设备。
  - `dispatch-task(form)` — 下发任务，携带当前表单值。

- **注意**：
  - 保留 `confirm()` 二次确认。
  - `Stop Selected` 在 `selectedCount === 0` 时禁用。
  - 跳转入口（Material Orchestration / Motor Scheduling / Realtime Console）保持 `router-link`。
  - 如果当前 Dashboard 模板中没有显示 dispatch 表单 UI，则 `dispatchForm` 可先作为预留 props 传入，不渲染表单；等后续需要时再扩展。

---

### 4.3 Spinning 业务组件（`components/spinning/`）

#### `MotorStatusBoard.vue`

电机状态板（Fleet 面板）。

```vue
<MotorStatusBoard :motors="motors" />
```

| Prop | 类型 | 说明 |
|---|---|---|
| `motors` | `Array` | 电机列表 |

- **保留逻辑**：表格展示 ID / Name / Availability / Description。
- **样式**：继续用手写 grid 表格（本次不替换），但把结构封装到组件内。

#### `ScheduleForm.vue`

调度表单。

```vue
<ScheduleForm
  :motors="motors"
  :model-value="scheduleForm"
  :errors="errors"
  @update:model-value="scheduleForm = $event"
  @submit="submitSchedule"
/>
```

| Prop | 类型 | 说明 |
|---|---|---|
| `motors` | `Array` | 电机下拉选项 |
| `modelValue` | `Object` | `{ motor_name, scheduled_time, motor_speed, duration_sec }` |
| `errors` | `Array` | 错误信息列表 |

- **Events**：
  - `update:model-value(form)` — 表单变化同步。
  - `submit` — 提交表单。

- **保留逻辑**：
  - `VueDatePicker` 绑定（组件内部使用 `date` ref，emit 完整 form）。
  - 提交按钮 `Create Schedule`。

#### `ScheduleQueue.vue`

已预约任务队列。

```vue
<ScheduleQueue :records="records" />
```

| Prop | 类型 | 说明 |
|---|---|---|
| `records` | `Array` | 调度记录列表 |

#### `QuickControl.vue`

实时控制面板。

```vue
<QuickControl
  :real-speed="real_speed"
  :target-speed="target_speed"
  @update:target-speed="target_speed = $event"
  @send="set_speed"
/>
```

| Prop | 类型 | 说明 |
|---|---|---|
| `realSpeed` | `Number` | 当前速度 |
| `targetSpeed` | `Number` | 目标速度 |

- **Events**：
  - `update:target-speed(value)` — 目标速度输入变化。
  - `send` — 发送电机指令。

---

## 5. API 模块设计（`services/api/`）

### 5.1 `client.js`

创建 axios 实例，保留 `baseURL` 配置。

```js
import axios from 'axios'

const apiClient = axios.create({
  baseURL: 'http://127.0.0.1:8000',
  timeout: 10000,
  headers: {
    'Content-Type': 'application/json'
  }
})

export default apiClient
```

### 5.2 `auth.js`

```js
import client from './client'

export default {
  validateToken(token) {
    return client.post('/api/token_validation/', { token })
  },
  login(email, password) {
    return client.post('/api/login/', { email, password })
  },
  signup(email, password) {
    return client.post('/api/signup/', { email, password })
  },
  changePassword(token, oldPassword, newPassword) {
    return client.post('/api/change_password/', {
      token,
      old_password: oldPassword,
      new_password: newPassword
    })
  }
}
```

### 5.3 `devices.js`

```js
import client from './client'

export default {
  getList() {
    return client.get('/api/device_list/')
  },
  emergencyStop(deviceIds, scope, reason, triggeredBy) {
    // 注意：原代码通过 WebSocket 发送，这里预留 REST 版本
    // MVP 阶段 Dashboard 仍通过 WebSocket 发送，本文件为后续扩展
    return client.post('/api/devices/emergency_stop/', {
      device_ids: deviceIds,
      scope,
      reason,
      triggered_by: triggeredBy
    })
  },
  resume(deviceIds, resumedBy) {
    return client.post('/api/devices/resume/', {
      device_ids: deviceIds,
      resumed_by: resumedBy
    })
  },
  dispatchTask(deviceId, motor, speed, duration) {
    return client.post('/api/devices/dispatch_task/', {
      device_id: deviceId,
      motor,
      speed,
      duration
    })
  }
}
```

### 5.4 `motors.js`

```js
import client from './client'

export default {
  getList(token) {
    return client.post('/api/get_motors/', { token })
  },
  getRecords(token) {
    return client.post('/api/spinning/', { token, data: null })
  },
  createSchedule(token, payload) {
    return client.post('/api/spinning/', {
      token,
      data: payload
    })
  },
  sendMqttMsg(topic, msg) {
    return client.post('/api/mqtt_msg/', { topic, msg })
  },
  getMqttMsg() {
    return client.get('/api/mqtt_msg/')
  }
}
```

### 5.5 预留模块

- `materials.js`：为 `RecipeDemo.vue` 后续拆分预留。
- `jobs.js`：为 `RecipeDemo.vue` 后续拆分预留。

---

## 6. 视图改造说明

### 6.1 `Dashboard.vue`

**本次改动范围**：
- 用 `ConnectionBar`、`FleetSummary`、`DeviceStatusTable`、`OperatorRail`、`LiveEventStream` 替换对应模板区域。
- 保留所有 data / computed / methods（设备规范化、WebSocket 监听、急停/恢复/下发、事件流、倒计时）。
- 保留所有 scoped 样式，但只保留视图级样式（如 `.operations-dashboard`、`.command-header`、`.summary-bar` 等）；表格样式随 `DeviceStatusTable` 组件迁移。
- axios 调用改为 `devicesApi.getList()`。

**改造后结构**：

```vue
<template>
  <section class="operations-dashboard">
    <ConnectionBar :status="wsStatus" :mqtt-available="mqttAvailable" />
    <header class="command-header">...</header>
    <FleetSummary :summary="summary" />
    <div class="console-grid">
      <section class="panel-card panel-card--status">
        <PanelHeader kicker="Fleet Monitor" title="Device Status Board" :badge="loading ? 'Updating' : 'Live'" />
        <div class="status-toolbar">...</div>
        <DeviceStatusTable
          :devices="devices"
          :selected-ids="selectedDeviceIds"
          :expanded-ids="expandedDeviceIds"
          :loading="loading"
          @update:selected-ids="selectedDeviceIds = $event"
          @update:expanded-ids="expandedDeviceIds = $event"
          @refresh="getDeviceList"
        />
      </section>
      <OperatorRail
        :selected-count="selectedDeviceIds.length"
        :dispatch-form="dispatchForm"
        @emergency-stop="emergencyStop"
        @resume="resumeDevices"
        @dispatch-task="dispatchTaskToSelected"
      />
    </div>
    <LiveEventStream :events="liveEvents" @clear="clearEvents" />
  </section>
</template>
```

### 6.2 `Dashboard/Spinning.vue`

**本次改动范围**：完整拆分。

- 模板只调用 `ConsoleHeader`、`MetricCard`、`PanelHeader`、`MotorStatusBoard`、`ScheduleForm`、`ScheduleQueue`、`QuickControl`。
- `data()` 保留 `motors`、`records`、`errors`、`real_speed`、`target_speed`、`listen_started`、`listener`。
- `setup()` 中 `date` ref 保留，但改为 `scheduleForm.scheduled_time` 的本地状态管理。
- methods 保留 `getMotors`、`getRecords`、`submit`（改名为 `submitSchedule`）、`datetime_formatter`、`set_speed`、`get_speed`。
- 所有手写表格样式迁移到对应组件的 `<style scoped>`。
- axios 调用改为 `motorsApi.getList()`、`motorsApi.getRecords()`、`motorsApi.createSchedule()`、`motorsApi.sendMqttMsg()`、`motorsApi.getMqttMsg()`。

**改造后结构**：

```vue
<template>
  <section class="spinning-console">
    <ConsoleHeader
      eyebrow="Motor Control Console"
      title="Spinning Operations"
      copy="..."
      :status-items="[{ label: 'Motors', value: motors.length }, { label: 'Scheduled Jobs', value: records.length }]"
    />
    <section class="metric-row">
      <MetricCard label="Selected Motor" :value="scheduleForm.motor_name || 'Not selected'" />
      <MetricCard label="Current Speed" :value="real_speed || 0" />
      <MetricCard label="Target Speed" :value="target_speed || 0" />
      <MetricCard label="Schedule Queue" :value="records.length" accent />
    </section>
    <div class="console-grid">
      <section class="panel-card">
        <PanelHeader kicker="Fleet" title="Motor Status Board" badge="Inventory" />
        <MotorStatusBoard :motors="motors" />
      </section>
      <section class="panel-card">
        <PanelHeader kicker="Scheduling" title="Register Spin Task" badge="Operator Entry" />
        <ScheduleForm
          :motors="motors"
          v-model="scheduleForm"
          :errors="errors"
          @submit="submitSchedule"
        />
      </section>
    </div>
    <div class="console-grid console-grid--bottom">
      <section class="panel-card">
        <PanelHeader kicker="Queue" title="Registration List" :badge="records.length + ' Item' + (records.length === 1 ? '' : 's')" />
        <ScheduleQueue :records="records" />
      </section>
      <section class="panel-card">
        <PanelHeader kicker="Live Control" title="Operating Information" badge="Realtime" />
        <QuickControl
          :real-speed="real_speed"
          :target-speed="target_speed"
          @update:target-speed="target_speed = $event"
          @send="set_speed"
        />
      </section>
    </div>
  </section>
</template>
```

---

## 7. 测试策略

### 7.1 依赖安装

```bash
cd vue_frontend
npm install --save-dev vitest @vue/test-utils jsdom @vitejs/plugin-vue
```

### 7.2 配置

- 新增 `vitest.config.js`（或扩展 `vite.config.js`）：
  - `test.environment: 'jsdom'`
  - `globals: true`
  - `setupFiles: ['./src/__tests__/setup.js']`
- `package.json` 增加 scripts：
  - `"test": "vitest"`
  - `"test:ui": "vitest --ui"`（可选）

### 7.3 测试范围

#### UI 组件测试（`src/__tests__/ui/`）

- `PanelHeader.spec.js`：验证 kicker/title/badge 渲染、slot 内容。
- `ConsoleHeader.spec.js`：验证标题、copy、status-items 渲染。
- `MetricCard.spec.js`：验证 label/value 渲染、accent 样式类。
- `StatusChip.spec.js`：验证 variant 对应的样式类。
- `ConnectionBar.spec.js`：验证三种状态文本与样式。
- `LiveEventStream.spec.js`：验证事件列表渲染、空状态、clear 事件触发。

#### Services 测试（`src/__tests__/api/`）

- `client.spec.js`：验证 axios 实例 baseURL 与 headers。
- `auth.spec.js`：使用 `vi.mock('axios')` 或 `nock` 验证接口调用。
- `motors.spec.js`：验证 `getList`、`createSchedule`、`sendMqttMsg` 的 URL 和 payload。
- `devices.spec.js`：验证 `getList` 调用 `/api/device_list/`。

#### WebSocket 服务测试（`src/__tests__/services/`）

- `websocket.spec.js`：
  - 单例模式验证。
  - `subscribe` / `onStatusChange` 回调触发。
  - `send` 在连接未建立时返回 false。

### 7.4 Mock 策略

- Vuex：在测试中使用 `global.plugins: [createStore({...})]`。
- Vue Router：使用 `createRouter` mock 或 `global.mocks: { $router: {...} }`。
- axios：使用 `vi.spyOn(apiClient, 'post')` 或 `vi.mock('./client.js')`。

---

## 8. 实施步骤

### 阶段 1：环境与基础层（0.5 天）

1. 安装测试依赖：`vitest`、`@vue/test-utils`、`jsdom`。
2. 配置 `vitest.config.js` 与 `package.json` 测试脚本。
3. 创建 `src/services/api/client.js`。
4. 创建 `src/services/api/auth.js`、`motors.js`、`devices.js`。
5. 编写 `client.spec.js`、`auth.spec.js`、`motors.spec.js`、`devices.spec.js`。

### 阶段 2：UI 基础组件（1 天）

1. 创建 `src/components/ui/PanelHeader.vue` 及测试。
2. 创建 `src/components/ui/ConsoleHeader.vue` 及测试。
3. 创建 `src/components/ui/MetricCard.vue` 及测试。
4. 创建 `src/components/ui/StatusChip.vue` 及测试。
5. 创建 `src/components/ui/ConnectionBar.vue` 及测试。
6. 创建 `src/components/ui/LiveEventStream.vue` 及测试。

### 阶段 3：Dashboard 设备表格 vxe-table 化（1~1.5 天）

1. 创建 `src/components/dashboard/DeviceDetailRow.vue`。
2. 创建 `src/components/dashboard/DeviceStatusTable.vue`（基于 `vxe-table`，展开行使用 `DeviceDetailRow`）。
3. 创建 `src/components/dashboard/FleetSummary.vue`。
4. 创建 `src/components/dashboard/OperatorRail.vue`。
5. 改造 `Dashboard.vue`：
   - 引入 UI 组件与 Dashboard 组件。
   - 替换模板中的连接状态条、摘要栏、设备表格、操作面板、事件流。
   - 将 `axios.get('/api/device_list/')` 改为 `devicesApi.getList()`。
   - 保留所有 WebSocket 监听与操作方法。
6. 运行 `npm run test` 与 `npm run build`，验证无报错。

### 阶段 4：Spinning.vue 完整拆分（1~1.5 天）

1. 创建 `src/components/spinning/MotorStatusBoard.vue`。
2. 创建 `src/components/spinning/ScheduleForm.vue`。
3. 创建 `src/components/spinning/ScheduleQueue.vue`。
4. 创建 `src/components/spinning/QuickControl.vue`。
5. 改造 `Spinning.vue`：
   - 引入 UI 组件与 Spinning 组件。
   - 将表单数据收敛到 `scheduleForm` 对象。
   - 将 axios 调用改为 `motorsApi.*`。
   - 保留 `get_speed` 轮询逻辑与 `beforeRouteLeave` 清理。
6. 运行测试与构建。

### 阶段 5：回归验证（0.5 天）

1. 启动 Django 后端与 task_manager。
2. 启动前端 dev server。
3. 验证：
   - Dashboard 设备列表正常加载。
   - 设备表格全选 / 单选 / 展开正常。
   - 急停 / 恢复按钮交互正常（包括 confirm 弹窗）。
   - WebSocket 连接状态条颜色变化正常。
   - Spinning 页面电机列表、调度表单、队列、实时控制功能正常。
   - 构建产物无错误。

---

## 9. 风险与回滚

| 风险 | 影响 | 缓解措施 |
|---|---|---|
| `vxe-table` 与现有 Bulma/Tailwind 样式冲突 | 表格外观异常 | 仅替换 Dashboard 设备表格一处，出问题可快速回退到旧表格；`DeviceStatusTable` 组件可临时切换为旧 grid 实现。 |
| 组件 props 传递导致状态不同步 | 选中/展开状态丢失 | 使用 `v-model` 风格双向同步，`Dashboard.vue` 保留 `selectedDeviceIds` / `expandedDeviceIds` 为唯一数据源。 |
| 测试环境配置导致构建失败 | CI / 构建受影响 | `vitest` 相关依赖只作为 `devDependencies`，不影响生产构建；配置独立 `vitest.config.js`。 |
| Spinning 拆分后日期选择器行为变化 | 表单提交异常 | 保留 `VueDatePicker` 使用方式，`datetime_formatter` 逻辑保留在视图内，组件只负责收集。 |
| API 模块封装后 token 传递遗漏 | 接口 401 | `motorsApi.getList(token)` 显式传入 token，与旧代码完全一致。 |

### 回滚策略

- 所有改动以新增文件为主，旧视图文件用 `StrReplaceFile` 局部替换模板与 axios 调用。
- 若出现问题，可通过 git 回滚单个视图的模板部分，组件文件可保留不引用。

---

## 10. 文件清单（MVP 新增/修改）

### 新增文件

```
vue_frontend/
├── vitest.config.js
├── src/__tests__/
│   ├── setup.js
│   ├── ui/PanelHeader.spec.js
│   ├── ui/ConsoleHeader.spec.js
│   ├── ui/MetricCard.spec.js
│   ├── ui/StatusChip.spec.js
│   ├── ui/ConnectionBar.spec.js
│   ├── ui/LiveEventStream.spec.js
│   ├── api/client.spec.js
│   ├── api/auth.spec.js
│   ├── api/motors.spec.js
│   ├── api/devices.spec.js
│   └── services/websocket.spec.js
├── src/services/api/
│   ├── client.js
│   ├── auth.js
│   ├── motors.js
│   ├── devices.js
│   ├── materials.js
│   └── jobs.js
└── src/components/
    ├── ui/
    │   ├── PanelHeader.vue
    │   ├── ConsoleHeader.vue
    │   ├── MetricCard.vue
    │   ├── StatusChip.vue
    │   ├── ConnectionBar.vue
    │   └── LiveEventStream.vue
    ├── dashboard/
    │   ├── FleetSummary.vue
    │   ├── DeviceStatusTable.vue
    │   ├── DeviceDetailRow.vue
    │   └── OperatorRail.vue
    └── spinning/
        ├── MotorStatusBoard.vue
        ├── ScheduleForm.vue
        ├── ScheduleQueue.vue
        └── QuickControl.vue
```

### 修改文件

```
vue_frontend/
├── package.json                # 新增 devDependencies 与 test scripts
├── vite.config.js              # 可选：不做修改，测试配置放在 vitest.config.js
├── src/views/Dashboard.vue     # 引入组件、替换模板区域、改用 api 模块
└── src/views/Dashboard/Spinning.vue  # 引入组件、完整拆分、改用 api 模块
```

### 不改动文件

- `src/App.vue`
- `src/main.js`
- `src/store/index.js`
- `src/router/index.js`
- `src/services/websocket.js`
- `src/views/Dashboard/RecipeDemo.vue`
- `src/views/Dashboard/Websocket.vue`
- 其他小型视图（Login/Signup/MyAccount/ChangePassword/Test/About/Home）

---

## 11. 验收标准

- [ ] `npm run test` 全部通过。
- [ ] `npm run build` 无错误、无警告。
- [ ] Dashboard 设备表格渲染正常，支持全选、单选、展开行。
- [ ] Dashboard 急停 / 恢复 / 刷新功能与改造前一致。
- [ ] Spinning 页面电机列表、调度表单、队列、实时控制功能与改造前一致。
- [ ] 所有现有路由可正常访问。
- [ ] 不引入任何功能删减或交互变更。
