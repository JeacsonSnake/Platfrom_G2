import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import MetricCard from '@/components/ui/MetricCard.vue'

describe('MetricCard', () => {
  it('renders label and value', () => {
    const wrapper = mount(MetricCard, {
      props: { label: 'Selected Motor', value: 'M1' }
    })
    expect(wrapper.text()).toContain('Selected Motor')
    expect(wrapper.text()).toContain('M1')
  })

  it('applies accent class', () => {
    const wrapper = mount(MetricCard, {
      props: { label: 'Queue', value: 3, accent: true }
    })
    expect(wrapper.find('.metric-card--accent').exists()).toBe(true)
  })
})
