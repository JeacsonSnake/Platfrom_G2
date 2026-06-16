<template>
    <div v-if="events.length" class="event-stream">
        <article class="event-card" v-for="event in events" :key="event.key">
            <div class="event-card__head">
                <span class="event-topic">
                    {{ event.topic }}
                    <span v-if="event.interfaceType" class="event-topic__meta">{{ event.interfaceType }}</span>
                </span>
                <span class="event-time">{{ event.time }}</span>
            </div>
            <p class="event-summary">{{ event.summary }}</p>
            <div v-if="event.progressLabel" class="event-progress">
                <span>{{ event.progressLabel }}</span>
            </div>
        </article>
    </div>

    <div v-else class="empty-state">
        Realtime controller events will appear here after dispatch or telemetry updates.
    </div>
</template>

<script>
export default {
    name: 'RecipeEventStream',
    props: {
        events: {
            type: Array,
            default: () => []
        }
    }
}
</script>

<style scoped>
.event-stream {
    display: grid;
    gap: 0.85rem;
}

.event-card {
    padding: 0.95rem 1rem;
    border-radius: 16px;
    background: #f9fbfe;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.event-card__head {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    margin-bottom: 0.35rem;
}

.event-topic {
    color: #111827;
    font-weight: 700;
}

.event-topic__meta {
    margin-left: 0.45rem;
    color: #64748b;
    font-size: 0.76rem;
    font-weight: 600;
    text-transform: uppercase;
}

.event-time {
    color: #64748b;
    font-size: 0.82rem;
}

.event-summary {
    color: #475569;
    line-height: 1.55;
}

.event-progress {
    margin-top: 0.5rem;
    color: #285b97;
    font-size: 0.82rem;
    font-weight: 700;
}

.empty-state {
    padding: 1.6rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    color: #64748b;
    background: #fbfcfd;
}
</style>
