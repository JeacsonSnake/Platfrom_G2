<template>
    <section class="event-stream">
        <PanelHeader kicker="Realtime Feed" title="Device Events">
            <button class="button clear-button" @click="handleClear">Clear</button>
        </PanelHeader>
        <div class="event-list">
            <div
                v-for="evt in events"
                :key="evt.key"
                class="event-row"
                :class="'event-row--' + evt.kind"
            >
                <span class="event-time">{{ evt.time }}</span>
                <span class="event-device">{{ evt.device }}</span>
                <span class="event-topic">{{ evt.topic }}</span>
                <span class="event-summary">{{ evt.summary }}</span>
            </div>
            <div v-if="!events.length" class="event-empty">
                Waiting for MQTT messages...
            </div>
        </div>
    </section>
</template>

<script>
import PanelHeader from './PanelHeader.vue'

export default {
    name: 'LiveEventStream',
    components: { PanelHeader },
    props: {
        events: {
            type: Array,
            default: () => []
        },
        maxEvents: {
            type: Number,
            default: 50
        }
    },
    emits: ['clear'],
    methods: {
        handleClear() {
            this.$emit('clear')
        }
    }
}
</script>

<style scoped>
.event-stream {
    margin-top: 1.25rem;
}

.event-list {
    max-height: 320px;
    overflow-y: auto;
    border-radius: 18px;
    border: 1px solid rgba(15, 23, 36, 0.08);
    background: #ffffff;
    padding: 0.75rem;
}

.event-row {
    display: grid;
    grid-template-columns: 80px 120px 120px 1fr;
    gap: 0.75rem;
    align-items: center;
    padding: 0.55rem 0.7rem;
    border-radius: 10px;
    font-size: 0.85rem;
}

.event-row:nth-child(even) {
    background: #f8fafc;
}

.event-time {
    color: #64748b;
    font-family: monospace;
    font-size: 0.78rem;
}

.event-device {
    color: #1e293b;
    font-weight: 600;
}

.event-topic {
    color: #325891;
    font-weight: 600;
}

.event-summary {
    color: #475569;
}

.event-row--heartbeat {
    border-left: 3px solid #22c55e;
}

.event-row--telemetry {
    border-left: 3px solid #3b82f6;
}

.event-row--task {
    border-left: 3px solid #8b5cf6;
}

.event-row--estop {
    border-left: 3px solid #ef4444;
}

.event-row--offline {
    border-left: 3px solid #64748b;
}

.event-row--error {
    border-left: 3px solid #ef4444;
    background: #fff2f2;
}

.event-empty {
    padding: 1.5rem;
    text-align: center;
    color: #64748b;
}

.clear-button {
    background: #eef3fb;
    color: #325891;
    border: none;
    font-weight: 600;
}
</style>
