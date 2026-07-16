<template>
    <section class="spinning-console">
        <ConsoleHeader
            eyebrow="Motor Control Console"
            title="Spinning Operations"
            copy="Schedule spin tasks, inspect actuator availability, and observe live motor speed from a single operator workspace."
            :status-items="[{ label: 'Motors', value: motors.length }, { label: 'Scheduled Jobs', value: records.length }]"
        />

        <section class="metric-row">
            <MetricCard label="Selected Motor" :value="scheduleForm.motor_name || 'Not selected'" />
            <MetricCard label="Current Speed" :value="real_speed || 0" />
            <MetricCard label="Target Speed" :value="target_speed || 0" />
            <MetricCard label="Schedule Queue" :value="records.length" accent />
        </section>

        <div class="console-grid">
            <section class="panel-card">
                <PanelHeader kicker="Fleet" title="Motor Status Board" badge="Inventory" />
                <MotorStatusBoard :motors="motors" />
            </section>

            <section class="panel-card">
                <PanelHeader kicker="Scheduling" title="Register Spin Task" badge="Operator Entry" />
                <ScheduleForm
                    :motors="motors"
                    v-model="scheduleForm"
                    :errors="errors"
                    @submit="submitSchedule"
                />
            </section>
        </div>

        <div class="console-grid console-grid--bottom">
            <section class="panel-card">
                <PanelHeader
                    kicker="Queue"
                    title="Registration List"
                    :badge="records.length + ' Item' + (records.length === 1 ? '' : 's')"
                />
                <ScheduleQueue :records="records" @cancel="cancelSchedule" />
            </section>

            <section class="panel-card">
                <PanelHeader kicker="Live Control" title="Operating Information" badge="Realtime" />
                <QuickControl
                    :real-speed="real_speed"
                    :target-speed="target_speed"
                    @update:target-speed="target_speed = $event"
                    @send="set_speed"
                />
            </section>
        </div>
    </section>
</template>

<script>
import motorsApi from '@/services/api/motors.js'
import ConsoleHeader from '@/components/ui/ConsoleHeader.vue'
import MetricCard from '@/components/ui/MetricCard.vue'
import PanelHeader from '@/components/ui/PanelHeader.vue'
import MotorStatusBoard from '@/components/spinning/MotorStatusBoard.vue'
import ScheduleForm from '@/components/spinning/ScheduleForm.vue'
import ScheduleQueue from '@/components/spinning/ScheduleQueue.vue'
import QuickControl from '@/components/spinning/QuickControl.vue'

export default {
    name: 'SpinningView',
    components: {
        ConsoleHeader,
        MetricCard,
        PanelHeader,
        MotorStatusBoard,
        ScheduleForm,
        ScheduleQueue,
        QuickControl
    },
    mounted() {
        this.getMotors()
        this.getRecords()
        this.refreshInterval = setInterval(() => this.getRecords(), 5000)
    },
    beforeRouteLeave() {
        clearInterval(this.listener)
        clearInterval(this.refreshInterval)
    },
    data() {
        return {
            motors: [],
            scheduleForm: {
                motor_name: '',
                scheduled_time: '',
                motor_speed: 0,
                duration_sec: 0
            },
            records: [],
            errors: [],
            real_speed: 0,
            target_speed: 0,
            listen_started: false,
            listener: null,
            refreshInterval: null
        }
    },
    methods: {
        getMotors() {
            motorsApi
                .getList(this.$store.state.token)
                .then(response => {
                    this.motors = response.data.motor_list
                    if (this.motors.length) {
                        this.scheduleForm.motor_name = this.motors[0].name
                    }
                })
        },
        getRecords() {
            motorsApi
                .getRecords(this.$store.state.token)
                .then(response => {
                    this.records = response.data.record_list
                })
        },
        submitSchedule() {
            this.errors = []
            const payload = {
                motor_name: this.scheduleForm.motor_name,
                scheduled_time: this.scheduleForm.scheduled_time,
                motor_speed: Number(this.scheduleForm.motor_speed),
                duration_sec: Number(this.scheduleForm.duration_sec)
            }
            motorsApi
                .createSchedule(this.$store.state.token, payload)
                .then(() => {
                    this.getRecords()
                })
                .catch(error => {
                    if (error.response) {
                        for (const property in error.response.data) {
                            this.errors.push(`${property}: ${error.response.data[property]}`)
                        }
                    } else if (error.message) {
                        this.errors.push(`Error:${error.message}`)
                    } else {
                        console.log(JSON.stringify(error))
                    }
                })
        },
        cancelSchedule(id) {
            this.errors = []
            motorsApi
                .cancelSchedule(this.$store.state.token, id)
                .then(() => {
                    this.getRecords()
                })
                .catch(error => {
                    if (error.response) {
                        for (const property in error.response.data) {
                            this.errors.push(`${property}: ${error.response.data[property]}`)
                        }
                    } else if (error.message) {
                        this.errors.push(`Error:${error.message}`)
                    } else {
                        console.log(JSON.stringify(error))
                    }
                })
        },
        set_speed() {
            this.errors = []
            motorsApi
                .sendMqttMsg('control', this.target_speed)
                .then(() => {
                    if (this.target_speed == 0) {
                        clearInterval(this.listener)
                        this.listen_started = false
                        this.real_speed = 0
                    }
                })
                .catch(error => {
                    if (error.response) {
                        for (const property in error.response.data) {
                            this.errors.push(`${property}: ${error.response.data[property]}`)
                        }
                    } else if (error.message) {
                        this.errors.push(`Error:${error.message}`)
                    } else {
                        console.log(JSON.stringify(error))
                    }
                })
            this.get_speed()
        },
        get_speed() {
            if (this.listen_started == false) {
                this.listener = setInterval(() => {
                    motorsApi
                        .getMqttMsg()
                        .then(response => {
                            this.real_speed = response.data.speed
                        })
                        .catch(error => {
                            console.log(error)
                        })
                }, 1000)
                this.listen_started = true
            }
        }
    }
}
</script>

<style scoped>
.spinning-console {
    padding: 1.25rem;
    min-height: calc(100vh - 4rem);
    background:
        linear-gradient(180deg, #101925 4%, #152132 15%, #eef3f8 30%, #eef3f8 100%);
}

.metric-row {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1.4rem;
}

.console-grid {
    display: grid;
    grid-template-columns: minmax(0, 1.25fr) minmax(360px, 0.95fr);
    gap: 1.25rem;
    margin-bottom: 1.25rem;
}

.console-grid--bottom {
    align-items: start;
}

.panel-card {
    padding: 1.35rem;
    border-radius: 20px;
    background: rgba(255, 255, 255, 0.96);
    border: 1px solid rgba(13, 22, 38, 0.08);
    box-shadow: 0 14px 36px rgba(15, 23, 36, 0.08);
}

@media screen and (max-width: 1180px) {
    .metric-row,
    .console-grid {
        grid-template-columns: 1fr;
    }
}

@media screen and (max-width: 768px) {
    .spinning-console {
        padding: 1rem;
    }
}
</style>
