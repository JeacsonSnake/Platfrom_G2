<template>
    <div v-if="records.length" class="queue-panel">
        <div class="queue-toolbar">
            <label class="select-all">
                <input
                    type="checkbox"
                    :checked="allSelected"
                    @change="toggleSelectAll"
                />
                <span>Select All</span>
            </label>
            <div class="bulk-actions">
                <button
                    class="button is-small is-danger is-outlined"
                    :disabled="selectedIds.length === 0"
                    @click="$emit('delete-selected', selectedIds)"
                >
                    Delete Selected
                </button>
                <button
                    class="button is-small is-danger is-outlined"
                    @click="$emit('clear-all')"
                >
                    Clear All
                </button>
            </div>
        </div>

        <div class="board-table">
            <div class="board-table__head board-table__head--records">
                <span></span>
                <span>ID</span>
                <span>Device</span>
                <span>Motors</span>
                <span>Scheduled Time</span>
                <span>Speed</span>
                <span>Duration</span>
                <span>Status</span>
                <span>Action</span>
            </div>
            <article class="board-row board-row--records" v-for="record in records" :key="record.id">
                <div class="board-cell board-cell--center">
                    <input
                        type="checkbox"
                        :value="record.id"
                        v-model="selectedIds"
                    />
                </div>
                <div class="board-cell board-cell--strong">{{ record.id }}</div>
                <div class="board-cell">{{ record.device_label || record.device_id }}</div>
                <div class="board-cell">{{ record.motor_display || record.motor_name }}</div>
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
                        class="button is-small is-danger is-outlined"
                        @click="$emit('delete', record.id)"
                    >
                        Delete
                    </button>
                </div>
            </article>
        </div>
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
    emits: ['delete', 'delete-selected', 'clear-all'],
    data() {
        return {
            selectedIds: []
        }
    },
    computed: {
        allSelected() {
            return this.records.length > 0 && this.selectedIds.length === this.records.length
        }
    },
    watch: {
        records() {
            // 删除已不存在的记录的选中状态
            const existingIds = new Set(this.records.map(r => r.id))
            this.selectedIds = this.selectedIds.filter(id => existingIds.has(id))
        }
    },
    methods: {
        toggleSelectAll(event) {
            if (event.target.checked) {
                this.selectedIds = this.records.map(r => r.id)
            } else {
                this.selectedIds = []
            }
        }
    }
}
</script>

<style scoped>
.queue-panel {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
}

.queue-toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.6rem 0.8rem;
    background: #f8fafc;
    border: 1px solid rgba(15, 23, 36, 0.08);
    border-radius: 12px;
}

.select-all {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.85rem;
    color: #334155;
    cursor: pointer;
}

.bulk-actions {
    display: flex;
    gap: 0.5rem;
}

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
    align-items: center;
    padding: 0.95rem 1rem;
}

.board-table__head--records,
.board-row--records {
    grid-template-columns: 0.3fr 0.25fr 0.75fr 0.75fr 1.1fr 0.4fr 0.4fr 0.6fr 0.55fr;
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

.board-cell--center {
    display: flex;
    justify-content: center;
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

.status-badge--running {
    background: #dcfce7;
    color: #166534;
}

.status-badge--finished {
    background: #dbeafe;
    color: #1e40af;
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

.button.is-danger:hover:not(:disabled) {
    background: #fee2e2;
}

.button.is-danger:disabled {
    opacity: 0.5;
    cursor: not-allowed;
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

    .queue-toolbar {
        flex-direction: column;
        align-items: flex-start;
        gap: 0.5rem;
    }
}
</style>
