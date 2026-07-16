<template>
    <div class="form-grid">
        <div class="field field--full">
            <label class="label">Device Selection</label>
            <div class="select is-fullwidth">
                <select :value="modelValue.device_id" @change="updateDevice($event.target.value)">
                    <option value="" disabled>Select a device</option>
                    <option v-for="device in devices" :key="device.device_id" :value="device.device_id">
                        {{ formatDeviceLabel(device) }}
                    </option>
                </select>
            </div>
        </div>

        <div class="field field--full">
            <label class="label">Motor Selection</label>
            <div class="motor-checklist">
                <label
                    v-for="motor in motors"
                    :key="motor.id"
                    class="motor-checkbox"
                    :class="{ 'motor-checkbox--disabled': !modelValue.device_id }"
                >
                    <input
                        type="checkbox"
                        :value="motor.name"
                        :checked="modelValue.motor_names.includes(motor.name)"
                        :disabled="!modelValue.device_id"
                        @change="toggleMotor(motor.name, $event.target.checked)"
                    />
                    <span>{{ motor.name }}</span>
                </label>
            </div>
            <p v-if="!modelValue.device_id" class="field-hint">Please select a device first.</p>
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
        <button class="button is-dark" :disabled="!canSubmit" @click="$emit('submit')">Create Schedule</button>
    </div>


</template>

<script>
import { ElDatePicker } from 'element-plus'
import 'element-plus/es/components/date-picker/style/css'

export default {
    name: 'ScheduleForm',
    components: { ElDatePicker },
    props: {
        devices: {
            type: Array,
            default: () => []
        },
        motors: {
            type: Array,
            default: () => []
        },
        modelValue: {
            type: Object,
            default: () => ({
                device_id: '',
                motor_names: [],
                scheduled_time: '',
                motor_speed: 0,
                duration_sec: 0
            })
        }
    },
    emits: ['update:model-value', 'submit'],
    computed: {
        canSubmit() {
            return (
                this.modelValue.device_id &&
                this.modelValue.motor_names.length > 0 &&
                this.modelValue.motor_speed > 0 &&
                this.modelValue.duration_sec > 0
            )
        }
    },
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
        updateDevice(deviceId) {
            this.$emit('update:model-value', {
                ...this.modelValue,
                device_id: deviceId,
                motor_names: []
            })
        },
        toggleMotor(motorName, checked) {
            const current = new Set(this.modelValue.motor_names)
            if (checked) {
                current.add(motorName)
            } else {
                current.delete(motorName)
            }
            // 按电机索引从小到大排序
            const sorted = this.sortMotorsByIndex(Array.from(current))
            this.updateField('motor_names', sorted)
        },
        sortMotorsByIndex(names) {
            return names.slice().sort((a, b) => {
                const indexA = this.motorIndexFromName(a)
                const indexB = this.motorIndexFromName(b)
                return indexA - indexB
            })
        },
        motorIndexFromName(name) {
            const match = String(name).match(/(\d+)$/)
            return match ? parseInt(match[1], 10) : 0
        },
        setImmediate() {
            this.updateField('scheduled_time', this.formatDateTime(new Date()))
        },
        formatDateTime(date) {
            const pad = (n) => String(n).padStart(2, '0')
            return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
        },
        formatDeviceLabel(device) {
            const id = this.formatDeviceId(device)
            if (device.label && device.label !== device.device_id) {
                return `${device.label} (${id})`
            }
            return id
        },
        formatDeviceId(device) {
            const raw = device.mac_address || device.device_id || ''
            const normalized = String(raw).toLowerCase().replace(/[^0-9a-f]/g, '')
            if (normalized.length === 12) {
                return `esp32_${normalized}`
            }
            return device.device_id || raw
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

.field--full {
    grid-column: 1 / -1;
}

.label {
    display: block;
    margin-bottom: 0.35rem;
    font-size: 0.85rem;
    font-weight: 600;
    color: #334155;
}

.select select,
.input {
    width: 100%;
    padding: 0.55rem 0.75rem;
    border-radius: 10px;
    border: 1px solid rgba(15, 23, 36, 0.12);
    background: #ffffff;
    color: #1f2937;
    font-size: 0.9rem;
}

.select select:disabled,
.input:disabled {
    background: #f1f5f9;
    color: #94a3b8;
}

.motor-checklist {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 0.5rem;
    padding: 0.6rem;
    background: #f8fafc;
    border: 1px solid rgba(15, 23, 36, 0.08);
    border-radius: 12px;
}

.motor-checkbox {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.4rem 0.5rem;
    border-radius: 8px;
    cursor: pointer;
    font-size: 0.88rem;
    color: #1f2937;
    transition: background 0.15s ease;
}

.motor-checkbox:hover:not(.motor-checkbox--disabled) {
    background: #f1f5f9;
}

.motor-checkbox--disabled {
    opacity: 0.6;
    cursor: not-allowed;
}

.motor-checkbox input[type='checkbox'] {
    width: 1rem;
    height: 1rem;
    accent-color: #0f172a;
}

.field-hint {
    margin: 0.4rem 0 0;
    font-size: 0.78rem;
    color: #64748b;
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

.button.is-dark {
    padding: 0.65rem 1.2rem;
    border-radius: 10px;
    border: none;
    background: #0f172a;
    color: #ffffff;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
}

.button.is-dark:hover:not(:disabled) {
    background: #1e293b;
}

.button.is-dark:disabled {
    background: #94a3b8;
    cursor: not-allowed;
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

    .motor-checklist {
        grid-template-columns: 1fr;
    }
}
</style>
