<template>
    <section class="operations-rail">
        <PanelHeader kicker="Operator Rail" title="Manual Control Tools" badge="Authorized" />

        <div class="action-stack">
            <article class="action-card action-card--danger">
                <div>
                    <p class="action-title">Emergency Stop</p>
                    <p class="action-copy">
                        Halt all motors on selected devices. Requires manual resume before further task dispatch.
                    </p>
                </div>
                <div class="action-buttons">
                    <button class="button estop-button" @click="handleEmergencyStop('single')" :disabled="!selectedCount">
                        Stop Selected
                    </button>
                    <button class="button estop-button estop-button--broadcast" @click="handleEmergencyStop('broadcast')">
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
                <button class="button resume-button" @click="handleResume" :disabled="!selectedCount">
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
                <span class="context-value">{{ operatorName }}</span>
            </div>
        </section>
    </section>
</template>

<script>
import PanelHeader from '@/components/ui/PanelHeader.vue'

export default {
    name: 'OperatorRail',
    components: { PanelHeader },
    props: {
        selectedCount: {
            type: Number,
            default: 0
        }
    },
    emits: ['emergency-stop', 'resume'],
    computed: {
        operatorName() {
            return this.$store.state.email || 'Authenticated User'
        }
    },
    methods: {
        handleEmergencyStop(scope) {
            if (scope !== 'broadcast' && !this.selectedCount) return
            const ok = confirm(scope === 'broadcast'
                ? '确认对所有设备执行急停？所有电机会立即停止。'
                : `确认对选中的 ${this.selectedCount} 个设备执行急停？`)
            if (!ok) return
            this.$emit('emergency-stop', scope)
        },
        handleResume() {
            if (!this.selectedCount) return
            this.$emit('resume')
        }
    }
}
</script>

<style scoped>
.operations-rail {
    display: grid;
    gap: 1rem;
    padding: 1.4rem;
    border-radius: 20px;
    background: rgba(255, 255, 255, 0.96);
    border: 1px solid rgba(13, 22, 38, 0.08);
    box-shadow: 0 14px 36px rgba(15, 23, 36, 0.08);
}

.action-stack {
    display: grid;
    gap: 0.85rem;
}

.action-card {
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
    padding: 1rem;
    border-radius: 16px;
    background: #f8fafc;
    border: 1px solid rgba(15, 23, 36, 0.06);
}

.action-card--danger {
    background: #fff5f5;
    border-color: rgba(220, 38, 38, 0.12);
}

.action-title {
    color: #111827;
    font-weight: 700;
    margin-bottom: 0.35rem;
}

.action-copy {
    color: #475569;
    font-size: 0.88rem;
    line-height: 1.5;
}

.action-buttons {
    display: flex;
    gap: 0.6rem;
    flex-wrap: wrap;
}

.estop-button {
    background: #ef4444;
    color: #ffffff;
    border: none;
    font-weight: 700;
}

.estop-button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.estop-button--broadcast {
    background: #b91c1c;
}

.resume-button {
    background: #22c55e;
    color: #ffffff;
    border: none;
    font-weight: 700;
}

.resume-button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.action-button {
    background: #325891;
    color: #ffffff;
    border: none;
    font-weight: 700;
    text-align: center;
}

.rail-card {
    padding: 1rem;
    border-radius: 16px;
    background: #f4f7fb;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.rail-title {
    color: #111827;
    font-weight: 700;
    margin-bottom: 0.75rem;
}

.runbook-row {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.75rem;
    align-items: start;
}

.runbook-row + .runbook-row {
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
    font-size: 0.88rem;
}

.rail-card--compact {
    padding: 0.85rem 1rem;
}

.context-row {
    display: flex;
    justify-content: space-between;
    gap: 0.5rem;
    padding: 0.35rem 0;
    border-bottom: 1px solid rgba(15, 23, 36, 0.05);
}

.context-row:last-child {
    border-bottom: none;
}

.context-label {
    color: #64748b;
    font-size: 0.82rem;
}

.context-value {
    color: #111827;
    font-size: 0.85rem;
    font-weight: 600;
}
</style>
