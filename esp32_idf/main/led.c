#include "main.h"

static char *TAG = "ESP32S3_STATUS_LED";
static int led_mode = LED_OFF;

void status_led_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    STATUS_LED_OFF();
    led_mode = LED_OFF;
    ESP_LOGI(TAG, "Status LED initialized on GPIO %d", STATUS_LED_GPIO);
}

void status_led_set_mode(int mode)
{
    led_mode = mode;
    ESP_LOGI(TAG, "LED mode changed to: %d", mode);
}

void status_led_task(void *pvParameters)
{
    status_led_init();
    
    int blink_interval = 0;
    int last_mode = -1;
    
    while (1)
    {
        // 根据模式设置闪烁频率
        if (led_mode != last_mode) {
            switch (led_mode) {
                case LED_OFF:
                    blink_interval = 0;
                    STATUS_LED_OFF();
                    break;
                case LED_BLINK_FAST:
                    blink_interval = 100;  // 100ms - 快速闪烁
                    break;
                case LED_BLINK_SLOW:
                    blink_interval = 500;  // 500ms - 慢速闪烁
                    break;
                case LED_ON:
                    blink_interval = 0;
                    STATUS_LED_ON();
                    break;
                default:
                    blink_interval = 0;
                    break;
            }
            last_mode = led_mode;
        }
        
        // 执行闪烁
        if (blink_interval > 0) {
            STATUS_LED_TOGGLE();
            vTaskDelay(pdMS_TO_TICKS(blink_interval));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // 状态检查间隔
        }
    }
}
