<template>
    <article class="metric-card" :class="{ 'metric-card--accent': accent }">
        <span class="metric-card__label">{{ label }}</span>
        <span class="metric-card__value">{{ value }}</span>
    </article>
</template>

<script>
export default {
    name: 'MetricCard',
    props: {
        label: {
            type: String,
            default: ''
        },
        value: {
            type: [String, Number],
            default: ''
        },
        accent: {
            type: Boolean,
            default: false
        }
    }
}
</script>

<style scoped>
.metric-card {
    display: flex;
    flex-direction: column;
    gap: 0.22rem;
    padding: 0.95rem 1rem;
    border-radius: 16px;
    background: #ffffff;
    border: 1px solid rgba(15, 23, 36, 0.08);
    box-shadow: 0 10px 24px rgba(15, 23, 36, 0.08);
}

.metric-card--accent {
    border-left: 4px solid #325891;
}

.metric-card__label {
    color: #64748b;
    font-size: 0.76rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.metric-card__value {
    color: #111827;
    font-size: 1.05rem;
    font-weight: 700;
}
</style>
