import client from './client'

export default {
  getList(token) {
    return client.post('/api/get_motors/', { token })
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

  sendMqttMsg(topic, msg) {
    return client.post('/api/mqtt_msg/', { topic, msg })
  },

  getMqttMsg() {
    return client.get('/api/mqtt_msg/')
  }
}
