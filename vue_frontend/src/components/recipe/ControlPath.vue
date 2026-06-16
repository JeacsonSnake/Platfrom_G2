<template>
    <div class="flow-list">
        <article class="flow-card">
            <span class="flow-index">01</span>
            <div>
                <p class="flow-title">Operator Request</p>
                <p class="flow-copy">Material or recipe selection enters the orchestration path from the control console.</p>
            </div>
        </article>
        <article class="flow-card">
            <span class="flow-index">02</span>
            <div>
                <p class="flow-title">Backend Resolution</p>
                <p class="flow-copy">The Django service resolves recipe parameters and device-specific execution steps.</p>
            </div>
        </article>
        <article class="flow-card">
            <span class="flow-index">03</span>
            <div>
                <p class="flow-title">MQTT Dispatch</p>
                <p class="flow-copy">Job start publishes step payloads to the embedded control channel when MQTT is available.</p>
            </div>
        </article>
        <article class="flow-card">
            <span class="flow-index">04</span>
            <div>
                <p class="flow-title">Feedback Loop</p>
                <p class="flow-copy">WebSocket events and backend polling close the loop so operators can verify physical execution.</p>
            </div>
        </article>
    </div>

    <div v-if="createdJob" class="job-summary">
        <p class="job-summary__title">Current Job Context</p>
        <div class="job-summary__row">
            <span>Job ID</span>
            <span>{{ createdJob.id }}</span>
        </div>
        <div class="job-summary__row">
            <span>Operator</span>
            <span>{{ createdJob.operator || 'N/A' }}</span>
        </div>
        <div class="job-summary__row">
            <span>Dispatch Count</span>
            <span>{{ startResult ? startResult.dispatched_messages.length : 0 }}</span>
        </div>
    </div>
</template>

<script>
export default {
    name: 'ControlPath',
    props: {
        createdJob: {
            type: Object,
            default: null
        },
        startResult: {
            type: Object,
            default: null
        }
    }
}
</script>

<style scoped>
.flow-list {
    display: grid;
    gap: 0.85rem;
}

.flow-card {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.8rem;
    padding: 0.95rem 1rem;
    border-radius: 16px;
    background: #f7fafc;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.flow-index {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 36px;
    height: 36px;
    border-radius: 10px;
    background: #e5edf6;
    color: #274c7d;
    font-size: 0.82rem;
    font-weight: 700;
}

.flow-title {
    color: #111827;
    font-weight: 700;
    margin-bottom: 0.2rem;
}

.flow-copy {
    color: #475569;
    line-height: 1.55;
}

.job-summary {
    margin-top: 1rem;
    padding: 1rem;
    border-radius: 16px;
    background: #f4f7fb;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.job-summary__title {
    color: #111827;
    font-weight: 700;
    margin-bottom: 0.65rem;
}

.job-summary__row {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    color: #334155;
}

.job-summary__row + .job-summary__row {
    margin-top: 0.45rem;
}
</style>
