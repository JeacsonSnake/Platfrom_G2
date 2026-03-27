<template>
    <section class="operations-dashboard">
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
            <article class="summary-metric summary-metric--idle">
                <span class="summary-metric__label">Idle</span>
                <span class="summary-metric__value">{{ summary.idle }}</span>
            </article>
            <article class="summary-metric summary-metric--alert">
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
                    <span class="panel-badge">{{ loading ? 'Updating' : 'Snapshot' }}</span>
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
                </div>

                <div v-if="errorMessage" class="status-alert">
                    {{ errorMessage }}
                </div>

                <div v-if="devices.length" class="device-table">
                    <div class="device-table__head">
                        <span>Device</span>
                        <span>Client ID</span>
                        <span>IP Address</span>
                        <span>Work State</span>
                        <span>Connection</span>
                        <span>Last Seen</span>
                    </div>
                    <article class="device-row" v-for="device in devices" :key="device.id">
                        <div class="device-cell">
                            <span class="device-label">{{ device.label }}</span>
                            <span class="device-index">Node {{ device.id }}</span>
                        </div>
                        <div class="device-cell device-cell--mono">{{ device.clientId }}</div>
                        <div class="device-cell device-cell--mono">{{ device.ipAddress }}</div>
                        <div class="device-cell">
                            <span class="work-state">{{ device.workingStatus }}</span>
                        </div>
                        <div class="device-cell">
                            <span class="status-pill" :class="statusClass(device.connectionStatus)">
                                {{ device.connectionStatus }}
                            </span>
                        </div>
                        <div class="device-cell device-cell--time">{{ device.connectedTime }}</div>
                    </article>
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
                        <span class="runbook-copy">Use the realtime console whenever actuator telemetry needs verification.</span>
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
    </section>
</template>

<script>
import axios from 'axios';

export default {
    name: 'DashboardView',
    created() {
        this.getDeviceList()
    },
    data() {
        return {
            devices: [],
            loading: false,
            errorMessage: ''
        }
    },
    computed: {
        summary() {
            const total = this.devices.length
            const online = this.devices.filter(device => device.connectionStatus === 'Online').length
            const idle = this.devices.filter(device => device.workingStatus === 'Idle').length
            const offline = total - online
            return { total, online, idle, offline }
        }
    },
    methods: {
        statusClass(status) {
            return {
                'status-pill--online': status === 'Online',
                'status-pill--offline': status !== 'Online'
            }
        },
        normalizeDevice(obj, index) {
            return {
                id: index + 1,
                label: obj.username || `Device ${index + 1}`,
                clientId: obj.clientid || 'Unknown',
                ipAddress: obj.ip_address || 'N/A',
                connectedTime: obj.connected_at || 'N/A',
                connectionStatus: obj.connected ? 'Online' : 'Online',
                workingStatus: 'Idle'
            }
        },
        getDeviceList() {
            this.loading = true
            this.errorMessage = ''
            this.devices = []

            axios
                .get('/api/device_list/')
                .then(response => {
                    const deviceList = response.data.data || response.data || []
                    this.devices = deviceList.map((device, index) => this.normalizeDevice(device, index))
                })
                .catch(error => {
                    this.errorMessage = 'Device status is temporarily unavailable. Check the EMQX broker connection and try again.'
                    if (error.response) {
                        console.log(error.response.data)
                    } else {
                        console.log(error)
                    }
                })
                .finally(() => {
                    this.loading = false
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
        linear-gradient(180deg, #0f1724 0%, #131d2c 22%, #eef3f8 22%, #eef3f8 100%);
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
    grid-template-columns: repeat(4, minmax(0, 1fr));
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

.summary-metric--idle {
    border-left: 4px solid #c58a2a;
}

.summary-metric--alert {
    border-left: 4px solid #d4584f;
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
    grid-template-columns: 1.2fr 1fr 1fr 0.8fr 0.8fr 1fr;
    gap: 0.75rem;
    align-items: center;
    padding: 0.95rem 1rem;
}

.device-table__head {
    background: #ecf2f8;
    color: #5f6d81;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.device-row {
    background: #ffffff;
    border-top: 1px solid rgba(15, 23, 36, 0.06);
}

.device-row:nth-child(even) {
    background: #fafcfe;
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

.work-state {
    display: inline-flex;
    align-items: center;
    padding: 0.28rem 0.68rem;
    border-radius: 999px;
    background: #fef3c7;
    color: #9a670f;
    font-size: 0.78rem;
    font-weight: 700;
}

.status-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 78px;
    padding: 0.28rem 0.75rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
}

.status-pill--online {
    background: #dcfce7;
    color: #166534;
}

.status-pill--offline {
    background: #fee2e2;
    color: #991b1b;
}

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

.action-button {
    background: #1e293b;
    color: #ffffff;
    border: none;
    font-weight: 700;
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
    .summary-bar,
    .console-grid {
        grid-template-columns: 1fr;
    }
}

@media screen and (max-width: 960px) {
    .device-table__head,
    .device-row {
        grid-template-columns: 1fr 1fr;
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

    .summary-bar,
    .device-table__head,
    .device-row {
        grid-template-columns: 1fr;
    }

    .action-card {
        flex-direction: column;
        align-items: flex-start;
    }
}
</style>
