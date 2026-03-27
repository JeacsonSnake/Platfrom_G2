``` esp-idf bash
[1068/1087] Building C object esp-idf/main/CMakeFiles/__idf_main.dir/heating_detect.c.obj                                             
E:/Platform_G2/esp32_idf/main/heating_detect.c: In function 'onewire_diagnose_bus':
E:/Platform_G2/esp32_idf/main/heating_detect.c:248:19: warning: unused variable 'orig_config' [-Wunused-variable]
  248 |     gpio_config_t orig_config;
      |                   ^~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c: In function 'onewire_search_rom':
E:/Platform_G2/esp32_idf/main/heating_detect.c:471:13: warning: variable 'last_zero' set but not used [-Wunused-but-set-variable]     
  471 |     uint8_t last_zero = 0;
      |             ^~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c: At top level:
E:/Platform_G2/esp32_idf/main/heating_detect.c:188:15: warning: 'rmt_onewire_encoder' defined but not used [-Wunused-function]        
  188 | static size_t rmt_onewire_encoder(rmt_channel_handle_t channel, const void *primary_data,
      |               ^~~~~~~~~~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c:126:17: warning: 's_conversion_start_time' defined but not used [-Wunused-variable]    
  126 | static uint32_t s_conversion_start_time = 0;            /**< 杞崲寮€濮嬫椂闂?*/
      |                 ^~~~~~~~~~~~~~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c:125:16: warning: 's_current_sensor' defined but not used [-Wunused-variable]
  125 | static uint8_t s_current_sensor = 0;                    /**< 褰撳墠澶勭悊鐨勪紶鎰熷櫒绱㈠紩 */
      |                ^~~~~~~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c:124:21: warning: 's_poll_state' defined but not used [-Wunused-variable]
  124 | static poll_state_t s_poll_state = POLL_STATE_IDLE;     /**< 褰撳墠杞鐘舵€?*/
      |                     ^~~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c:97:29: warning: 's_rmt_encoder' defined but not used [-Wunused-variable]
   97 | static rmt_encoder_handle_t s_rmt_encoder = NULL;       /**< RMT缂栫爜鍣?*/
      |                             ^~~~~~~~~~~~~
E:/Platform_G2/esp32_idf/main/heating_detect.c:96:29: warning: 's_rmt_rx_channel' defined but not used [-Wunused-variable]
   96 | static rmt_channel_handle_t s_rmt_rx_channel = NULL;    /**< RMT RX閫氶亾 */
```