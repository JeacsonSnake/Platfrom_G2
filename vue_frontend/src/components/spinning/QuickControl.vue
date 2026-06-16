<template>
    <div class="telemetry-grid">
        <article class="telemetry-card">
            <span class="telemetry-card__label">Motor 1 Speed</span>
            <span class="telemetry-card__value">{{ realSpeed }}</span>
        </article>
        <article class="telemetry-card">
            <span class="telemetry-card__label">Target Speed Range</span>
            <span class="telemetry-card__value">0-70 rps</span>
        </article>
    </div>

    <div class="field">
        <label class="label">Direct Target Speed</label>
        <input type="number" class="input" :value="targetSpeed" @input="$emit('update:target-speed', Number($event.target.value))">
    </div>

    <div class="action-row">
        <button class="button is-success" @click="$emit('send')">Send Motor Command</button>
    </div>

    <section class="runbook-card">
        <p class="runbook-title">Operator Notes</p>
        <div class="runbook-row">
            <span class="runbook-index">01</span>
            <span class="runbook-copy">Use scheduled registration for repeatable operations and traceability.</span>
        </div>
        <div class="runbook-row">
            <span class="runbook-index">02</span>
            <span class="runbook-copy">Use direct target speed only for manual intervention or supervised testing.</span>
        </div>
        <div class="runbook-row">
            <span class="runbook-index">03</span>
            <span class="runbook-copy">Set target speed to zero to stop live polling and reset the observed speed.</span>
        </div>
    </section>
</template>

<script>
export default {
    name: 'QuickControl',
    props: {
        realSpeed: {
            type: Number,
            default: 0
        },
        targetSpeed: {
            type: Number,
            default: 0
        }
    },
    emits: ['update:target-speed', 'send']
}
</script>

<style scoped>
.telemetry-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1rem;
}

.telemetry-card {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    padding: 0.9rem 1rem;
    border-radius: 16px;
    background: #f9fbfe;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.telemetry-card__label {
    color: #64748b;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.telemetry-card__value {
    color: #111827;
    font-weight: 700;
}

.action-row {
    display: flex;
    gap: 0.75rem;
    margin-top: 1rem;
}

.runbook-card {
    margin-top: 1rem;
    padding: 1rem;
    border-radius: 18px;
    background: #f4f7fb;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.runbook-title {
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
}

@media screen and (max-width: 960px) {
    .telemetry-grid {
        grid-template-columns: 1fr;
    }
}
</style>
