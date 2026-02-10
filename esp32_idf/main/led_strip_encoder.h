/*
 * Simple WS2812 LED Strip Encoder Header
 * For ESP-IDF RMT peripheral
 */

#pragma once

#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WS2812 encoder configuration
 */
typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;

/**
 * @brief Create RMT encoder for WS2812 LED strip
 *
 * @param[in] config Encoder configuration
 * @param[out] ret_encoder Returned encoder handle
 * @return
 *      - ESP_OK: Create encoder successfully
 *      - ESP_ERR_INVALID_ARG: Create encoder failed because of invalid argument
 *      - ESP_ERR_NO_MEM: Create encoder failed because of out of memory
 */
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif
