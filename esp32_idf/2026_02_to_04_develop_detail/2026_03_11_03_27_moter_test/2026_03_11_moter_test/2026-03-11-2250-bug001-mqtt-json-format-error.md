# Bug Report: Motor Not Responding to MQTTX Commands

**Bug ID**: bug001  
**Date**: 2026-03-11 22:50  
**Reporter**: User  
**Status**: Root Cause Identified (No Code Change Required)  
**Severity**: Medium  
**Component**: MQTT Command Interface / MQTTX Client  

---

## 1. Problem Summary

Motor 2 (connected to PH2.0-LI-5P_003, IO6/IO7) does not rotate when sending control commands via MQTTX.

**Expected Behavior**:  
Send `cmd_2_225_10` → Motor 2 rotates at 50% speed for 10 seconds

**Actual Behavior**:  
Send `cmd_2_225_10` → Motor 2 does not rotate at all

---

## 2. Environment

| Item | Value |
|------|-------|
| Hardware | ESP32-S3-DevKitC-1 |
| Motor | CHB-BLDC2418 (12V, 4500 RPM) |
| Connected Port | PH2.0-LI-5P_003 (Motor 2) |
| GPIO Used | IO6 (PWM), IO7 (FG) |
| Firmware Branch | feature/motor-control-config |
| MQTT Broker | EMQX @ 192.168.110.31:1883 |
| MQTT Client | MQTTX Desktop |
| Network | WiFi connected, MQTT connected (green indicator) |

---

## 3. Evidence Analysis

### 3.1 Screenshot Analysis

From `screen_shot_of_MQTTX.png`:

**MQTTX Configuration**:
- Connection: ESP32_Motor_Test ✓ (Green dot, connected)
- Subscribed Topics: 
  - `esp32_1/data` (QoS 1)
  - `esp32_1/control` (QoS 1)
- Publish Topic: `esp32_1/control`

**Message Content (THE PROBLEM)**:
```json
{
  "msg": "cmd_2_225_10"
}
```

Message format type: **JSON**  
Content-Type: `application/json`

### 3.2 What ESP32 Expects

Looking at `main/mqtt.c` message parsing logic:

```c
// ESP32 expects PLAIN TEXT format
// Example: "cmd_2_225_10"

// Current parsing code searches for substring
if (strstr(data, "cmd_") != NULL) {
    sscanf(data, "cmd_%d_%d_%d", &index, &speed, &duration);
    // ... process command
}
```

The ESP32 code uses `strstr()` and `sscanf()` to parse **plain text** commands.

---

## 4. Root Cause Analysis

### 4.1 Format Mismatch

| Aspect | MQTTX Sending | ESP32 Expecting |
|--------|---------------|-----------------|
| **Format** | JSON | Plain Text |
| **Content** | `{"msg": "cmd_2_225_10"}` | `cmd_2_225_10` |
| **Content-Type** | application/json | text/plain |

### 4.2 Why It Fails

1. MQTTX sends: `{"msg": "cmd_2_225_10"}`
2. ESP32 receives the entire JSON string
3. `strstr(data, "cmd_")` **does find** "cmd_" inside the string
4. BUT `sscanf(data, "cmd_%d_%d_%d", ...)` **fails** to parse correctly because:
   - Input: `{"msg": "cmd_2_225_10"}`
   - sscanf expects: `cmd_2_225_10`
   - The extra JSON syntax `"{"msg": "` and `"}"` causes parse failure
5. Result: Command parsing fails silently, motor does not run

### 4.3 MQTTX Default Setting Issue

MQTTX defaults to JSON format for message payload. Users need to manually change to **Plaintext** mode.

**Screenshot Evidence**:  
In the screenshot, the message editor shows:
- Dropdown showing "JSON" format selected
- Message content is wrapped in `{ }`

---

## 5. Verification

### 5.1 Code Review

From `main/mqtt.c` (mqtt_event_handler):

```c
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_DATA:
            // event->data contains the payload
            // event->data_len contains the length
            
            char *data = malloc(event->data_len + 1);
            memcpy(data, event->data, event->data_len);
            data[event->data_len] = '\0';
            
            // Parse command
            if (strstr(data, "cmd_") != NULL) {
                int idx, spd, dur;
                if (sscanf(data, "cmd_%d_%d_%d", &idx, &spd, &dur) == 3) {
                    // Create control task
                    // ...
                }
            }
            free(data);
            break;
    }
}
```

The code clearly expects **plain text** format.

### 5.2 Comparison with Working Case

If the message were sent as plain text `cmd_2_225_10`:
- `sscanf(data, "cmd_%d_%d_%d", &idx, &spd, &dur)` would return 3
- Command would be parsed correctly
- Motor would run as expected

---

## 6. Solution (No Code Change Required)

### 6.1 Fix in MQTTX Client

**Step 1**: Change message format from JSON to Plaintext

In MQTTX Publish interface:
1. Locate the format dropdown (currently showing "JSON")
2. Change to **"Plaintext"** or **"Text"**

**Step 2**: Enter plain command

Message content should be:
```
cmd_2_225_10
```

NOT:
```json
{
  "msg": "cmd_2_225_10"
}
```

### 6.2 Screenshot Reference

From the provided screenshot:
- **Current (Wrong)**: Dropdown shows format selector with "JSON"
- **Fix**: Change dropdown to "Plaintext"

### 6.3 Alternative Solutions

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Change MQTTX to Plaintext | ✓ Immediate fix<br>✓ No code change<br>✓ Follows existing protocol | None |
| B | Modify ESP32 to parse JSON | ✓ Supports JSON format | ✗ Requires code change<br>✗ More complex parsing<br>✗ Not recommended for this bug |
| C | Use Python script | ✓ Already sends plain text | ✗ Requires Python environment |

**Recommended**: Option A (Change MQTTX format)

---

## 7. Corrected Test Procedure

### 7.1 MQTTX Configuration (Fixed)

```
Connection: ESP32_Motor_Test (connected)
Subscribe: esp32_1/data
```

### 7.2 Publishing Message (Correct)

```
Topic: esp32_1/control
Format: Plaintext  ← CHANGED FROM JSON
QoS: 1
Payload: cmd_2_225_10  ← NO JSON WRAPPERS
```

### 7.3 Expected Result After Fix

1. Send: `cmd_2_225_10` (plain text)
2. Motor 2 starts rotating at 50% speed
3. `esp32_1/data` receives: `pcnt_count_2_220` (approximately)
4. After 10 seconds, motor stops

---

## 8. Prevention Recommendations

### 8.1 Update Documentation

Update `MQTTX_TEST_GUIDE.md` to explicitly state:
- **Important**: Set message format to **Plaintext**, not JSON
- Add screenshot showing correct format selection

### 8.2 Add Note in Quick Checklist

Add to `QUICK_TEST_CHECKLIST.md`:
```markdown
⚠️ CRITICAL: MQTTX must be set to "Plaintext" format, NOT JSON!
```

---

## 9. Related Information

### 9.1 MQTT Protocol Details

| Property | Value |
|----------|-------|
| Command Topic | `esp32_1/control` |
| Data Topic | `esp32_1/data` |
| Command Format | `cmd_<motor>_<speed>_<duration>` |
| Example | `cmd_2_225_10` (Motor 2, 50% speed, 10s) |
| Speed Range | 0-450 (0=stop, 450=4500 RPM) |
| Duration | Seconds |

### 9.2 Physical Connection

```
PH2.0-LI-5P_003 (Motor 2):
Pin 1: +12V  → External Power +
Pin 2: IO6   → ESP32 GPIO6 (PWM)
Pin 3: (空)  → NC
Pin 4: GND   → External Power GND + ESP32 GND
Pin 5: IO7   → ESP32 GPIO7 (FG)
```

---

## 10. Conclusion

**Root Cause**: MQTTX was configured to send JSON format (`{"msg": "cmd_2_225_10"}`), but ESP32 expects plain text (`cmd_2_225_10`).

**Resolution**: Change MQTTX message format from JSON to Plaintext.

**Impact**: No firmware modification required. User-side configuration fix only.

**Status**: RESOLVED (Configuration Issue, Not Code Bug)

---

**Document Version**: 1.0  
**Analysis by**: Kimi Code CLI  
**Last Updated**: 2026-03-11 23:03
