import { describe, it, expect, vi } from 'vitest'
import authApi from '@/services/api/auth'
import client from '@/services/api/client'

describe('api/auth', () => {
  it('validateToken calls correct endpoint', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await authApi.validateToken('test-token')
    expect(spy).toHaveBeenCalledWith('/api/token_validation/', { token: 'test-token' })
    spy.mockRestore()
  })

  it('login calls correct endpoint', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await authApi.login('a@b.com', 'pw')
    expect(spy).toHaveBeenCalledWith('/api/login/', { email: 'a@b.com', password: 'pw' })
    spy.mockRestore()
  })

  it('signup calls correct endpoint', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await authApi.signup('a@b.com', 'pw')
    expect(spy).toHaveBeenCalledWith('/api/signup/', { email: 'a@b.com', password: 'pw' })
    spy.mockRestore()
  })

  it('changePassword calls correct endpoint', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await authApi.changePassword('tk', 'old', 'new')
    expect(spy).toHaveBeenCalledWith('/api/change_password/', {
      token: 'tk',
      old_password: 'old',
      new_password: 'new'
    })
    spy.mockRestore()
  })
})
