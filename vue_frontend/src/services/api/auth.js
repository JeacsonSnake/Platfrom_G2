import client from './client'

export default {
  validateToken(token) {
    return client.post('/api/token_validation/', { token })
  },

  login(email, password) {
    return client.post('/api/login/', { email, password })
  },

  signup(email, password) {
    return client.post('/api/signup/', { email, password })
  },

  changePassword(token, oldPassword, newPassword) {
    return client.post('/api/change_password/', {
      token,
      old_password: oldPassword,
      new_password: newPassword
    })
  }
}
