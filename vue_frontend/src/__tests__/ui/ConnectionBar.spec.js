import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ConnectionBar from '@/components/ui/ConnectionBar.vue'

describe('ConnectionBar', () => {
  it('renders connected status for both WebSocket and MQTT', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'connected', mqttConnected: true }
    })
    expect(wrapper.text()).toContain('WebSocket Connected')
    expect(wrapper.text()).toContain('MQTT Connected')
    expect(wrapper.find('.connection-item--connected').exists()).toBe(true)
  })

  it('renders disconnected status for both WebSocket and MQTT when MQTT is null', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'disconnected' }
    })
    expect(wrapper.text()).toContain('WebSocket Disconnected')
    expect(wrapper.text()).toContain('MQTT Disconnected')
    expect(wrapper.findAll('.connection-item--disconnected').length).toBe(2)
  })

  it('renders mqtt disconnected while websocket connected', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'connected', mqttConnected: false }
    })
    expect(wrapper.text()).toContain('WebSocket Connected')
    expect(wrapper.text()).toContain('MQTT Disconnected')
    expect(wrapper.findAll('.connection-item--connected').length).toBe(1)
    expect(wrapper.findAll('.connection-item--disconnected').length).toBe(1)
  })

  it('renders connecting status when mqttStatus prop is provided', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'connecting', mqttStatus: 'connecting' }
    })
    expect(wrapper.text()).toContain('WebSocket Connecting…')
    expect(wrapper.text()).toContain('MQTT Connecting…')
    expect(wrapper.findAll('.connection-item--connecting').length).toBe(2)
  })
})
