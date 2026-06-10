/**
 * WebSocket 服务封装（单例）
 * - 自动重连（指数退避）
 * - 消息订阅/分发模式：按 topic 注册回调
 * - 心跳保活（每 25 秒发送 ping）
 * - 支持通过 WebSocket 向后端发送控制指令
 */

const WS_BASE_URL = import.meta.env.VITE_WS_URL || 'ws://127.0.0.1:8000/websocket/'

class WebSocketService {
  constructor() {
    this.url = WS_BASE_URL
    this.client = null
    this.listeners = new Map() // topic -> callback[]
    this.reconnectInterval = 2000
    this.maxReconnectInterval = 30000
    this.reconnectTimer = null
    this.heartbeatTimer = null
    this.isIntentionallyClosed = false
    this.connectionStatus = 'disconnected' // 'connected' | 'connecting' | 'disconnected'
    this.statusListeners = []
  }

  static getInstance() {
    if (!WebSocketService.instance) {
      WebSocketService.instance = new WebSocketService()
    }
    return WebSocketService.instance
  }

  connect() {
    if (this.client && (this.client.readyState === WebSocket.CONNECTING || this.client.readyState === WebSocket.OPEN)) {
      return
    }

    this.isIntentionallyClosed = false
    this.connectionStatus = 'connecting'
    this._notifyStatusListeners()

    try {
      this.client = new WebSocket(this.url)
    } catch (err) {
      console.error('[WebSocket] create failed:', err)
      this._scheduleReconnect()
      return
    }

    this.client.onopen = () => {
      console.log('[WebSocket] connected.')
      this.connectionStatus = 'connected'
      this.reconnectInterval = 2000
      this._notifyStatusListeners()
      this._startHeartbeat()
      // 连接成功后请求一次全量快照
      this.send({ action: 'get_snapshot' })
    }

    this.client.onmessage = (event) => {
      this._handleMessage(event.data)
    }

    this.client.onclose = () => {
      console.log('[WebSocket] closed.')
      this.connectionStatus = 'disconnected'
      this._notifyStatusListeners()
      this._stopHeartbeat()
      if (!this.isIntentionallyClosed) {
        this._scheduleReconnect()
      }
    }

    this.client.onerror = (err) => {
      console.error('[WebSocket] error:', err)
      this.connectionStatus = 'disconnected'
      this._notifyStatusListeners()
    }
  }

  disconnect() {
    this.isIntentionallyClosed = true
    this._stopHeartbeat()
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    if (this.client) {
      this.client.close()
      this.client = null
    }
    this.connectionStatus = 'disconnected'
    this._notifyStatusListeners()
  }

  send(payload) {
    if (!this.client || this.client.readyState !== WebSocket.OPEN) {
      console.warn('[WebSocket] cannot send, not connected.', payload)
      return false
    }
    try {
      const text = typeof payload === 'string' ? payload : JSON.stringify(payload)
      this.client.send(text)
      return true
    } catch (err) {
      console.error('[WebSocket] send failed:', err)
      return false
    }
  }

  // 订阅特定 topic 的消息
  subscribe(topic, callback) {
    if (!this.listeners.has(topic)) {
      this.listeners.set(topic, [])
    }
    this.listeners.get(topic).push(callback)

    // 返回取消订阅函数
    return () => {
      const arr = this.listeners.get(topic)
      if (arr) {
        const idx = arr.indexOf(callback)
        if (idx !== -1) arr.splice(idx, 1)
      }
    }
  }

  // 订阅连接状态变化
  onStatusChange(callback) {
    this.statusListeners.push(callback)
    // 立即推送当前状态
    callback(this.connectionStatus)
    return () => {
      const idx = this.statusListeners.indexOf(callback)
      if (idx !== -1) this.statusListeners.splice(idx, 1)
    }
  }

  _handleMessage(data) {
    let payload
    try {
      payload = JSON.parse(data)
    } catch (err) {
      console.warn('[WebSocket] non-JSON message:', data)
      return
    }

    const topic = payload.topic || 'unknown'
    const callbacks = this.listeners.get(topic) || []
    callbacks.forEach((cb) => {
      try {
        cb(payload)
      } catch (err) {
        console.error(`[WebSocket] listener error on topic ${topic}:`, err)
      }
    })

    // 也广播到通配符 '*' 订阅者（用于调试日志等）
    const allCallbacks = this.listeners.get('*') || []
    allCallbacks.forEach((cb) => {
      try {
        cb(payload)
      } catch (err) {
        console.error('[WebSocket] wildcard listener error:', err)
      }
    })
  }

  _scheduleReconnect() {
    if (this.reconnectTimer) return
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null
      console.log(`[WebSocket] reconnecting... (interval=${this.reconnectInterval}ms)`)
      this.connect()
      this.reconnectInterval = Math.min(this.reconnectInterval * 1.5, this.maxReconnectInterval)
    }, this.reconnectInterval)
  }

  _startHeartbeat() {
    this._stopHeartbeat()
    this.heartbeatTimer = setInterval(() => {
      // 使用 get_snapshot 作为轻量级心跳/保活消息
      this.send({ action: 'get_snapshot' })
    }, 25000)
  }

  _stopHeartbeat() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer)
      this.heartbeatTimer = null
    }
  }

  _notifyStatusListeners() {
    this.statusListeners.forEach((cb) => {
      try {
        cb(this.connectionStatus)
      } catch (err) {
        console.error('[WebSocket] status listener error:', err)
      }
    })
  }
}

WebSocketService.instance = null

export default WebSocketService
