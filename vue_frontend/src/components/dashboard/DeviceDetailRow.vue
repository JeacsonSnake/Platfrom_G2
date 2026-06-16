<template>
    <div class="device-detail">
        <div class="detail-section">
            <h4 class="detail-title">Telemetry</h4>
            <div class="motor-grid">
                <div class="motor-card" v-for="mIdx in 4" :key="mIdx">
                    <span class="motor-card__label">Motor {{ mIdx - 1 }}</span>
                    <div class="motor-card__values">
                        <span>PWM: {{ getTelemetry(mIdx - 1, 'pwm') }}</span>
                        <span>PCNT: {{ getTelemetry(mIdx - 1, 'pcnt') }}</span>
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
</template>

<script>
export default {
    name: 'DeviceDetailRow',
    props: {
        device: {
            type: Object,
            required: true
        }
    },
    methods: {
        getTelemetry(motorIdx, key) {
            const motorKey = `motor_${motorIdx}`
            const val = this.device.telemetry && this.device.telemetry[motorKey] && this.device.telemetry[motorKey][key]
            return val !== undefined ? val : 'N/A'
        }
    }
}
</script>

<style scoped>
.device-detail {
    padding: 1rem;
    background: #f8fafc;
    border-radius: 14px;
}

.detail-section {
    margin-bottom: 1rem;
}

.detail-section:last-child {
    margin-bottom: 0;
}

.detail-title {
    color: #111827;
    font-size: 0.95rem;
    font-weight: 700;
    margin-bottom: 0.75rem;
}

.motor-grid {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 0.75rem;
    margin-bottom: 0.75rem;
}

.motor-card {
    padding: 0.75rem;
    border-radius: 12px;
    background: #ffffff;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.motor-card__label {
    display: block;
    color: #64748b;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
    margin-bottom: 0.35rem;
}

.motor-card__values {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    color: #1f2937;
    font-size: 0.88rem;
}

.temperature-row {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    padding: 0.5rem 0;
}

.detail-label {
    color: #64748b;
    font-size: 0.85rem;
    font-weight: 600;
}

.detail-value {
    color: #1f2937;
    font-size: 0.9rem;
}

.detail-value--na {
    color: #94a3b8;
}

.task-detail__row {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.35rem;
}

.task-progress {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-top: 0.75rem;
}

.task-progress__bar {
    flex: 1;
    height: 8px;
    border-radius: 999px;
    background: #e2e8f0;
    overflow: hidden;
}

.task-progress__fill {
    height: 100%;
    background: #325891;
    border-radius: 999px;
    transition: width 0.3s ease;
}

.task-progress__text {
    color: #325891;
    font-size: 0.85rem;
    font-weight: 700;
    min-width: 40px;
    text-align: right;
}

@media screen and (max-width: 960px) {
    .motor-grid {
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }
}

@media screen and (max-width: 640px) {
    .motor-grid {
        grid-template-columns: 1fr;
    }
}
</style>
