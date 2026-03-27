#include "main.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

static char *TAG = "ESP32S3_STATUS_LED";
static int led_mode = LED_OFF;
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t encoder = NULL;

// WS2812 时序配置 (800 KHz)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution

// LED 颜色定义 (GRB 格式)
typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} led_color_t;

static const led_color_t COLOR_OFF = {0, 0, 0};
static const led_color_t COLOR_GREEN = {255, 0, 0};
static const led_color_t COLOR_BLUE = {0, 0, 255};
static const led_color_t COLOR_YELLOW = {255, 255, 0};

static void set_led_color(const led_color_t *color)
{
    uint8_t led_data[3] = {color->g, color->r, color->b};
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    rmt_transmit(led_chan, encoder, led_data, sizeof(led_data), &tx_config);
    rmt_tx_wait_all_done(led_chan, 100);
}

void status_led_init(void)
{
    // RMT TX 通道配置
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = STATUS_LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    // WS2812 编码器配置
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &encoder));

    // 启用通道
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    // 初始熄灭
    set_led_color(&COLOR_OFF);
    led_mode = LED_OFF;

    ESP_LOGI(TAG, "WS2812 RGB LED initialized on GPIO %d", STATUS_LED_GPIO);

    // 注意：启动测试已移至 WiFi 连接成功后执行，避免阻塞 WiFi 事件处理
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
                    set_led_color(&COLOR_OFF);
                    break;
                case LED_BLINK_FAST:
                    blink_interval = 100;  // 100ms - 快速闪烁（黄色）
                    break;
                case LED_BLINK_SLOW:
                    blink_interval = 500;  // 500ms - 慢速闪烁（蓝色）
                    break;
                case LED_ON:
                    blink_interval = 0;
                    set_led_color(&COLOR_GREEN);  // 常亮绿色
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
                    set_led_color(&COLOR_YELLOW);  // WiFi 连接中 - 黄色
                } else {
                    set_led_color(&COLOR_BLUE);    // MQTT 连接中 - 蓝色
                }
            } else {
                set_led_color(&COLOR_OFF);
            }
            led_state = !led_state;
            vTaskDelay(pdMS_TO_TICKS(blink_interval));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // 状态检查间隔
        }
    }
}
