import client from './client'

export default {
  getList() {
    return client.get('/api/device_list/')
  },

  emergencyStop(deviceIds, scope, reason, triggeredBy) {
    return client.post('/api/devices/emergency_stop/', {
      device_ids: deviceIds,
      scope,
      reason,
      triggered_by: triggeredBy
    })
  },

  resume(deviceIds, resumedBy) {
    return client.post('/api/devices/resume/', {
      device_ids: deviceIds,
      resumed_by: resumedBy
    })
  },

  dispatchTask(deviceId, motor, speed, duration) {
    return client.post('/api/devices/dispatch_task/', {
      device_id: deviceId,
      motor,
      speed,
      duration
    })
  }
}
