<template>
    <div class="form-grid">
        <div class="field">
            <label class="label">Motor Selection</label>
            <div class="select is-fullwidth">
                <select :value="modelValue.motor_name" @change="updateField('motor_name', $event.target.value)">
                    <option v-for="motor in motors" :key="motor.id" :value="motor.name">
                        {{ motor.name }}
                    </option>
                </select>
            </div>
        </div>

        <div class="field">
            <label class="label">Scheduled Time</label>
            <div class="datetime-row">
                <el-date-picker
                    :model-value="modelValue.scheduled_time"
                    type="datetime"
                    placeholder="Select date and time"
                    value-format="YYYY-MM-DDTHH:mm:ss"
                    format="YYYY-MM-DD HH:mm:ss"
                    style="flex: 1;"
                    @update:model-value="updateField('scheduled_time', $event)"
                />
                <button class="button is-light" @click="setImmediate">
                    立即执行
                </button>
            </div>
        </div>

        <div class="field">
            <label class="label">Spinning Speed</label>
            <input type="number" class="input" :value="modelValue.motor_speed" @input="updateField('motor_speed', Number($event.target.value))">
        </div>

        <div class="field">
            <label class="label">Spinning Time (s)</label>
            <input type="number" class="input" :value="modelValue.duration_sec" @input="updateField('duration_sec', Number($event.target.value))">
        </div>
    </div>

    <div class="action-row">
        <button class="button is-dark" @click="$emit('submit')">Create Schedule</button>
    </div>

    <div v-if="errors.length" class="console-message console-message--error">
        <p v-for="error in errors" :key="error">{{ error }}</p>
    </div>
</template>

<script>
import { ElDatePicker } from 'element-plus'
import 'element-plus/es/components/date-picker/style/css'

export default {
    name: 'ScheduleForm',
    components: { ElDatePicker },
    props: {
        motors: {
            type: Array,
            default: () => []
        },
        modelValue: {
            type: Object,
            default: () => ({
                motor_name: '',
                scheduled_time: '',
                motor_speed: 0,
                duration_sec: 0
            })
        },
        errors: {
            type: Array,
            default: () => []
        }
    },
    emits: ['update:model-value', 'submit'],
    mounted() {
        // 默认 Scheduled Time 为“立即执行”（当前本地时间）
        if (!this.modelValue.scheduled_time) {
            this.setImmediate()
        }
    },
    methods: {
        updateField(key, value) {
            this.$emit('update:model-value', { ...this.modelValue, [key]: value })
        },
        setImmediate() {
            this.updateField('scheduled_time', this.formatDateTime(new Date()))
        },
        formatDateTime(date) {
            const pad = (n) => String(n).padStart(2, '0')
            return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
        }
    }
}
</script>

<style scoped>
.form-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 0.9rem;
}

.datetime-row {
    display: flex;
    gap: 0.6rem;
    align-items: center;
}

.action-row {
    display: flex;
    gap: 0.75rem;
    margin-top: 1rem;
}

.console-message {
    margin-top: 1rem;
    padding: 0.85rem 1rem;
    border-radius: 14px;
    background: #fff2f2;
    border: 1px solid #f5d0d0;
    color: #a13b3b;
}

.button.is-light {
    padding: 0.55rem 0.9rem;
    border-radius: 10px;
    border: 1px solid rgba(15, 23, 36, 0.12);
    background: #f8fafc;
    color: #334155;
    cursor: pointer;
    font-size: 0.85rem;
    white-space: nowrap;
}

.button.is-light:hover {
    background: #f1f5f9;
}

@media screen and (max-width: 960px) {
    .form-grid {
        grid-template-columns: 1fr;
    }

    .datetime-row {
        flex-direction: column;
        align-items: stretch;
    }
}
</style>
