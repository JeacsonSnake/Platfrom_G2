import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ConnectionBar from '@/components/ui/ConnectionBar.vue'

describe('ConnectionBar', () => {
  it('renders connected status', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'connected', mqttAvailable: true }
    })
    expect(wrapper.text()).toContain('WebSocket Connected')
    expect(wrapper.text()).toContain('MQTT 可用')
    expect(wrapper.find('.connection-bar--connected').exists()).toBe(true)
  })

  it('renders disconnected status without mqtt label when null', () => {
    const wrapper = mount(ConnectionBar, {
      props: { status: 'disconnected' }
    })
    expect(wrapper.text()).toContain('WebSocket Disconnected')
    expect(wrapper.text()).not.toContain('MQTT')
  })
})
