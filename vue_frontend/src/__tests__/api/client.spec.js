import { describe, it, expect } from 'vitest'
import apiClient from '@/services/api/client'

describe('api/client', () => {
  it('should have correct baseURL', () => {
    expect(apiClient.defaults.baseURL).toBe('http://127.0.0.1:8000')
  })

  it('should have json content type header', () => {
    expect(apiClient.defaults.headers['Content-Type']).toBe('application/json')
  })

  it('should have timeout configured', () => {
    expect(apiClient.defaults.timeout).toBe(10000)
  })
})
