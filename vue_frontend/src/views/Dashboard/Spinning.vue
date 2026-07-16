<template>
    <section class="spinning-console">
        <ConsoleHeader
            eyebrow="Motor Control Console"
            title="Spinning Operations"
            copy="Schedule spin tasks, inspect actuator availability, and observe live motor speed from a single operator workspace."
            :status-items="[
                { label: 'Devices', value: devices.length },
                { label: 'Motors', value: motors.length },
                { label: 'Scheduled Jobs', value: records.length }
            ]"
        />

        <section class="metric-row">
            <MetricCard label="Selected Device" :value="selectedDeviceLabel || 'Not selected'" />
            <MetricCard label="Selected Motors" :value="selectedMotorDisplay || 'None'" />
            <MetricCard label="Schedule Queue" :value="records.length" accent />
        </section>

        <div class="console-grid">
            <section class="panel-card">
                <PanelHeader kicker="Fleet" title="Motor Status Board" badge="Inventory" />
                <MotorStatusBoard
                    :motors="motors"
                    :devices="devices"
                    :selected-device-id="selectedDeviceId"
                    :loading="loadingStatus"
                    @select-device="onSelectDevice"
                    @refresh="refreshMotors"
                />
            </section>

            <section class="panel-card">
                <PanelHeader kicker="Scheduling" title="Register Spin Task" badge="Operator Entry" />
                <ScheduleForm
                    :devices="devices"
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
                <ScheduleQueue
                    :records="records"
                    @cancel="cancelSchedule"
                    @delete="deleteSchedule"
                    @delete-selected="deleteSelected"
                    @clear-all="clearSchedules"
                />
            </section>
        </div>
    </section>
</template>

<script>
import motorsApi from '@/services/api/motors.js'
import devicesApi from '@/services/api/devices.js'
import { ElMessageBox, ElMessage } from 'element-plus'
import 'element-plus/es/components/message-box/style/css'
import 'element-plus/es/components/message/style/css'
import 'element-plus/theme-chalk/base.css'
import ConsoleHeader from '@/components/ui/ConsoleHeader.vue'
import MetricCard from '@/components/ui/MetricCard.vue'
import PanelHeader from '@/components/ui/PanelHeader.vue'
import MotorStatusBoard from '@/components/spinning/MotorStatusBoard.vue'
import ScheduleForm from '@/components/spinning/ScheduleForm.vue'
import ScheduleQueue from '@/components/spinning/ScheduleQueue.vue'

export default {
    name: 'SpinningView',
    components: {
        ConsoleHeader,
        MetricCard,
        PanelHeader,
        MotorStatusBoard,
        ScheduleForm,
        ScheduleQueue
    },
    mounted() {
        this.getDevices().then(() => {
            this.getMotors()
        })
        this.getRecords()
        this.refreshInterval = setInterval(() => {
            this.getMotors()
            this.getRecords()
        }, 5000)
    },
    beforeRouteLeave() {
        clearInterval(this.refreshInterval)
    },
    data() {
        return {
            devices: [],
            selectedDeviceId: '',
            deviceMotors: {},
            motors: [],
            scheduleForm: {
                device_id: '',
                motor_names: [],
                scheduled_time: '',
                motor_speed: 0,
                duration_sec: 0
            },
            records: [],
            errors: [],
            refreshInterval: null,
            loadingStatus: false
        }
    },
    computed: {
        selectedDevice() {
            return this.devices.find(d => d.device_id === this.selectedDeviceId) || null
        },
        selectedDeviceLabel() {
            const device = this.selectedDevice
            if (!device) return ''
            if (device.label && device.label !== device.device_id) {
                return `${device.label} (${this.formatMac(device)})`
            }
            return this.formatMac(device)
        },
        selectedMotorDisplay() {
            if (!this.scheduleForm.motor_names.length) return ''
            return this.scheduleForm.motor_names.join(', ')
        }
    },
    methods: {
        async getDevices() {
            try {
                const response = await devicesApi.getList()
                this.devices = response.data.data || []
                if (this.devices.length) {
                    // 默认选中第一个在线设备，否则第一个设备
                    const online = this.devices.find(d => d.is_online)
                    this.selectedDeviceId = online ? online.device_id : this.devices[0].device_id
                    this.scheduleForm.device_id = this.selectedDeviceId
                }
            } catch (error) {
                this.handleApiError(error)
            }
        },
        getMotors() {
            if (!this.selectedDeviceId) return
            this.loadingStatus = true
            motorsApi
                .getList(this.$store.state.token, this.selectedDeviceId)
                .then(response => {
                    const list = response.data.motor_list || []
                    this.deviceMotors = { ...this.deviceMotors, [this.selectedDeviceId]: list }
                    this.motors = list
                })
                .catch(this.handleApiError)
                .finally(() => {
                    this.loadingStatus = false
                })
        },
        getRecords() {
            motorsApi
                .getRecords(this.$store.state.token)
                .then(response => {
                    this.records = response.data.record_list || []
                })
                .catch(this.handleApiError)
        },
        onSelectDevice(deviceId) {
            this.selectedDeviceId = deviceId
            this.scheduleForm.device_id = deviceId
            this.scheduleForm.motor_names = []
            if (this.deviceMotors[deviceId]) {
                this.motors = this.deviceMotors[deviceId]
            }
            this.getMotors()
        },
        refreshMotors() {
            this.getMotors()
            ElMessage({ message: 'Motor status refreshed', type: 'success' })
        },
        async submitSchedule() {
            this.errors = []
            const { device_id, motor_names, scheduled_time, motor_speed, duration_sec } = this.scheduleForm

            if (!device_id) {
                this.errors.push('Please select a device.')
                return
            }
            if (!motor_names.length) {
                this.errors.push('Please select at least one motor.')
                return
            }

            let scheduledTime = scheduled_time

            // 以服务端当前时间为基准，根据偏差决定行为
            if (scheduledTime) {
                let checkResult
                try {
                    const response = await motorsApi.checkScheduleTime(
                        this.$store.state.token,
                        scheduledTime
                    )
                    checkResult = response.data
                } catch (error) {
                    this.handleApiError(error)
                    return
                }

                if (checkResult.expired) {
                    if (checkResult.diff_seconds > 30) {
                        // 超过 30 秒：询问用户是否立即执行
                        try {
                            await ElMessageBox.confirm(
                                '预约时间已早于服务端当前时间超过 30 秒，是否立即执行？',
                                '时间过期',
                                {
                                    confirmButtonText: '立即执行',
                                    cancelButtonText: '重新调整时间（取消发送指令）',
                                    type: 'warning',
                                    closeOnClickModal: false
                                }
                            )
                            // 用户选择“立即执行”
                            scheduledTime = checkResult.server_now
                        } catch {
                            // 用户选择“重新调整时间”：取消发送，不执行任何操作
                            return
                        }
                    } else {
                        // 30 秒以内：自动调整为当前时间并立即执行
                        ElMessage({
                            message: '预约时间已略早于服务端当前时间，已自动调整为当前时间并立即执行',
                            type: 'info'
                        })
                        scheduledTime = checkResult.server_now
                    }
                }
            }

            const payload = {
                device_id,
                motor_names,
                scheduled_time: scheduledTime,
                motor_speed: Number(motor_speed),
                duration_sec: Number(duration_sec)
            }
            motorsApi
                .createSchedule(this.$store.state.token, payload)
                .then(() => {
                    this.getRecords()
                    ElMessage({ message: 'Schedule created', type: 'success' })
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
        formatDateTime(date) {
            const pad = (n) => String(n).padStart(2, '0')
            return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
        },
        formatMac(device) {
            const mac = device.mac_address || device.device_id || ''
            const normalized = String(mac).toLowerCase().replace(/[^0-9a-f]/g, '')
            if (normalized.length === 12) {
                return normalized.match(/.{1,2}/g).join(':')
            }
            return mac
        },
        cancelSchedule(id) {
            this.errors = []
            motorsApi
                .cancelSchedule(this.$store.state.token, id)
                .then(() => {
                    this.getRecords()
                })
                .catch(this.handleApiError)
        },
        deleteSchedule(id) {
            this.errors = []
            motorsApi
                .deleteSchedules(this.$store.state.token, [id])
                .then(() => {
                    this.getRecords()
                })
                .catch(this.handleApiError)
        },
        deleteSelected(ids) {
            this.errors = []
            motorsApi
                .deleteSchedules(this.$store.state.token, ids)
                .then(() => {
                    this.getRecords()
                })
                .catch(this.handleApiError)
        },
        clearSchedules() {
            this.errors = []
            if (!confirm('Are you sure you want to clear all scheduled records?')) {
                return
            }
            motorsApi
                .clearSchedules(this.$store.state.token)
                .then(() => {
                    this.getRecords()
                })
                .catch(this.handleApiError)
        },
        handleApiError(error) {
            if (error.response) {
                for (const property in error.response.data) {
                    this.errors.push(`${property}: ${error.response.data[property]}`)
                }
            } else if (error.message) {
                this.errors.push(`Error:${error.message}`)
            } else {
                console.log(JSON.stringify(error))
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
    grid-template-columns: repeat(3, minmax(0, 1fr));
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
