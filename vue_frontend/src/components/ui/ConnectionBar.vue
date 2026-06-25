<template>
    <div class="connection-bar">
        <div class="connection-item" :class="wsItemClass">
            <span class="connection-dot"></span>
            <span class="connection-label">{{ wsLabel }}</span>
        </div>
        <div class="connection-item" :class="mqttItemClass">
            <span class="connection-dot"></span>
            <span class="connection-label">{{ mqttLabel }}</span>
        </div>
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
        mqttConnected: {
            type: [Boolean, null],
            default: null
        },
        mqttStatus: {
            type: String,
            default: null,
            validator(value) {
                return value === null || ['connected', 'connecting', 'disconnected'].includes(value)
            }
        }
    },
    computed: {
        wsLabel() {
            const map = {
                connected: 'WebSocket Connected',
                connecting: 'WebSocket Connecting…',
                disconnected: 'WebSocket Disconnected'
            }
            return map[this.status] || 'WebSocket Disconnected'
        },
        mqttState() {
            if (this.mqttStatus) {
                return this.mqttStatus
            }
            if (this.mqttConnected === true) {
                return 'connected'
            }
            if (this.mqttConnected === false) {
                return 'disconnected'
            }
            return 'disconnected'
        },
        mqttLabel() {
            const map = {
                connected: 'MQTT Connected',
                connecting: 'MQTT Connecting…',
                disconnected: 'MQTT Disconnected'
            }
            return map[this.mqttState] || 'MQTT Disconnected'
        },
        wsItemClass() {
            return `connection-item--${this.status}`
        },
        mqttItemClass() {
            return `connection-item--${this.mqttState}`
        }
    }
}
</script>

<style scoped>
.connection-bar {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-bottom: 1rem;
}

.connection-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.35rem 0.85rem;
    border-radius: 999px;
    font-size: 0.82rem;
    font-weight: 600;
    background: rgba(255, 255, 255, 0.92);
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.connection-item--connected {
    color: #166534;
    background: #dcfce7;
    border-color: #bbf7d0;
}

.connection-item--connecting {
    color: #9a670f;
    background: #fef3c7;
    border-color: #fde68a;
}

.connection-item--disconnected {
    color: #991b1b;
    background: #fee2e2;
    border-color: #fecaca;
}

.connection-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: currentColor;
}
</style>
