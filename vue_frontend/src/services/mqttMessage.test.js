import { describe, it, expect, vi, beforeEach } from 'vitest'
import { showMqttMessage, closeMqttMessage } from './mqttMessage.js'

const mockClose = vi.fn()
const mockElMessage = vi.fn(() => ({ close: mockClose }))

vi.mock('element-plus/es/components/message/index.mjs', () => ({
  default: (...args) => mockElMessage(...args)
}))

describe('mqttMessage service', () => {
  beforeEach(() => {
    // 清理跨测试用例的 message 实例，避免单例状态泄漏
    closeMqttMessage()
    vi.clearAllMocks()
  })

  it('shows a success message with 5s duration and close button', () => {
    showMqttMessage({ connected: true, text: 'MQTT connection restored' })

    expect(mockElMessage).toHaveBeenCalledTimes(1)
    const options = mockElMessage.mock.calls[0][0]
    expect(options.type).toBe('success')
    expect(options.message).toBe('MQTT connection restored')
    expect(options.duration).toBe(5000)
    expect(options.showClose).toBe(true)
  })

  it('shows an error message without auto-close/close button and with a reconnect button', () => {
    const onRefresh = vi.fn()
    showMqttMessage({
      connected: false,
      text: 'MQTT disconnected',
      onRefresh
    })

    expect(mockElMessage).toHaveBeenCalledTimes(1)
    const options = mockElMessage.mock.calls[0][0]
    expect(options.type).toBe('error')
    expect(options.duration).toBe(0)
    expect(options.showClose).toBe(false)
    expect(options.customClass).toBe('mqtt-message--error')

    // message should be a VNode containing the text and a button
    const vnode = options.message
    expect(vnode).toBeDefined()
    expect(vnode.type).toBe('div')

    const children = vnode.children
    expect(children).toBeInstanceOf(Array)
    expect(children.length).toBe(2)

    const textSpan = children[0]
    expect(textSpan.type).toBe('span')
    expect(textSpan.children).toBe('MQTT disconnected')

    const button = children[1]
    expect(button.type).toBe('button')
    expect(button.children).toBe('Reconnect')

    // simulate clicking the button
    button.props.onClick()
    expect(onRefresh).toHaveBeenCalledTimes(1)
  })

  it('closes the previous message before showing a new one', () => {
    showMqttMessage({ connected: false, text: 'first' })
    expect(mockClose).not.toHaveBeenCalled()

    showMqttMessage({ connected: true, text: 'second' })
    expect(mockClose).toHaveBeenCalledTimes(1)
    expect(mockElMessage).toHaveBeenCalledTimes(2)
  })

  it('closes the current message when closeMqttMessage is called', () => {
    showMqttMessage({ connected: false, text: 'MQTT disconnected' })
    closeMqttMessage()
    expect(mockClose).toHaveBeenCalledTimes(1)
  })
})
