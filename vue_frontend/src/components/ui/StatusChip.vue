<template>
    <div class="status-chip" :class="variantClass">
        <span v-if="label" class="status-chip__label">{{ label }}</span>
        <span class="status-chip__value">{{ value }}</span>
    </div>
</template>

<script>
export default {
    name: 'StatusChip',
    props: {
        label: {
            type: String,
            default: ''
        },
        value: {
            type: [String, Number],
            default: ''
        },
        variant: {
            type: String,
            default: 'default',
            validator(value) {
                return ['default', 'success', 'danger', 'warning', 'info'].includes(value)
            }
        }
    },
    computed: {
        variantClass() {
            return {
                'status-chip--success': this.variant === 'success',
                'status-chip--danger': this.variant === 'danger',
                'status-chip--warning': this.variant === 'warning',
                'status-chip--info': this.variant === 'info'
            }
        }
    }
}
</script>

<style scoped>
.status-chip {
    display: flex;
    flex-direction: column;
    justify-content: center;
    min-width: 120px;
    padding: 0.8rem 0.95rem;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(148, 163, 184, 0.14);
}

.status-chip__label {
    color: #8ea2bd;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
}

.status-chip__value {
    color: #f8fafc;
    font-size: 0.98rem;
    font-weight: 700;
}

/* 用于面板内部浅色背景 */
.status-chip--success {
    background: #dcfce7;
    border-color: rgba(22, 101, 52, 0.15);
}
.status-chip--success .status-chip__label,
.status-chip--success .status-chip__value {
    color: #166534;
}

.status-chip--danger {
    background: #fee2e2;
    border-color: rgba(153, 27, 27, 0.15);
}
.status-chip--danger .status-chip__label,
.status-chip--danger .status-chip__value {
    color: #991b1b;
}

.status-chip--warning {
    background: #fef3c7;
    border-color: rgba(154, 103, 15, 0.15);
}
.status-chip--warning .status-chip__label,
.status-chip--warning .status-chip__value {
    color: #9a670f;
}

.status-chip--info {
    background: #dbeafe;
    border-color: rgba(30, 64, 175, 0.15);
}
.status-chip--info .status-chip__label,
.status-chip--info .status-chip__value {
    color: #1e40af;
}
</style>
