<template>
    <div class="connection-bar" :class="statusClass">
        <span class="connection-dot"></span>
        <span class="connection-label">{{ statusLabel }}</span>
        <span v-if="mqttAvailable !== null || mqttConnected !== null" class="connection-sublabel">
            · {{ mqttLabel }}
        </span>
    </div>
</template>

<script>
export default {
    name: 'ConnectionBar',
    props: {
        status: {
            type: String,
            default: 'disconnected',
            validator(value) {
                return ['connected', 'connecting', 'disconnected'].includes(value)
            }
        },
        mqttAvailable: {
            type: [Boolean, null],
            default: null
        },
        mqttConnected: {
            type: [Boolean, null],
            default: null
        }
    },
    computed: {
        statusLabel() {
            const map = {
                connected: 'WebSocket Connected',
                connecting: 'WebSocket Connecting…',
                disconnected: 'WebSocket Disconnected'
            }
            return map[this.status] || this.status
        },
        mqttLabel() {
            if (this.mqttConnected === true) return 'MQTT 已连接'
            if (this.mqttConnected === false) return 'MQTT 已断开'
            if (this.mqttAvailable === true) return 'MQTT 可用'
            if (this.mqttAvailable === false) return 'MQTT 不可用'
            return 'MQTT 未知'
        },
        statusClass() {
            return {
                'connection-bar--connected': this.status === 'connected',
                'connection-bar--connecting': this.status === 'connecting',
                'connection-bar--disconnected': this.status === 'disconnected'
            }
        }
    }
}
</script>

<style scoped>
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
</style>
