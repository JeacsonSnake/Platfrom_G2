/**
 * MQTT 连接状态提示服务
 * 参考 Element UI Message 的服务式调用 + 顶部下滑动画，
 * 但针对成功/失败两种状态做差异化交互：
 *   - 成功：显示 5 秒，可关闭
 *   - 失败：不自动关闭、无关闭按钮，提供刷新按钮
 */

import { h } from 'vue'
import ElMessage from 'element-plus/es/components/message/index.mjs'
import 'element-plus/es/components/message/style/css'

let currentMessageInstance = null

function closeCurrent() {
  if (currentMessageInstance && typeof currentMessageInstance.close === 'function') {
    currentMessageInstance.close()
  }
  currentMessageInstance = null
}

/**
 * 显示 MQTT 状态提示
 * @param {Object} options
 * @param {boolean} options.connected - true 成功 / false 失败
 * @param {string} options.text - 提示文本
 * @param {Function} [options.onRefresh] - 失败时的刷新回调
 */
export function showMqttMessage({ connected, text, onRefresh }) {
  closeCurrent()

  if (connected) {
    currentMessageInstance = ElMessage({
      message: text,
      type: 'success',
      duration: 5000,
      showClose: true,
      center: true,
      offset: 16,
    })
    return
  }

  // 失败状态：不自动关闭、无关闭按钮，显示刷新按钮
  const content = h(
    'div',
    { class: 'mqtt-message-content is-flex is-align-items-center is-justify-content-space-between' },
    [
      h('span', { class: 'mqtt-message-text' }, text),
      h(
        'button',
        {
          class: 'button is-small is-outlined is-white ml-3',
          onClick: () => {
            if (typeof onRefresh === 'function') {
              onRefresh()
            }
          },
        },
        'Reconnect'
      ),
    ]
  )

  currentMessageInstance = ElMessage({
    message: content,
    type: 'error',
    duration: 0,
    showClose: false,
    center: true,
    offset: 16,
    customClass: 'mqtt-message--error',
  })
}

/**
 * 手动关闭当前 MQTT 提示
 */
export function closeMqttMessage() {
  closeCurrent()
}
