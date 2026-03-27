# MAX31850KATB+ Temperature Sensor Driver Implementation - Phase 2

**Date**: 2026-03-27  
**Branch**: `feature/heating`  
**Task Description**: Fix 1-Wire communication timing issues for stable temperature reading from MAX31850 sensors

---

## 1. Background

### Previous Status (Phase 1)
After initial implementation, the driver could:
- Initialize GPIO14 for 1-Wire communication
- Detect external pull-up resistors
- Successfully perform ROM search and find 4 MAX31850 devices
- Read temperature once successfully (1.58°C)

### Issues Identified
- **First read success, subsequent reads failed**: Temperature conversion and reading became unstable after the initial successful read
- **Various error types**: Short to GND, Open circuit, CRC errors
- **Root cause**: Insufficient timing delays in 1-Wire bit-level operations and byte-level transfers

---

## 2. Problem Analysis

### 2.1 Timing Issues

| Issue | Description | Impact |
|-------|-------------|--------|
| Bit-level timing | No delay between bits during read/write | Signal instability |
| Byte-level timing | No delay between bytes in scratchpad read | Data corruption |
| Conversion timing | 110ms conversion time insufficient | Incomplete conversion |
| Release timing | Bus not properly released after Convert T | Power supply issue during conversion |

### 2.2 GPIO Mode Issue

**Critical Finding**: ESP32-S3 GPIO14 cannot drive high level in push-pull mode (returns 0 when forced high), but works correctly with open-drain mode + external pull-up resistor.

```
Diagnostic Results:
- Test 1 (Floating input): 1 ✓ (external pull-up present)
- Test 2 (Internal pull-up): 1 ✓
- Test 3 (Forced low): 0 ✓
- Test 4 (Forced high): 0 ✗ (push-pull high fails!)
- Test 5 (Released): 1 ✓ (open-drain with pull-up works)
```

**Conclusion**: GPIO14 has hardware limitation - must use open-drain mode exclusively.

---

## 3. Implementation Solutions

### 3.1 Modified Files

#### `main/heating_detect.c`

**1. GPIO Initialization Fix**
```c
// Configure GPIO as open-drain with pull-up
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << onewire_pin),
    .mode = GPIO_MODE_INPUT_OUTPUT_OD,  // Open-drain mode (not push-pull)
    .pull_up_en = GPIO_PULLUP_ENABLE,    // Enable internal pull-up
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
```

**2. onewire_reset() Timing Optimization**
- Changed from push-pull to open-drain mode
- Added 15μs delay after releasing bus to ensure rise time
- Reordered timing: 15μs wait → sample at ~70μs → sample at ~100μs

**3. onewire_write_bit() Implementation**
```c
static esp_err_t onewire_write_bit(uint8_t bit)
{
    // Use open-drain mode throughout
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
    
    if (bit & 0x01) {
        // Write 1: Low for 10μs, then release
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE1_US);
        gpio_set_level(s_onewire_pin, 1);  // Release
        esp_rom_delay_us(ONEWIRE_SLOT_US - ONEWIRE_WRITE1_US);
    } else {
        // Write 0: Low for 70μs, then release
        gpio_set_level(s_onewire_pin, 0);
        esp_rom_delay_us(ONEWIRE_WRITE0_US);
        gpio_set_level(s_onewire_pin, 1);  // Release
        esp_rom_delay_us(ONEWIRE_RECOVERY_US);
    }
    return ESP_OK;
}
```

**4. onewire_read_bit() Stabilization**
- Added 3μs stabilization delay before sampling
- Total timing: 5μs (init low) → 3μs (stabilize) → 7μs (to 15μs sample point)

**5. Byte-level Timing Fixes**
```c
// Added 2μs delay between bits
static esp_err_t onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(onewire_write_bit(data & 0x01));
        data >>= 1;
        esp_rom_delay_us(2);  // Bit-to-bit delay
    }
    return ESP_OK;
}

// Added 2μs delay between bits and 10μs between bytes
static esp_err_t onewire_read_byte(uint8_t *data)
{
    *data = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t bit = 0;
        ESP_ERROR_CHECK(onewire_read_bit(&bit));
        *data |= (bit << i);
        esp_rom_delay_us(2);  // Bit-to-bit delay
    }
    return ESP_OK;
}
```

**6. max31850_start_conversion() Fix**
```c
static esp_err_t max31850_start_conversion(const uint8_t *rom_id)
{
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    onewire_write_byte(MAX31850_CMD_CONVERT_T);
    
    // Critical: Release bus after Convert T command
    // Sensor needs pull-up power during conversion
    gpio_set_direction(s_onewire_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(s_onewire_pin, GPIO_PULLUP_ONLY);
    
    return ESP_OK;
}
```

**7. max31850_read_scratchpad() Optimization**
```c
static esp_err_t max31850_read_scratchpad(const uint8_t *rom_id, uint8_t *scratchpad)
{
    if (!scratchpad) return ESP_ERR_INVALID_ARG;
    
    ESP_ERROR_CHECK(onewire_match_rom(rom_id));
    onewire_write_byte(MAX31850_CMD_READ_SCRATCH);
    
    for (int i = 0; i < MAX31850_SCRATCHPAD_LEN; i++) {
        onewire_read_byte(&scratchpad[i]);
        esp_rom_delay_us(10);  // Inter-byte delay for stability
    }
    
    return ESP_OK;
}
```

#### `main/heating_detect.h`

**Conversion Time Adjustment**
```c
#define MAX31850_CONVERSION_TIME_MS 150  // Increased from 110ms to 150ms
```

---

## 4. Timing Parameters Summary

| Parameter | Old Value | New Value | Description |
|-----------|-----------|-----------|-------------|
| Conversion time | 110ms | 150ms | Time to wait for temperature conversion |
| Reset rise delay | 5μs | 15μs | Delay after releasing bus in reset |
| Read stabilization | 0μs | 3μs | Delay before sampling during read |
| Bit-to-bit delay | 0μs | 2μs | Delay between bits in byte operations |
| Byte-to-byte delay | 0μs | 10μs | Delay between bytes in scratchpad read |

---

## 5. Unused Variables and Functions (Reserved for Future Use)

The following variables and functions are defined but not currently used. They are reserved for future enhancements:

### 5.1 RMT-Related (Reserved for RMT Implementation)

```c
// Variables
static rmt_channel_handle_t s_rmt_tx_channel = NULL;    // RMT TX channel
static rmt_channel_handle_t s_rmt_rx_channel = NULL;    // RMT RX channel
static rmt_encoder_handle_t s_rmt_encoder = NULL;       // RMT encoder

// Function
static size_t rmt_onewire_encoder(rmt_channel_handle_t channel, const void *primary_data,
                                   size_t data_size, size_t symbols_written,
                                   size_t symbols_free, rmt_symbol_word_t *symbols,
                                   bool *done, void *arg);
```

**Purpose**: Hardware-accelerated 1-Wire implementation using ESP32-S3 RMT peripheral. Currently using software bit-bang for better timing control.

### 5.2 State Machine Variables (Reserved for Non-Blocking State Machine)

```c
static poll_state_t s_poll_state = POLL_STATE_IDLE;     // Current polling state
static uint8_t s_current_sensor = 0;                    // Current sensor index
static uint32_t s_conversion_start_time = 0;            // Conversion start timestamp
```

**Purpose**: Full non-blocking state machine implementation for temperature reading. Currently using simplified blocking approach with delays.

### 5.3 ROM Search Variable

```c
uint8_t last_zero = 0;  // In onewire_search_rom()
```

**Purpose**: Tracks last discrepancy position in ROM search algorithm. Currently implemented but not used for continued search.

### 5.4 GPIO Configuration

```c
gpio_config_t orig_config;  // In onewire_diagnose_bus()
```

**Purpose**: Save/restore original GPIO configuration during diagnostics. Currently allocated but not used (ESP-IDF lacks gpio_get_config function).

---

## 6. Git Commit History

### Commit 1: Initial Implementation with Open-Drain Mode
```bash
git commit -m "fix(heating): fix 1-Wire timing and add GPIO diagnostic (based on 8802dad)

Based on original 8802dad commit, fix 1-Wire implementation:

1. Add onewire_diagnose_bus() function for hardware diagnostics:
   - Test floating input (detect external pull-up)
   - Test internal pull-up
   - Test forced low/high output
   - Test released state
   - Provide diagnostic conclusions

2. Fix onewire_reset() timing:
   - Add 5us delay after releasing bus to ensure rise time
   - Enable internal pull-up to assist external 4.7K resistor
   - Add detailed level logging (level1 at 70us, level2 at 100us)
   - Improve error messages with level values

3. Fix onewire_write_bit() timing:
   - Output high before switching to input
   - Add 2us delay for rise time
   - Enable internal pull-up after releasing

4. Fix onewire_read_bit() timing:
   - Output high before switching to input
   - Add 1us delay for rise time
   - Optimize sampling point

Root cause: ESP32-S3 GPIO14 needs extra rise time and internal
pull-up assistance to work reliably with 1-Wire protocol."
```

### Commit 2: Temperature Conversion Fix
```bash
git commit -m "fix(heating): fix temperature conversion and read timing

- Fix max31850_start_conversion(): Release bus after sending Convert T
  command to allow pull-up resistor to power the sensor during conversion
- Increase conversion time from 110ms to 120ms for reliable completion
- Optimize onewire_read_bit(): Add 3us stabilization delay before sampling
  to ensure bus rises properly with pull-up resistor

Root cause: MAX31850 needs bus to be high (powered by pull-up) during
conversion. Previous code didn't explicitly release bus after sending
Convert T command, potentially causing power issues during conversion."
```

### Commit 3: Bit and Byte Level Timing
```bash
git commit -m "fix(heating): add timing delays for stable 1-Wire communication

- Add 10us delay between bytes when reading scratchpad
- Add 2us delay between bits in write_byte and read_byte
- Increase conversion time from 120ms to 150ms

These delays help stabilize 1-Wire communication with MAX31850,
especially when using GPIO14 with open-drain mode and pull-up resistors."
```

---

## 7. Current Status

### Working Features
- ✅ GPIO14 initialization with open-drain mode
- ✅ Hardware diagnostic (5 tests)
- ✅ ROM search (finds 4 devices)
- ✅ Temperature reading (first read successful)

### Known Issues
- ⚠️ Subsequent temperature reads may fail with various errors
- ⚠️ Requires additional timing stabilization

### Hardware Requirements
- 4× MAX31850KATB+ sensors with K-type thermocouples
- 4.7KΩ pull-up resistors on each DQ pin
- GPIO14 connection to all DQ pins (parallel)
- 3.3V power supply to sensors

---

## 8. Reference Documentation

### MAX31850 Datasheet Information
- **Family Code**: 0x3B
- **Conversion Time**: ~100ms (datasheet recommends ≥100ms)
- **Resolution**: 0.0625°C (14-bit)
- **Temperature Range**: -270°C to +1372°C
- **Power Supply**: 3.0V - 3.6V

### 1-Wire Protocol Timing
- **Reset Low**: 480μs
- **Presence Detect**: 15-60μs after release
- **Write 1**: Low 10μs, then release
- **Write 0**: Low 70μs, then release
- **Read**: Low 5μs, release, sample at 15μs

---

## 9. Next Steps

### Recommended Actions
1. **Verify hardware connections**: Check thermocouple connections to T+/T- terminals
2. **Test with single sensor**: Isolate and test each sensor individually
3. **Adjust pull-up resistor**: Try lower value (2.2KΩ) if bus rise time is slow
4. **Add CRC retry**: Implement retry logic for CRC errors
5. **Implement RMT driver**: Use hardware RMT for more precise timing

### Debugging Tips
- Use `max31850_dump_scratchpad()` to view raw 9-byte scratchpad data
- Check fault register (byte 4) for thermocouple connection issues
- Monitor GPIO14 with oscilloscope to verify signal integrity

---

**Author**: Kimi Code CLI  
**Start Time**: 2026-03-27 13:45  
**Update Time**: 2026-03-27 19:07  
**Status**: Phase 2 Complete - Basic functionality working, timing optimization ongoing
