#include "main.h"

static char *TAG = "ESP32S3_STATUS_LED";
static int led_mode = LED_OFF;
static led_strip_handle_t led_strip = NULL;

// LED 颜色定义 (R, G, B)
#define LED_COLOR_OFF       0, 0, 0
#define LED_COLOR_RED       255, 0, 0
#define LED_COLOR_GREEN     0, 255, 0
#define LED_COLOR_BLUE      0, 0, 255
#define LED_COLOR_YELLOW    255, 255, 0
#define LED_COLOR_WHITE     255, 255, 255

void status_led_init(void)
{
    // WS2812 RGB LED 配置
    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,  // 只有 1 个 LED
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // 初始熄灭
    led_strip_clear(led_strip);
    led_mode = LED_OFF;
    
    ESP_LOGI(TAG, "WS2812 RGB LED initialized on GPIO %d", STATUS_LED_GPIO);
    
    // 启动测试：LED 闪烁 3 次（白色）
    ESP_LOGI(TAG, "LED startup test - blinking 3 times...");
    for (int i = 0; i < 3; i++) {
        led_strip_set_pixel(led_strip, 0, LED_COLOR_WHITE);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
        led_strip_clear(led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG, "LED startup test complete");
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
    bool led_state = false;
    
    while (1)
    {
        // 根据模式设置颜色和闪烁频率
        if (led_mode != last_mode) {
            switch (led_mode) {
                case LED_OFF:
                    blink_interval = 0;
                    led_strip_clear(led_strip);
                    break;
                case LED_BLINK_FAST:
                    blink_interval = 100;  // 100ms - 快速闪烁（黄色）
                    break;
                case LED_BLINK_SLOW:
                    blink_interval = 500;  // 500ms - 慢速闪烁（蓝色）
                    break;
                case LED_ON:
                    blink_interval = 0;
                    led_strip_set_pixel(led_strip, 0, LED_COLOR_GREEN);  // 常亮绿色
                    led_strip_refresh(led_strip);
                    break;
                default:
                    blink_interval = 0;
                    break;
            }
            last_mode = led_mode;
        }
        
        // 执行闪烁
        if (blink_interval > 0) {
            if (led_state) {
                // 根据模式选择颜色
                if (led_mode == LED_BLINK_FAST) {
                    led_strip_set_pixel(led_strip, 0, LED_COLOR_YELLOW);  // WiFi 连接中 - 黄色
                } else {
                    led_strip_set_pixel(led_strip, 0, LED_COLOR_BLUE);    // MQTT 连接中 - 蓝色
                }
                led_strip_refresh(led_strip);
            } else {
                led_strip_clear(led_strip);
            }
            led_state = !led_state;
            vTaskDelay(pdMS_TO_TICKS(blink_interval));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // 状态检查间隔
        }
    }
}
