<template>
    <div class="status-board">
        <div v-if="selectedDevice" class="device-summary">
            <span class="device-summary__label">{{ selectedDeviceLabel }}</span>
            <span
                class="device-summary__badge"
                :class="selectedDevice.is_online ? 'device-summary__badge--online' : 'device-summary__badge--offline'"
            >
                {{ selectedDevice.is_online ? 'Online' : 'Offline' }}
            </span>
            <span class="device-summary__task">{{ selectedDevice.task_status || 'idle' }}</span>
        </div>

        <div v-if="motors.length" class="board-table">
            <div class="board-table__head">
                <span>ID</span>
                <span>Name</span>
                <span>Availability</span>
                <span>Status</span>
                <span>Target RPM</span>
                <span>Actual RPM</span>
            </div>
            <article class="board-row" v-for="motor in motors" :key="motor.id">
                <div class="board-cell board-cell--strong">{{ motor.id }}</div>
                <div class="board-cell">{{ motor.name }}</div>
                <div class="board-cell">
                    <span class="availability-pill" :class="motor.avaliable ? 'availability-pill--good' : 'availability-pill--bad'">
                        {{ motor.avaliable ? 'Available' : 'Unavailable' }}
                    </span>
                </div>
                <div class="board-cell">
                    <span class="status-pill" :class="`status-pill--${motor.status}`">
                        {{ formatStatus(motor.status) }}
                    </span>
                </div>
                <div class="board-cell">{{ motor.target_speed ?? 0 }}</div>
                <div class="board-cell">{{ motor.actual_speed ?? 0 }}</div>
            </article>
        </div>

        <div v-else class="empty-state">
            Motor records will appear here after the backend returns the motor list.
        </div>

        <div v-if="devices.length" class="device-tabs">
            <button
                v-for="device in devices"
                :key="device.device_id"
                class="device-tab"
                :class="{ 'device-tab--active': device.device_id === selectedDeviceId }"
                :disabled="loading"
                @click="selectDevice(device.device_id)"
            >
                {{ formatMac(device) }}
            </button>
            <button
                class="device-tab device-tab--refresh"
                :disabled="loading || !selectedDeviceId"
                @click="refresh"
            >
                <span class="refresh-icon">↻</span>
                手动刷新
            </button>
        </div>
    </div>
</template>

<script>
export default {
    name: 'MotorStatusBoard',
    props: {
        motors: {
            type: Array,
            default: () => []
        },
        devices: {
            type: Array,
            default: () => []
        },
        selectedDeviceId: {
            type: String,
            default: ''
        },
        loading: {
            type: Boolean,
            default: false
        }
    },
    emits: ['select-device', 'refresh'],
    computed: {
        selectedDevice() {
            return this.devices.find(d => d.device_id === this.selectedDeviceId) || null
        },
        selectedDeviceLabel() {
            const device = this.selectedDevice
            if (!device) return ''
            if (device.label && device.label !== device.device_id) {
                return `${device.label} (${device.device_id})`
            }
            return device.device_id
        }
    },
    methods: {
        formatStatus(status) {
            const map = {
                idle: 'Idle',
                running: 'Running',
                fault: 'Fault',
                offline: 'Offline'
            }
            return map[status] || status
        },
        formatMac(device) {
            const mac = device.mac_address || device.device_id || ''
            const normalized = mac.toLowerCase().replace(/[^0-9a-f]/g, '')
            if (normalized.length === 12) {
                return normalized.match(/.{1,2}/g).join(':')
            }
            return mac || device.device_id
        },
        selectDevice(deviceId) {
            if (deviceId !== this.selectedDeviceId) {
                this.$emit('select-device', deviceId)
            }
        },
        refresh() {
            this.$emit('refresh')
        }
    }
}
</script>

<style scoped>
.status-board {
    display: flex;
    flex-direction: column;
    gap: 1rem;
}

.device-summary {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.65rem 0.9rem;
    background: #f8fafc;
    border: 1px solid rgba(15, 23, 36, 0.08);
    border-radius: 12px;
    font-size: 0.9rem;
}

.device-summary__label {
    font-weight: 700;
    color: #111827;
}

.device-summary__badge {
    display: inline-flex;
    align-items: center;
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
    font-size: 0.75rem;
    font-weight: 700;
    text-transform: uppercase;
}

.device-summary__badge--online {
    background: #dcfce7;
    color: #166534;
}

.device-summary__badge--offline {
    background: #fee2e2;
    color: #991b1b;
}

.device-summary__task {
    margin-left: auto;
    color: #64748b;
    font-size: 0.8rem;
    text-transform: capitalize;
}

.board-table {
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.board-table__head,
.board-row {
    display: grid;
    grid-template-columns: 0.4fr 0.8fr 0.8fr 0.8fr 0.8fr 0.8fr;
    gap: 0.75rem;
    align-items: center;
    padding: 0.95rem 1rem;
}

.board-table__head {
    background: #ecf2f8;
    color: #5f6d81;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.board-row {
    background: #ffffff;
    border-top: 1px solid rgba(15, 23, 36, 0.06);
}

.board-row:nth-child(even) {
    background: #fafcfe;
}

.board-cell {
    color: #1f2937;
    font-size: 0.92rem;
}

.board-cell--strong {
    font-weight: 700;
    color: #111827;
}

.availability-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 92px;
    padding: 0.28rem 0.72rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
}

.availability-pill--good {
    background: #dcfce7;
    color: #166534;
}

.availability-pill--bad {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 72px;
    padding: 0.28rem 0.72rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
    text-transform: capitalize;
    background: #e2e8f0;
    color: #334155;
}

.status-pill--idle {
    background: #f1f5f9;
    color: #475569;
}

.status-pill--running {
    background: #dcfce7;
    color: #166534;
}

.status-pill--fault {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill--offline {
    background: #f3f4f6;
    color: #6b7280;
}

.empty-state {
    padding: 1.6rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    color: #64748b;
    background: #fbfcfd;
}

.device-tabs {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    padding: 0.5rem;
    background: #f1f5f9;
    border-radius: 14px;
    border: 1px solid rgba(15, 23, 36, 0.06);
}

.device-tab {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    padding: 0.45rem 0.9rem;
    border-radius: 10px;
    border: 1px solid transparent;
    background: #ffffff;
    color: #475569;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
    font-family: 'SF Mono', Monaco, monospace;
}

.device-tab:hover:not(:disabled) {
    background: #f8fafc;
    border-color: rgba(15, 23, 36, 0.12);
}

.device-tab--active {
    background: #0f172a;
    color: #ffffff;
    box-shadow: 0 4px 12px rgba(15, 23, 36, 0.15);
}

.device-tab--active:hover:not(:disabled) {
    background: #1e293b;
}

.device-tab:disabled {
    opacity: 0.6;
    cursor: not-allowed;
}

.device-tab--refresh {
    margin-left: auto;
    background: #e0f2fe;
    color: #0369a1;
}

.device-tab--refresh:hover:not(:disabled) {
    background: #bae6fd;
}

.refresh-icon {
    font-size: 0.95rem;
}

@media screen and (max-width: 960px) {
    .board-table__head,
    .board-row {
        grid-template-columns: 1fr;
    }

    .device-tabs {
        flex-direction: column;
        align-items: stretch;
    }

    .device-tab--refresh {
        margin-left: 0;
    }
}
</style>
