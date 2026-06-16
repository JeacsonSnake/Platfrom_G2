<template>
    <div class="device-table-wrapper">
        <vxe-table
            ref="tableRef"
            class="device-table"
            :data="devices"
            row-id="id"
            :loading="loading"
            :expand-config="{ trigger: 'row', showIcon: true }"
            @checkbox-change="onSelectionChange"
            @checkbox-all="onSelectionChange"
            @toggle-row-expand="onExpandChange"
        >
            <vxe-column type="checkbox" width="50" align="center"></vxe-column>

            <vxe-column field="label" title="Device" min-width="160">
                <template #default="{ row }">
                    <div class="device-cell">
                        <span class="device-label">{{ row.label }}</span>
                        <span class="device-index">{{ row.deviceId }}</span>
                    </div>
                </template>
            </vxe-column>

            <vxe-column field="taskStatus" title="Status" width="120">
                <template #default="{ row }">
                    <span class="status-pill" :class="statusClass(row.taskStatus)">
                        {{ row.taskStatus }}
                    </span>
                </template>
            </vxe-column>

            <vxe-column field="currentTask" title="Task" min-width="160">
                <template #default="{ row }">
                    <div v-if="row.currentTask && row.currentTask.motor !== undefined" class="task-mini">
                        <span class="task-mini__motor">M{{ row.currentTask.motor }}</span>
                        <span class="task-mini__speed">{{ row.currentTask.speed }} rpm</span>
                        <span v-if="row.currentTask.remainingSec > 0" class="task-mini__remaining">{{ row.currentTask.remainingSec }}s left</span>
                    </div>
                    <span v-else class="task-mini task-mini--empty">--</span>
                </template>
            </vxe-column>

            <vxe-column field="connectionStatus" title="Connection" width="120">
                <template #default="{ row }">
                    <span class="status-pill" :class="connectionClass(row.connectionStatus)">
                        {{ row.connectionStatus }}
                    </span>
                </template>
            </vxe-column>

            <vxe-column field="lastSeenText" title="Last Seen" width="120">
                <template #default="{ row }">
                    <span class="last-seen">{{ row.lastSeenText }}</span>
                </template>
            </vxe-column>

            <vxe-column type="expand" width="80" title="">
                <template #expand="{ row }">
                    <DeviceDetailRow :device="row" />
                </template>
            </vxe-column>
        </vxe-table>

        <button class="button refresh-button" @click="$emit('refresh')" :disabled="loading">
            {{ loading ? 'Refreshing…' : 'Refresh Fleet' }}
        </button>
    </div>
</template>

<script>
import DeviceDetailRow from './DeviceDetailRow.vue'

export default {
    name: 'DeviceStatusTable',
    components: { DeviceDetailRow },
    props: {
        devices: {
            type: Array,
            default: () => []
        },
        selectedIds: {
            type: Array,
            default: () => []
        },
        expandedIds: {
            type: Array,
            default: () => []
        },
        loading: {
            type: Boolean,
            default: false
        }
    },
    emits: ['update:selected-ids', 'update:expanded-ids', 'refresh'],
    watch: {
        devices: {
            handler() {
                this.$nextTick(() => {
                    this.syncSelection()
                    this.syncExpand()
                })
            },
            immediate: true
        },
        selectedIds() {
            this.$nextTick(() => this.syncSelection())
        },
        expandedIds() {
            this.$nextTick(() => this.syncExpand())
        }
    },
    mounted() {
        this.$nextTick(() => {
            this.syncSelection()
            this.syncExpand()
        })
    },
    methods: {
        statusClass(status) {
            return {
                'status-pill--online': status === 'Idle',
                'status-pill--busy': status === 'Busy',
                'status-pill--alert': status === 'E-Stopped',
                'status-pill--offline': status === 'Offline'
            }
        },
        connectionClass(status) {
            return {
                'status-pill--online': status === 'Online',
                'status-pill--offline': status !== 'Online'
            }
        },
        onSelectionChange() {
            const table = this.$refs.tableRef
            if (!table) return
            const selectedRows = table.getCheckboxRecords()
            this.$emit('update:selected-ids', selectedRows.map(r => r.id))
        },
        onExpandChange() {
            const table = this.$refs.tableRef
            if (!table) return
            const expandedRows = table.getRowExpandRecords()
            this.$emit('update:expanded-ids', expandedRows.map(r => r.id))
        },
        syncSelection() {
            const table = this.$refs.tableRef
            if (!table) return
            this.devices.forEach(device => {
                const shouldCheck = this.selectedIds.includes(device.id)
                table.setCheckboxRow(device, shouldCheck)
            })
        },
        syncExpand() {
            const table = this.$refs.tableRef
            if (!table) return
            this.devices.forEach(device => {
                const shouldExpand = this.expandedIds.includes(device.id)
                table.setRowExpand(device, shouldExpand)
            })
        }
    }
}
</script>

<style scoped>
.device-table-wrapper {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
}

.device-table {
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(15, 23, 36, 0.08);
    background: #ffffff;
}

.device-cell {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
}

.device-label {
    color: #111827;
    font-weight: 700;
    font-size: 0.92rem;
}

.device-index {
    color: #64748b;
    font-size: 0.78rem;
}

.last-seen {
    color: #475569;
    font-size: 0.88rem;
}

.status-pill {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 80px;
    padding: 0.28rem 0.72rem;
    border-radius: 999px;
    font-size: 0.78rem;
    font-weight: 700;
}

.status-pill--online {
    background: #dcfce7;
    color: #166534;
}

.status-pill--busy {
    background: #dbeafe;
    color: #1e40af;
}

.status-pill--alert {
    background: #fee2e2;
    color: #991b1b;
}

.status-pill--offline {
    background: #e2e8f0;
    color: #475569;
}

.task-mini {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem;
    align-items: center;
}

.task-mini__motor {
    padding: 0.15rem 0.45rem;
    border-radius: 6px;
    background: #eef3fb;
    color: #325891;
    font-size: 0.75rem;
    font-weight: 700;
}

.task-mini__speed {
    color: #1f2937;
    font-size: 0.85rem;
}

.task-mini__remaining {
    color: #325891;
    font-size: 0.78rem;
    font-weight: 600;
}

.task-mini--empty {
    color: #94a3b8;
}

.refresh-button {
    align-self: flex-start;
    background: #d9e4f2;
    color: #142131;
    border: none;
    font-weight: 700;
}
</style>
