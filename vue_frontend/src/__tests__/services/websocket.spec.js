import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import WebSocketService from '@/services/websocket'

describe('WebSocketService', () => {
  let service

  beforeEach(() => {
    service = WebSocketService.getInstance()
    service.disconnect()
    WebSocketService.instance = null
    service = WebSocketService.getInstance()
  })

  afterEach(() => {
    service.disconnect()
  })

  it('should be singleton', () => {
    const another = WebSocketService.getInstance()
    expect(service).toBe(another)
  })

  it('should return false when send without connection', () => {
    expect(service.send({ action: 'test' })).toBe(false)
  })

  it('should notify status listeners', () => {
    const cb = vi.fn()
    service.onStatusChange(cb)
    expect(cb).toHaveBeenCalledWith('disconnected')
  })

  it('should dispatch message to subscribed listeners', () => {
    const cb = vi.fn()
    service.subscribe('heartbeat', cb)
    service._handleMessage(JSON.stringify({ topic: 'heartbeat', device_id: 'd1' }))
    expect(cb).toHaveBeenCalled()
  })

  it('should ignore non-JSON messages', () => {
    const cb = vi.fn()
    service.subscribe('heartbeat', cb)
    service._handleMessage('not json')
    expect(cb).not.toHaveBeenCalled()
  })
})
