<template>
    <section class="summary-bar">
        <MetricCard label="Total Devices" :value="summary.total" />
        <MetricCard label="Online" :value="summary.online" />
        <MetricCard label="Busy" :value="summary.busy" />
        <MetricCard label="E-Stopped" :value="summary.estopped" />
        <MetricCard label="Idle" :value="summary.idle" />
        <MetricCard label="Offline" :value="summary.offline" />
    </section>
</template>

<script>
import MetricCard from '@/components/ui/MetricCard.vue'

export default {
    name: 'FleetSummary',
    components: { MetricCard },
    props: {
        summary: {
            type: Object,
            required: true,
            default: () => ({
                total: 0,
                online: 0,
                busy: 0,
                estopped: 0,
                idle: 0,
                offline: 0
            })
        }
    }
}
</script>

<style scoped>
.summary-bar {
    display: grid;
    grid-template-columns: repeat(6, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1.5rem;
}

@media screen and (max-width: 960px) {
    .summary-bar {
        grid-template-columns: repeat(3, minmax(0, 1fr));
    }
}

@media screen and (max-width: 640px) {
    .summary-bar {
        grid-template-columns: repeat(2, minmax(0, 1fr));
    }
}
</style>
