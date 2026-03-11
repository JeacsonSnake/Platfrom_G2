# CHB-BLDC2418 Motor Configuration Document

**Date**: 2026-03-11  
**Branch**: `feature/motor-control-config`  
**Motor Model**: CHB-BLDC2418 Permanent Magnet Brushless DC Motor  
**Motor Spec**: 12V, 4500 RPM  

---

## 1. Motor Specifications

### 1.1 Basic Parameters

| Parameter | Value | Unit |
|-----------|-------|------|
| Model | CHB-BLDC2418 | - |
| Voltage | 12 | V |
| Rated Speed | 4500 | RPM |
| Max Current | 0.16 | A |
| Power Supply Range | 6~12 | V |

### 1.2 FG (Tachometer) Signal Specifications

| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| VOH (Output High) | 5.0 MAX | V | High level output voltage |
| VOL (Output Low) | 0.5 MAX | V | Low level output voltage |
| FG Signal Current | 2 | mA | Output current |
| Pulse Output | 6 | Pulse/cycle | Pulses per rotation |
| Signal Type | High Pulse | - | High signal pulse outputs |
| Direction | Motor -> ESP32 | - | FG is OUTPUT for motor |

**Note**: 6 pulses per rotation means 4500 RPM = 4500 × 6 = 27000 pulses/min = 450 pulses/sec

### 1.3 PWM Speed Control Specifications

| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| Input Voltage Range | 0.0~5.0 | V | PWM input voltage |
| PWM Frequency | 15K~25K | Hz | Recommended 20KHz |
| VIH (Input High) | 2.0 MIN | V | High level threshold (or open) |
| VIL (Input Low) | 0.5 MAX | V | Low level threshold |
| Logic | Inverted | - | High = Motor OFF, Low = Motor ON |
| Direction | ESP32 -> Motor | - | PWM is INPUT for motor |

### 1.4 CW/CCW Control Specifications (Reserved for Future)

| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| Input Voltage Range | 0.0~5.0 | V | Control voltage |
| VIH (Input High) | 2.0 MIN | V | High level threshold |
| VIL (Input Low) | 0.5 MAX | V | Low level threshold |
| Logic | High = CCW, Low = CW | - | Rotation direction control |
| Status | Reserved | - | Not connected currently |

---

## 2. GPIO Pinout Configuration

### 2.1 ESP32-S3 GPIO Assignment

| Motor Channel | PWM GPIO (Output) | FG GPIO (Input) | ESP32 GPIO Num |
|---------------|-------------------|-----------------|----------------|
| Motor 0 | IO1 | IO2 | GPIO1, GPIO2 |
| Motor 1 | IO4 | IO5 | GPIO4, GPIO5 |
| Motor 2 | IO6 | IO7 | GPIO6, GPIO7 |
| Motor 3 | IO8 | IO9 | GPIO8, GPIO9 |

### 2.2 PH2.0-LI-5P Interface Definition

Each motor connects to ESP32-S3 via PH2.0-LI-5P connector:

```
PH2.0-LI-5P_001: +12V | IO1(PWM) | (empty) | GND | IO2(FG)
PH2.0-LI-5P_002: +12V | IO4(PWM) | (empty) | GND | IO5(FG)
PH2.0-LI-5P_003: +12V | IO6(PWM) | (empty) | GND | IO7(FG)
PH2.0-LI-5P_004: +12V | IO8(PWM) | (empty) | GND | IO9(FG)
```

Pin Definition:
- **Pin 1**: +12V (External power supply, NOT from ESP32)
- **Pin 2**: PWM signal (GPIO1/4/6/8) - ESP32 output to motor
- **Pin 3**: (empty) - Reserved for CW/CCW control
- **Pin 4**: GND (External ground, NOT from ESP32)
- **Pin 5**: FG signal (GPIO2/5/7/9) - Motor output to ESP32

---

## 3. Software Configuration

### 3.1 PWM Configuration

```c
// PWM Timer Parameters
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES       LEDC_TIMER_13_BIT    // 13-bit resolution (0-8191)
#define LEDC_DUTY           (0)                   // Initial duty (0 = Full speed, inverted logic)
#define LEDC_FREQ           (20000)               // 20KHz PWM frequency

// PWM GPIO Pins (IO1, IO4, IO6, IO8)
#define LEDC_GPIO_LIST      {1, 4, 6, 8}
#define LEDC_CHANNEL_LIST   {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3}
```

**Important**: Inverted PWM Logic
- Duty = 8191 (100%) → Motor OFF (High voltage)
- Duty = 0 (0%) → Motor ON/Full speed (Low voltage)
- Duty calculation: `actual_duty = 8191 - speed_duty`

### 3.2 PCNT (Pulse Counter) Configuration

```c
// PCNT GPIO Pins (IO2, IO5, IO7, IO9)
#define PCNT_GPIO           {2, 5, 7, 9}

// PCNT Count Range
#define PCNT_HIGH_LIMIT     10000
#define PCNT_LOW_LIMIT      -10000
```

### 3.3 Speed Calculation

With 6 pulses per rotation and 1-second sampling:

```
PCNT count per second = (RPM / 60) × 6

Max PCNT count at 4500 RPM:
PCNT_max = (4500 / 60) × 6 = 450 pulses/second

Recommended PID max_pcnt: 450
```

### 3.4 PID Configuration

```c
struct PID_params {
    .Kp         = 8,        // Proportional gain
    .Ki         = 0.02,     // Integral gain
    .Kd         = 0.01,     // Derivative gain
    .max_pwm    = 8191,     // Maximum PWM duty
    .min_pwm    = 0,        // Minimum PWM duty
    .max_pcnt   = 450,      // Max PCNT count (4500 RPM)
    .min_pcnt   = 0         // Min PCNT count
};
```

---

## 4. Hardware Connection Notes

### 4.1 Power Supply
- **+12V and GND**: External power supply (NOT from ESP32-S3)
- **ESP32-S3**: Only provides signal lines (PWM, FG)
- **Motor Current**: Max 0.16A per motor

### 4.2 Signal Levels
- **PWM Output**: 3.3V logic from ESP32-S3
  - VOH(ESP32) ≈ 3.3V > VIH(Motor) = 2.0V ✓
  - Compatible
- **FG Input**: 5V logic from motor
  - VOH(Motor) = 5.0V > VIH(ESP32) = 2.75V (3.3V × 0.75)
  - **Warning**: May need level shifter or voltage divider
  - ESP32-S3 GPIOs are 5V tolerant when configured as input

### 4.3 PWM Frequency Selection
- **Selected**: 20KHz (within 15K~25K range)
- **Reason**: Above human hearing range (20Hz~20KHz), no audible noise
- **Resolution**: 13-bit (8192 levels) at 20KHz with 40MHz clock

---

## 5. Changes from Previous Configuration

| Item | Previous | Current | Reason |
|------|----------|---------|--------|
| PWM GPIO | {5, 6, 7, 8} | {1, 4, 6, 8} | New hardware wiring (IO1, IO4, IO6, IO8) |
| PCNT GPIO | {11, 12, 13, 14} | {2, 5, 7, 9} | New hardware wiring (IO2, IO5, IO7, IO9) |
| PWM Frequency | 5KHz | 20KHz | Motor requires 15K~25KHz |
| PWM Logic | Normal | Inverted | High = OFF, Low = ON |
| FG Pulses/Rev | N/A | 6 | Motor specification |
| Max PCNT | 435 | 450 | 4500 RPM × 6 / 60 = 450 |

---

## 6. Verification Checklist

- [ ] PWM output on GPIO1, GPIO4, GPIO6, GPIO8
- [ ] PCNT input on GPIO2, GPIO5, GPIO7, GPIO9
- [ ] PWM frequency = 20KHz
- [ ] Inverted PWM logic implemented
- [ ] FG signal reading (6 pulses/rotation)
- [ ] PID controller with max_pcnt = 450
- [ ] Speed control range: 0~4500 RPM

---

**Document Version**: 1.0  
**Author**: Kimi Code CLI  
**Last Updated**: 2026-03-11
