# ESP32-S3 Motor Control IoT Project

> This document provides essential information for AI coding agents working on this project.

## Project Overview

This is an ESP32-S3 based motor control system with IoT capabilities, built using ESP-IDF 5.5.2. The project implements a closed-loop motor control system with PID algorithm, featuring WiFi connectivity and MQTT communication for remote monitoring and control.

**Key Features:**
- 4-channel motor control with PWM output (GPIO 1, 4, 6, 8)
- PID closed-loop speed control with anti-windup protection
- Encoder feedback via PCNT (Pulse Counter) on GPIO 2, 5, 7, 9
- WiFi station mode connectivity
- MQTT protocol for remote control and data transmission
- RGB LED status indication (WS2812 @ GPIO48)
- MQTT connection monitoring with SNTP time synchronization
- Connection statistics tracking and error diagnosis
- Soft-start protection for motors

**Target Hardware:** ESP32-S3-DevKitC-1
- Flash: 8MB (detected), 1MB (configured in partitions.csv)
- PSRAM: 2MB
- USB: USB-Serial/JTAG
- MAC Address: `7c:df:a1:e6:d3:cc` (used in MQTT client ID)
- Motor Driver: CHB-BLDC2418 (inverted PWM logic: High=OFF, Low=ON)

## Technology Stack

| Component | Technology |
|-----------|------------|
| Framework | ESP-IDF 5.5.2 |
| Build System | CMake 3.16+ |
| Language | C (C11) |
| Target Chip | ESP32-S3 (Xtensa dual-core, 240MHz) |
| RTOS | FreeRTOS |
| MQTT Broker | EMQX (VMware NAT: 192.168.110.31:1883) |
| NTP Servers | cn.pool.ntp.org, ntp.aliyun.com, ntp.tencent.com |

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
│   ├── monitor.c                  # MQTT connection monitoring with NTP sync
│   ├── monitor.h                  # Monitor module header
│   ├── led_strip_encoder.c        # RMT encoder for WS2812
│   ├── led_strip_encoder.h        # Encoder header
│   └── CMakeLists.txt             # Component build configuration
├── CMakeLists.txt                 # Project build configuration
├── sdkconfig                      # ESP-IDF project configuration
├── partitions.csv                 # Flash partition table
├── .vscode/                       # VS Code configuration
│   ├── settings.json              # VS Code settings (IDF path, clangd)
│   └── c_cpp_properties.json      # C/C++ IntelliSense config
├── .clangd                        # Clangd LSP configuration
├── esp_analysis.py                # ESP32 analysis script (esptool wrapper)
├── analyze.md                     # Hardware analysis report
├── Developer_Notes.md             # Developer notes
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
- NTP time sync trigger after connection
- WiFi power save disabled (`WIFI_PS_NONE`) for stable MQTT

### mqtt.c
- MQTT client configuration and connection
- Message parsing and command dispatch
- Topics:
  - `esp32_1/control` - Command reception
  - `esp32_1/heartbeat` - Heartbeat messages
  - `esp32_1/data` - Sensor/control data output
- Command format: `cmd_<index>_<speed>_<duration>`
- Connection health check and error statistics tasks
- Exponential backoff reconnection strategy
- Keepalive: 60s, Session persistence enabled
- Comprehensive error type tracking and reporting

### pwm.c
- LEDC PWM configuration (Timer 0, 13-bit, 5KHz)
- 4 PWM channels on GPIO 1, 4, 6, 8
- Duty range: 0-8191 (inverted logic for CHB-BLDC2418)
- Auto-notifies MQTT on duty change

### pcnt.c
- PCNT unit initialization for 4 channels
- GPIO: 2, 5, 7, 9 (encoder inputs)
- 200ms sampling interval (converted to per-second rate)
- Count range: -10000 to 10000
- Idle detection for motor stop state
- Startup protection (3-second noise filtering)
- Abnormal value detection and filtering
- Per-motor diagnostic statistics

### pid.c
- PID algorithm implementation with anti-windup
- Default parameters: Kp=8, Ki=0.02, Kd=0.01
- 4 independent PID controllers (one per motor)
- Control period: 10ms when data updated
- Inverted PWM output (8191 - calculated_duty)
- Soft-start protection (limits initial PWM for 2 seconds)
- Command execution task creation

### led.c
- WS2812 RGB LED control via RMT peripheral
- GPIO48 (ESP32-S3-DevKitC-1 onboard RGB LED)
- Status modes:
  - OFF: System not started
  - Fast blink (Yellow): WiFi connecting (100ms)
  - Slow blink (Blue): MQTT connecting (500ms)
  - ON (Green): All connected

### monitor.c / monitor.h
- MQTT connection statistics tracking
- SNTP time synchronization (China timezone UTC+8)
- Disconnect event logging (up to 100 events)
- Connection keepalive rate calculation
- Statistical reports every 8 minutes
- Thread-safe with mutex protection
- Time sync timeout: 30 seconds, 3 retries

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

# Hardware analysis
python esp_analysis.py
```

## Code Style Guidelines

### Naming Conventions
- **Macros/Constants**: `UPPER_CASE_WITH_UNDERSCORES`
  - Example: `WIFI_SSID`, `LED_BLINK_FAST`, `LEDC_TIMER`, `NTP_SERVER_PRIMARY`, `MONITOR_REPORT_INTERVAL_MS`
- **Global variables**: `snake_case`
  - Example: `mqtt_client`, `motor_speed_list`
- **Local variables**: `snake_case`
- **Functions**: `snake_case`
  - Example: `wifi_init()`, `pid_process_init()`, `monitor_record_connect()`
- **Structs**: `PascalCase` (struct) + `snake_case` (typedef)
  - Example: `struct PID_params`, `mqtt_connection_stats_t`
- **Files**: `snake_case.c`

### Comments
- Use **Chinese** comments for functional descriptions
- Use **English** for technical terms (MQTT, PWM, PCNT, PID, NTP, SNTP)
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
- Thread-safe access to shared variables using mutex

## Hardware Pinout

| Function | GPIO | Description |
|----------|------|-------------|
| PWM_CH0 | 1 | Motor 0 PWM output |
| PWM_CH1 | 4 | Motor 1 PWM output |
| PWM_CH2 | 6 | Motor 2 PWM output |
| PWM_CH3 | 8 | Motor 3 PWM output |
| PCNT_CH0 | 2 | Encoder 0 input |
| PCNT_CH1 | 5 | Encoder 1 input |
| PCNT_CH2 | 7 | Encoder 2 input |
| PCNT_CH3 | 9 | Encoder 3 input |
| RGB LED | 48 | WS2812 status LED |

### CHB-BLDC2418 Motor Driver Notes

> **详细配置请参考**: [CHB-BLDC2418-Motor-Configuration.md](hardware_info/CHB-BLDC2418-Motor-Configuration.md) - 包含完整的电机规格、FG信号参数、PWM配置、GPIO引脚定义和PID参数。

- **Inverted PWM logic**: High level = Motor OFF, Low level = Motor ON
- **PWM Frequency**: 5KHz (compromise between noise and ESP32-S3 hardware limits)
- **Recommended**: 15K~25KHz for noise reduction (but ESP32-S3 cannot achieve 20KHz + 13-bit simultaneously)
- **Max PCNT**: 450 counts/sec at 4500 RPM (6 pulses per rotation)
- **Soft-start**: Initial PWM limited to 3000 for 2 seconds to prevent overshoot

#### Motor Specifications Summary

| Parameter | Value | Description |
|-----------|-------|-------------|
| Model | CHB-BLDC2418 | 12V Permanent Magnet Brushless DC Motor |
| Rated Speed | 4500 RPM | Maximum rated rotation speed |
| Max Current | 0.16A | Per motor current consumption |
| FG Signal | 6 pulses/rotation | Tachometer output (450 pulses/sec at max speed) |
| PWM Logic | Inverted | Duty 8191=OFF (stop), 0=ON (full speed) |
| PWM Frequency | 15K~25KHz | Recommended for noise reduction |

## Network Configuration

### WiFi
- SSID: `WeShare-6148`
- Password: `1234567890`
- Mode: Station (STA)
- Power save: Disabled (`WIFI_PS_NONE`)
- Timeout: 60 seconds

### MQTT Broker
- URI: `mqtt://192.168.110.31`
- Port: `1883`
- Client ID: `ESP32S3_7cdfa1e6d3cc`
- Username: `ESP32_1`
- Password: `123456`
- Keepalive: 60 seconds
- Clean session: Disabled (session persistence enabled)
- Reconnect timeout: 3000ms
- Buffer size: 4096 bytes
- Task priority: 5
- Task stack size: 8192 bytes

### NTP Time Sync
- Primary: `cn.pool.ntp.org`
- Backup: `ntp.aliyun.com`
- Fallback: `ntp.tencent.com`
- Timezone: `CST-8` (UTC+8, China Standard Time)
- Initial sync interval: 1 second (fast first sync)
- Normal poll interval: 1 hour
- Timeout: 30 seconds
- Retry: 3 times

## Flash Partition Table

| Name | Type | SubType | Offset | Size | Purpose |
|------|------|---------|--------|------|---------|
| nvs | data | nvs | 0x9000 | 24KB | Non-volatile storage |
| phy_init | data | phy | 0xF000 | 4KB | PHY initialization |
| factory | app | factory | 0x10000 | 1MB | Main application |
| storage | data | - | - | 256KB | Data storage |

## Task Priorities

| Task | Priority | Stack Size | Description |
|------|----------|------------|-------------|
| LED_TASK | 2 | 4096 | Status LED indication |
| MONITOR_TASK | 3 | 4096 | MQTT connection monitoring |
| MQTT_INIT | 2 | 4096 | MQTT initialization (self-deleting) |
| MQTT_HB | 1 | 4096 | MQTT heartbeat sender (30s interval) |
| MQTT_CHK | 1 | 4096 | MQTT health check (10s interval) |
| MQTT_ERR | 1 | 4096 | MQTT error statistics report (5min interval) |
| PID_TASK x4 | 1 | 4096 | PID control loops |
| PCNT_TASK x4 | 1 | 4096 | Encoder monitoring |
| CMD_TASK | 1 | 4096 | Command execution (dynamic) |

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
   - OFF: System startup/idle
   - Yellow fast blink (100ms): WiFi connecting
   - Blue slow blink (500ms): MQTT connecting  
   - Green solid: Fully connected

2. **MQTT Test:**
   ```bash
   # Subscribe to data channel
   mosquitto_sub -h 192.168.110.31 -t "esp32_1/data"
   
   # Send control command
   mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_100_5"
   # Format: cmd_<motor_index>_<speed>_<duration_seconds>
   ```

3. **Monitor Test:**
   - Check serial output for `MQTT连接统计报告`
   - Verify time sync: `时间同步成功！`
   - Check connection events: `[连接事件]` / `[断开事件]`

### Debug Output
Monitor serial output at 115200 baud:
```powershell
idf.py -p COM9 monitor
```

## Security Considerations

⚠️ **Current configuration is for development only:**
- Secure Boot: Disabled
- Flash Encryption: Disabled
- MQTT credentials in plaintext
- No TLS/SSL for MQTT

For production deployment, enable security features via `menuconfig` and use encrypted communication.

## Important Notes

1. **NVS Usage**: `nvs_flash_init()` is called in `wifi_init()` for WiFi configuration storage.

2. **HTTP Removal**: HTTP client feature was removed as per `Developer_Notes.md` - MQTT is the sole communication protocol.

3. **PWM Inversion**: Note that duty value is inverted in PID calculation (`8191 - new_input`) - this matches CHB-BLDC2418 motor driver requirements (High=OFF, Low=ON).

4. **PCNT Function Name**: Do NOT use `pcnt_init()` as function name - it's reserved by ESP-IDF internal functions. Use `pcnt_func_init()` instead.

5. **Command Parameters**: The `cmd_params` struct is passed to `control_cmd()` task. Be careful with stack vs heap allocation when creating dynamic tasks.

6. **Time Sync**: SNTP time sync is started automatically after WiFi connection. Monitor logs for `时间同步成功！` message.

7. **Session Persistence**: MQTT is configured with `disable_clean_session = false` to enable session recovery and reduce reconnection overhead.

8. **Error Statistics**: MQTT error types are tracked and reported every 5 minutes. Check `mqtt_error_report_task()` for details.

9. **GPIO Pin Changes**: The PWM and PCNT GPIO pins have been updated from earlier versions. Current configuration uses GPIO 1,4,6,8 for PWM and GPIO 2,5,7,9 for PCNT.

10. **Soft-start Protection**: PID controller implements soft-start to prevent motor overshoot. Initial PWM is limited to 3000 for the first 2 seconds after motor start.

11. **PCNT Sampling**: PCNT samples every 200ms but converts to per-second rate for PID comparison and MQTT reporting. This provides faster response while maintaining consistent units.

## Common Issues

| Issue | Solution |
|-------|----------|
| WiFi connection timeout | Check SSID/password; ensure 2.4GHz network; verify signal strength |
| MQTT connection fail | Verify broker IP and port; check network connectivity; check firewall |
| LED not working | Verify GPIO48 connection; check RMT configuration |
| Time sync fail | Verify NTP server access; check internet connectivity; use domestic NTP servers |
| Build errors | Run `idf.py fullclean` then rebuild |
| Stack overflow | Increase task stack size (e.g., from 2048 to 4096) |
| MQTT frequent disconnect | Check WiFi signal strength; review keepalive settings; check broker load |
| Motor not responding | Verify PWM wiring; check inverted logic (duty 8191=OFF, 0=ON) |
| PID oscillation | Adjust Kp/Ki/Kd parameters; check PCNT signal quality |
| Motor overshoot | Soft-start should handle this; verify startup_phase logic |

## Reference Documentation

### Project Documents
- [CHB-BLDC2418-Motor-Configuration.md](hardware_info/CHB-BLDC2418-Motor-Configuration.md) - CHB-BLDC2418电机完整配置文档（规格参数、GPIO定义、PID设置）

### External Documentation
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/index.html)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ESP-IDF MQTT Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html)
- [ESP-IDF SNTP Time Sync](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html#sntp-time-synchronization)
