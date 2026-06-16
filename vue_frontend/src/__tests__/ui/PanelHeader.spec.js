import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import PanelHeader from '@/components/ui/PanelHeader.vue'

describe('PanelHeader', () => {
  it('renders kicker, title and badge', () => {
    const wrapper = mount(PanelHeader, {
      props: { kicker: 'Fleet', title: 'Motor Status Board', badge: 'Inventory' }
    })
    expect(wrapper.text()).toContain('Fleet')
    expect(wrapper.text()).toContain('Motor Status Board')
    expect(wrapper.text()).toContain('Inventory')
  })

  it('hides badge when badge is false', () => {
    const wrapper = mount(PanelHeader, {
      props: { kicker: 'Fleet', title: 'Motor Status Board', badge: false }
    })
    expect(wrapper.find('.panel-badge').exists()).toBe(false)
  })

  it('renders slot content', () => {
    const wrapper = mount(PanelHeader, {
      props: { title: 'Title' },
      slots: { default: '<button class="slot-btn">Action</button>' }
    })
    expect(wrapper.find('.slot-btn').exists()).toBe(true)
  })
})
