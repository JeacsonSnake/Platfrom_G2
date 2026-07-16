<template>
    <div v-if="records.length" class="board-table">
        <div class="board-table__head board-table__head--records">
            <span>ID</span>
            <span>Motor</span>
            <span>Scheduled Time</span>
            <span>Speed</span>
            <span>Duration</span>
            <span>Status</span>
            <span>Action</span>
        </div>
        <article class="board-row board-row--records" v-for="record in records" :key="record.id">
            <div class="board-cell board-cell--strong">{{ record.id }}</div>
            <div class="board-cell">{{ record.motor_name }}</div>
            <div class="board-cell">{{ record.scheduled_time }}</div>
            <div class="board-cell">{{ record.motor_speed }}</div>
            <div class="board-cell">{{ record.duration_sec }}</div>
            <div class="board-cell">
                <span class="status-badge" :class="`status-badge--${record.status.toLowerCase()}`">
                    {{ record.status }}
                </span>
            </div>
            <div class="board-cell">
                <button
                    v-if="record.status === 'PENDING'"
                    class="button is-small is-danger is-outlined"
                    @click="$emit('cancel', record.id)"
                >
                    Cancel
                </button>
            </div>
        </article>
    </div>

    <div v-else class="empty-state">
        Scheduled motor jobs will appear here after registration.
    </div>
</template>

<script>
export default {
    name: 'ScheduleQueue',
    props: {
        records: {
            type: Array,
            default: () => []
        }
    },
    emits: ['cancel']
}
</script>

<style scoped>
.board-table {
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.board-table__head,
.board-row {
    display: grid;
    grid-template-columns: 0.4fr 0.8fr 0.8fr 1.6fr;
    gap: 0.75rem;
    align-items: start;
    padding: 0.95rem 1rem;
}

.board-table__head--records,
.board-row--records {
    grid-template-columns: 0.35fr 0.75fr 1.2fr 0.5fr 0.5fr 0.7fr 0.7fr;
}

.board-table__head {
    background: #ecf2f8;
    color: #5f6d81;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.board-row {
    background: #ffffff;
    border-top: 1px solid rgba(15, 23, 36, 0.06);
}

.board-row:nth-child(even) {
    background: #fafcfe;
}

.board-cell {
    color: #1f2937;
    font-size: 0.92rem;
}

.board-cell--strong {
    font-weight: 700;
    color: #111827;
}

.status-badge {
    display: inline-block;
    padding: 0.25rem 0.55rem;
    border-radius: 9999px;
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    background: #e2e8f0;
    color: #334155;
}

.status-badge--pending {
    background: #fef3c7;
    color: #92400e;
}

.status-badge--sent {
    background: #dbeafe;
    color: #1e40af;
}

.status-badge--failed {
    background: #fee2e2;
    color: #991b1b;
}

.status-badge--completed {
    background: #d1fae5;
    color: #065f46;
}

.status-badge--cancelled {
    background: #f3f4f6;
    color: #4b5563;
}

.button.is-small {
    padding: 0.35rem 0.7rem;
    font-size: 0.78rem;
    border-radius: 8px;
    cursor: pointer;
    border: 1px solid currentColor;
    background: transparent;
}

.button.is-danger {
    color: #b91c1c;
    border-color: #fca5a5;
}

.button.is-danger:hover {
    background: #fee2e2;
}

.empty-state {
    padding: 1.6rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    color: #64748b;
    background: #fbfcfd;
}

@media screen and (max-width: 960px) {
    .board-table__head,
    .board-row,
    .board-table__head--records,
    .board-row--records {
        grid-template-columns: 1fr;
    }
}
</style>
