<template>
    <header class="console-header">
        <div>
            <p v-if="eyebrow" class="eyebrow">{{ eyebrow }}</p>
            <h1 v-if="title" class="title console-title">{{ title }}</h1>
            <p v-if="copy" class="console-copy">{{ copy }}</p>
        </div>
        <div v-if="statusItems.length" class="header-status">
            <StatusChip
                v-for="(item, index) in statusItems"
                :key="index"
                :label="item.label"
                :value="item.value"
            />
        </div>
    </header>
</template>

<script>
import StatusChip from './StatusChip.vue'

export default {
    name: 'ConsoleHeader',
    components: { StatusChip },
    props: {
        eyebrow: {
            type: String,
            default: ''
        },
        title: {
            type: String,
            default: ''
        },
        copy: {
            type: String,
            default: ''
        },
        statusItems: {
            type: Array,
            default: () => []
        }
    }
}
</script>

<style scoped>
.console-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: stretch;
    margin-bottom: 1.4rem;
    padding: 1.25rem 1.4rem;
    border-radius: 20px;
    background: linear-gradient(180deg, rgba(9, 14, 22, 0.88) 0%, rgba(19, 29, 45, 0.9) 100%);
    border: 1px solid rgba(148, 163, 184, 0.15);
    box-shadow: 0 18px 40px rgba(5, 10, 18, 0.3);
}

.eyebrow {
    margin-bottom: 0.3rem;
    color: #8fb3d9;
    font-size: 0.8rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    font-weight: 700;
}

.console-title {
    margin-bottom: 0.5rem !important;
    color: #f8fafc;
}

.console-copy {
    max-width: 56rem;
    color: #adbacd;
    line-height: 1.6;
}

.header-status {
    display: flex;
    gap: 0.8rem;
}
</style>
