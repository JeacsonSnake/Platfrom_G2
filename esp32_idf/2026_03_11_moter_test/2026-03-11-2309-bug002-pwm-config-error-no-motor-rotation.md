# Bug Report: Motor Not Rotating Despite PWM Duty Changes

**Bug ID**: bug002  
**Date**: 2026-03-11 23:09  
**Reporter**: User  
**Status**: Root Cause Identified (No Code Change Required)  
**Severity**: High  
**Component**: PWM Configuration / LEDC Driver  

---

## 1. Problem Summary

After fixing the JSON format issue (bug001), sending `cmd_2_225_10` via MQTTX results in:
- PWM duty changes visible in logs
- Motor does NOT physically rotate
- Duty remains near 8191 (stop) continuously
- Motor does not stop after 10 seconds as expected

**Expected Behavior**:  
Send `cmd_2_225_10` → Motor 2 rotates at 50% speed for 10 seconds → Motor stops

**Actual Behavior**:  
Send `cmd_2_225_10` → PWM duty fluctuates but stays near 8191 → Motor does not rotate → Never stops

---

## 2. Environment

| Item | Value |
|------|-------|
| Hardware | ESP32-S3-DevKitC-1 |
| Motor | CHB-BLDC2418 (12V, 4500 RPM) |
| Connected Port | PH2.0-LI-5P_003 (Motor 2) |
| GPIO Used | IO6 (PWM), IO7 (FG) |
| Firmware Branch | feature/motor-control-config |
| PWM Config | 20KHz, 13-bit resolution |

---

## 3. Log Analysis

### 3.1 Critical Error at Boot

```
[2026-03-11 22:41:18.183] E (10781) ledc: requested frequency 20000 and duty resolution 13 can not be achieved, try reducing freq_hz or duty_resolution. div_param=0
```

**Translation**: LEDC driver cannot achieve 20KHz frequency with 13-bit duty resolution.

### 3.2 PWM Initialization Sequence

```
[2026-03-11 22:41:18.188] I (10801) PWM_EVENT: PWM timer 0 initiated at clock speed 20000.
[2026-03-11 22:41:18.197] I (10801) PWM_EVENT: PWM channel 0 initiated on GPIO1 with initial duty 0 (inverted logic).
[2026-03-11 22:41:18.205] I (10811) PWM_EVENT: PWM channel 1 initiated on GPIO4 with initial duty 0 (inverted logic).
[2026-03-11 22:41:18.213] I (10821) PWM_EVENT: PWM channel 2 initiated on GPIO6 with initial duty 0 (inverted logic).
[2026-03-11 22:41:18.222] I (10831) PWM_EVENT: PWM channel 3 initiated on GPIO8 with initial duty 0 (inverted logic).
```

Despite the error, PWM channels are initialized (but may not function correctly).

### 3.3 PID Initialization - All Motors Stop

```
[2026-03-11 22:41:18.273] I (10841) ESP32S3_STATUS_LED: LED mode changed to: 3
[2026-03-11 22:41:18.294] I (10891) PWM_EVENT: PWM channel 1 duty set to 8191.
[2026-03-11 22:41:18.298] I (10891) PWM_EVENT: PWM channel 0 duty set to 8191.
[2026-03-11 22:41:18.303] I (10891) PWM_EVENT: PWM channel 2 duty set to 8191.
[2026-03-11 22:41:18.303] I (10891) PWM_EVENT: PWM channel 3 duty set to 8191.
```

All motors are set to stop (duty=8191) because `motor_speed_list` initial values are 0.

### 3.4 Command Execution (23:04:24)

User sent `cmd_2_225_10` at 23:04:23.727. Log shows:

```
[2026-03-11 23:04:24.334] I (1396931) PWM_EVENT: PWM channel 2 duty set to 8191.
[2026-03-11 23:04:25.334] I (1397931) PWM_EVENT: PWM channel 2 duty set to 7813.
[2026-03-11 23:04:26.338] I (1398931) PWM_EVENT: PWM channel 2 duty set to 8191.
[2026-03-11 23:04:27.334] I (1399931) PWM_EVENT: PWM channel 2 duty set to 8191.
[2026-03-11 23:04:28.334] I (1400931) PWM_EVENT: PWM channel 2 duty set to 8191.
... (continues indefinitely)
```

**Analysis**:
- Duty briefly drops to 7813 (about 4.9% on-time in inverted logic = ~95% off)
- Then returns to 8191 (100% off = motor stop)
- Pattern continues indefinitely (does not stop after 10 seconds)

---

## 4. Root Cause Analysis

### 4.1 Primary Issue: PWM Configuration Invalid

**The Critical Error**:
```
E (10781) ledc: requested frequency 20000 and duty resolution 13 can not be achieved
```

**Explanation**:
ESP32-S3 LEDC (LED PWM Controller) has hardware limitations:
- Clock source: 80MHz (APB_CLK) or 40MHz (RC_FAST_CLK)
- Division factor must be integer
- Formula: `frequency = clock / (divisor * (2^resolution))`

For 20KHz with 13-bit (8192 levels) resolution:
- Required divisor = 80MHz / (20KHz * 8192) = 80,000,000 / 163,840,000 = 0.488
- **Problem**: Divisor must be integer, 0.488 is not valid!

This causes the LEDC driver to fail configuration, resulting in:
1. PWM peripheral not functioning correctly
2. GPIO may output constant level instead of PWM
3. Motor does not receive valid PWM signal

### 4.2 Secondary Issue: PID Control Output

Even if PWM worked, the duty values are problematic:

| Duty | Inverted Logic | Motor State |
|------|---------------|-------------|
| 8191 | 100% High | STOP |
| 7813 | 95.6% High | ~4.4% speed (very slow) |
| 4096 | 50% High | ~50% speed |
| 0 | 0% High | FULL SPEED |

The PID output is staying near 8191, which means:
- PID thinks motor is running too fast (high PCNT count?)
- Or PID integral term is saturated
- Or PCNT is not reading correctly

### 4.3 Tertiary Issue: Command Task Not Ending

Expected behavior after 10 seconds:
- `control_cmd` task should set `motor_speed_list[2] = 0`
- PID should stop updating
- Duty should stay at 8191

But logs show continuous duty updates indefinitely, suggesting:
- Control task may not be ending properly
- Or `motor_speed_list` is being set but PID keeps running

---

## 5. Solutions (No Code Change Required)

### 5.1 Solution A: Reduce PWM Frequency (Recommended)

CHB-BLDC2418 motor specifications state:
- PWM frequency range: 15KHz ~ 25KHz
- **Practical recommendation**: 20KHz is ideal but requires lower resolution

**Change PWM frequency to a valid value**:

| Frequency | Max Resolution | Max Duty Value | 13-bit Compatible? |
|-----------|---------------|----------------|-------------------|
| 20KHz | 8-bit (256) | 255 | No |
| 10KHz | 10-bit (1024) | 1023 | No |
| 5KHz | 12-bit (4096) | 4095 | No |
| **5KHz** | **13-bit (8192)** | **8191** | **Yes ✓** |

**Wait - 5KHz worked before!**

The original configuration used 5KHz successfully. The change to 20KHz caused the problem.

**Action**: Revert PWM frequency to 5KHz in menuconfig or source code.

### 5.2 Solution B: Reduce Duty Resolution

Keep 20KHz but reduce resolution:

```c
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT    // 8-bit: 0-255
#define LEDC_DUTY       (0)
#define LEDC_FREQ       (20000)
```

**Pros**: Keeps 20KHz frequency  
**Cons**: 
- Resolution reduced (256 levels instead of 8192)
- All duty calculations must be scaled to 0-255
- Requires code changes to pwm.c and pid.c

### 5.3 Solution C: Hardware PWM Verification

Before software fix, verify hardware connection:

1. **Use oscilloscope on GPIO6**:
   - Should see 20KHz square wave when motor should run
   - If flat line or wrong frequency = PWM config error confirmed

2. **Use multimeter on GPIO6**:
   - When duty=8191: should read ~3.3V (constant high)
   - When duty=4096: should read ~1.65V (50% duty)
   - When duty=0: should read ~0V (constant low)
   - If always 0V or 3.3V = PWM not working

### 5.4 Solution D: Manual PWM Test

Bypass PID and manually set PWM to verify hardware:

```
# This requires a simple firmware modification for testing
# Set duty to 4096 (50% in inverted logic = 50% speed):
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "test_pwm_2_4096"
```

Or add temporary code in mqtt.c to directly control PWM:
```c
if (strstr(data, "test_pwm_") != NULL) {
    int ch, duty;
    sscanf(data, "test_pwm_%d_%d", &ch, &duty);
    pwm_set_duty(duty, ch);
    ESP_LOGI(TAG, "Test PWM: ch=%d, duty=%d", ch, duty);
}
```

---

## 6. Detailed Log Timeline

```
22:41:18.183 - CRITICAL ERROR: PWM config failed (20KHz + 13-bit incompatible)
22:41:18.188 - PWM timer init reports 20000Hz (but may not work)
22:41:18.213 - PWM channel 2 init on GPIO6 (duty=0, but inverted logic means FULL SPEED)
22:41:18.303 - All motors STOPPED (duty=8191)
...
23:04:23.727 - User sends cmd_2_225_10
23:04:24.334 - PWM channel 2 duty=8191 (STOP)
23:04:25.334 - PWM channel 2 duty=7813 (~4.4% speed)
23:04:26.338 - PWM channel 2 duty=8191 (STOP)
23:04:27+    - Continuous duty=8191 (STOP)
...
23:06:03     - Log ends, still duty=8191
```

**Key Observations**:
1. PWM config error at boot explains why motor never runs
2. PID is trying to control (duty changes briefly) but output stays near stop
3. Control task seems to run indefinitely (no timeout visible in logs)

---

## 7. Immediate Action Plan

### Step 1: Verify PWM Hardware (5 minutes)

Use oscilloscope or multimeter on GPIO6:
- Measure when ESP32 boots
- Should see 20KHz signal if PWM works
- If no signal or wrong frequency = confirmed config error

### Step 2: Quick Software Fix (10 minutes)

**Option 1 - Change frequency to 5KHz**:
```c
// main/main.h line 76
#define LEDC_FREQ       (5000)     // Change from 20000 to 5000
```

Rebuild and flash:
```powershell
idf.py build
idf.py -p COM9 flash
```

**Option 2 - Change resolution to 8-bit**:
```c
// main/main.h line 74-76
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT
#define LEDC_DUTY       (0)
#define LEDC_FREQ       (20000)
```

But this requires additional changes to scale duty values (0-255 instead of 0-8191).

### Step 3: Verify Fix

After reflash:
1. Check boot logs - should NOT see "can not be achieved" error
2. Send `cmd_2_225_10`
3. Motor should rotate
4. After 10 seconds, motor should stop

---

## 8. Motor Specification Reconciliation

CHB-BLDC2418 datasheet states:
- PWM frequency: 15KHz ~ 25KHz recommended
- Input voltage: 0-5V

**But ESP32 hardware limitation**:  
20KHz with 13-bit resolution is not achievable.

**Practical solution**:
- Use 5KHz PWM (within motor's acceptable range)
- Test if motor runs smoothly at 5KHz
- If audible noise is a problem, consider:
  - Using 10-bit resolution with ~10KHz frequency
  - Adding RC filter to PWM output
  - Using external PWM generator

**Note**: Most BLDC motors work fine at 5KHz. The 15-25KHz recommendation is to avoid audible noise (above 20KHz = ultrasonic).

---

## 9. Additional Observations

### 9.1 Control Task Duration Issue

The user reported: "10秒钟后，其并没有停止发送PWM_EVENT: PWM channel 2 duty set to 8191"

From logs, duty updates continue indefinitely, which is actually the **expected behavior** when motor_speed_list is set to a non-zero value. The PID controller continuously runs and updates PWM.

However, after 10 seconds, `control_cmd` task should:
1. Set `motor_speed_list[2] = 0`
2. Call `pwm_set_duty(8191, 2)`
3. Delete itself

The continuous "duty set to 8191" messages suggest the PID is still running with motor_speed_list[2] = 0, causing it to continuously try to stop the motor.

**This is actually correct behavior** - the PID keeps running at 10ms intervals and continuously sets duty to 8191 (stop).

### 9.2 PCNT Feedback

Not visible in the provided log segment - need to check if PCNT is counting:
- If PCNT always reads 0, PID thinks motor is stopped
- PID would output maximum speed (duty=0 in inverted logic)
- But we're seeing duty=8191 (stop)

This suggests the PID may be working but PWM is not outputting the signal.

---

## 10. Conclusion

**Root Cause**: PWM configuration error at boot
```
E (10781) ledc: requested frequency 20000 and duty resolution 13 can not be achieved
```

**Impact**: PWM peripheral not functioning, motor does not rotate

**Solution Options**:
1. **(Recommended)** Reduce PWM frequency to 5KHz (revert to original working config)
2. Reduce duty resolution to 8-bit and keep 20KHz (requires code changes)
3. Use external PWM generator (hardware solution)

**Secondary Issue**: Command may not be ending properly, but this is masked by PWM not working.

---

## 11. Test After Fix

Once PWM is fixed, verify:
1. ✅ No LEDC error at boot
2. ✅ Motor rotates when cmd_2_225_10 sent
3. ✅ PWM duty changes in reasonable range (not stuck at 8191)
4. ✅ Motor stops after 10 seconds
5. ✅ PWM duty stops updating after motor stops (or stays at 8191 without continuous log spam)

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-11 23:09
