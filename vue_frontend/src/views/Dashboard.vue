<template>
    <section class="operations-dashboard">
        <ConnectionBar :status="wsStatus" :mqtt-available="mqttAvailable" :mqtt-connected="backendMqttConnected" />

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

        <FleetSummary :summary="summary" />

        <div class="console-grid">
            <section class="panel-card panel-card--status">
                <PanelHeader kicker="Fleet Monitor" title="Device Status Board" :badge="loading ? 'Updating' : 'Live'" />

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

                <DeviceStatusTable
                    :devices="devices"
                    :selected-ids="selectedDeviceIds"
                    :expanded-ids="expandedDeviceIds"
                    :loading="loading"
                    @update:selected-ids="selectedDeviceIds = $event"
                    @update:expanded-ids="expandedDeviceIds = $event"
                    @refresh="getDeviceList"
                />

                <div v-if="!devices.length && !loading" class="empty-state">
                    <p class="empty-title">No active controller records</p>
                    <p class="empty-copy">
                        The operations console is available, but the fleet board will populate only when the broker reports active clients.
                    </p>
                </div>
            </section>

            <OperatorRail
                :selected-count="selectedDeviceIds.length"
                @emergency-stop="emergencyStop"
                @resume="resumeDevices"
                @acknowledge="acknowledgeDevices"
            />
        </div>

        <LiveEventStream :events="liveEvents" @clear="clearEvents" />
    </section>
</template>

<script>
import WebSocketService from '@/services/websocket.js'
import devicesApi from '@/services/api/devices.js'
import { showMqttMessage, closeMqttMessage } from '@/services/mqttMessage.js'
import ConnectionBar from '@/components/ui/ConnectionBar.vue'
import FleetSummary from '@/components/dashboard/FleetSummary.vue'
import PanelHeader from '@/components/ui/PanelHeader.vue'
import DeviceStatusTable from '@/components/dashboard/DeviceStatusTable.vue'
import OperatorRail from '@/components/dashboard/OperatorRail.vue'
import LiveEventStream from '@/components/ui/LiveEventStream.vue'

export default {
    name: 'DashboardView',
    components: {
        ConnectionBar,
        FleetSummary,
        PanelHeader,
        DeviceStatusTable,
        OperatorRail,
        LiveEventStream
    },
    created() {
        this.getDeviceList()
        this.initWebSocket()
        this.startTaskCountdown()
    },
    beforeUnmount() {
        if (this.countdownTimer) {
            clearInterval(this.countdownTimer)
        }
        if (this.mqttReconnectPollTimer) {
            clearInterval(this.mqttReconnectPollTimer)
        }
        closeMqttMessage()
        // WebSocket 服务作为单例保留，不在这里断开
    },
    data() {
        return {
            devices: [],
            loading: false,
            errorMessage: '',
            wsStatus: 'disconnected',
            mqttAvailable: null,
            backendMqttConnected: null,
            mqttRefreshing: false,
            mqttReconnectPollTimer: null,
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
        
    },
    methods: {
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
            if (s === 'error') return 'Error'
            if (s === 'completed') return 'Completed'
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
        async getDeviceList() {
            this.loading = true
            this.errorMessage = ''
            try {
                const response = await devicesApi.getList()
                const result = response.data || {}
                this.mqttAvailable = result.mqtt_available
                this.backendMqttConnected = result.mqtt_connected
                this.updateMqttBanner(result.mqtt_connected)
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
            const unsubMqttStatus = this.wsService.subscribe('mqtt_connection_status', (payload) => {
                this.handleMqttConnectionStatus(payload)
            })
            const unsubAcknowledgeResult = this.wsService.subscribe('acknowledge_result', (payload) => {
                this.addEvent({
                    kind: 'acknowledge',
                    device: (payload.results || []).map(r => r.device_id).join(', '),
                    topic: 'Acknowledge',
                    summary: payload.success === false ? `Acknowledge failed: ${payload.error}` : 'Devices acknowledged',
                    time: new Date().toLocaleTimeString()
                })
                this.getDeviceList()
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
                if (!payload.success && payload.error) {
                    this.showErrorMessage(`下发失败: ${payload.error}`)
                }
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
                unsubStatusEvent, unsubSnapshot, unsubMqttStatus, unsubAcknowledgeResult,
                unsubEstopResult, unsubResumeResult, unsubDispatchResult
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
            } else if (p.event === 'task_completed_pending_ack') {
                d.taskStatus = 'Completed'
                d.currentTask = {}
                this.addEvent({
                    kind: 'task',
                    device: deviceId,
                    topic: 'Task Completed',
                    summary: p.message || `Motor ${p.motor} finished. Waiting for acknowledgement.`,
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
            } else if (p.event === 'aborted') {
                d.connectionStatus = 'Offline'
                d.taskStatus = 'Error'
                d.currentTask = {}
            } else if (p.event === 'acknowledged') {
                d.taskStatus = 'Idle'
                d.currentTask = {}
            }
            this.addEvent({
                kind: p.event === 'estopped' ? 'estop' : (p.event === 'offline' ? 'offline' : (p.event === 'aborted' ? 'error' : 'status')),
                device: deviceId,
                topic: 'Status',
                summary: `Device ${p.event}${p.reason ? ': ' + p.reason : ''}`,
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
        handleMqttConnectionStatus(payload) {
            const connected = payload.payload && payload.payload.connected
            this.backendMqttConnected = connected
            this.updateMqttBanner(connected)
        },
        updateMqttBanner(connected) {
            if (connected === true) {
                showMqttMessage({
                    connected: true,
                    text: 'MQTT 已恢复连接'
                })
            } else if (connected === false) {
                showMqttMessage({
                    connected: false,
                    text: 'MQTT 已断开，命令将无法下发',
                    onRefresh: () => this.refreshMqttConnection()
                })
            }
            // connected 为 null/undefined 时不主动提示
        },
        async refreshMqttConnection() {
            this.mqttRefreshing = true
            try {
                const resp = await devicesApi.mqttReconnect()
                const result = resp.data || {}
                // 立即根据后端返回的最新状态更新 UI（即使尚未真正连上）
                this.backendMqttConnected = result.connected
                if (result.connected) {
                    this.updateMqttBanner(true)
                } else if (result.success) {
                    // 202 Accepted：连接握手仍在进行
                    showMqttMessage({
                        connected: false,
                        text: 'MQTT 正在重连，请稍候…',
                        onRefresh: () => this.refreshMqttConnection()
                    })
                    this.startMqttReconnectPolling()
                } else {
                    showMqttMessage({
                        connected: false,
                        text: `MQTT 重连失败：${result.error || '未知错误'}`,
                        onRefresh: () => this.refreshMqttConnection()
                    })
                }
            } catch (error) {
                const msg = error.response?.data?.error || error.message || '网络错误'
                showMqttMessage({
                    connected: false,
                    text: `MQTT 重连请求失败：${msg}`,
                    onRefresh: () => this.refreshMqttConnection()
                })
            } finally {
                // 按钮 Loading 至少保留 1 秒，避免频繁点击
                setTimeout(() => {
                    this.mqttRefreshing = false
                }, 1000)
            }
        },
        startMqttReconnectPolling() {
            if (this.mqttReconnectPollTimer) return
            let attempts = 0
            const maxAttempts = 15 // 最多 15 次 * 2 秒 = 30 秒
            this.mqttReconnectPollTimer = setInterval(() => {
                attempts += 1
                this.getDeviceList()
                if (this.backendMqttConnected || attempts >= maxAttempts) {
                    clearInterval(this.mqttReconnectPollTimer)
                    this.mqttReconnectPollTimer = null
                }
            }, 2000)
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
        showErrorMessage(message) {
            this.errorMessage = message
            setTimeout(() => {
                this.errorMessage = ''
            }, 5000)
        },
        acknowledgeDevices() {
            if (!this.selectedDeviceIds.length) return
            const ackable = this.devices.filter(d => this.selectedDeviceIds.includes(d.id) && ['E-Stopped', 'Error', 'Completed'].includes(d.taskStatus))
            if (!ackable.length) {
                alert('选中的设备没有需要确认的状态（急停/异常/完成）。')
                return
            }
            this.wsService.send({
                action: 'acknowledge_device',
                device_ids: ackable.map(d => d.id),
                acknowledged_by: this.$store.state.email || 'operator'
            })
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
            const notReady = this.devices.filter(d => this.selectedDeviceIds.includes(d.id) && (d.connectionStatus !== 'Online' || d.taskStatus !== 'Idle'))
            if (notReady.length) {
                alert(`以下设备不在线或不空闲，无法下发任务：${notReady.map(d => `${d.deviceId}(${d.connectionStatus}, ${d.taskStatus})`).join(', ')}`)
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

@media screen and (max-width: 1180px) {
    .console-grid {
        grid-template-columns: 1fr;
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
}
</style>
