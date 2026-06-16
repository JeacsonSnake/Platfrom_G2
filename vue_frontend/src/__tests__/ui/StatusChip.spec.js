import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import StatusChip from '@/components/ui/StatusChip.vue'

describe('StatusChip', () => {
  it('renders label and value', () => {
    const wrapper = mount(StatusChip, {
      props: { label: 'Online', value: 'Responsive' }
    })
    expect(wrapper.text()).toContain('Online')
    expect(wrapper.text()).toContain('Responsive')
  })

  it('applies success variant class', () => {
    const wrapper = mount(StatusChip, {
      props: { value: 'OK', variant: 'success' }
    })
    expect(wrapper.find('.status-chip--success').exists()).toBe(true)
  })

  it('applies danger variant class', () => {
    const wrapper = mount(StatusChip, {
      props: { value: 'Error', variant: 'danger' }
    })
    expect(wrapper.find('.status-chip--danger').exists()).toBe(true)
  })
})
