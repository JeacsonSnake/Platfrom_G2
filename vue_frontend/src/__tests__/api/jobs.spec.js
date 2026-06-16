import { describe, it, expect, vi } from 'vitest'
import jobsApi from '@/services/api/jobs'
import client from '@/services/api/client'

describe('api/jobs', () => {
  it('createJob calls POST /api/v1/jobs/ with payload', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    const payload = { recipe_id: 1, operator: 'op', overrides: {} }
    await jobsApi.createJob(payload)
    expect(spy).toHaveBeenCalledWith('/api/v1/jobs/', payload)
    spy.mockRestore()
  })

  it('startJob calls POST /api/v1/jobs/{id}/start/', async () => {
    const spy = vi.spyOn(client, 'post').mockResolvedValue({ data: {} })
    await jobsApi.startJob(7)
    expect(spy).toHaveBeenCalledWith('/api/v1/jobs/7/start/', {})
    spy.mockRestore()
  })

  it('getJobStatus calls GET /api/v1/jobs/{id}/status/', async () => {
    const spy = vi.spyOn(client, 'get').mockResolvedValue({ data: {} })
    await jobsApi.getJobStatus(7)
    expect(spy).toHaveBeenCalledWith('/api/v1/jobs/7/status/')
    spy.mockRestore()
  })
})
