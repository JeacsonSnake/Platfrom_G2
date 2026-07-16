import client from './client'

export default {
  getList(token, deviceId = null) {
    const payload = { token }
    if (deviceId) {
      payload.device_id = deviceId
    }
    return client.post('/api/get_motors/', payload)
  },

  getRecords(token) {
    return client.post('/api/spinning/', { token, data: null })
  },

  createSchedule(token, payload) {
    return client.post('/api/spinning/', {
      token,
      data: payload
    })
  },

  cancelSchedule(token, id) {
    return client.post('/api/spinning/cancel/', { token, id })
  },

  deleteSchedules(token, ids) {
    return client.post('/api/spinning/delete/', { token, ids })
  },

  clearSchedules(token) {
    return client.post('/api/spinning/clear/', { token })
  },

  checkScheduleTime(token, scheduledTime) {
    return client.post('/api/spinning/check_time/', { token, scheduled_time: scheduledTime })
  },

  sendMqttMsg(topic, msg) {
    return client.post('/api/mqtt_msg/', { topic, msg })
  },

  getMqttMsg() {
    return client.get('/api/mqtt_msg/')
  }
}
