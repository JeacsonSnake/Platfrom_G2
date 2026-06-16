import { config } from '@vue/test-utils'

// 全局 mock window.alert / confirm，避免组件调用时抛出
config.global.mocks = {
  $route: {},
  $router: {
    push: () => {}
  }
}

// jsdom 默认没有 ResizeObserver，vxe-table 可能依赖
if (typeof window !== 'undefined' && !window.ResizeObserver) {
  window.ResizeObserver = class ResizeObserver {
    observe() {}
    unobserve() {}
    disconnect() {}
  }
}
