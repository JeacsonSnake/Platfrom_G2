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
            <VueDatePicker :model-value="date" @update:model-value="onDateChange" />
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
import { ref, watch } from 'vue'
import VueDatePicker from '@vuepic/vue-datepicker'
import '@vuepic/vue-datepicker/dist/main.css'

export default {
    name: 'ScheduleForm',
    components: { VueDatePicker },
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
    setup(props, { emit }) {
        const date = ref(props.modelValue.scheduled_time ? new Date(props.modelValue.scheduled_time) : undefined)

        watch(() => props.modelValue.scheduled_time, (newVal) => {
            date.value = newVal ? new Date(newVal) : undefined
        })

        return { date }
    },
    methods: {
        updateField(key, value) {
            this.$emit('update:model-value', { ...this.modelValue, [key]: value })
        },
        onDateChange(newDate) {
            this.date = newDate
            const formatted = newDate ? this.datetimeFormatter(newDate) : ''
            this.updateField('scheduled_time', formatted)
        },
        datetimeFormatter(date) {
            const data = {
                year: date.getFullYear(),
                month: date.getMonth() + 1,
                date: date.getDate(),
                hours: date.getHours(),
                minutes: date.getMinutes(),
                seconds: date.getSeconds()
            }
            data.month = data.month >= 10 ? data.month : `0${data.month}`
            data.date = data.date >= 10 ? data.date : `0${data.date}`
            data.hours = data.hours >= 10 ? data.hours : `0${data.hours}`
            data.minutes = data.minutes >= 10 ? data.minutes : `0${data.minutes}`
            data.seconds = data.seconds >= 10 ? data.seconds : `0${data.seconds}`
            return `${data.year}-${data.month}-${data.date}T${data.hours}:${data.minutes}:${data.seconds}`
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

@media screen and (max-width: 960px) {
    .form-grid {
        grid-template-columns: 1fr;
    }
}
</style>
