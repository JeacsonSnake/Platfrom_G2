import { describe, it, expect, vi } from 'vitest'
import motorsApi from '@/services/api/motors'
import client from '@/services/api/client'

describe('api/motors', () => {
  it('getList calls /api/get_motors/ with token', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await motorsApi.getList('token123')
    expect(spy).toHaveBeenCalledWith('/api/get_motors/', { token: 'token123' })
    spy.mockRestore()
  })

  it('getRecords calls /api/spinning/ with null data', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await motorsApi.getRecords('token123')
    expect(spy).toHaveBeenCalledWith('/api/spinning/', { token: 'token123', data: null })
    spy.mockRestore()
  })

  it('createSchedule wraps payload with token', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    const payload = { motor_name: 'M1', scheduled_time: '2026-01-01T00:00:00', motor_speed: 100, duration_sec: 10 }
    await motorsApi.createSchedule('token123', payload)
    expect(spy).toHaveBeenCalledWith('/api/spinning/', { token: 'token123', data: payload })
    spy.mockRestore()
  })

  it('sendMqttMsg calls /api/mqtt_msg/', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await motorsApi.sendMqttMsg('control', '3000')
    expect(spy).toHaveBeenCalledWith('/api/mqtt_msg/', { topic: 'control', msg: '3000' })
    spy.mockRestore()
  })

  it('getMqttMsg calls GET /api/mqtt_msg/', async () => {
    const spy = vi.spyOn(client, 'get').mockResolvedValue({ data: {} })
    await motorsApi.getMqttMsg()
    expect(spy).toHaveBeenCalledWith('/api/mqtt_msg/')
    spy.mockRestore()
  })
})
