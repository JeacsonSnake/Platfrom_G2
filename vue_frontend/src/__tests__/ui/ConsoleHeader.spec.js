import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import ConsoleHeader from '@/components/ui/ConsoleHeader.vue'

describe('ConsoleHeader', () => {
  it('renders eyebrow, title and copy', () => {
    const wrapper = mount(ConsoleHeader, {
      props: {
        eyebrow: 'Motor Control Console',
        title: 'Spinning Operations',
        copy: 'Schedule spin tasks'
      }
    })
    expect(wrapper.text()).toContain('Motor Control Console')
    expect(wrapper.text()).toContain('Spinning Operations')
    expect(wrapper.text()).toContain('Schedule spin tasks')
  })

  it('renders status items', () => {
    const wrapper = mount(ConsoleHeader, {
      props: {
        title: 'Title',
        statusItems: [{ label: 'Motors', value: 4 }, { label: 'Jobs', value: 2 }]
      }
    })
    expect(wrapper.text()).toContain('Motors')
    expect(wrapper.text()).toContain('4')
    expect(wrapper.text()).toContain('Jobs')
    expect(wrapper.text()).toContain('2')
  })
})
