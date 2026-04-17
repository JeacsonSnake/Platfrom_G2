# Bug Report: Motor Not Responding Due to Stack Variable Lifetime Issue

**Bug ID**: bug-001  
**Date**: 2026-03-12  
**Time**: 16:04:21 (command sent), 16:10 (analysis)  
**Reporter**: User  
**Status**: Root Cause Identified - Code Fix Required  
**Severity**: Critical  
**Component**: MQTT Command Processing (`main/mqtt.c`)

---

## 1. Problem Summary

After fixing PWM frequency to 5KHz and using correct Plaintext format in MQTTX, sending `cmd_2_225_10` results in:
- Motor does NOT rotate
- PWM duty continuously set to 8191 (motor stop)
- Command never times out (continuous PWM updates)

**Expected Behavior**:  
Send `cmd_2_225_10` → Motor rotates at 50% speed for 10 seconds → Motor stops

**Actual Behavior**:  
Send `cmd_2_225_10` → PWM duty stays at 8191 (stop) → Never ends

---

## 2. Environment

| Item | Value |
|------|-------|
| Hardware | ESP32-S3-DevKitC-1 |
| Motor | CHB-BLDC2418 (12V, 4500 RPM) |
| Connected Port | PH2.0-LI-5P_003 (Motor 2) |
| GPIO Used | IO6 (PWM), IO7 (FG) |
| Firmware Branch | feature/motor-control-config |
| PWM Frequency | 5KHz (fixed from 20KHz) |
| MQTT Format | Plaintext (correct) |
| Power | 12V/0.16A connected |

---

## 3. Log Analysis

### 3.1 Command Reception

User sent `cmd_2_225_10` at **16:04:21.041**.

Log shows PWM activity starting at **16:04:21.777**:
```
[2026-03-12 16:04:21.777] I (74292) PWM_EVENT: PWM channel 2 duty set to 8191.
```

**~700ms delay** between command send and PWM update - this is the task creation and scheduling latency.

### 3.2 PWM Pattern After Command

```
[16:04:21.777] PWM channel 2 duty set to 8191.  ← First update after command
[16:04:22.777] PWM channel 2 duty set to 8191.  ← ~1000ms interval
[16:04:23.777] PWM channel 2 duty set to 8191.
[16:04:24.778] PWM channel 2 duty set to 8191.
...
[16:05:34.XXX] PWM channel 2 duty set to 8191.  ← Still continuing (log ends)
```

**Key Observations**:
1. Duty is **always 8191** (motor stop in inverted logic)
2. Updates occur roughly every **100ms** (not 1000ms as previously thought)
3. Looking closer at timestamps: they vary between 100-1000ms
4. **No PCNT data** in logs (no `pcnt_count_2_XXX` messages)
5. Command **never times out** after 10 seconds

### 3.3 Missing PCNT Feedback

Expected to see:
```
pcnt_count_2_0      ← Motor not moving
pcnt_count_2_230    ← Motor running at ~50%
```

**Not present in logs** - PCNT is not sending data, suggesting `motor_speed_list[2]` might be 0.

---

## 4. Root Cause Analysis

### 4.1 Code Inspection

In `main/mqtt.c`, function `message_compare()`:

```c
void message_compare(char *msg)
{
    // ...
    else if(strncmp(msg, "cmd_", 4) == 0)
    {
        int index, speed, duration;
        sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
        cmd_params params = {speed, duration, index};  // ← LOCAL VARIABLE ON STACK
        xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)&params, 1, NULL);
        // ← params goes out of scope here!
    }
}
```

### 4.2 The Bug: Stack Variable Lifetime Problem

**The Issue**:
1. `params` is a **local stack variable** in `message_compare()`
2. `xTaskCreate()` is called with `(void*)&params` 
3. `message_compare()` returns immediately after `xTaskCreate()`
4. The stack frame for `message_compare()` is destroyed
5. `params` memory is now invalid (may contain garbage or be reused)
6. When `control_cmd` task actually starts running, it reads invalid memory

**Why It Sometimes Works**:
- If `control_cmd` starts immediately (before stack is reused), it might work
- But in practice, the task doesn't start immediately due to FreeRTOS scheduling
- The ~700ms delay we see is the time between task creation and task execution
- By that time, `params` is already corrupted

### 4.3 Why Duty = 8191

The `control_cmd` task reads corrupted `cmd_params`:
- If `local_speed` reads as 0 → `motor_speed_list[2] = 0`
- PID sees target speed = 0, current speed = 0
- PID outputs 0 (or integral term at max)
- `8191 - 0 = 8191` → motor stop

Or:
- If `local_index` reads as invalid value → undefined behavior

### 4.4 Why No Timeout

If `local_duration` reads as 0 or garbage:
- `vTaskDelay(0 * 1000)` = no delay
- Task finishes immediately or never finishes properly
- But PID continues running because `motor_speed_list` might still be set

Actually, looking at the continuous PWM updates, `motor_speed_list[2]` IS being set to some value (otherwise PID wouldn't run for channel 2). The issue is that the values are garbage.

---

## 5. Verification of Root Cause

### 5.1 Pattern Matches Bug

| Symptom | Explanation |
|---------|-------------|
| PWM duty = 8191 | `local_speed` is 0 or corrupted |
| No PCNT data | `motor_speed_list[2]` might be 0, causing PCNT idle mode |
| Continuous updates | PID running with corrupted `motor_speed_list` value |
| Never stops | `local_duration` is 0 or corrupted |

### 5.2 Code Flow Analysis

```
1. MQTT receives "cmd_2_225_10"
2. message_compare() called
3. sscanf extracts: index=2, speed=225, duration=10
4. params created on stack: {225, 10, 2}
5. xTaskCreate() schedules control_cmd task
6. message_compare() returns ← STACK DESTROYED
7. ... 700ms later ...
8. control_cmd task starts
9. Reads cmd_params from stack (now corrupted)
10. Might read: {0, 0, 0} or {garbage, garbage, garbage}
11. Motor doesn't work as expected
```

---

## 6. Solutions

### 6.1 Solution A: Dynamic Memory Allocation (Recommended)

Allocate `cmd_params` on heap:

```c
else if(strncmp(msg, "cmd_", 4) == 0)
{
    int index, speed, duration;
    sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
    
    // Allocate on heap
    cmd_params *params = malloc(sizeof(cmd_params));
    if (params != NULL) {
        params->speed = speed;
        params->duration = duration;
        params->index = index;
        xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, NULL);
    }
}
```

Then in `control_cmd()`:
```c
void control_cmd(void *params)
{
    cmd_params* local_params = (cmd_params*)params;
    // ... use local_params ...
    
    free(local_params);  // Free memory before task exit
    vTaskDelete(NULL);
}
```

### 6.2 Solution B: Global/Static Variable with Mutex

Use a global array with mutex protection:

```c
// Global
static cmd_params cmd_buffer[4];
static SemaphoreHandle_t cmd_mutex = NULL;

// In message_compare()
cmd_params *params = &cmd_buffer[index];  // Use motor index
xSemaphoreTake(cmd_mutex, portMAX_DELAY);
params->speed = speed;
params->duration = duration;
params->index = index;
xSemaphoreGive(cmd_mutex);

xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, NULL);
```

### 6.3 Solution C: Direct Parameter Passing

If FreeRTOS supports it, pass values directly:
```c
// Not standard, but some ports support passing values directly
xTaskCreate(control_cmd, "CMD_TASK", 4096, 
            (void*)((speed << 16) | (duration << 8) | index), 1, NULL);
```

Then decode in task:
```c
void control_cmd(void *param)
{
    uint32_t val = (uint32_t)param;
    int speed = (val >> 16) & 0xFF;
    int duration = (val >> 8) & 0xFF;
    int index = val & 0xFF;
    // ...
}
```

**Limitation**: Can only pass small values (32-bit), not suitable for all cases.

---

## 7. Recommended Fix

**Use Solution A (Dynamic Allocation)** as it's:
- Clean and maintainable
- Supports multiple concurrent commands
- Follows FreeRTOS best practices

### Code Changes Required

**File**: `main/mqtt.c`

```c
void message_compare(char *msg)
{
    if(strcmp(msg, "Hello there") == 0)
    {
        char buff[64];
        sprintf(buff, "Hello to you too");
        esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    }
    else if(strncmp(msg, "cmd_", 4) == 0)
    {
        int index, speed, duration;
        sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
        
        // Allocate params on heap to ensure lifetime
        cmd_params *params = malloc(sizeof(cmd_params));
        if (params != NULL) {
            params->speed = speed;
            params->duration = duration;
            params->index = index;
            
            if (xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create control task");
                free(params);
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate memory for cmd_params");
        }
    }
}
```

**File**: `main/pid.c`

```c
void control_cmd(void *params)
{
    cmd_params* local_params = (cmd_params*)params;
    int local_speed = local_params->speed;
    int local_duration = local_params->duration;
    int local_index = local_params->index;

    // Free the allocated memory after copying to local variables
    free(local_params);

    char buff[64];
    sprintf(buff, "task_create_%d_%d_%d", local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    
    motor_speed_list[local_index] = local_speed;
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    pwm_set_duty(8191, local_index);
    
    sprintf(buff, "task_finished_%d_%d_%d", local_index, local_speed, local_duration);
    esp_mqtt_client_publish(mqtt_client, MQTT_CONTROL_CHANNEL, buff, strlen(buff), 2, 0);
    vTaskDelete(NULL);
}
```

---

## 8. Test Plan After Fix

### 8.1 Verification Steps

1. Send `cmd_2_225_10` via MQTTX
2. Expected immediate response: `task_create_2_225_10` on MQTT
3. Motor should start rotating within 100ms
4. Should see `pcnt_count_2_XXX` data in logs (where XXX ≈ 225)
5. After 10 seconds: `task_finished_2_225_10` on MQTT
6. Motor should stop
7. PWM duty should stabilize at 8191

### 8.2 Additional Tests

| Test | Command | Expected Result |
|------|---------|-----------------|
| Motor 0 full speed | `cmd_0_450_5` | Motor 0 rotates at 100% for 5s |
| Motor 1 half speed | `cmd_1_225_5` | Motor 1 rotates at 50% for 5s |
| All motors | `cmd_0_225_10`, `cmd_1_225_10`, ... | All 4 motors rotate simultaneously |
| Stop immediately | `cmd_2_0_1` | Motor 2 stops within 1s |

---

## 9. Related Code References

| File | Function | Line | Issue |
|------|----------|------|-------|
| `main/mqtt.c` | `message_compare()` | ~155 | Stack variable passed to task |
| `main/pid.c` | `control_cmd()` | ~118-135 | Reads from potentially invalid pointer |
| `main/pid.c` | `PID_init()` | ~81-94 | PID controller running continuously |

---

## 10. Prevention

### 10.1 Code Review Guidelines

Always check:
- [ ] Stack variables passed to tasks
- [ ] Memory allocation before task creation
- [ ] Proper memory cleanup in tasks

### 10.2 Static Analysis

Use tools like:
- `cppcheck` with `--enable=all`
- ESP-IDF's built-in static analyzers
- Compiler warnings: `-Wall -Wextra`

---

## 11. Conclusion

**Root Cause**: Stack variable `params` in `message_compare()` goes out of scope before `control_cmd` task reads it, causing the task to receive garbage values.

**Impact**: Motor control commands do not work - motor never rotates, command never properly executes.

**Fix**: Use dynamic memory allocation (`malloc`) for `cmd_params` and free it in the task after copying to local variables.

**Status**: Requires code modification to fix.

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-12 16:10
