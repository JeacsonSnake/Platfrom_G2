# PID Speed Control Step Response Analysis

**Date**: 2026-03-16  
**Time**: 16:30  
**Issue ID**: pounder_001  
**Status**: Analysis Complete - Root Cause Identified  
**Severity**: Medium  
**Component**: PID Controller (`main/pid.c`), PCNT Sampling (`main/pcnt.c`)

---

## 1. Problem Summary

### Observed Behavior
Motor speed exhibits **step-like (阶梯函数) changes** during speed regulation:
- Speed does not transition smoothly to target
- Sudden jumps in RPM observed during external measurement
- Control output appears to change in discrete steps rather than gradually

### Expected Behavior
Smooth, continuous speed transition following a curve:
```
Speed
  ^
  |        ______ target
  |       /
  |      /
  |     /
  |    /
  |___/_________> Time
    Smooth curve
```

### Actual Behavior
Step-wise speed changes:
```
Speed
  ^
  |    ____    ____
  |   |    |  |    |
  |   |    |__|    |____ target
  |___|                |
      |________________|__> Time
      Step-like jumps
```

---

## 2. Environment

| Item | Value |
|------|-------|
| Hardware | ESP32-S3-DevKitC-1 |
| Motor | CHB-BLDC2418 (12V, 4500 RPM) |
| PWM Frequency | 5KHz |
| PWM Resolution | 13-bit (0-8191) |
| Control Algorithm | PID Closed-loop |

### Current PID Parameters
```c
Kp = 8.0      // Proportional gain
Ki = 0.02     // Integral gain
Kd = 0.01     // Derivative gain
```

---

## 3. Code Analysis

### 3.1 Current Control Loop Timing

**PCNT Sampling (pcnt.c)**:
```c
void pcnt_monitor(void* params) {
    while(1) {
        pcnt_unit_get_count(unit, &pcnt_count_list[index]);
        pcnt_unit_clear_count(unit);
        // ... publish MQTT ...
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // 1000ms = 1Hz
    }
}
```
- **Sampling Rate**: 1 second (1 Hz)
- **Problem**: Too slow for real-time control

**PID Control (pid.c)**:
```c
void PID_init(void* params) {
    while(1){
        if(pcnt_updated_list[index] == true) {
            double new_input = PID_Calculate(pid_params, &data, temp, pcnt_count_list[index]);
            pwm_set_duty(new_input_int, index);
            pcnt_updated_list[index] = false;
        }
        else{
            vTaskDelay(10 / portTICK_PERIOD_MS);  // 10ms polling
        }
    }
}
```
- **Control Loop**: Waits for PCNT update flag
- **Effective Control Rate**: 1 Hz (limited by PCNT)

### 3.2 Root Cause Analysis

#### Root Cause 1: Extreme Sampling/Control Rate Mismatch

| Parameter | Current Value | Ideal Value |
|-----------|---------------|-------------|
| PCNT Sampling | 1000ms (1Hz) | 200ms (5Hz) |
| PID Execution | Event-driven | 10-20ms |
| Control Loop Bandwidth | ~0.5 Hz | ~5-10 Hz |

**Impact**:
- PID controller runs at only 1 Hz (once per second)
- Between samples, motor runs with constant PWM
- When new sample arrives, large error calculated
- PID makes large correction → **step change**

#### Root Cause 2: Large Integral Accumulation

With Ki=0.02 and 1-second intervals:
```
Error persists for 1 second before correction
Integral accumulates: I += error * 1.0s

Example:
  Target: 225 PCNT
  Actual: 100 PCNT
  Error: 125 (persists for 1s)
  
  Integral increase per cycle: 125 * 0.02 = 2.5
  
  After several cycles, integral becomes large
  When error finally reduces, integral causes overshoot
```

#### Root Cause 3: Discrete Control Action

Current flow:
```
Time:  0ms    1000ms   2000ms   3000ms
       |       |        |        |
PCNT:  READ →  READ  →  READ  →  READ
       |       |        |        |
PID:   CALC →  CALC  →  CALC  →  CALC
       |       |        |        |
PWM:   SET  →  SET   →  SET   →  SET
              ↑
              Large PWM jump possible
```

With 1-second gaps, PWM remains constant, then suddenly changes.

#### Root Cause 4: Derivative Term Ineffective

Current Kd=0.01 with 1-second sampling:
```
Derivative = (current_error - previous_error) / dt
           = (error - prev_error) / 1.0s

Small Kd + large dt = negligible damping effect
```

The derivative term cannot provide smooth damping with such slow sampling.

---

## 4. Mathematical Analysis

### 4.1 PID Output Step Size Calculation

Given:
- Max PCNT change between samples: ~200-400 counts/sec
- Kp = 8
- Output range: 0-8191 (PWM)

Maximum step in one control cycle:
```
ΔOutput = Kp × ΔError
        = 8 × 400
        = 3200 (out of 8191)
        ≈ 39% of full scale!
```

This explains the observed step-like behavior.

### 4.2 System Response Time

With 1Hz control:
- System takes ~3-5 seconds to settle (3-5 control cycles)
- Each cycle can produce large PWM change
- Result: Speed changes in visible "steps"

---

## 5. Recommended Solutions

### Solution 1: Increase PCNT Sampling Rate (Primary Fix)

**Change**: 1000ms → 200ms (5 Hz)

```c
// In pcnt.c
vTaskDelay(200 / portTICK_PERIOD_MS);  // 200ms = 5Hz
```

**Benefits**:
- 5× faster feedback
- Smaller error per sample
- Smoother PWM adjustments

### Solution 2: Adjust PID Parameters for Higher Rate

With 5× faster sampling, retune PID:

| Parameter | Current | Recommended | Rationale |
|-----------|---------|-------------|-----------|
| Kp | 8.0 | 4.0-6.0 | Reduce proportional gain |
| Ki | 0.02 | 0.005-0.01 | Scale down integral (×1/5) |
| Kd | 0.01 | 0.02-0.05 | Increase for damping |

**Why scale Ki down**:
- With 5× samples per second, integral accumulates 5× faster
- Must reduce Ki to maintain same integral effect

### Solution 3: Add Output Rate Limiting (Optional)

Limit maximum PWM change per cycle:
```c
#define MAX_PWM_DELTA 200  // Maximum change per cycle

int delta = new_input_int - current_duty;
if (delta > MAX_PWM_DELTA) delta = MAX_PWM_DELTA;
if (delta < -MAX_PWM_DELTA) delta = -MAX_PWM_DELTA;
new_input_int = current_duty + delta;
```

### Solution 4: Enable PID at Fixed Interval

Instead of event-driven, run PID at fixed interval:
```c
void PID_init(void* params) {
    while(1) {
        if(pcnt_updated_list[index]) {
            // Calculate PID
            // ...
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);  // 50Hz regardless
    }
}
```

---

## 6. Implementation Plan

### Phase 1: Increase Sampling Rate
1. Modify `pcnt.c`: Change delay from 1000ms to 200ms
2. Adjust MQTT publish rate accordingly
3. Test basic functionality

### Phase 2: Retune PID Parameters
1. Reduce Kp from 8.0 to 5.0
2. Reduce Ki from 0.02 to 0.005
3. Increase Kd from 0.01 to 0.03
4. Test and fine-tune

### Phase 3: Verification
1. Log PWM duty changes over time
2. Verify smooth transition curves
3. Measure settling time and overshoot

---

## 7. Test Verification Criteria

| Test | Expected Result |
|------|-----------------|
| Send `cmd_2_225_10` | Speed increases smoothly |
| PWM duty log | Gradual changes, no large jumps |
| PCNT feedback | 5 messages per second |
| Settling time | < 3 seconds to reach ±5% of target |
| Overshoot | < 10% of target speed |

---

## 8. Summary

| Issue | Root Cause | Solution |
|-------|-----------|----------|
| Step-like speed changes | PCNT sampling too slow (1Hz) | Increase to 5Hz (200ms) |
| Large PWM jumps | High Kp with large time gaps | Reduce Kp, adjust Ki/Kd |
| Poor damping | Ineffective derivative term | Increase Kd, faster sampling |

**Primary Recommendation**: 
Increase PCNT sampling rate to 200ms (5Hz) and retune PID parameters accordingly.

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-16 16:30
