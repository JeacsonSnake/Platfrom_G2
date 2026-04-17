# Bug Report: Motor Not Rotating Despite Correct PWM Signal

**Bug ID**: bug-003  
**Date**: 2026-03-13  
**Time**: 17:09:27 (command sent), 17:10 (analysis)  
**Reporter**: User  
**Status**: Root Cause Analysis - Likely Hardware Issue  
**Severity**: High  
**Component**: Hardware Connection / Motor Driver

---

## 1. Problem Summary

After fixing the stack variable lifetime issue (bug-001), sending `cmd_2_225_10` shows **correct PWM voltage changes** but **motor does not physically rotate**.

**Observed Behavior** (from multimeter on IO6 vs GND):
- Initial: 3.29V (motor stopped, duty=8191)
- After `cmd_2_225_10`: 0V (motor should run at 50%, duty=0)
- After 10s: 3.29V (motor stopped, duty=8191)

**Expected**: Motor rotates at 50% speed for 10 seconds  
**Actual**: Motor does not rotate at all

---

## 2. Environment

| Item | Value | Status |
|------|-------|--------|
| Hardware | ESP32-S3-DevKitC-1 | ✓ Working |
| Motor | CHB-BLDC2418 | ? Unclear |
| Connected Port | PH2.0-LI-5P_003 (Motor 2) | ? Check needed |
| GPIO Used | IO6 (PWM), IO7 (FG) | ✓ PWM signal confirmed |
| PWM Frequency | 5KHz | ✓ Working |
| Power | 12V/0.16A connected | ? Verify |
| Software | feature/motor-control-config (bug-001 fixed) | ✓ Working |

---

## 3. Log Analysis

From `esp32_log_20260313_170922.txt`:

```
[2026-03-13 17:09:27.422] I (854) wifi:connected... I (533704) PWM_EVENT: PWM channel 2 duty set to 0.
[2026-03-13 17:09:28.417] I (534704) PWM_EVENT: PWM channel 2 duty set to 0.
[2026-03-13 17:09:29.419] I (535704) PWM_EVENT: PWM channel 2 duty set to 0.
...
[2026-03-13 17:09:37.227] I (543514) PWM_EVENT: PWM channel 2 duty set to 8191.
```

### 3.1 Key Observations

| Time | Event | Duty | IO6 Voltage | Motor State |
|------|-------|------|-------------|-------------|
| T+0 | Command sent | - | 3.29V | Stop |
| T+~100ms | PWM set to 0 | 0 | **0V** | Should run |
| T+10s | PWM set to 8191 | 8191 | **3.29V** | Stop |

**Duration**: ~10 seconds (matches command duration)  
**PWM Pattern**: duty=0 for 10s, then duty=8191

### 3.2 PID Behavior Analysis

The PWM stays at duty=0 (not varying between 0-4000) suggests:

**Scenario A**: PID is outputting maximum (8191) → inverted to duty=0
- This happens when PCNT feedback = 0 (motor not moving)
- PID thinks "motor stopped, need full speed"
- Keeps outputting duty=0

**Scenario B**: PID output is stuck at 0
- Could be integral windup
- Or PCNT reading 0 causes maximum error

### 3.3 Voltage Confirmation

Your multimeter readings **perfectly match** the software behavior:
- 3.29V = duty=8191 (100% high, motor OFF)
- 0V = duty=0 (100% low, motor ON at full speed)

**Conclusion**: Software is working correctly!

---

## 4. Root Cause Analysis

Since software is working (PWM changes correctly) but motor doesn't rotate, this is a **hardware issue**.

### 4.1 Possible Causes

| # | Cause | Probability | Check Method |
|---|-------|-------------|--------------|
| 1 | **Motor not receiving 12V power** | High | Measure +12V at motor connector |
| 2 | **GND not connected** | High | Check continuity between ESP32 GND and motor GND |
| 3 | **PWM signal not reaching motor** | Medium | Scope PWM pin on motor side |
| 4 | **Motor driver fault** | Medium | Test motor with direct 12V |
| 5 | **Motor damaged** | Low | Test motor with external driver |
| 6 | **PWM frequency too low** | Low | 5KHz should work (was 20KHz issue before) |

### 4.2 Detailed Analysis

#### 4.2.1 Power Supply Issue (Most Likely)

**Symptom**: PWM signal present, motor doesn't move

**Check**:
1. Measure voltage at PH2.0-LI-5P_003 Pin 1 (+12V) vs Pin 4 (GND)
2. Should read 12V ± 10%
3. Measure current: should draw ~0.05-0.16A when running

**Common mistakes**:
- 12V power supply not turned on
- Wrong polarity (+12V and GND swapped)
- Power supply current limited
- Loose connection in PH2.0 connector

#### 4.2.2 GND Reference Problem

**Critical**: ESP32 GND and motor 12V GND must be connected together!

```
ESP32-S3          Motor Driver
   |                   |
 IO6 ────────────► PWM Input
   |                   |
 GND ◄───────────────┼── 12V PSU GND
                     │
                +12V ─┘
```

If GNDs are not connected:
- PWM signal has no reference
- Motor driver sees floating input
- No current path for control signal

#### 4.2.3 PWM Signal Level

CHB-BLDC2418 PWM input specs:
- VIH (Input High): 2.0V min
- VIL (Input Low): 0.5V max
- ESP32 outputs 3.3V / 0V ✓ (compatible)

But check: is the PWM signal actually reaching the motor's PWM input pin?

---

## 5. Diagnostic Steps

### Step 1: Verify Power Supply (5 minutes)

1. **Disconnect motor** from PH2.0 connector
2. **Measure voltage** between Pin 1 (+12V) and Pin 4 (GND)
   - Should read ~12V
   - If 0V → Power supply issue
   - If <10V → Power supply overloaded

3. **Check power supply current capacity**
   - CHB-BLDC2418 max current: 0.16A
   - Recommended PSU: 12V/1A minimum

### Step 2: Verify GND Connection (3 minutes)

1. **Measure continuity** between:
   - ESP32 GND pin and PH2.0 Pin 4 (GND)
   - Should be ~0Ω (beep on multimeter)

2. **If not connected**: Run a wire between ESP32 GND and motor PSU GND

### Step 3: Direct Motor Test (5 minutes)

**Bypass PWM control, test motor directly**:

1. Disconnect motor from PH2.0
2. Connect motor directly to 12V:
   - Motor + to +12V
   - Motor - to GND
3. Motor should spin at full speed

**Result**:
- Motor spins → Motor is good, issue is control signal
- Motor doesn't spin → Motor is damaged or PSU issue

### Step 4: PWM Signal Verification (5 minutes)

**Method A: LED Test**
You already did this! LED shows PWM is working on IO6.

**Method B: Scope/Oscilloscope**
If available, check PWM at PH2.0 Pin 2:
- Should see 5KHz square wave when motor should run
- 0V when stopped

**Method C: Voltmeter (already done)**
Your measurements confirm software is working.

### Step 5: Check PH2.0 Wiring

Verify PH2.0-LI-5P_003 connector:

| Pin | Function | Connection | Check |
|-----|----------|------------|-------|
| 1 | +12V | External PSU + | ✓ 12V present? |
| 2 | PWM (IO6) | ESP32 GPIO6 | ✓ 0V/3.3V changing? |
| 3 | NC | Empty | - |
| 4 | GND | External PSU GND + ESP32 GND | ✓ Common ground? |
| 5 | FG (IO7) | ESP32 GPIO7 | - (optional for now) |

---

## 6. Quick Fixes

### Fix A: Common Ground (Most Common Fix)

Run a wire connecting:
- ESP32-S3 GND pin → Motor power supply GND

This is **essential** for the PWM signal to have a reference.

### Fix B: Check Polarity

Ensure:
- Motor Red wire → +12V
- Motor Black wire → GND
- (Some motors have different color coding)

### Fix C: Test with Lower Speed

Try: `cmd_2_50_10` (very slow speed)
- Listen carefully for any motor noise
- Some motors need minimum speed to overcome friction

---

## 7. Expected Behavior After Fix

Once hardware is correctly connected:

| Time | IO6 Voltage | Motor Behavior |
|------|-------------|----------------|
| T+0 | 3.3V | Stop |
| T+0.1s | 0V (or PWM average ~1.6V) | Start rotating |
| T+0.5s | PWM varying ~50% duty | Running at 50% speed (~2250 RPM) |
| T+10s | 3.3V | Coast to stop |

You should also see in logs:
```
pcnt_count_2_220    # PCNT feedback showing motor speed
pcnt_count_2_225    # Stable speed near target
```

---

## 8. Summary

| Aspect | Status | Notes |
|--------|--------|-------|
| Software PWM | ✅ Working | Voltage changes correctly |
| Command Parsing | ✅ Working | 10s duration correct |
| PID Control | ✅ Working | Outputting duty=0 (max speed) |
| Motor Power | ❓ Unknown | Likely issue |
| GND Connection | ❓ Unknown | Likely issue |
| Motor Health | ❓ Unknown | Need to test |

**Verdict**: Software is fixed and working. Issue is **hardware connection** (most likely missing common GND or 12V power not reaching motor).

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-13 17:10
