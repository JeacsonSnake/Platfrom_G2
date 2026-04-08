# MAX31850KATB+ Temperature Sensor Driver Implementation v3.0

**Date**: 2026-04-01  
**Branch**: `feature/heating`  
**Commit**: `1c7b278`  
**Status**: Implementation Complete - Ready for Hardware Testing

---

## 1. Summary

Complete rewrite of MAX31850KATB+ temperature sensor driver for ESP32-S3.  
**Key fix**: Strict 1-Wire timing with critical section protection.

---

## 2. Key Design Decisions

### 2.1 Timing Implementation
| Aspect | Implementation |
|--------|---------------|
| Delay method | `__asm__ __volatile__("nop")` loop |
| CPU frequency | 240MHz (1μs = 240 cycles) |
| Loop calibration | ~24 iterations per μs |
| Interrupt control | `portENTER_CRITICAL` during transactions |

### 2.2 1-Wire Timing Parameters
| Operation | Timing | Description |
|-----------|--------|-------------|
| Reset Low | 480μs | Standard reset pulse |
| Presence Wait | 70μs | Wait for device response |
| Reset Recovery | 410μs | Recovery after reset |
| Write 1 Low | 5μs | Short pulse for '1' |
| Write 1 Recovery | 55μs | Total slot 60μs |
| Write 0 Low | 70μs | Long pulse for '0' |
| Write 0 Recovery | 5μs | Recovery after write |
| Read Init Low | 3μs | Initiate read slot |
| Read Sample Delay | 10μs | Wait to 13μs point |
| Read Recovery | 50μs | Complete slot |
| Bit Interval | 2μs | Extra margin between bits |

### 2.3 MAX31850 Specifics
- **Read-Only device**: NO `Convert T (0x44)` command sent
- **Auto-conversion**: ~100ms cycle after power-on
- **Temperature resolution**: 14-bit (0.25°C), right-shift 2 bits
- **Data frame**: 9 bytes with CRC8 (X8+X5+X4+1 = 0x31)

---

## 3. File Structure

```
main/
├── heating_detect.h    # API definitions (150 lines)
└── heating_detect.c    # Implementation (~900 lines)
```

### 3.1 API Functions
```c
// Initialization
esp_err_t max31850_init(gpio_num_t gpio_num);

// Temperature reading
max31850_err_t max31850_get_temperature(uint8_t sensor_id, float *temp);
max31850_err_t max31850_get_data(uint8_t sensor_id, max31850_sensor_t *data);
max31850_err_t max31850_force_update(uint8_t sensor_id, float *temp, TickType_t timeout);

// Status & debugging
bool max31850_is_online(uint8_t sensor_id);
void max31850_dump_scratchpad(uint8_t sensor_id);
const char* max31850_err_to_string(max31850_err_t err);

// Example task
void heating_print_task(void *pvParameters);
```

---

## 4. Software Architecture

```
┌─────────────────────────────────────────┐
│  max31850_init()                        │
│  - GPIO14 open-drain configuration      │
│  - Search ROM (auto-discover 4 devices) │
│  - Create poll task                     │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  max31850_poll_task()                   │
│  - 250ms interval per sensor            │
│  - Match ROM + Read 9 bytes             │
│  - CRC validation                       │
│  - Parse temperature                    │
│  - Mutex-protected data update          │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  Application API                        │
│  - max31850_get_temperature()           │
│  - max31850_force_update()              │
└─────────────────────────────────────────┘
```

---

## 5. Hardware Configuration

| Parameter | Value |
|-----------|-------|
| GPIO | GPIO14 (IO14) |
| Mode | Open-Drain (GPIO_MODE_INPUT_OUTPUT_OD) |
| Pull-up | Internal + External 4.7KΩ per sensor |
| Sensors | 4× MAX31850KATB+ |
| Addressing | Hardware (AD0/AD1): 00, 01, 10, 11 |

---

## 6. Expected Debug Output

### 6.1 Initialization
```
I (1234) MAX31850: Initializing MAX31850 on GPIO14...
I (1245) MAX31850: ROM search complete. Found 4 device(s)
I (1246) MAX31850: Sensor [0]: ROM=0x3B..., HW_ADDR=00
I (1247) MAX31850: Sensor [1]: ROM=0x3B..., HW_ADDR=01
I (1248) MAX31850: Sensor [2]: ROM=0x3B..., HW_ADDR=02
I (1249) MAX31850: Sensor [3]: ROM=0x3B..., HW_ADDR=03
I (1250) MAX31850: Sensor [0] initial read: Temp=25.50°C, CJ=23.25°C
...
I (1260) MAX31850: MAX31850 initialized successfully, found 4 sensor(s)
```

### 6.2 Temperature Report (heating_print_task)
```
I (5000) HEATING: ========== Temperature Report ==========
I (5001) HEATING: Sensor 0 (P1): 25.50°C  [OK]
I (5002) HEATING: Sensor 1 (P2): 26.25°C  [OK]
I (5003) HEATING: Sensor 2 (P3): 24.75°C  [OK]
I (5004) HEATING: Sensor 3 (P4): 25.00°C  [OK]
I (5005) HEATING: =======================================
```

### 6.3 Error Conditions
```
W (6000) MAX31850: Sensor fault: Thermocouple Open Circuit
W (6001) HEATING: Sensor 2 (P3): Thermocouple Open  [ONLINE]

W (7000) MAX31850: CRC error: calc=0x3A, recv=0x10
W (7001) HEATING: Sensor 1 (P2): CRC Error  [ONLINE]
```

---

## 7. Troubleshooting

### 7.1 "No device present during search"
- Check GPIO14 connection to DQ pins
- Verify 4.7KΩ pull-up resistors
- Check sensor power (3.3V)
- Use oscilloscope to verify signal

### 7.2 "CRC Error"
- Signal integrity issue
- Try reducing pull-up resistor (2.2KΩ)
- Check wiring length (<10m)
- Verify timing with oscilloscope

### 7.3 "Thermocouple Open"
- Check thermocouple connection
- Measure thermocouple resistance (<100Ω)
- Check MAX31850 T+/T- soldering

---

## 8. Changes from Previous Implementation

| Aspect | Old (Buggy) | New (Fixed) |
|--------|-------------|-------------|
| Delay method | `esp_rom_delay_us()` | `__asm__("nop")` loop |
| Interrupt control | None | `portENTER_CRITICAL` |
| Timing source | ROM function | Calibrated cycles |
| Read-Only handling | Sent Convert T | No Convert T |
| Temperature parse | Wrong shift | Right-shift 2 bits |
| CRC table | Wrong polynomial | Correct 0x31 |

---

## 9. Git Commit

```bash
commit 1c7b278
Author: Kimi Code CLI
Date:   2026-04-01

    feat(heating): reimplement MAX31850 driver with strict timing
    
    - GPIO14 open-drain mode with critical section protection
    - Precise microsecond delays using nop loop (240MHz)
    - Strict 1-Wire timing: Reset 480μs, Write 5/70μs, Read 3+10μs
    - MAX31850 Read-Only: NO Convert T command
    - 14-bit temperature parsing (right-shift 2 bits)
    - CRC8 polynomial X8+X5+X4+1 = 0x31
    - Search ROM auto-discovery, Match ROM access
    - Polling task with mutex protection
    
    Files: main/heating_detect.c, main/heating_detect.h
```

---

## 10. Next Steps

1. **Hardware test**: Flash and verify on actual hardware
2. **Oscilloscope check**: Verify 1-Waveform timing
3. **Pull-up optimization**: Try 2.2KΩ if CRC errors occur
4. **Single sensor test**: Isolate multi-device issues if needed

---

**Author**: Kimi Code CLI  
**Completion Time**: 2026-04-01 15:35
