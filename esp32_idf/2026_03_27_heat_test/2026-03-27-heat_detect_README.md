# MAX31850KATB+ Temperature Sensor Driver Implementation

**Date**: 2026-03-27  
**Branch**: `feature/heating`  
**Task Description**: Implement 4-channel MAX31850KATB+ temperature sensor driver using 1-Wire protocol on ESP32-S3 (GPIO14), with non-blocking polling, CRC validation, and fault detection

---

## 1. Background

### Hardware Requirements
The ESP32-S3 motor control system requires temperature monitoring capability with the following specifications:
- **Sensors**: 4× MAX31850KATB+ (K-type thermocouple digitizer with cold-junction compensation)
- **Bus Protocol**: 1-Wire, 4 sensors paralleled on single bus
- **GPIO**: IO14 (GPIO_NUM_14)
- **Pull-ups**: 4.7KΩ per sensor (independent pull-up to 3.3V)
- **Addressing**: Hardware-configured via AD0/AD1 pins (00, 01, 10, 11)
- **Power**: 3.3V external supply (non-parasitic mode)

### Technical Challenges
1. **1-Wire Timing Precision**: Requires microsecond-level timing accuracy for reliable communication
2. **Multi-device Management**: Must use Match ROM (0x55) + 64-bit ROM ID, cannot use Skip ROM (0xCC)
3. **Non-blocking Operation**: 100ms conversion time must not block other tasks
4. **Fault Detection**: Distinguish between thermocouple open, short-to-GND, short-to-VCC, and CRC errors
5. **Bus Fault Recovery**: Handle bus short/open conditions gracefully

---

## 2. Design Analysis

### 2.1 Requirements

| Requirement | Description |
|-------------|-------------|
| Protocol | 1-Wire with precise timing (GPIO bit-bang implementation) |
| Device Discovery | Auto-enumerate 4 sensors at initialization via Search ROM |
| Access Method | Match ROM (0x55) + 64-bit ROM ID for individual sensor access |
| Polling Rate | 1-second interval for all 4 sensors (non-blocking state machine) |
| Conversion Time | ≥100ms wait between Convert T and Read Scratchpad |
| CRC Validation | X8+X5+X4+1 polynomial verification on scratchpad data |
| Fault Detection | Open circuit, Short to GND, Short to VCC detection |
| Error Recovery | Mark offline after 3 consecutive failures, periodic retry |

### 2.2 State Machine Design

```
POLL_STATE_IDLE ──→ POLL_STATE_CONVERT ──→ POLL_STATE_WAIT_CONVERSION
       ↑                                              │
       └──────────────────────────────────────────────┘
       │
       └──→ POLL_STATE_READ ──→ POLL_STATE_PARSE ──→ POLL_STATE_NEXT_SENSOR
```

**State Descriptions**:
- `CONVERT`: Send Convert T (0x44) command to selected sensor
- `WAIT_CONVERSION`: Delay 110ms (non-blocking via vTaskDelay)
- `READ`: Send Read Scratchpad (0xBE), receive 9 bytes
- `PARSE`: Verify CRC, extract temperature, check fault register
- `NEXT_SENSOR`: Move to next sensor or restart cycle

### 2.3 Data Structures

```c
// Error codes
typedef enum {
    MAX31850_OK = 0,            // Success
    MAX31850_ERR_OPEN,          // Thermocouple open circuit
    MAX31850_ERR_SHORT_GND,     // Short to GND
    MAX31850_ERR_SHORT_VCC,     // Short to VCC
    MAX31850_ERR_CRC,           // CRC validation failed
    MAX31850_ERR_TIMEOUT,       // Communication timeout
    MAX31850_ERR_OFFLINE,       // Sensor marked offline
    MAX31850_ERR_INVALID_ID,    // Invalid sensor ID
    MAX31850_ERR_BUS_FAULT,     // Bus electrical fault
} max31850_err_t;

// Sensor status structure
typedef struct {
    uint8_t rom_id[8];          // 64-bit ROM ID (family + serial + CRC)
    float temperature;           // Last read temperature (°C)
    int16_t raw_temp;            // Raw 16-bit temperature value
    uint8_t fault_reg;           // Fault register (byte 4 of scratchpad)
    max31850_err_t last_error;   // Last error code
    uint8_t fail_count;          // Consecutive failure counter
    bool online;                 // Online status
    bool data_valid;             // Data validity flag
    uint32_t last_read_time;     // Last successful read timestamp
} max31850_sensor_t;
```

---

## 3. Implementation

### 3.1 New Files

#### `main/heating_detect.h`
- Driver header file with complete API definitions
- Error code enumeration (`max31850_err_t`)
- Data structures (`max31850_sensor_t`)
- 1-Wire command constants (`ONEWIRE_CMD_*`, `MAX31850_CMD_*`)
- Public function declarations (init, get_temp, force_update, etc.)
- Fault register bit definitions (`MAX31850_FAULT_*`)

#### `main/heating_detect.c`
- Complete driver implementation (~1000 lines)
- **CRC8 Lookup Table**: X8+X5+X4+1 polynomial for scratchpad validation
- **1-Wire Low-level Functions**:
  - `onewire_reset()`: Reset pulse + Presence detection
  - `onewire_write_bit()`: Single bit write with precise timing
  - `onewire_read_bit()`: Single bit read with sampling at 15μs
  - `onewire_write_byte()`: Byte write (LSB first)
  - `onewire_read_byte()`: Byte read (LSB first)
- **ROM Operations**:
  - `onewire_search_rom()`: Binary search tree algorithm for device discovery
  - `onewire_match_rom()`: Select specific device by ROM ID
- **MAX31850 Specific**:
  - `max31850_start_conversion()`: Send Convert T command
  - `max31850_read_scratchpad()`: Read 9-byte scratchpad
  - `max31850_parse_scratchpad()`: Parse temperature and fault bits
- **Polling Task**: `max31850_poll_task()` with state machine implementation
- **Thread Safety**: Mutex protection for 1-Wire bus access

#### `2026_03_27_heat_test/Wiring_Verification_Guide.md`
- Comprehensive wiring verification guide
- Hardware connection diagrams
- Step-by-step verification procedures with multimeter
- Oscilloscope/logic analyzer waveform references
- Troubleshooting guide for common issues
- FAQ section

### 3.2 Modified Files

#### `main/CMakeLists.txt`
```cmake
idf_component_register(SRCS "heating_detect.c" "monitor.c" ...)
```

#### `main/main.c`
```c
#include "heating_detect.h"

void app_main(void) {
    // ... existing initializations ...
    
    // Initialize MAX31850 temperature sensors (GPIO14)
    ESP_LOGI("MAIN", "Initializing MAX31850 temperature sensors...");
    esp_err_t temp_err = max31850_init(MAX31850_ONEWIRE_GPIO);
    if (temp_err != ESP_OK) {
        ESP_LOGW("MAIN", "MAX31850 init returned %d, will retry", temp_err);
    }
    
    // Create temperature print task (every 2 seconds)
    xTaskCreate(heating_print_task, "HEATING_PRINT", 4096, NULL, 1, NULL);
    
    // ...
}

// Temperature print task - prints 4 channels every 2 seconds
void heating_print_task(void *pvParameters) {
    float temp;
    max31850_err_t err;
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("HEATING", "Temperature print task started");
    
    while (1) {
        ESP_LOGI("HEATING", "========== Temperature Report ==========");
        for (uint8_t i = 0; i < MAX31850_SENSOR_COUNT; i++) {
            err = max31850_get_temperature(i, &temp);
            if (err == MAX31850_OK) {
                ESP_LOGI("HEATING", "Sensor %d (P%d): %.2f °C  [OK]", i, i+1, temp);
            } else {
                ESP_LOGW("HEATING", "Sensor %d (P%d): %s  [%s]",
                    i, i+1, max31850_err_to_string(err),
                    max31850_is_online(i) ? "ONLINE" : "OFFLINE");
            }
        }
        ESP_LOGI("HEATING", "=======================================");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

#### `main/main.h`
```c
//////////////////////////////////////////////////////////////
//////////////////////// HEATING /////////////////////////////
//////////////////////////////////////////////////////////////
#include "heating_detect.h"

// Heating Print Task
void heating_print_task(void *pvParameters);
```

---

## 4. Key Technical Implementations

### 4.1 1-Wire Timing (GPIO Bit-bang)

```c
// Reset Pulse: 480μs low, then release
static esp_err_t onewire_reset(bool *presence) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    esp_rom_delay_us(480);           // 480μs reset pulse
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(70);            // Wait for presence
    *presence = (gpio_get_level(pin) == 0);
    esp_rom_delay_us(410);           // Wait for reset to complete
}

// Write 1: Low for 10μs
// Write 0: Low for 70μs
static esp_err_t onewire_write_bit(uint8_t bit) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    if (bit) {
        esp_rom_delay_us(10);        // Write 1
        gpio_set_level(pin, 1);
        esp_rom_delay_us(60);
    } else {
        esp_rom_delay_us(70);        // Write 0
        gpio_set_level(pin, 1);
        esp_rom_delay_us(5);
    }
}

// Read: Low for 5μs, release, sample at 15μs
static esp_err_t onewire_read_bit(uint8_t *bit) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    esp_rom_delay_us(5);             // Init low
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(10);            // Wait to sampling point
    *bit = gpio_get_level(pin);
    esp_rom_delay_us(55);            // Wait for slot to end
}
```

### 4.2 CRC8 Validation (X8+X5+X4+1)

```c
static const uint8_t crc8_table[256] = {
    0x00, 0x5E, 0xBC, 0xE2, ...  // 256-byte lookup table
};

static uint8_t crc8_calculate(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

// Verify scratchpad CRC (byte 8 should match calculated CRC of bytes 0-7)
if (crc8_calculate(scratchpad, 8) != scratchpad[8]) {
    return MAX31850_ERR_CRC;
}
```

### 4.3 Temperature Parsing

```c
// MAX31850 temperature format (16-bit, big-endian, 0.0625°C resolution)
int16_t raw_temp = ((int16_t)scratchpad[0] << 8) | scratchpad[1];
float temperature = (float)raw_temp * 0.0625f;

// Check fault register (byte 4)
uint8_t fault_reg = scratchpad[4];
if (fault_reg & MAX31850_FAULT_OPEN) {
    return MAX31850_ERR_OPEN;           // Thermocouple open circuit
}
if (fault_reg & MAX31850_FAULT_SHORT_GND) {
    return MAX31850_ERR_SHORT_GND;      // Short to GND
}
if (fault_reg & MAX31850_FAULT_SHORT_VCC) {
    return MAX31850_ERR_SHORT_VCC;      // Short to VCC
}
```

### 4.4 ROM Search Algorithm

```c
static esp_err_t onewire_search_rom(void) {
    uint8_t rom_id[8];
    uint8_t last_discrepancy = 0;
    int device_count = 0;
    
    while (device_count < MAX31850_SENSOR_COUNT) {
        onewire_reset(&presence);
        onewire_write_byte(ONEWIRE_CMD_SEARCH_ROM);
        
        for (uint8_t bit_pos = 0; bit_pos < 64; bit_pos++) {
            // Read two bits: actual value and complement
            onewire_read_bit(&bit_actual);
            onewire_read_bit(&bit_complement);
            
            if (bit_actual == 0 && bit_complement == 0) {
                // Discrepancy - multiple devices differ at this bit
                // Use binary search tree algorithm to traverse all devices
                selected_bit = (bit_pos == last_discrepancy) ? 1 : 0;
            }
            // ... handle other cases ...
            
            onewire_write_bit(selected_bit);
        }
        
        // Verify ROM CRC (byte 7 is CRC of bytes 0-6)
        if (crc8_calculate(rom_id, 7) == rom_id[7]) {
            memcpy(sensors[device_count].rom_id, rom_id, 8);
            device_count++;
        }
    }
}
```

---

## 5. Git Commit Record

### First Commit: MAX31850 Driver Implementation

```bash
# Create feature branch
git checkout -b feature/heating

# Add new driver files
git add main/heating_detect.c main/heating_detect.h

# Add documentation
git add 2026_03_27_heat_test/Wiring_Verification_Guide.md

# Modify existing files
git add main/main.c main/main.h main/CMakeLists.txt

# Commit
git commit -m "feat(heating): implement MAX31850 temperature sensor driver

- Add heating_detect.c/h: Complete 1-Wire driver for MAX31850KATB+
- Implement GPIO bit-bang 1-Wire protocol with precise timing:
  - Reset/Presence detection (480μs/120μs)
  - Write bit: 10μs (1) / 70μs (0)
  - Read bit: sample at 15μs
- Implement ROM operations:
  - Search ROM algorithm for auto-enumeration
  - Match ROM for individual device access
- MAX31850 specific features:
  - Convert T command with 110ms non-blocking wait
  - Read Scratchpad (9 bytes) with CRC validation
  - Temperature parsing: 0.0625°C resolution
  - Fault detection: Open, Short-GND, Short-VCC
- Polling task with state machine:
  - 1-second interval for all 4 sensors
  - Non-blocking: CONVERT → WAIT → READ → PARSE
  - Thread-safe: mutex protection for bus access
- Error handling:
  - CRC validation (X8+X5+X4+1)
  - Offline detection after 3 consecutive failures
  - Periodic retry for offline sensors
- API functions:
  - max31850_init(): Initialize GPIO and search devices
  - max31850_get_temperature(): Non-blocking read
  - max31850_force_update(): Blocking immediate read
  - max31850_dump_scratchpad(): Debug scratchpad dump
  - max31850_err_to_string(): Error code to string
- Integration:
  - Update CMakeLists.txt to include heating_detect.c
  - Add heating_print_task in main.c for 2-second status output
  - Add heating_detect.h include in main.h
- Documentation:
  - Wiring_Verification_Guide.md with connection diagrams
  - Troubleshooting guide for common issues

Hardware: ESP32-S3, GPIO14, 4×MAX31850KATB+, 4.7KΩ pull-ups"
```

**Commit Info**:
- New files: 3 (`heating_detect.c`, `heating_detect.h`, `Wiring_Verification_Guide.md`)
- Modified files: 3 (`main.c`, `main.h`, `CMakeLists.txt`)
- Total lines: ~1200 lines

---

## 6. Usage Instructions

### 6.1 Build and Flash

```powershell
# Set up ESP-IDF environment
. $env:IDF_PATH/export.ps1

# Clean and build
idf.py fullclean
idf.py build

# Flash (enter download mode: hold BOOT, press RESET, release BOOT)
idf.py -p COM9 flash

# Monitor serial output
idf.py -p COM9 monitor
```

### 6.2 Expected Output

#### Initialization Phase
```
I (1234) MAIN: Initializing MAX31850 temperature sensors...
I (1245) MAX31850: Initializing MAX31850 on GPIO14...
I (1256) MAX31850: Starting 1-Wire ROM search...
I (1267) MAX31850: Found device 1: ROM ID 3B1234567890ABCD
I (1278) MAX31850: Found device 2: ROM ID 3B1234567890ABCE
I (1289) MAX31850: Found device 3: ROM ID 3B1234567890ABCF
I (1300) MAX31850: Found device 4: ROM ID 3B1234567890ABD0
I (1311) MAX31850: ROM search complete. Found 4 device(s)
I (1322) MAX31850: ===============================================
I (1333) MAX31850: MAX31850 ROM ID List (4 device(s) found):
I (1344) MAX31850:   Sensor 0 (P1): 3B-12-34-56-78-90-AB-CD  ONLINE
I (1355) MAX31850:     Family: 0x3B (MAX31850/MAX31851)
I (1366) MAX31850:   Sensor 1 (P2): 3B-12-34-56-78-90-AB-CE  ONLINE
I (1377) MAX31850:     Family: 0x3B (MAX31850/MAX31851)
I (1388) MAX31850:   Sensor 2 (P3): 3B-12-34-56-78-90-AB-CF  ONLINE
I (1399) MAX31850:   Sensor 3 (P4): 3B-12-34-56-78-90-AB-D0  ONLINE
I (1410) MAX31850: ===============================================
I (1421) MAX31850: MAX31850 initialized successfully
I (1432) MAX31850: Poll task started
I (1443) HEATING: Temperature print task started
```

#### Normal Operation (every 2 seconds)
```
I (5000) HEATING: ========== Temperature Report ==========
I (5001) HEATING: Sensor 0 (P1): 25.50 °C  [OK]
I (5002) HEATING: Sensor 1 (P2): 26.25 °C  [OK]
I (5003) HEATING: Sensor 2 (P3): 24.75 °C  [OK]
I (5004) HEATING: Sensor 3 (P4): 25.00 °C  [OK]
I (5005) HEATING: =======================================
```

#### Error Conditions
```
W (12345) MAX31850: Sensor 2: Thermocouple open circuit (断线)
W (12346) HEATING: Sensor 2 (P3): Thermocouple OPEN (断线)  [ONLINE]

W (23456) MAX31850: Sensor 1: CRC error or data corruption
W (23457) HEATING: Sensor 1 (P2): CRC Error  [ONLINE]

E (34567) MAX31850: Sensor 3: Marked OFFLINE after 3 consecutive failures
W (34568) HEATING: Sensor 3 (P4): Sensor Offline  [OFFLINE]
```

### 6.3 API Usage Examples

```c
// Initialize sensors
esp_err_t err = max31850_init(GPIO_NUM_14);
if (err != ESP_OK) {
    ESP_LOGE("APP", "Initialization failed");
}

// Get temperature (non-blocking, returns cached value)
float temp;
max31850_err_t result = max31850_get_temperature(0, &temp);
if (result == MAX31850_OK) {
    ESP_LOGI("APP", "Temperature: %.2f °C", temp);
} else {
    ESP_LOGW("APP", "Error: %s", max31850_err_to_string(result));
}

// Force immediate update (blocking, ~120ms)
result = max31850_force_update(0, &temp, pdMS_TO_TICKS(200));

// Check online status
if (max31850_is_online(0)) {
    ESP_LOGI("APP", "Sensor 0 is online");
}

// Debug: dump scratchpad
max31850_dump_scratchpad(0);
```

---

## 7. Wiring Verification

### 7.1 Pinout Summary

| Sensor | PCB Ref | Address | AD0 | AD1 | DQ Pull-up |
|--------|---------|---------|-----|-----|------------|
| P1 | U1 | 00 | GND | GND | 4.7KΩ to 3.3V |
| P2 | U2 | 01 | 3.3V | GND | 4.7KΩ to 3.3V |
| P3 | U3 | 10 | GND | 3.3V | 4.7KΩ to 3.3V |
| P4 | U4 | 11 | 3.3V | 3.3V | 4.7KΩ to 3.3V |

**Common Connections**:
- VDD → 3.3V (all sensors)
- GND → GND (all sensors)
- DQ → GPIO14 (via individual 4.7KΩ pull-ups)

### 7.2 Quick Verification

```powershell
# 1. Check GPIO14 idles high (~3.3V)
# 2. Check each sensor has 4.7KΩ between DQ and 3.3V
# 3. Check address pins with multimeter:
#    - P1: AD0=0V, AD1=0V
#    - P2: AD0=3.3V, AD1=0V
#    - P3: AD0=0V, AD1=3.3V
#    - P4: AD0=3.3V, AD1=3.3V
```

**See full guide**: `2026_03_27_heat_test/Wiring_Verification_Guide.md`

---

## 8. Troubleshooting

### 8.1 "No device present during search"

**Causes**:
- GPIO14 not connected to DQ pins
- Missing pull-up resistors
- Sensors not powered

**Check**:
1. Measure GPIO14 voltage (idle: 3.3V)
2. Check DQ-3.3V resistance (~4.7KΩ)
3. Verify VDD=3.3V on all sensors

### 8.2 "ROM ID CRC error"

**Causes**:
- Signal integrity issues
- Bus capacitance too high
- Pull-up resistor value inappropriate

**Solutions**:
1. Shorten wire length (<10m)
2. Adjust pull-up: short(<1m)=4.7K, medium(1-5m)=2.2K, long(5-10m)=1K
3. Add 100nF decoupling capacitors near sensors

### 8.3 "Thermocouple OPEN"

**Causes**:
- Thermocouple not connected
- Thermocouple wire broken
- MAX31850 T+/T- pins not soldered

**Check**:
1. Verify thermocouple plugged in
2. Measure thermocouple resistance (<100Ω)
3. Use `max31850_dump_scratchpad()` to verify fault register

### 8.4 Partial Device Discovery

**Causes**:
- Duplicate addresses (AD0/AD1 misconfiguration)
- Faulty sensor pulling down bus

**Check**:
1. Verify unique AD0/AD1 combinations for each sensor
2. Disconnect sensors one-by-one to isolate faulty device

---

## 9. Resource Usage

| Resource | Usage | Notes |
|----------|-------|-------|
| GPIO | 1 (GPIO14) | 1-Wire bus |
| RMT Channels | 0 | GPIO bit-bang used for precise timing |
| Tasks | 1 | `max31850_poll_task`, 4096 stack, priority 2 |
| Mutex | 1 | Bus access protection |
| RAM | ~500 bytes | Sensor structures + buffers |
| CPU | <1% | During idle; brief spikes during 1-Wire transactions |

---

## 10. References

- [MAX31850KATB+ Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31850-MAX31851.pdf)
- [1-Wire Protocol Specification](https://www.analog.com/en/technical-articles/1wire-communication-through-software.html)
- [ESP-IDF GPIO Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html)
- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html)
- Wiring Verification: `2026_03_27_heat_test/Wiring_Verification_Guide.md`

---

**Author**: Kimi Code CLI  
**Start Time**: 2026-03-27 13:45  
**Completion Time**: 2026-03-27 14:40
