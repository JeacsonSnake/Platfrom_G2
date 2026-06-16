<template>
    <div class="panel-header">
        <div>
            <p v-if="kicker" class="panel-kicker">{{ kicker }}</p>
            <h2 v-if="title" class="panel-title">{{ title }}</h2>
        </div>
        <span v-if="badge !== false" class="panel-badge">{{ badge }}</span>
        <slot></slot>
    </div>
</template>

<script>
export default {
    name: 'PanelHeader',
    props: {
        kicker: {
            type: String,
            default: ''
        },
        title: {
            type: String,
            default: ''
        },
        badge: {
            type: [String, Boolean],
            default: ''
        }
    }
}
</script>

<style scoped>
.panel-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: flex-start;
    margin-bottom: 1rem;
}

.panel-kicker {
    color: #9c5f16;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.12em;
    margin-bottom: 0.25rem;
    font-weight: 700;
}

.panel-title {
    color: #111827;
    font-size: 1.22rem;
    font-weight: 700;
}

.panel-badge {
    display: inline-flex;
    align-items: center;
    height: fit-content;
    padding: 0.3rem 0.7rem;
    border-radius: 999px;
    background: #eef3fb;
    color: #325891;
    font-size: 0.76rem;
    font-weight: 700;
}
</style>
