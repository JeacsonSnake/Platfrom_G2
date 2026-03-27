<template>
    <section class="spinning-console">
        <header class="console-header">
            <div>
                <p class="eyebrow">Motor Control Console</p>
                <h1 class="title console-title">Spinning Operations</h1>
                <p class="console-copy">
                    Schedule spin tasks, inspect actuator availability, and observe live motor speed from a single operator workspace.
                </p>
            </div>
            <div class="header-status">
                <div class="status-chip">
                    <span class="status-chip__label">Motors</span>
                    <span class="status-chip__value">{{ motors.length }}</span>
                </div>
                <div class="status-chip">
                    <span class="status-chip__label">Scheduled Jobs</span>
                    <span class="status-chip__value">{{ records.length }}</span>
                </div>
            </div>
        </header>

        <section class="metric-row">
            <article class="metric-card">
                <span class="metric-card__label">Selected Motor</span>
                <span class="metric-card__value">{{ motor_selected || 'Not selected' }}</span>
            </article>
            <article class="metric-card">
                <span class="metric-card__label">Current Speed</span>
                <span class="metric-card__value">{{ real_speed || 0 }}</span>
            </article>
            <article class="metric-card">
                <span class="metric-card__label">Target Speed</span>
                <span class="metric-card__value">{{ target_speed || 0 }}</span>
            </article>
            <article class="metric-card metric-card--accent">
                <span class="metric-card__label">Schedule Queue</span>
                <span class="metric-card__value">{{ records.length }}</span>
            </article>
        </section>

        <div class="console-grid">
            <section class="panel-card">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Fleet</p>
                        <h2 class="panel-title">Motor Status Board</h2>
                    </div>
                    <span class="panel-badge">Inventory</span>
                </div>

                <div v-if="motors.length" class="board-table">
                    <div class="board-table__head">
                        <span>ID</span>
                        <span>Name</span>
                        <span>Availability</span>
                        <span>Description</span>
                    </div>
                    <article class="board-row" v-for="motor in motors" :key="motor.id">
                        <div class="board-cell board-cell--strong">{{ motor.id }}</div>
                        <div class="board-cell">{{ motor.name }}</div>
                        <div class="board-cell">
                            <span class="availability-pill" :class="motor.avaliable ? 'availability-pill--good' : 'availability-pill--bad'">
                                {{ motor.avaliable ? 'Available' : 'Unavailable' }}
                            </span>
                        </div>
                        <div class="board-cell">{{ motor.description }}</div>
                    </article>
                </div>

                <div v-else class="empty-state">
                    Motor records will appear here after the backend returns the motor list.
                </div>
            </section>

            <section class="panel-card">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Scheduling</p>
                        <h2 class="panel-title">Register Spin Task</h2>
                    </div>
                    <span class="panel-badge">Operator Entry</span>
                </div>

                <div class="form-grid">
                    <div class="field">
                        <label class="label">Motor Selection</label>
                        <div class="select is-fullwidth">
                            <select v-model="motor_selected">
                                <option v-for="motor in motors" :key="motor.id">
                                    {{ motor.name }}
                                </option>
                            </select>
                        </div>
                    </div>

                    <div class="field">
                        <label class="label">Scheduled Time</label>
                        <VueDatePicker v-model="date" />
                    </div>

                    <div class="field">
                        <label class="label">Spinning Speed</label>
                        <input type="number" class="input" v-model="speed">
                    </div>

                    <div class="field">
                        <label class="label">Spinning Time (s)</label>
                        <input type="number" class="input" v-model="duration">
                    </div>
                </div>

                <div class="action-row">
                    <button class="button is-dark" @click="submit">Create Schedule</button>
                </div>

                <div v-if="errors.length" class="console-message console-message--error">
                    <p v-for="error in errors" :key="error">{{ error }}</p>
                </div>
            </section>
        </div>

        <div class="console-grid console-grid--bottom">
            <section class="panel-card">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Queue</p>
                        <h2 class="panel-title">Registration List</h2>
                    </div>
                    <span class="panel-badge">{{ records.length }} Item{{ records.length === 1 ? '' : 's' }}</span>
                </div>

                <div v-if="records.length" class="board-table">
                    <div class="board-table__head board-table__head--records">
                        <span>ID</span>
                        <span>Motor</span>
                        <span>Scheduled Time</span>
                        <span>Speed</span>
                        <span>Duration</span>
                    </div>
                    <article class="board-row board-row--records" v-for="record in records" :key="record.id">
                        <div class="board-cell board-cell--strong">{{ record.id }}</div>
                        <div class="board-cell">{{ record.motor_name }}</div>
                        <div class="board-cell">{{ record.scheduled_time }}</div>
                        <div class="board-cell">{{ record.motor_speed }}</div>
                        <div class="board-cell">{{ record.duration_sec }}</div>
                    </article>
                </div>

                <div v-else class="empty-state">
                    Scheduled motor jobs will appear here after registration.
                </div>
            </section>

            <section class="panel-card">
                <div class="panel-header">
                    <div>
                        <p class="panel-kicker">Live Control</p>
                        <h2 class="panel-title">Operating Information</h2>
                    </div>
                    <span class="panel-badge">Realtime</span>
                </div>

                <div class="telemetry-grid">
                    <article class="telemetry-card">
                        <span class="telemetry-card__label">Motor 1 Speed</span>
                        <span class="telemetry-card__value">{{ real_speed }}</span>
                    </article>
                    <article class="telemetry-card">
                        <span class="telemetry-card__label">Target Speed Range</span>
                        <span class="telemetry-card__value">0-70 rps</span>
                    </article>
                </div>

                <div class="field">
                    <label class="label">Direct Target Speed</label>
                    <input type="number" class="input" v-model="target_speed">
                </div>

                <div class="action-row">
                    <button class="button is-success" @click="set_speed">Send Motor Command</button>
                </div>

                <section class="runbook-card">
                    <p class="runbook-title">Operator Notes</p>
                    <div class="runbook-row">
                        <span class="runbook-index">01</span>
                        <span class="runbook-copy">Use scheduled registration for repeatable operations and traceability.</span>
                    </div>
                    <div class="runbook-row">
                        <span class="runbook-index">02</span>
                        <span class="runbook-copy">Use direct target speed only for manual intervention or supervised testing.</span>
                    </div>
                    <div class="runbook-row">
                        <span class="runbook-index">03</span>
                        <span class="runbook-copy">Set target speed to zero to stop live polling and reset the observed speed.</span>
                    </div>
                </section>
            </section>
        </div>
    </section>
</template>

<script>
import axios from 'axios';
import { ref } from 'vue';
import VueDatePicker from '@vuepic/vue-datepicker';
import '@vuepic/vue-datepicker/dist/main.css';

export default {
    mounted() {
        this.getMotors()
        this.getRecords()
    },
    beforeRouteLeave() {
        clearInterval(this.listener)
    },
    setup() {
        const date = ref();

        return {
            date
        }
    },
    data() {
        return {
            motors: [],
            motor_selected: '',
            speed: 0,
            duration: 0,
            records: [],
            errors: [],
            real_speed: 0,
            target_speed: 0,
            listen_started: false,
            listener: null
        }
    },
    methods: {
        getMotors() {
            const data = {
                token: this.$store.state.token
            }
            axios
                .post('/api/get_motors/', data)
                .then(response => {
                    this.motors = response.data.motor_list
                    if (this.motors.length) {
                        this.motor_selected = this.motors[0].name
                    }
                })
        },
        getRecords() {
            const data = {
                token: this.$store.state.token,
                data: null
            }
            axios
                .post('/api/spinning/', data)
                .then(response => {
                    this.records = response.data.record_list
                })
        },
        submit() {
            this.errors = []
            const data = {
                token: this.$store.state.token,
                data: {
                    motor_name: this.motor_selected,
                    scheduled_time: this.datetime_formatter(),
                    motor_speed: this.speed,
                    duration_sec: this.duration
                }
            }
            axios
                .post('/api/spinning/', data)
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
        datetime_formatter() {
            const data = {
                year: this.date.getFullYear(),
                month: this.date.getMonth() + 1,
                date: this.date.getDate(),
                hours: this.date.getHours(),
                minutes: this.date.getMinutes(),
                seconds: this.date.getSeconds(),
            }
            data.month = data.month >= 10 ? data.month : `0${data.month}`;
            data.date = data.date >= 10 ? data.date : `0${data.date}`;
            data.hours = data.hours >= 10 ? data.hours : `0${data.hours}`;
            data.minutes = data.minutes >= 10 ? data.minutes : `0${data.minutes}`;
            data.seconds = data.seconds >= 10 ? data.seconds : `0${data.seconds}`;
            return `${data.year}-${data.month}-${data.date}T${data.hours}:${data.minutes}:${data.seconds}`
        },
        set_speed() {
            this.errors = []
            const data = {
                topic: 'control',
                msg: this.target_speed
            }
            axios
                .post('/api/mqtt_msg/', data)
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
                    axios
                        .get('/api/mqtt_msg/')
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
    },
    components: { VueDatePicker }
}
</script>

<style scoped>
.spinning-console {
    padding: 1.25rem;
    min-height: calc(100vh - 4rem);
    background:
        linear-gradient(180deg, #101925 0%, #152132 20%, #eef3f8 20%, #eef3f8 100%);
}

.console-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: stretch;
    margin-bottom: 1.4rem;
    padding: 1.25rem 1.4rem;
    border-radius: 20px;
    background: linear-gradient(180deg, rgba(9, 14, 22, 0.88) 0%, rgba(19, 29, 45, 0.9) 100%);
    border: 1px solid rgba(148, 163, 184, 0.15);
    box-shadow: 0 18px 40px rgba(5, 10, 18, 0.3);
}

.eyebrow {
    margin-bottom: 0.3rem;
    color: #8fb3d9;
    font-size: 0.8rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    font-weight: 700;
}

.console-title {
    margin-bottom: 0.5rem !important;
    color: #f8fafc;
}

.console-copy {
    max-width: 56rem;
    color: #adbacd;
    line-height: 1.6;
}

.header-status {
    display: flex;
    gap: 0.8rem;
}

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

.metric-row {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1.4rem;
}

.metric-card {
    display: flex;
    flex-direction: column;
    gap: 0.22rem;
    padding: 0.95rem 1rem;
    border-radius: 16px;
    background: #ffffff;
    border: 1px solid rgba(15, 23, 36, 0.08);
    box-shadow: 0 10px 24px rgba(15, 23, 36, 0.08);
}

.metric-card--accent {
    border-left: 4px solid #325891;
}

.metric-card__label {
    color: #64748b;
    font-size: 0.76rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.metric-card__value {
    color: #111827;
    font-size: 1.05rem;
    font-weight: 700;
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

.panel-header {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    align-items: flex-start;
    margin-bottom: 1rem;
}

.panel-kicker {
    color: #9c5f16;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.12em;
    margin-bottom: 0.25rem;
    font-weight: 700;
}

.panel-title {
    color: #111827;
    font-size: 1.18rem;
    font-weight: 700;
}

.panel-badge {
    display: inline-flex;
    align-items: center;
    height: fit-content;
    padding: 0.3rem 0.7rem;
    border-radius: 999px;
    background: #eef3fb;
    color: #325891;
    font-size: 0.76rem;
    font-weight: 700;
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
    align-items: start;
    padding: 0.95rem 1rem;
}

.board-table__head--records,
.board-row--records {
    grid-template-columns: 0.4fr 0.8fr 1.5fr 0.6fr 0.6fr;
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

.telemetry-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1rem;
}

.telemetry-card {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    padding: 0.9rem 1rem;
    border-radius: 16px;
    background: #f9fbfe;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.telemetry-card__label {
    color: #64748b;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.telemetry-card__value {
    color: #111827;
    font-weight: 700;
}

.runbook-card {
    margin-top: 1rem;
    padding: 1rem;
    border-radius: 18px;
    background: #f4f7fb;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.runbook-title {
    color: #111827;
    font-weight: 700;
    margin-bottom: 0.75rem;
}

.runbook-row {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.75rem;
    align-items: start;
}

.runbook-row + .runbook-row {
    margin-top: 0.7rem;
}

.runbook-index {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 34px;
    height: 34px;
    border-radius: 10px;
    background: #e5edf6;
    color: #274c7d;
    font-size: 0.8rem;
    font-weight: 700;
}

.runbook-copy {
    color: #475569;
    line-height: 1.55;
}

.empty-state {
    padding: 1.6rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    color: #64748b;
    background: #fbfcfd;
}

@media screen and (max-width: 1180px) {
    .metric-row,
    .console-grid {
        grid-template-columns: 1fr;
    }
}

@media screen and (max-width: 960px) {
    .form-grid,
    .telemetry-grid,
    .board-table__head,
    .board-row,
    .board-table__head--records,
    .board-row--records {
        grid-template-columns: 1fr;
    }
}

@media screen and (max-width: 768px) {
    .spinning-console {
        padding: 1rem;
    }

    .console-header {
        flex-direction: column;
    }

    .header-status,
    .action-row {
        flex-wrap: wrap;
    }
}
</style>
