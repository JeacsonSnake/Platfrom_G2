import { describe, it, expect, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import LiveEventStream from '@/components/ui/LiveEventStream.vue'

describe('LiveEventStream', () => {
  it('renders events', () => {
    const wrapper = mount(LiveEventStream, {
      props: {
        events: [
          { key: '1', time: '10:00', device: 'd1', topic: 'Heartbeat', summary: 'online', kind: 'heartbeat' }
        ]
      }
    })
    expect(wrapper.text()).toContain('d1')
    expect(wrapper.text()).toContain('Heartbeat')
    expect(wrapper.text()).toContain('online')
  })

  it('shows empty state', () => {
    const wrapper = mount(LiveEventStream, {
      props: { events: [] }
    })
    expect(wrapper.text()).toContain('Waiting for MQTT messages')
  })

  it('emits clear event', async () => {
    const wrapper = mount(LiveEventStream, {
      props: { events: [] }
    })
    await wrapper.find('.clear-button').trigger('click')
    expect(wrapper.emitted('clear')).toHaveLength(1)
  })
})
