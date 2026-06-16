import client from './client'

export default {
  createJob(payload) {
    return client.post('/api/v1/jobs/', payload)
  },

  startJob(id) {
    return client.post(`/api/v1/jobs/${id}/start/`, {})
  },

  getJobStatus(id) {
    return client.get(`/api/v1/jobs/${id}/status/`)
  }
}
