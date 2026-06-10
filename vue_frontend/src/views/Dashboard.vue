<template>
    <section class="operations-dashboard">
        <!-- 连接状态条 -->
        <div class="connection-bar" :class="wsStatusClass">
            <span class="connection-dot"></span>
            <span class="connection-label">{{ wsStatusLabel }}</span>
            <span v-if="mqttAvailable !== null" class="connection-sublabel">
                · MQTT {{ mqttAvailable ? '可用' : '不可用' }}
            </span>
        </div>

        <header class="command-header">
            <div class="command-header__main">
                <p class="eyebrow">SmartLab Control Surface</p>
                <h1 class="title command-title">Lab Device Operations</h1>
                <p class="command-copy">
                    Supervisory view for connected controllers, operator tools, and execution readiness across the lab platform.
                </p>
            </div>
            <div class="command-header__actions">
                <div class="sync-card">
                    <span class="sync-label">Status Poll</span>
                    <span class="sync-value">{{ loading ? 'Running' : 'Ready' }}</span>
                </div>
                <button class="button refresh-button" @click="getDeviceList" :disabled="loading">
                    {{ loading ? 'Refreshing…' : 'Refresh Fleet' }}
                </button>
            </div>
        </header>

        <section class="summary-bar">
            <article class="summary-metric">
                <span class="summary-metric__label">Total Devices</span>
                <span class="summary-metric__value">{{ summary.total }}</span>
            </article>
            <article class="summary-metric summary-metric--good">
                <span class="summary-metric__label">Online</span>
                <span class="summary-metric__value">{{ summary.online }}</span>
            </article>
            <article class="summary-metric summary-metric--busy">
                <span class="summary-metric__label">Busy</span>
                <span class="summary-metric__value">{{ summary.busy }}</span>
            </article>
            <article class="summary-metric summary-metric--alert">
                <span class="summary-metric__label">E-Stopped</span>
                <span class="summary-metric__value">{{ summary.estopped }}</span>
            </article>
            <article class="summary-metric summary-metric--idle">
                <span class="summary-metric__label">Idle</span>
                <span class="summary-metric__value">{{ summary.idle }}</span>
            </article>
            <article class="summary-metric summary-metric--offline">
                <span class="summary-metric__label">Offline</span>
                <span class="summary-metric__value">{{ summary.offline }}</span>
            </article>
        </section>

        <div class="console-grid">
            <section class="panel-card panel-card--status">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Fleet Monitor</p>
                        <h2 class="panel-title">Device Status Board</h2>
                    </div>
                    <span class="panel-badge">{{ loading ? 'Updating' : 'Live' }}</span>
                </div>

                <div class="status-toolbar">
                    <div class="toolbar-item">
                        <span class="toolbar-label">Broker Source</span>
                        <span class="toolbar-value">EMQX</span>
                    </div>
                    <div class="toolbar-item">
                        <span class="toolbar-label">Connection Health</span>
                        <span class="toolbar-value" :class="summary.online ? 'toolbar-value--good' : 'toolbar-value--warn'">
                            {{ summary.online ? 'Responsive' : 'Unavailable' }}
                        </span>
                    </div>
                    <div class="toolbar-item" v-if="selectedDeviceIds.length">
                        <span class="toolbar-label">Selected</span>
                        <span class="toolbar-value toolbar-value--highlight">{{ selectedDeviceIds.length }}</span>
                    </div>
                </div>

                <div v-if="errorMessage" class="status-alert">
                    {{ errorMessage }}
                </div>

                <div v-if="devices.length" class="device-table">
                    <div class="device-table__head">
                        <span class="head-cell head-cell--check">
                            <input type="checkbox" :checked="isAllSelected" @change="toggleSelectAll">
                        </span>
                        <span>Device</span>
                        <span>Status</span>
                        <span>Task</span>
                        <span>Connection</span>
                        <span>Last Seen</span>
                        <span></span>
                    </div>
                    <div class="device-table__body">
                        <template v-for="device in devices" :key="device.id">
                            <article class="device-row" :class="{'device-row--selected': isSelected(device.id), 'device-row--estopped': device.taskStatus === 'E-Stopped', 'device-row--offline': device.connectionStatus === 'Offline'}">
                                <div class="device-cell device-cell--check">
                                    <input type="checkbox" :checked="isSelected(device.id)" @change="toggleSelect(device.id)">
                                </div>
                                <div class="device-cell">
                                    <span class="device-label">{{ device.label }}</span>
                                    <span class="device-index">{{ device.deviceId }}</span>
                                </div>
                                <div class="device-cell">
                                    <span class="status-pill" :class="statusClass(device.taskStatus)">
                                        {{ device.taskStatus }}
                                    </span>
                                </div>
                                <div class="device-cell">
                                    <div v-if="device.currentTask && device.currentTask.motor !== undefined" class="task-mini">
                                        <span class="task-mini__motor">M{{ device.currentTask.motor }}</span>
                                        <span class="task-mini__speed">{{ device.currentTask.speed }} rpm</span>
                                        <span v-if="device.currentTask.remainingSec > 0" class="task-mini__remaining">{{ device.currentTask.remainingSec }}s left</span>
                                    </div>
                                    <span v-else class="task-mini task-mini--empty">--</span>
                                </div>
                                <div class="device-cell">
                                    <span class="status-pill" :class="connectionClass(device.connectionStatus)">
                                        {{ device.connectionStatus }}
                                    </span>
                                </div>
                                <div class="device-cell device-cell--time">{{ device.lastSeenText }}</div>
                                <div class="device-cell device-cell--action">
                                    <button class="expand-button" @click="toggleExpand(device.id)">
                                        {{ isExpanded(device.id) ? 'Collapse' : 'Expand' }}
                                    </button>
                                </div>
                            </article>
                            <article v-if="isExpanded(device.id)" class="device-detail-row">
                                <div class="device-detail">
                                    <div class="detail-section">
                                        <h4 class="detail-title">Telemetry</h4>
                                        <div class="motor-grid">
                                            <div class="motor-card" v-for="mIdx in 4" :key="mIdx">
                                                <span class="motor-card__label">Motor {{ mIdx - 1 }}</span>
                                                <div class="motor-card__values">
                                                    <span>PWM: {{ getTelemetry(device, mIdx - 1, 'pwm') }}</span>
                                                    <span>PCNT: {{ getTelemetry(device, mIdx - 1, 'pcnt') }}</span>
                                                </div>
                                            </div>
                                        </div>
                                        <div class="temperature-row">
                                            <span class="detail-label">Temperature:</span>
                                            <span class="detail-value detail-value--na">N/A (reserved for hardware update)</span>
                                        </div>
                                    </div>
                                    <div class="detail-section" v-if="device.currentTask && device.currentTask.motor !== undefined">
                                        <h4 class="detail-title">Current Task</h4>
                                        <div class="task-detail">
                                            <div class="task-detail__row">
                                                <span class="detail-label">Motor:</span>
                                                <span class="detail-value">{{ device.currentTask.motor }}</span>
                                            </div>
                                            <div class="task-detail__row">
                                                <span class="detail-label">Speed:</span>
                                                <span class="detail-value">{{ device.currentTask.speed }} rpm</span>
                                            </div>
                                            <div class="task-detail__row">
                                                <span class="detail-label">Duration:</span>
                                                <span class="detail-value">{{ device.currentTask.durationSec }} s</span>
                                            </div>
                                            <div class="task-detail__row">
                                                <span class="detail-label">Remaining:</span>
                                                <span class="detail-value">{{ device.currentTask.remainingSec }} s</span>
                                            </div>
                                            <div class="task-progress">
                                                <div class="task-progress__bar">
                                                    <div class="task-progress__fill" :style="{width: device.currentTask.progressPercent + '%'}"></div>
                                                </div>
                                                <span class="task-progress__text">{{ device.currentTask.progressPercent }}%</span>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            </article>
                        </template>
                    </div>
                </div>

                <div v-else-if="!loading" class="empty-state">
                    <p class="empty-title">No active controller records</p>
                    <p class="empty-copy">
                        The operations console is available, but the fleet board will populate only when the broker reports active clients.
                    </p>
                </div>
            </section>

            <section class="operations-rail">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Operator Rail</p>
                        <h2 class="panel-title">Manual Control Tools</h2>
                    </div>
                    <span class="panel-badge">Authorized</span>
                </div>

                <div class="action-stack">
                    <article class="action-card action-card--danger">
                        <div>
                            <p class="action-title">Emergency Stop</p>
                            <p class="action-copy">
                                Halt all motors on selected devices. Requires manual resume before further task dispatch.
                            </p>
                        </div>
                        <div class="action-buttons">
                            <button class="button estop-button" @click="emergencyStop('single')" :disabled="!selectedDeviceIds.length">
                                Stop Selected
                            </button>
                            <button class="button estop-button estop-button--broadcast" @click="emergencyStop('broadcast')">
                                Stop All
                            </button>
                        </div>
                    </article>

                    <article class="action-card">
                        <div>
                            <p class="action-title">Resume Devices</p>
                            <p class="action-copy">
                                Unlock task dispatch for selected E-Stopped devices after manual safety confirmation.
                            </p>
                        </div>
                        <button class="button resume-button" @click="resumeDevices" :disabled="!selectedDeviceIds.length">
                            Resume Selected
                        </button>
                    </article>


                    <article class="action-card">
                        <div>
                            <p class="action-title">Material Orchestration</p>
                            <p class="action-copy">
                                Route material or recipe input into backend planning and device command generation.
                            </p>
                        </div>
                        <router-link class="button action-button" to="/dashboard/material-demo">Launch</router-link>
                    </article>

                    <article class="action-card">
                        <div>
                            <p class="action-title">Motor Scheduling</p>
                            <p class="action-copy">
                                Register motor tasks, inspect actuator availability, and submit scheduled spin jobs.
                            </p>
                        </div>
                        <router-link class="button action-button" to="/dashboard/spinning">Launch</router-link>
                    </article>

                    <article class="action-card">
                        <div>
                            <p class="action-title">Realtime Device Console</p>
                            <p class="action-copy">
                                Inspect websocket events, PWM activity, PCNT feedback, and command flow in real time.
                            </p>
                        </div>
                        <router-link class="button action-button" to="/dashboard/websocket">Launch</router-link>
                    </article>
                </div>

                <section class="rail-card">
                    <p class="rail-title">Runbook</p>
                    <div class="runbook-row">
                        <span class="runbook-index">01</span>
                        <span class="runbook-copy">Confirm the target controller is online before issuing any manual command.</span>
                    </div>
                    <div class="runbook-row">
                        <span class="runbook-index">02</span>
                        <span class="runbook-copy">Use Emergency Stop only when immediate halt is required. Always resume after safety check.</span>
                    </div>
                    <div class="runbook-row">
                        <span class="runbook-index">03</span>
                        <span class="runbook-copy">Prefer scheduled workflows over ad hoc commands for repeatable lab operation.</span>
                    </div>
                </section>

                <section class="rail-card rail-card--compact">
                    <p class="rail-title">Platform Context</p>
                    <div class="context-row">
                        <span class="context-label">Transport</span>
                        <span class="context-value">MQTT / WebSocket</span>
                    </div>
                    <div class="context-row">
                        <span class="context-label">Mode</span>
                        <span class="context-value">Supervisory Control</span>
                    </div>
                    <div class="context-row">
                        <span class="context-label">Operator</span>
                        <span class="context-value">{{ $store.state.email || 'Authenticated User' }}</span>
                    </div>
                </section>
            </section>
        </div>

        <!-- 实时事件流 -->
        <section class="event-stream">
            <div class="panel-header">
                <div>
                    <p class="panel-kicker">Realtime Feed</p>
                    <h2 class="panel-title">Device Events</h2>
                </div>
                <button class="button clear-button" @click="clearEvents">Clear</button>
            </div>
            <div class="event-list">
                <div v-for="evt in liveEvents" :key="evt.key" class="event-row" :class="'event-row--' + evt.kind">
                    <span class="event-time">{{ evt.time }}</span>
                    <span class="event-device">{{ evt.device }}</span>
                    <span class="event-topic">{{ evt.topic }}</span>
                    <span class="event-summary">{{ evt.summary }}</span>
                </div>
                <div v-if="!liveEvents.length" class="event-empty">
                    Waiting for MQTT messages...
                </div>
            </div>
        </section>
    </section>
</template>

<script>
import axios from 'axios'
import WebSocketService from '@/services/websocket.js'

export default {
    name: 'DashboardView',
    created() {
        this.getDeviceList()
        this.initWebSocket()
        this.startTaskCountdown()
    },
    beforeUnmount() {
        if (this.countdownTimer) {
            clearInterval(this.countdownTimer)
        }
        // WebSocket 服务作为单例保留，不在这里断开
    },
    data() {
        return {
            devices: [],
            loading: false,
            errorMessage: '',
            wsStatus: 'disconnected',
            mqttAvailable: null,
            selectedDeviceIds: [],
            expandedDeviceIds: [],
            liveEvents: [],
            wsService: null,
            dispatchForm: {
                motor: 0,
                speed: 3000,
                duration: 10
            },
            countdownTimer: null,
            unsubscribeCallbacks: []
        }
    },
    computed: {
        summary() {
            const total = this.devices.length
            const online = this.devices.filter(d => d.connectionStatus === 'Online').length
            const busy = this.devices.filter(d => d.taskStatus === 'Busy').length
            const estopped = this.devices.filter(d => d.taskStatus === 'E-Stopped').length
            const idle = this.devices.filter(d => d.taskStatus === 'Idle').length
            const offline = total - online
            return { total, online, busy, estopped, idle, offline }
        },
        isAllSelected() {
            if (!this.devices.length) return false
            return this.devices.every(d => this.selectedDeviceIds.includes(d.id))
        },
        wsStatusLabel() {
            const map = {
                connected: 'WebSocket Connected',
                connecting: 'WebSocket Connecting…',
                disconnected: 'WebSocket Disconnected'
            }
            return map[this.wsStatus] || this.wsStatus
        },
        wsStatusClass() {
            return {
                'connection-bar--connected': this.wsStatus === 'connected',
                'connection-bar--connecting': this.wsStatus === 'connecting',
                'connection-bar--disconnected': this.wsStatus === 'disconnected'
            }
        }
    },
    methods: {
        statusClass(status) {
            return {
                'status-pill--online': status === 'Idle',
                'status-pill--busy': status === 'Busy',
                'status-pill--alert': status === 'E-Stopped',
                'status-pill--offline': status === 'Offline'
            }
        },
        connectionClass(status) {
            return {
                'status-pill--online': status === 'Online',
                'status-pill--offline': status !== 'Online'
            }
        },
        normalizeDevice(raw) {
            return {
                id: raw.device_id || raw.id,
                deviceId: raw.device_id || raw.id,
                label: raw.label || raw.device_id || `Device`,
                clientId: raw.client_id || 'Unknown',
                ipAddress: raw.ip_address || 'N/A',
                connectedTime: raw.connected_at || 'N/A',
                connectionStatus: raw.is_online ? 'Online' : 'Offline',
                taskStatus: this.mapTaskStatus(raw.task_status),
                lastSeenText: raw.last_heartbeat ? this.timeAgo(raw.last_heartbeat) : 'N/A',
                lastHeartbeatIso: raw.last_heartbeat,
                isRegistered: raw.is_registered,
                currentTask: this.normalizeTask(raw.current_task),
                telemetry: raw.telemetry || {},
                macAddress: raw.mac_address || '',
                raw
            }
        },
        mapTaskStatus(ts) {
            if (!ts) return 'Idle'
            const s = String(ts).toLowerCase()
            if (s === 'busy') return 'Busy'
            if (s === 'estopped') return 'E-Stopped'
            if (s === 'offline') return 'Offline'
            return 'Idle'
        },
        normalizeTask(task) {
            if (!task || task.motor === undefined) return {}
            const now = Date.now()
            const started = task.started_at ? new Date(task.started_at).getTime() : now
            const expected = task.expected_finished_at ? new Date(task.expected_finished_at).getTime() : (started + (task.duration_sec || 0) * 1000)
            const remaining = Math.max(0, Math.ceil((expected - now) / 1000))
            const total = task.duration_sec || 1
            const elapsed = total - remaining
            const progress = Math.min(100, Math.max(0, Math.round((elapsed / total) * 100)))
            return {
                motor: task.motor,
                speed: task.speed || 0,
                durationSec: task.duration_sec || 0,
                startedAt: task.started_at,
                expectedFinishedAt: task.expected_finished_at,
                remainingSec: remaining,
                progressPercent: progress
            }
        },
        updateTaskRemaining() {
            // 每秒刷新前端倒计时
            this.devices.forEach(d => {
                if (d.currentTask && d.currentTask.expectedFinishedAt) {
                    const expected = new Date(d.currentTask.expectedFinishedAt).getTime()
                    const remaining = Math.max(0, Math.ceil((expected - Date.now()) / 1000))
                    d.currentTask.remainingSec = remaining
                    const total = d.currentTask.durationSec || 1
                    const elapsed = total - remaining
                    d.currentTask.progressPercent = Math.min(100, Math.max(0, Math.round((elapsed / total) * 100)))
                }
            })
        },
        startTaskCountdown() {
            this.countdownTimer = setInterval(() => this.updateTaskRemaining(), 1000)
        },
        timeAgo(iso) {
            if (!iso) return 'N/A'
            const t = new Date(iso).getTime()
            const diff = Math.floor((Date.now() - t) / 1000)
            if (diff < 5) return 'just now'
            if (diff < 60) return `${diff}s ago`
            if (diff < 3600) return `${Math.floor(diff / 60)}m ago`
            return `${Math.floor(diff / 3600)}h ago`
        },
        getTelemetry(device, motorIdx, key) {
            const motorKey = `motor_${motorIdx}`
            const val = device.telemetry[motorKey] && device.telemetry[motorKey][key]
            return val !== undefined ? val : 'N/A'
        },
        async getDeviceList() {
            this.loading = true
            this.errorMessage = ''
            try {
                const response = await axios.get('/api/device_list/')
                const result = response.data || {}
                this.mqttAvailable = result.mqtt_available
                const list = result.data || []
                // 保留当前设备的选中/展开状态
                const selected = new Set(this.selectedDeviceIds)
                const expanded = new Set(this.expandedDeviceIds)
                this.devices = list.map((item, index) => {
                    const normalized = this.normalizeDevice(item)
                    normalized.label = normalized.label || `Device ${index + 1}`
                    return normalized
                })
                // 如果之前有选中/展开，保持（id 不变）
                this.selectedDeviceIds = this.devices.filter(d => selected.has(d.id)).map(d => d.id)
                this.expandedDeviceIds = this.devices.filter(d => expanded.has(d.id)).map(d => d.id)
            } catch (error) {
                this.errorMessage = 'Device status is temporarily unavailable. Check the EMQX broker connection and try again.'
                console.error(error)
            } finally {
                this.loading = false
            }
        },
        initWebSocket() {
            this.wsService = WebSocketService.getInstance()
            this.wsService.connect()

            // 状态监听
            const unsubStatus = this.wsService.onStatusChange((status) => {
                this.wsStatus = status
            })

            // 各种 topic 监听
            const unsubHeartbeat = this.wsService.subscribe('heartbeat', (payload) => {
                this.handleHeartbeat(payload)
            })
            const unsubTelemetry = this.wsService.subscribe('telemetry', (payload) => {
                this.handleTelemetry(payload)
            })
            const unsubTask = this.wsService.subscribe('task_status', (payload) => {
                this.handleTaskStatus(payload)
            })
            const unsubStatusEvent = this.wsService.subscribe('device_status', (payload) => {
                this.handleDeviceStatus(payload)
            })
            const unsubSnapshot = this.wsService.subscribe('device_snapshot', (payload) => {
                this.handleSnapshot(payload)
            })
            const unsubEstopResult = this.wsService.subscribe('estop_result', (payload) => {
                this.addEvent({
                    kind: 'estop',
                    device: payload.scope === 'broadcast' ? 'ALL' : (payload.results || []).map(r => r.device_id).join(', '),
                    topic: 'E-Stop',
                    summary: `Emergency stop executed (${payload.scope})`,
                    time: new Date().toLocaleTimeString()
                })
                // 刷新列表以同步后端状态
                this.getDeviceList()
            })
            const unsubResumeResult = this.wsService.subscribe('resume_result', (payload) => {
                this.addEvent({
                    kind: 'resume',
                    device: (payload.results || []).map(r => r.device_id).join(', '),
                    topic: 'Resume',
                    summary: 'Devices resumed',
                    time: new Date().toLocaleTimeString()
                })
                this.getDeviceList()
            })
            const unsubDispatchResult = this.wsService.subscribe('dispatch_result', (payload) => {
                this.addEvent({
                    kind: payload.success ? 'dispatch' : 'error',
                    device: payload.topic || '',
                    topic: 'Dispatch',
                    summary: payload.success ? `Task dispatched: ${payload.command}` : `Dispatch failed: ${payload.error}`,
                    time: new Date().toLocaleTimeString()
                })
            })

            this.unsubscribeCallbacks = [
                unsubStatus, unsubHeartbeat, unsubTelemetry, unsubTask,
                unsubStatusEvent, unsubSnapshot, unsubEstopResult, unsubResumeResult, unsubDispatchResult
            ]
        },
        handleHeartbeat(payload) {
            const deviceId = payload.device_id
            const d = this.devices.find(x => x.id === deviceId)
            if (d) {
                d.connectionStatus = 'Online'
                d.lastSeenText = 'just now'
            }
            this.addEvent({
                kind: 'heartbeat',
                device: deviceId,
                topic: 'Heartbeat',
                summary: payload.payload && payload.payload.message ? payload.payload.message : 'Device online',
                time: new Date().toLocaleTimeString()
            })
        },
        handleTelemetry(payload) {
            const deviceId = payload.device_id
            const p = payload.payload || {}
            const motor = p.motor
            const key = p.telemetry_type
            const value = p[key]
            if (deviceId === undefined || motor === undefined || key === undefined) return

            const d = this.devices.find(x => x.id === deviceId)
            if (d) {
                if (!d.telemetry) d.telemetry = {}
                const motorKey = `motor_${motor}`
                if (!d.telemetry[motorKey]) d.telemetry[motorKey] = {}
                d.telemetry[motorKey][key] = value
            }
            // 事件流不过度刷屏：仅对 task 相关做展示，telemetry 可选显示
            // 这里每 5 条 telemetry 显示 1 条，简化日志
            if (Math.random() < 0.2) {
                this.addEvent({
                    kind: 'telemetry',
                    device: deviceId,
                    topic: `Telemetry M${motor}`,
                    summary: `${key.toUpperCase()}=${value}`,
                    time: new Date().toLocaleTimeString()
                })
            }
        },
        handleTaskStatus(payload) {
            const deviceId = payload.device_id
            const p = payload.payload || {}
            const d = this.devices.find(x => x.id === deviceId)
            if (!d) return

            if (p.event === 'task_create') {
                d.taskStatus = 'Busy'
                d.currentTask = this.normalizeTask({
                    motor: p.motor,
                    speed: p.speed,
                    duration_sec: p.duration_sec,
                    started_at: new Date().toISOString(),
                    expected_finished_at: new Date(Date.now() + (p.duration_sec || 0) * 1000).toISOString()
                })
                this.addEvent({
                    kind: 'task',
                    device: deviceId,
                    topic: 'Task Start',
                    summary: `Motor ${p.motor} started at ${p.speed} rpm for ${p.duration_sec}s`,
                    time: new Date().toLocaleTimeString()
                })
            } else if (p.event === 'task_done') {
                d.taskStatus = 'Idle'
                d.currentTask = {}
                this.addEvent({
                    kind: 'task',
                    device: deviceId,
                    topic: 'Task Done',
                    summary: `Motor ${p.motor} finished`,
                    time: new Date().toLocaleTimeString()
                })
            }
        },
        handleDeviceStatus(payload) {
            const deviceId = payload.device_id
            const p = payload.payload || {}
            const d = this.devices.find(x => x.id === deviceId)
            if (!d) {
                // 未知设备，刷新列表以拉取
                this.getDeviceList()
                return
            }
            if (p.event === 'online') {
                d.connectionStatus = 'Online'
                d.lastSeenText = 'just now'
            } else if (p.event === 'offline') {
                d.connectionStatus = 'Offline'
                d.taskStatus = 'Idle'
                d.currentTask = {}
            } else if (p.event === 'estopped') {
                d.taskStatus = 'E-Stopped'
                d.currentTask = {}
            } else if (p.event === 'resumed') {
                d.taskStatus = 'Idle'
            }
            this.addEvent({
                kind: p.event === 'estopped' ? 'estop' : (p.event === 'offline' ? 'offline' : 'status'),
                device: deviceId,
                topic: 'Status',
                summary: `Device ${p.event}`,
                time: new Date().toLocaleTimeString()
            })
        },
        handleSnapshot(payload) {
            const snapshot = payload.payload || {}
            Object.keys(snapshot).forEach(deviceId => {
                const state = snapshot[deviceId]
                const d = this.devices.find(x => x.id === deviceId)
                if (!d) {
                    // 快照中有但列表中没有，稍后刷新列表
                    return
                }
                if (state.is_online !== undefined) {
                    d.connectionStatus = state.is_online ? 'Online' : 'Offline'
                }
                if (state.task_status) {
                    d.taskStatus = this.mapTaskStatus(state.task_status)
                }
                if (state.telemetry) {
                    d.telemetry = state.telemetry
                }
                if (state.current_task) {
                    d.currentTask = this.normalizeTask(state.current_task)
                }
                if (state.last_heartbeat) {
                    d.lastSeenText = this.timeAgo(state.last_heartbeat)
                }
            })
        },
        addEvent(evt) {
            evt.key = `${Date.now()}-${Math.random().toString(36).slice(2, 7)}`
            this.liveEvents.unshift(evt)
            if (this.liveEvents.length > 50) {
                this.liveEvents = this.liveEvents.slice(0, 50)
            }
        },
        clearEvents() {
            this.liveEvents = []
        },
        isSelected(deviceId) {
            return this.selectedDeviceIds.includes(deviceId)
        },
        toggleSelect(deviceId) {
            const idx = this.selectedDeviceIds.indexOf(deviceId)
            if (idx === -1) {
                this.selectedDeviceIds.push(deviceId)
            } else {
                this.selectedDeviceIds.splice(idx, 1)
            }
        },
        toggleSelectAll() {
            if (this.isAllSelected) {
                this.selectedDeviceIds = []
            } else {
                this.selectedDeviceIds = this.devices.map(d => d.id)
            }
        },
        isExpanded(deviceId) {
            return this.expandedDeviceIds.includes(deviceId)
        },
        toggleExpand(deviceId) {
            const idx = this.expandedDeviceIds.indexOf(deviceId)
            if (idx === -1) {
                this.expandedDeviceIds.push(deviceId)
            } else {
                this.expandedDeviceIds.splice(idx, 1)
            }
        },
        emergencyStop(scope) {
            if (scope !== 'broadcast' && !this.selectedDeviceIds.length) return
            const ok = confirm(scope === 'broadcast'
                ? '确认对所有设备执行急停？所有电机会立即停止。'
                : `确认对选中的 ${this.selectedDeviceIds.length} 个设备执行急停？`)
            if (!ok) return

            this.wsService.send({
                action: 'emergency_stop',
                device_ids: scope === 'broadcast' ? [] : this.selectedDeviceIds,
                scope: scope,
                reason: 'Manual emergency stop from Dashboard',
                triggered_by: this.$store.state.email || 'operator'
            })
        },
        resumeDevices() {
            if (!this.selectedDeviceIds.length) return
            this.wsService.send({
                action: 'resume_device',
                device_ids: this.selectedDeviceIds,
                resumed_by: this.$store.state.email || 'operator'
            })
        },
        dispatchTaskToSelected() {
            if (!this.selectedDeviceIds.length) return
            const blocked = this.devices.filter(d => this.selectedDeviceIds.includes(d.id) && d.taskStatus === 'E-Stopped')
            if (blocked.length) {
                alert(`以下设备处于急停状态，无法下发任务：${blocked.map(d => d.deviceId).join(', ')}`)
                return
            }
            this.selectedDeviceIds.forEach(deviceId => {
                this.wsService.send({
                    action: 'dispatch_task',
                    device_id: deviceId,
                    motor: this.dispatchForm.motor,
                    speed: this.dispatchForm.speed,
                    duration: this.dispatchForm.duration
                })
            })
        }
    }
}
</script>

<style scoped>
.operations-dashboard {
    padding: 1.25rem;
    min-height: calc(100vh - 4rem);
    background:
        linear-gradient(180deg, #0f1724 4%, #131d2c 15%, #eef3f8 30%, #eef3f8 100%);
}

/* 连接状态条 */
.connection-bar {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 1rem;
    padding: 0.5rem 1rem;
    border-radius: 12px;
    font-size: 0.85rem;
    font-weight: 600;
    background: rgba(255, 255, 255, 0.92);
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.connection-bar--connected {
    color: #166534;
    background: #dcfce7;
}

.connection-bar--connecting {
    color: #9a670f;
    background: #fef3c7;
}

.connection-bar--disconnected {
    color: #991b1b;
    background: #fee2e2;
}

.connection-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: currentColor;
}

.connection-sublabel {
    opacity: 0.8;
    font-weight: 500;
}

.command-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: stretch;
    margin-bottom: 1.5rem;
    padding: 1.25rem 1.4rem;
    border-radius: 20px;
    background: linear-gradient(180deg, rgba(9, 14, 22, 0.86) 0%, rgba(20, 29, 43, 0.86) 100%);
    border: 1px solid rgba(148, 163, 184, 0.15);
    box-shadow: 0 18px 40px rgba(5, 10, 18, 0.32);
}

.command-header__main {
    max-width: 55rem;
}

.command-header__actions {
    display: flex;
    gap: 0.85rem;
    align-items: flex-start;
}

.eyebrow {
    margin-bottom: 0.35rem;
    color: #8fb3d9;
    font-size: 0.82rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    font-weight: 700;
}

.command-title {
    margin-bottom: 0.5rem !important;
    color: #f8fafc;
}

.command-copy {
    max-width: 52rem;
    color: #aeb9c9;
    font-size: 0.98rem;
    line-height: 1.6;
}

.sync-card {
    display: flex;
    flex-direction: column;
    min-width: 120px;
    padding: 0.75rem 0.9rem;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(148, 163, 184, 0.14);
}

.sync-label {
    color: #8ea2bd;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
}

.sync-value {
    color: #f8fafc;
    font-size: 1rem;
    font-weight: 700;
}

.refresh-button {
    background: #d9e4f2;
    color: #142131;
    border: none;
    font-weight: 700;
}

.summary-bar {
    display: grid;
    grid-template-columns: repeat(6, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1.5rem;
}

.summary-metric {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    padding: 0.9rem 1rem;
    border-radius: 16px;
    background: #ffffff;
    border: 1px solid rgba(15, 23, 36, 0.08);
    box-shadow: 0 10px 24px rgba(15, 23, 36, 0.08);
}

.summary-metric--good {
    border-left: 4px solid #1c8c63;
}

.summary-metric--busy {
    border-left: 4px solid #3b82f6;
}

.summary-metric--alert {
    border-left: 4px solid #d4584f;
}

.summary-metric--idle {
    border-left: 4px solid #c58a2a;
}

.summary-metric--offline {
    border-left: 4px solid #64748b;
}

.summary-metric__label {
    color: #64748b;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.summary-metric__value {
    color: #111827;
    font-size: 1.8rem;
    font-weight: 700;
}

.console-grid {
    display: grid;
    grid-template-columns: minmax(0, 1.7fr) minmax(340px, 0.9fr);
    gap: 1.25rem;
}

.panel-card {
    padding: 1.4rem;
    border-radius: 20px;
    background: rgba(255, 255, 255, 0.96);
    border: 1px solid rgba(13, 22, 38, 0.08);
    box-shadow: 0 14px 36px rgba(15, 23, 36, 0.08);
}

.panel-card--status {
    min-height: 520px;
}

.operations-rail {
    display: grid;
    gap: 1rem;
    padding: 1.4rem;
    border-radius: 20px;
    background: rgba(255, 255, 255, 0.96);
    border: 1px solid rgba(13, 22, 38, 0.08);
    box-shadow: 0 14px 36px rgba(15, 23, 36, 0.08);
}

.panel-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: flex-start;
    margin-bottom: 1rem;
}

.panel-kicker {
    color: #9c5f16;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.12em;
    margin-bottom: 0.25rem;
    font-weight: 700;
}

.panel-title {
    color: #111827;
    font-size: 1.22rem;
    font-weight: 700;
}

.panel-badge {
    display: inline-flex;
    align-items: center;
    height: fit-content;
    padding: 0.3rem 0.7rem;
    border-radius: 999px;
    background: #eef3fb;
    color: #325891;
    font-size: 0.76rem;
    font-weight: 700;
}

.status-toolbar {
    display: flex;
    gap: 1rem;
    margin-bottom: 1rem;
    padding: 0.8rem 0.95rem;
    border-radius: 16px;
    background: #f5f8fc;
    border: 1px solid rgba(15, 23, 36, 0.06);
}

.toolbar-item {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
}

.toolbar-label {
    color: #64748b;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.toolbar-value {
    color: #0f172a;
    font-weight: 700;
}

.toolbar-value--good {
    color: #0f7a59;
}

.toolbar-value--warn {
    color: #a35f14;
}

.toolbar-value--highlight {
    color: #3b82f6;
}

.status-alert {
    margin-bottom: 1rem;
    padding: 0.85rem 1rem;
    border-radius: 14px;
    background: #fff6df;
    border: 1px solid #f0dfaa;
    color: #9b6a12;
}

.device-table {
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.device-table__head,
.device-row {
    display: grid;
    grid-template-columns: 40px 1.3fr 0.9fr 1.1fr 0.9fr 1fr 80px;
    gap: 0.6rem;
    align-items: center;
    padding: 0.85rem 0.9rem;
}

.device-table__head {
    background: #ecf2f8;
    color: #5f6d81;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.head-cell--check,
.device-cell--check {
    display: flex;
    align-items: center;
    justify-content: center;
}

.device-row {
    background: #ffffff;
    border-top: 1px solid rgba(15, 23, 36, 0.06);
    transition: background 0.15s ease;
}

.device-row:nth-child(even) {
    background: #fafcfe;
}

.device-row--selected {
    background: #eff6ff !important;
}

.device-row--estopped {
    border-left: 4px solid #d4584f;
}

.device-row--offline {
    opacity: 0.7;
}

.device-cell {
    color: #1f2937;
    font-size: 0.94rem;
}

.device-cell--mono {
    font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
    font-size: 0.86rem;
}

.device-cell--time {
    color: #475569;
    font-size: 0.88rem;
}

.device-cell--action {
    text-align: right;
}

.device-label {
    display: block;
    font-weight: 700;
    color: #111827;
}

.device-index {
    display: block;
    font-size: 0.82rem;
    color: #64748b;
}

.expand-button {
    padding: 0.35rem 0.6rem;
    border-radius: 8px;
    border: 1px solid rgba(15, 23, 36, 0.12);
    background: #ffffff;
    color: #325891;
    font-size: 0.78rem;
    font-weight: 600;
    cursor: pointer;
}

.expand-button:hover {
    background: #f4f7fb;
}

.status-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 64px;
    padding: 0.28rem 0.65rem;
    border-radius: 999px;
    font-size: 0.76rem;
    font-weight: 700;
}

.status-pill--online {
    background: #dcfce7;
    color: #166534;
}

.status-pill--busy {
    background: #dbeafe;
    color: #1e40af;
}

.status-pill--alert {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill--offline {
    background: #f1f5f9;
    color: #475569;
}

/* 展开详情 */
.device-detail-row {
    background: #f8fafc;
    border-top: 1px dashed rgba(15, 23, 36, 0.08);
}

.device-detail {
    padding: 1rem 1.2rem;
    display: grid;
    gap: 1.2rem;
}

.detail-section {
    background: #ffffff;
    border-radius: 14px;
    padding: 1rem;
    border: 1px solid rgba(15, 23, 36, 0.06);
}

.detail-title {
    font-size: 0.92rem;
    font-weight: 700;
    color: #111827;
    margin-bottom: 0.7rem;
}

.motor-grid {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 0.6rem;
}

.motor-card {
    background: #f4f7fb;
    border-radius: 12px;
    padding: 0.75rem;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
}

.motor-card__label {
    font-size: 0.75rem;
    color: #64748b;
    font-weight: 700;
    text-transform: uppercase;
}

.motor-card__values {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
    font-size: 0.9rem;
    color: #0f172a;
    font-weight: 600;
}

.temperature-row {
    margin-top: 0.75rem;
    display: flex;
    gap: 0.5rem;
    font-size: 0.88rem;
}

.detail-label {
    color: #64748b;
    font-weight: 600;
}

.detail-value {
    color: #0f172a;
    font-weight: 600;
}

.detail-value--na {
    color: #94a3b8;
    font-style: italic;
}

.task-mini {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem;
    align-items: center;
}

.task-mini--empty {
    color: #94a3b8;
}

.task-mini__motor {
    background: #eff6ff;
    color: #1e40af;
    padding: 0.15rem 0.4rem;
    border-radius: 6px;
    font-size: 0.72rem;
    font-weight: 700;
}

.task-mini__speed {
    color: #1f2937;
    font-weight: 600;
}

.task-mini__remaining {
    color: #0f7a59;
    font-size: 0.8rem;
    font-weight: 700;
}

.task-detail__row {
    display: flex;
    justify-content: space-between;
    padding: 0.3rem 0;
    border-bottom: 1px solid rgba(15, 23, 36, 0.04);
}

.task-progress {
    margin-top: 0.6rem;
    display: flex;
    align-items: center;
    gap: 0.6rem;
}

.task-progress__bar {
    flex: 1;
    height: 8px;
    background: #e2e8f0;
    border-radius: 999px;
    overflow: hidden;
}

.task-progress__fill {
    height: 100%;
    background: #3b82f6;
    transition: width 0.3s ease;
}

.task-progress__text {
    font-size: 0.8rem;
    font-weight: 700;
    color: #325891;
    min-width: 36px;
    text-align: right;
}

/* 操作面板 */
.action-stack {
    display: grid;
    gap: 1rem;
}

.action-card {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
    padding: 1rem 1rem 1rem 1.05rem;
    border-radius: 18px;
    background: #fbfcfe;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.action-card--danger {
    border: 1px solid rgba(212, 88, 79, 0.25);
    background: #fffafa;
}

.action-title {
    color: #111827;
    font-size: 1rem;
    font-weight: 700;
    margin-bottom: 0.3rem;
}

.action-copy {
    color: #526174;
    font-size: 0.9rem;
    line-height: 1.5;
}

.action-buttons {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
}

.action-button {
    background: #1e293b;
    color: #ffffff;
    border: none;
    font-weight: 700;
}

.estop-button {
    background: #d4584f;
    color: #ffffff;
    border: none;
    font-weight: 700;
}

.estop-button:hover:not(:disabled) {
    background: #b93e36;
}

.estop-button--broadcast {
    background: #991b1b;
}

.estop-button:disabled {
    background: #fca5a5;
    cursor: not-allowed;
}

.resume-button {
    background: #1c8c63;
    color: #ffffff;
    border: none;
    font-weight: 700;
}

.resume-button:disabled {
    background: #86efac;
    cursor: not-allowed;
}

.dispatch-button {
    background: #3b82f6;
    color: #ffffff;
    border: none;
    font-weight: 700;
    margin-top: 0.5rem;
    width: 100%;
}

.dispatch-button:disabled {
    background: #93c5fd;
    cursor: not-allowed;
}

.dispatch-form {
    display: grid;
    gap: 0.5rem;
    min-width: 140px;
}

.form-row {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
}

.form-label {
    font-size: 0.75rem;
    color: #64748b;
    font-weight: 700;
}

.form-input,
.form-select {
    padding: 0.4rem 0.5rem;
    border-radius: 8px;
    border: 1px solid rgba(15, 23, 36, 0.12);
    font-size: 0.9rem;
    background: #ffffff;
}

.rail-card {
    padding: 1rem;
    border-radius: 18px;
    background: #f4f7fb;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.rail-card--compact {
    background: #f8fafc;
}

.rail-title {
    color: #111827;
    font-size: 0.96rem;
    font-weight: 700;
    margin-bottom: 0.8rem;
}

.runbook-row,
.context-row {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.75rem;
    align-items: start;
}

.runbook-row + .runbook-row,
.context-row + .context-row {
    margin-top: 0.7rem;
}

.runbook-index {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 34px;
    height: 34px;
    border-radius: 10px;
    background: #e5edf6;
    color: #274c7d;
    font-size: 0.8rem;
    font-weight: 700;
}

.runbook-copy {
    color: #475569;
    line-height: 1.55;
}

.context-label {
    color: #64748b;
    font-size: 0.8rem;
    font-weight: 700;
}

.context-value {
    color: #0f172a;
    font-weight: 600;
}

/* 实时事件流 */
.event-stream {
    margin-top: 1.5rem;
    padding: 1.4rem;
    border-radius: 20px;
    background: rgba(255, 255, 255, 0.96);
    border: 1px solid rgba(13, 22, 38, 0.08);
    box-shadow: 0 14px 36px rgba(15, 23, 36, 0.08);
}

.clear-button {
    background: transparent;
    color: #64748b;
    border: 1px solid rgba(15, 23, 36, 0.12);
    font-weight: 600;
    font-size: 0.85rem;
}

.event-list {
    display: grid;
    gap: 0.4rem;
    max-height: 240px;
    overflow-y: auto;
}

.event-row {
    display: grid;
    grid-template-columns: 90px 110px 110px 1fr;
    gap: 0.75rem;
    align-items: center;
    padding: 0.55rem 0.75rem;
    border-radius: 10px;
    background: #f8fafc;
    font-size: 0.88rem;
}

.event-row--estop {
    background: #fee2e2;
}

.event-row--error {
    background: #fff7ed;
}

.event-row--task {
    background: #eff6ff;
}

.event-row--heartbeat {
    background: #f0fdf4;
}

.event-time {
    color: #64748b;
    font-family: "SFMono-Regular", Consolas, monospace;
    font-size: 0.8rem;
}

.event-device {
    color: #111827;
    font-weight: 700;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.event-topic {
    color: #325891;
    font-weight: 600;
    font-size: 0.8rem;
}

.event-summary {
    color: #475569;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.event-empty {
    padding: 1.2rem;
    text-align: center;
    color: #94a3b8;
    font-size: 0.9rem;
}

.empty-state {
    padding: 2.2rem 1.2rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    background: #f8fafc;
    text-align: center;
}

.empty-title {
    color: #0f172a;
    font-size: 1rem;
    font-weight: 700;
    margin-bottom: 0.35rem;
}

.empty-copy {
    color: #64748b;
}

@media screen and (max-width: 1280px) {
    .summary-bar {
        grid-template-columns: repeat(3, minmax(0, 1fr));
    }
}

@media screen and (max-width: 1180px) {
    .console-grid {
        grid-template-columns: 1fr;
    }

    .motor-grid {
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }
}

@media screen and (max-width: 960px) {
    .device-table__head,
    .device-row {
        grid-template-columns: 36px 1fr 1fr 80px;
    }

    .device-table__head > span:nth-child(4),
    .device-table__head > span:nth-child(5),
    .device-table__head > span:nth-child(6),
    .device-row > .device-cell:nth-child(5),
    .device-row > .device-cell:nth-child(6),
    .device-row > .device-cell:nth-child(7) {
        display: none;
    }
}

@media screen and (max-width: 768px) {
    .operations-dashboard {
        padding: 1rem;
    }

    .command-header {
        flex-direction: column;
    }

    .command-header__actions {
        width: 100%;
        justify-content: space-between;
    }

    .summary-bar {
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .motor-grid {
        grid-template-columns: 1fr;
    }

    .action-card {
        flex-direction: column;
        align-items: flex-start;
    }

    .event-row {
        grid-template-columns: 1fr;
        gap: 0.2rem;
    }
}
</style>
