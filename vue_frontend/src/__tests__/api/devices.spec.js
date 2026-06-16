import { describe, it, expect, vi } from 'vitest'
import devicesApi from '@/services/api/devices'
import client from '@/services/api/client'

describe('api/devices', () => {
  it('getList calls GET /api/device_list/', async () => {
    const spy = vi.spyOn(client, 'get').mockResolvedValue({ data: {} })
    await devicesApi.getList()
    expect(spy).toHaveBeenCalledWith('/api/device_list/')
    spy.mockRestore()
  })

  it('emergencyStop sends correct payload', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await devicesApi.emergencyStop(['d1'], 'single', 'reason', 'op')
    expect(spy).toHaveBeenCalledWith('/api/devices/emergency_stop/', {
      device_ids: ['d1'],
      scope: 'single',
      reason: 'reason',
      triggered_by: 'op'
    })
    spy.mockRestore()
  })

  it('resume sends correct payload', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await devicesApi.resume(['d1'], 'op')
    expect(spy).toHaveBeenCalledWith('/api/devices/resume/', {
      device_ids: ['d1'],
      resumed_by: 'op'
    })
    spy.mockRestore()
  })

  it('dispatchTask sends correct payload', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await devicesApi.dispatchTask('d1', 0, 3000, 10)
    expect(spy).toHaveBeenCalledWith('/api/devices/dispatch_task/', {
      device_id: 'd1',
      motor: 0,
      speed: 3000,
      duration: 10
    })
    spy.mockRestore()
  })
})
