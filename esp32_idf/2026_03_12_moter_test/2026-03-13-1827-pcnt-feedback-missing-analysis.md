# PCNT Feedback Missing Analysis

**Date**: 2026-03-13  
**Time**: 18:19 (log recorded), 18:27 (analysis)  
**Reporter**: User  
**Status**: Analysis Complete - Hardware Connection Issue Identified  
**Severity**: Low (Motor works, but no speed feedback)

---

## 1. Summary

**Good News**: Motor is now rotating correctly!
- `cmd_2_450_5` executed successfully
- PWM duty changes visible in logs (4578 → 960 → 0 → 8191)
- 5-second duration working correctly

**Issue**: No `pcnt_count_2_XXX` messages in logs or MQTT

---

## 2. Log Analysis

From `esp32_log_20260313_181914.txt`:

```
[18:20:52.356] PWM channel 2 duty set to 4578.   ← Motor starts
[18:20:53.357] PWM channel 2 duty set to 960.    ← PID adjusting
[18:20:54.356] PWM channel 2 duty set to 0.      ← Near target speed
[18:20:55.357] PWM channel 2 duty set to 0.      ← Running
[18:20:56.357] PWM channel 2 duty set to 0.      ← Running
[18:20:56.466] PWM channel 2 duty set to 8191.   ← Stopped after 5s
```

**Missing**: No `pcnt_count_2_XXX` messages anywhere in the log

---

## 3. Expected Behavior

PCNT (Pulse Counter) should send feedback every second:

```
Expected log pattern:
[18:20:52.356] PWM channel 2 duty set to 4578.
[18:20:52.xxx] pcnt_count_2_0       ← Motor just started, low count
[18:20:53.357] PWM channel 2 duty set to 960.
[18:20:53.xxx] pcnt_count_2_180     ← Motor accelerating
[18:20:54.356] PWM channel 2 duty set to 0.
[18:20:54.xxx] pcnt_count_2_430     ← Near target (450)
[18:20:55.357] PWM channel 2 duty set to 0.
[18:20:55.xxx] pcnt_count_2_445     ← Stable at target
...
```

---

## 4. Root Cause Analysis

### 4.1 PCNT Code Review

In `main/pcnt.c`, the `pcnt_monitor()` task:

```c
void pcnt_monitor(void* params)
{
    int index = *((int *) params);
    // ...
    while(1)
    {
        pcnt_unit_get_count(unit, &pcnt_count_list[index]);
        pcnt_unit_clear_count(unit);

        if(motor_speed_list[index] == 0 && idle == false)
        {
            // Send PCNT count via MQTT
            sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
            // ...
        }
        else if(motor_speed_list[index] != 0)
        {
            // Send PCNT count via MQTT
            sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
            esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
            // ...
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // 1 second interval
    }
}
```

**Code Analysis**:
- PCNT task runs every 1 second
- It should send MQTT message to `esp32_1/data` topic
- Uses QoS 2 for reliable delivery
- Message format: `pcnt_count_<index>_<count>`

### 4.2 Why No Messages?

| Possible Cause | Probability | Check Method |
|---------------|-------------|--------------|
| **FG signal not connected** | **High** | Check IO7 connection |
| **FG signal not reaching ESP32** | **Medium** | Measure voltage on IO7 |
| **PCNT counting 0 always** | **Medium** | If count=0, still should send message |
| **MQTT publish failing** | **Low** | PWM messages work, so MQTT is OK |
| **PCNT thread crashed** | **Low** | Would see other symptoms |

### 4.3 Most Likely Cause: FG Signal Line

**The PH2.0-LI-5P_003 connector**:
```
Pin 1: +12V  (External power)
Pin 2: IO6   (PWM output) ✓ Working
Pin 3: (NC)  (Reserved)
Pin 4: GND   (Power ground) ✓ Connected
Pin 5: IO7   (FG input) ? Check this!
```

**FG (Tachometer) Signal**:
- Motor outputs 6 pulses per rotation
- Signal: 5V logic, 2mA current
- Must be connected to ESP32 GPIO7 (IO7)

**If IO7 is not connected**:
- PCNT counts nothing (always 0 or noise)
- PCNT still sends `pcnt_count_2_0` every second
- But you don't see ANY pcnt messages...

Wait - if PCNT sends even when count=0, why no messages?

### 4.4 Secondary Issue: PCNT Thread Not Running?

Let me check: Are there PCNT initialization logs in your boot sequence?

From previous logs (2026-03-12), I saw:
```
[16:03:25.702] PCNT_EVENT: PCNT channel 2 has been initiated on pin 7.
```

But in the latest log (2026-03-13 18:19), the log starts AFTER boot:
```
[18:19:39.518] ESP32S3_MQTT_EVENT: 心跳已发送
```

**The log doesn't show the boot sequence!**

This suggests:
1. PCNT initialized successfully (or we would have seen errors)
2. PCNT thread is running
3. But it's not sending messages...

### 4.5 The Real Issue: PCNT vs PWM Priority

Looking at `pcnt_monitor_init()`:
```c
xTaskCreate(pcnt_monitor, "PCNT_TASK", 4096, (void*) j, 1, NULL);
```

Priority = 1 (low)

And `control_cmd`:
```c
xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, NULL);
```

Priority = 1 (same)

With only 5 seconds of motor runtime, and PCNT sampling every 1 second:
- T+0: Motor starts
- T+1s: First PCNT read
- T+2s: Second PCNT read
- T+3s: Third PCNT read
- T+4s: Fourth PCNT read
- T+5s: Motor stops

**There SHOULD be 4-5 pcnt messages!**

### 4.6 Alternative: MQTT QoS 2 Issue

PCNT uses QoS 2:
```c
esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
```

PWM uses QoS 2 as well:
```c
esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
```

So if PWM messages work, QoS 2 is not the issue.

### 4.7 Conclusion: PCNT Count is 0, Messages Not Sent?

Wait - let me re-read the PCNT code more carefully:

```c
if(motor_speed_list[index] == 0 && idle == false)
{
    sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
    esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
    pwm_set_duty(8191, index);  // ← This would show in logs!
    pcnt_updated_list[index] = false;
    if(pcnt_count_list[index] == 0){
        idle = true;
    }
}
else if(motor_speed_list[index] != 0)
{
    sprintf(buff, "pcnt_count_%d_%d", index, pcnt_count_list[index]);
    esp_mqtt_client_publish(mqtt_client, MQTT_DATA_CHANNEL, buff, strlen(buff), 2, 0);
    idle = false;
    pcnt_updated_list[index] = true;
}
```

**Key insight**: If `motor_speed_list[index] != 0`, it sends PCNT message AND sets `pcnt_updated_list[index] = true`.

The PID controller checks `pcnt_updated_list[index]`:
```c
if(pcnt_updated_list[index] == true)
{
    double temp = motor_speed_list[index];
    double new_input = PID_Calculate(pid_params, &data, temp, pcnt_count_list[index]);
    // ...
}
```

Since PID IS running (we see PWM duty changes), `pcnt_updated_list[2]` must be `true`, which means the PCNT message SHOULD have been sent...

**Unless**: The `pcnt_count_list[index]` is always reading as 0, and the PCNT message is being sent but not logged/displayed?

Actually, looking at PWM messages - they appear in the log. So ESP_LOGI works. If PCNT used ESP_LOGI, we would see it.

But PCNT only sends MQTT, no local log! That's why we don't see it in the serial log!

---

## 5. Verification Steps

### Step 1: Check MQTT Subscription

In MQTTX, subscribe to `esp32_1/data` and look for:
```
pcnt_count_2_0
pcnt_count_2_120
pcnt_count_2_380
...
```

**Note**: Serial log only shows ESP_LOGI messages, not all MQTT traffic. You need to check MQTT client (MQTTX) for the PCNT data.

### Step 2: Check IO7 Connection

If no PCNT messages in MQTTX either:

1. **Check IO7 is connected**:
   - PH2.0 Pin 5 (IO7) should be connected to motor FG output
   - Use multimeter continuity test

2. **Check FG signal voltage**:
   - When motor rotates, FG should output ~5V pulses
   - Use oscilloscope or multimeter AC mode

### Step 3: Check PCNT Configuration

Add debug log to PCNT initialization:
```c
ESP_LOGI(TAG, "PCNT reading on GPIO%d: count=%d", pcnt_gpios[index], pcnt_count_list[index]);
```

---

## 6. Solutions

### Solution A: Verify MQTT Reception (Do This First!)

1. Open MQTTX
2. Subscribe to `esp32_1/data`
3. Send `cmd_2_450_5`
4. **Look for `pcnt_count_2_XXX` messages in MQTTX**

**If you see them in MQTTX but not in serial log**: This is normal! PCNT only sends MQTT, doesn't log to serial.

**If you don't see them in MQTTX**: Go to Solution B.

### Solution B: Check FG Signal Connection

**Verify PH2.0-LI-5P_003 wiring**:
```
Pin 1: +12V  → Power supply +
Pin 2: IO6   → ESP32 GPIO6 (PWM) ✓ Confirmed working
Pin 3: NC    → Empty
Pin 4: GND   → Power supply GND ✓ Confirmed working
Pin 5: IO7   → ESP32 GPIO7 (FG) ? CHECK THIS!
```

**Check steps**:
1. Is there a wire from motor FG output to PH2.0 Pin 5?
2. Is PH2.0 Pin 5 connected to ESP32 GPIO7?
3. Use multimeter: GPIO7 should have voltage pulses when motor rotates

### Solution C: Add Serial Debug Output

Modify `main/pcnt.c` to add serial logging:

```c
// In pcnt_monitor(), after reading count:
pcnt_unit_get_count(unit, &pcnt_count_list[index]);
ESP_LOGI(TAG, "PCNT%d reading: count=%d, speed_target=%d", 
         index, pcnt_count_list[index], (int)motor_speed_list[index]);
```

Rebuild and flash to see PCNT readings in serial log.

### Solution D: Bypass PCNT (Temporary)

If FG signal is not available, you can run motor in open-loop mode:
- PID uses PCNT feedback for closed-loop control
- Without PCNT, motor still runs but speed control is less accurate
- PWM duty is set directly without feedback correction

---

## 7. Expected Results After Fix

### With FG Connected:

**MQTTX receives**:
```
pcnt_count_2_0      # Before motor starts
pcnt_count_2_0      # Motor starting
pcnt_count_2_210    # Accelerating
pcnt_count_2_435    # Near target speed
pcnt_count_2_448    # Stable
pcnt_count_2_445    # Stable
pcnt_count_2_0      # After motor stops
```

### Without FG (Open Loop):

- Motor still rotates
- PWM duty based on target speed only
- No `pcnt_count` messages
- Speed may vary with load

---

## 8. Summary

| Item | Status | Note |
|------|--------|------|
| Motor rotation | ✅ Working | PWM changes confirm |
| 5s duration | ✅ Working | Timing correct |
| PID control | ✅ Working | Duty varies 4578→0 |
| PCNT feedback | ❓ Unknown | Check MQTTX subscription |
| FG connection | ❓ Unknown | Likely not connected |

**Most Likely Scenario**: 
- FG signal line (IO7) not connected
- OR PCNT messages are being sent but you only checked serial log, not MQTTX

**Action Required**:
1. Check MQTTX for `pcnt_count_2_XXX` messages
2. If not present, check IO7 physical connection
3. If present, everything is working!

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-13 18:27
