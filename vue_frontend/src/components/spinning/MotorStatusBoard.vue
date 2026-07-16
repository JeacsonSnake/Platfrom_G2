<template>
    <div v-if="motors.length" class="board-table">
        <div class="board-table__head">
            <span>ID</span>
            <span>Name</span>
            <span>Availability</span>
            <span>Status</span>
            <span>Target RPM</span>
            <span>Actual RPM</span>
        </div>
        <article class="board-row" v-for="motor in motors" :key="motor.id">
            <div class="board-cell board-cell--strong">{{ motor.id }}</div>
            <div class="board-cell">{{ motor.name }}</div>
            <div class="board-cell">
                <span class="availability-pill" :class="motor.avaliable ? 'availability-pill--good' : 'availability-pill--bad'">
                    {{ motor.avaliable ? 'Available' : 'Unavailable' }}
                </span>
            </div>
            <div class="board-cell">
                <span class="status-pill" :class="`status-pill--${motor.status}`">
                    {{ formatStatus(motor.status) }}
                </span>
            </div>
            <div class="board-cell">{{ motor.target_speed ?? 0 }}</div>
            <div class="board-cell">{{ motor.actual_speed ?? 0 }}</div>
        </article>
    </div>

    <div v-else class="empty-state">
        Motor records will appear here after the backend returns the motor list.
    </div>
</template>

<script>
export default {
    name: 'MotorStatusBoard',
    props: {
        motors: {
            type: Array,
            default: () => []
        }
    },
    methods: {
        formatStatus(status) {
            const map = {
                idle: 'Idle',
                running: 'Running',
                fault: 'Fault',
                offline: 'Offline'
            }
            return map[status] || status
        }
    }
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
    grid-template-columns: 0.4fr 0.8fr 0.8fr 0.8fr 0.8fr 0.8fr;
    gap: 0.75rem;
    align-items: center;
    padding: 0.95rem 1rem;
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

.availability-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 92px;
    padding: 0.28rem 0.72rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
}

.availability-pill--good {
    background: #dcfce7;
    color: #166534;
}

.availability-pill--bad {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 72px;
    padding: 0.28rem 0.72rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
    text-transform: capitalize;
    background: #e2e8f0;
    color: #334155;
}

.status-pill--idle {
    background: #f1f5f9;
    color: #475569;
}

.status-pill--running {
    background: #dcfce7;
    color: #166534;
}

.status-pill--fault {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill--offline {
    background: #f3f4f6;
    color: #6b7280;
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
    .board-row {
        grid-template-columns: 1fr;
    }
}
</style>
