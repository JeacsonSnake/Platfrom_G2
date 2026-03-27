# ESP32-S3 Motor Control IoT Project

> This document provides essential information for AI coding agents working on this project.

## Project Overview

This is an ESP32-S3 based motor control system with IoT capabilities, built using ESP-IDF 5.5.2. The project implements a closed-loop motor control system with PID algorithm, featuring WiFi connectivity and MQTT communication for remote monitoring and control.

**Key Features:**
- 4-channel motor control with PWM output
- PID closed-loop speed control
- Encoder feedback via PCNT (Pulse Counter)
- WiFi station mode connectivity
- MQTT protocol for remote control and data transmission
- RGB LED status indication (WS2812)

**Target Hardware:** ESP32-S3-DevKitC-1
- Flash: 8MB (detected), 2MB (configured)
- PSRAM: 2MB
- USB: USB-Serial/JTAG

## Technology Stack

| Component | Technology |
|-----------|------------|
| Framework | ESP-IDF 5.5.2 |
| Build System | CMake |
| Language | C (C11) |
| Target Chip | ESP32-S3 (Xtensa dual-core, 240MHz) |
| RTOS | FreeRTOS |
| MQTT Broker | EMQX (VMware NAT: 192.168.110.31:1883) |

## Project Structure

```
esp32_idf/
├── main/                          # Application source code
│   ├── main.c                     # Application entry point
│   ├── main.h                     # Global definitions and declarations
│   ├── wifi.c                     # WiFi connection management
│   ├── mqtt.c                     # MQTT client implementation
│   ├── pwm.c                      # PWM motor control (LEDC)
│   ├── pcnt.c                     # Pulse counter (encoder reading)
│   ├── pid.c                      # PID control algorithm
│   ├── led.c                      # RGB status LED (WS2812)
│   ├── led_strip_encoder.c        # RMT encoder for WS2812
│   ├── led_strip_encoder.h        # Encoder header
│   └── CMakeLists.txt             # Component build configuration
├── CMakeLists.txt                 # Project build configuration
├── sdkconfig                      # ESP-IDF project configuration
├── partitions.csv                 # Flash partition table
├── .vscode/                       # VS Code configuration
│   ├── settings.json
│   └── c_cpp_properties.json
├── .clangd                        # Clangd LSP configuration
├── esp_analysis.py                # ESP32 analysis script (esptool wrapper)
└── build/                         # Build output directory
```

## Module Descriptions

### main.c
- Application entry point (`app_main`)
- Task creation and initialization order
- Main loop with delay

### wifi.c
- WiFi station mode initialization
- Event-driven connection handling
- SSID: "WeShare-6148", Password: "1234567890"
- 60-second connection timeout with retry

### mqtt.c
- MQTT client configuration and connection
- Message parsing and command dispatch
- Topics:
  - `esp32_1/control` - Command reception
  - `esp32_1/heartbeat` - Heartbeat messages
  - `esp32_1/data` - Sensor/control data output
- Command format: `cmd_<index>_<speed>_<duration>`

### pwm.c
- LEDC PWM configuration (Timer 0, 13-bit, 5KHz)
- 6 PWM channels (GPIO 5-10)
- Duty range: 0-8192

### pcnt.c
- PCNT unit initialization for 4 channels
- GPIO: 11, 12, 13, 14
- 1-second sampling interval
- Count range: -10000 to 10000

### pid.c
- PID algorithm implementation with anti-windup
- Default parameters: Kp=8, Ki=0.02, Kd=0.01
- 4 independent PID controllers (one per motor)
- Control period: 10ms when data updated

### led.c
- WS2812 RGB LED control via RMT peripheral
- GPIO48 (ESP32-S3-DevKitC-1 onboard RGB LED)
- Status modes:
  - OFF: System not started
  - Fast blink (Yellow): WiFi connecting (100ms)
  - Slow blink (Blue): MQTT connecting (500ms)
  - ON (Green): All connected

## Build Commands

### Prerequisites
- ESP-IDF 5.5.2 installed and configured
- Python 3.x with esptool
- CMake 3.16+

### Build
```powershell
# Set up ESP-IDF environment
. $env:IDF_PATH/export.ps1

# Configure project (menuconfig)
idf.py menuconfig

# Build the project
idf.py build

# Flash to device (adjust COM port as needed)
idf.py -p COM9 flash

# Monitor serial output
idf.py -p COM9 monitor

# Build, flash and monitor
idf.py -p COM9 flash monitor

# Full clean
idf.py fullclean
```

### Alternative Flash Commands
```powershell
# Using esptool directly
python -m esptool --port COM9 --chip esp32s3 write_flash 0x0 build/test.bin

# Read flash
python -m esptool --port COM9 --chip esp32s3 read_flash 0x0 0x800000 firmware_dump.bin
```

## Code Style Guidelines

### Naming Conventions
- **Macros/Constants**: `UPPER_CASE_WITH_UNDERSCORES`
  - Example: `WIFI_SSID`, `LED_BLINK_FAST`, `LEDC_TIMER`
- **Global variables**: `snake_case`
  - Example: `mqtt_client`, `motor_speed_list`
- **Local variables**: `snake_case`
- **Functions**: `snake_case`
  - Example: `wifi_init()`, `pid_process_init()`
- **Structs**: `PascalCase`
  - Example: `PID_params`, `PID_data`
- **Files**: `snake_case.c`

### Comments
- Use Chinese comments for functional descriptions
- Use English for technical terms (MQTT, PWM, PCNT, PID)
- Section headers use block comment style:
```c
//////////////////////////////////////////////////////////////
//////////////////////// WIFI ////////////////////////////////
//////////////////////////////////////////////////////////////
```

### Code Organization
- Header includes at top of file
- Macro definitions grouped by module
- Function declarations in header, definitions in source
- Global variables declared extern in header, defined in main.c

## Hardware Pinout

| Function | GPIO | Description |
|----------|------|-------------|
| PWM_CH0 | 5 | Motor 0 PWM output |
| PWM_CH1 | 6 | Motor 1 PWM output |
| PWM_CH2 | 7 | Motor 2 PWM output |
| PWM_CH3 | 8 | Motor 3 PWM output |
| PWM_CH4 | 9 | Spare PWM output |
| PWM_CH5 | 10 | Spare PWM output |
| PCNT_CH0 | 11 | Encoder 0 input |
| PCNT_CH1 | 12 | Encoder 1 input |
| PCNT_CH2 | 13 | Encoder 2 input |
| PCNT_CH3 | 14 | Encoder 3 input |
| RGB LED | 48 | WS2812 status LED |

## Network Configuration

### WiFi
- SSID: `WeShare-6148`
- Password: `1234567890`
- Mode: Station (STA)
- Power save: Disabled (`WIFI_PS_NONE`)

### MQTT Broker
- URI: `mqtt://192.168.110.31`
- Port: `1883`
- Client ID: `ESP32S3_7cdfa1e6d3cc`
- Username: `ESP32_1`
- Password: `123456`
- Keepalive: 60 seconds

## Flash Partition Table

| Name | Type | SubType | Offset | Size | Purpose |
|------|------|---------|--------|------|---------|
| nvs | data | nvs | 0x9000 | 24KB | Non-volatile storage |
| phy_init | data | phy | 0xF000 | 4KB | PHY initialization |
| factory | app | factory | 0x10000 | 1MB | Main application |
| storage | data | - | - | 256KB | Data storage |

## Task Priorities

| Task | Priority | Description |
|------|----------|-------------|
| LED_TASK | 5 | Status LED indication (highest) |
| MQTT_TASK | 1 | MQTT communication |
| PID_TASK x4 | 1 | PID control loops |
| PCNT_TASK x4 | 1 | Encoder monitoring |
| CMD_TASK | 1 | Command execution (dynamic) |

## Testing Instructions

### Flash the Device
1. Enter download mode:
   - Hold BOOT button
   - Press and release RESET button
   - Release BOOT button
   - Port may change (e.g., COM8 -> COM9)

2. Flash firmware:
   ```powershell
   idf.py -p COM9 flash
   ```

### Verify Operation
1. **LED Status Check:**
   - Yellow fast blink: WiFi connecting
   - Blue slow blink: MQTT connecting  
   - Green solid: Fully connected

2. **MQTT Test:**
   ```bash
   # Subscribe to data channel
   mosquitto_sub -h 192.168.110.31 -t "esp32_1/data"
   
   # Send control command
   mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_100_5"
   # Format: cmd_<motor_index>_<speed>_<duration_seconds>
   ```

### Debug Output
Monitor serial output at 115200 baud:
```powershell
idf.py -p COM9 monitor
```

## Important Notes

1. **NVS Usage**: Despite developer notes mentioning NVS removal, `nvs_flash_init()` is still called in `wifi_init()` for WiFi configuration storage.

2. **HTTP Removal**: HTTP client feature was removed as per `Developer_Notes.md` - MQTT is the sole communication protocol.

3. **PWM Inversion**: Note that duty value is inverted in PID calculation (`8192 - new_input`) - verify if this matches your motor driver requirements.

4. **PCNT Function Name**: Do NOT use `pcnt_init()` as function name - it's reserved by ESP-IDF internal functions. Use `pcnt_func_init()` instead.

5. **Command Parameters**: The `cmd_params` struct is passed to `control_cmd()` task. Be careful with stack vs heap allocation when creating dynamic tasks.

## Common Issues

| Issue | Solution |
|-------|----------|
| WiFi connection timeout | Check SSID/password; ensure 2.4GHz network |
| MQTT connection fail | Verify broker IP and port; check network connectivity |
| LED not working | Verify GPIO48 connection; check RMT configuration |
| Build errors | Run `idf.py fullclean` then rebuild |

## Development History

See archived documentation in `2026_02_10_wifi_test/` directory for detailed troubleshooting records, including:
- WiFi connection fixes
- MQTT stability improvements
- Stack overflow fixes
- LED GPIO48 implementation

## Security Considerations

⚠️ **Current configuration is for development only:**
- Secure Boot: Disabled
- Flash Encryption: Disabled
- MQTT credentials in plaintext
- No TLS/SSL for MQTT

For production deployment, enable security features via `menuconfig` and use encrypted communication.

## Reference Documentation

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/index.html)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
