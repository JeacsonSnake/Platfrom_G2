# ESP32_IDF Project Development To-Do List (v2 - MVP)
## Django Backend Integration - Core Features Only

**Project:** ESP32 Motor Control System  
**Date:** 2026-02-06  
**Scope:** Phase 1-2 + Phase 4.1 ONLY (Excluding Security & OTA)  
**Purpose:** Wi-Fi STA Mode + MQTT Integration with Django Backend (via EMQX Broker)

---

## Executive Summary

| Component | Current Status | Target Status | Priority |
|-----------|---------------|---------------|----------|
| Wi-Fi STA Mode | ✅ Functional | ✅ Production Ready | Medium |
| MQTT Connection | ✅ Basic | ✅ Production Ready | High |
| Django Protocol | ❌ String-based | JSON API Required | **Critical** |
| Configuration | ❌ Hardcoded | NVS Storage | **Critical** |
| Auto-Reconnection | ❌ Missing | Exponential Backoff | **Critical** |
| **Security (TLS/SSL)** | ❌ None | **POSTPONED** | - |
| **OTA Updates** | ❌ None | **POSTPONED** | - |

---

## ✅ IN SCOPE

### Phase 1: Configuration Management (CRITICAL)

#### 1.1 WiFi Configuration Storage
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 2-3 hours  
**Dependencies:** None

- [ ] Implement NVS storage for WiFi credentials
- [ ] Create `wifi_config_load()` function to read from NVS
- [ ] Create `wifi_config_save()` function to write to NVS
- [ ] Add fallback to default credentials if NVS empty
- [ ] Add WiFi configuration via UART/Serial commands (simple text-based)

**Files to Modify:**
- `main/wifi.c` - Add NVS read/write functions
- `main/main.h` - Add config structures

**Configuration Structure:**
```c
typedef struct {
    char ssid[32];
    char password[64];
} wifi_config_storage_t;

// Read WiFi config from NVS
esp_err_t wifi_config_load(wifi_config_t *config);
// Save WiFi config to NVS  
esp_err_t wifi_config_save(const wifi_config_t *config);
```

**Serial Command Interface:**
```
SET_WIFI_SSID MyNetwork
SET_WIFI_PASS MyPassword
SAVE_WIFI_CONFIG
```

---

#### 1.2 MQTT Configuration Storage
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 2-3 hours  
**Dependencies:** 1.1

- [ ] Store MQTT broker IP/hostname in NVS
- [ ] Store MQTT port (default: 1883 - plain TCP, no TLS)
- [ ] Store MQTT credentials (username/password) in NVS
- [ ] Store device ID / client ID in NVS
- [ ] Create config validation functions
- [ ] Add MQTT configuration via UART/Serial commands

**Files to Modify:**
- `main/mqtt.c` - Load config from NVS on init
- `main/main.h` - Define MQTT config structure

**Configuration Structure:**
```c
typedef struct {
    char broker_uri[64];      // mqtt://192.168.31.74 (NO mqtts://)
    uint16_t port;            // 1883 only (NO 8883 for now)
    char client_id[32];       // esp32_1
    char username[32];        // ESP32_1
    char password[64];        // 123456
    char topic_prefix[32];    // esp32_1
} mqtt_config_t;
```

**⚠️ NOTE:** TLS/SSL certificates NOT included in this scope. Using plain MQTT only.

---

#### 1.3 Device Identity Management
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 1-2 hours  
**Dependencies:** 1.2

- [ ] Generate unique device ID from MAC address
- [ ] Store device ID persistently in NVS
- [ ] Use device ID for MQTT client ID and topic naming
- [ ] Add device model/firmware version constants
- [ ] Create automatic topic naming: `{device_id}/control`, `{device_id}/data`

**Implementation:**
```c
// Get device ID from MAC address (esp32_a1b2c3d4 format)
esp_err_t device_get_id(char *device_id, size_t len);

// Generate topics dynamically
void mqtt_get_topic(char *buffer, size_t len, const char *suffix);
// Result: "esp32_a1b2c3d4/control"
```

---

### Phase 2: Django Protocol Implementation (CRITICAL)

#### 2.1 JSON Command Parser
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 3-4 hours  
**Dependencies:** None

- [ ] Replace string parsing (`cmd_0_200_10`) with JSON parsing
- [ ] Use `cJSON` library (already included in ESP-IDF)
- [ ] Define command schema for all motor operations
- [ ] Implement JSON validation with error reporting
- [ ] Add error handling for malformed JSON

**Current (Legacy) - TO BE REMOVED:**
```c
// OLD: String parsing in mqtt.c
sscanf(msg, "cmd_%d_%d_%d", &index, &speed, &duration);
```

**Target (JSON):**
```c
// NEW: JSON parsing
cJSON *root = cJSON_Parse(msg);
cJSON *command = cJSON_GetObjectItem(root, "command");
cJSON *params = cJSON_GetObjectItem(root, "params");
```

**Files to Modify:**
- `main/mqtt.c` - Replace `message_compare()` with JSON handler
- `main/main.h` - Define command structures

---

#### 2.2 Command Protocol Definition
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 2-3 hours  
**Dependencies:** 2.1

- [ ] Define `set_speed` command schema
- [ ] Define `stop` command schema  
- [ ] Define `stop_all` command schema
- [ ] Define `get_status` command schema
- [ ] Define `set_pid_params` command schema (optional for MVP)
- [ ] Define `emergency_stop` command
- [ ] Document all commands with examples

**Command Schema Examples:**

**set_speed:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2026-02-06T16:50:47Z",
  "command": "set_speed",
  "params": {
    "motor_index": 0,
    "target_speed": 200,
    "duration_ms": 10000,
    "use_pid": true
  }
}
```

**stop:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440001",
  "command": "stop",
  "params": {
    "motor_index": 0
  }
}
```

**get_status:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440002",
  "command": "get_status"
}
```

**emergency_stop:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440003",
  "command": "emergency_stop"
}
```

---

#### 2.3 Response Protocol Implementation
**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 3-4 hours  
**Dependencies:** 2.2

- [ ] Implement JSON response builder utility
- [ ] Include `request_id` in all responses for correlation
- [ ] Add status field (`success` / `error`)
- [ ] Define error codes enum
- [ ] Add error messages
- [ ] Publish to dedicated response topic: `{device_id}/response`

**Success Response Schema:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2026-02-06T16:50:48Z",
  "status": "success",
  "command": "set_speed",
  "data": {
    "motor_index": 0,
    "target_speed": 200,
    "actual_speed": 0,
    "state": "accelerating"
  }
}
```

**Error Response Schema:**
```json
{
  "request_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2026-02-06T16:50:48Z",
  "status": "error",
  "command": "set_speed",
  "error": {
    "code": "INVALID_MOTOR_INDEX",
    "message": "Motor index must be between 0 and 3"
  }
}
```

**Error Codes to Define:**
```c
typedef enum {
    ERR_SUCCESS = 0,
    ERR_INVALID_JSON,
    ERR_INVALID_COMMAND,
    ERR_INVALID_MOTOR_INDEX,
    ERR_INVALID_SPEED,
    ERR_INVALID_DURATION,
    ERR_MOTOR_BUSY,
    ERR_INTERNAL_ERROR
} error_code_t;
```

**Files to Modify:**
- `main/mqtt.c` - Add response publishing functions
- `main/pid.c` - Update `control_cmd()` to send JSON responses
- `main/main.h` - Add error codes and response structures

---

#### 2.4 Device Status Reporting
**Priority:** 🟡 HIGH  
**Estimated Effort:** 2-3 hours  
**Dependencies:** 2.3

- [ ] Create comprehensive status JSON structure
- [ ] Include system info (IP address, WiFi RSSI, uptime, free heap)
- [ ] Include motor states (target speed, actual speed, PWM duty, running state)
- [ ] Include PID state (if running, Kp/Ki/Kd values)
- [ ] Publish on `{device_id}/status` topic
- [ ] Trigger status publish on request via `get_status` command
- [ ] Optional: Periodic status publish (every 30 seconds)

**Status Report Schema:**
```json
{
  "device_id": "esp32_a1b2c3d4",
  "timestamp": "2026-02-06T16:50:47Z",
  "system": {
    "firmware_version": "1.0.0",
    "uptime_seconds": 3600,
    "free_heap": 123456,
    "wifi_rssi": -45,
    "ip_address": "192.168.31.100"
  },
  "motors": [
    {
      "index": 0,
      "state": "running",
      "target_speed": 200,
      "actual_speed": 198,
      "pwm_duty": 4000,
      "pid_enabled": true
    },
    {
      "index": 1,
      "state": "idle",
      "target_speed": 0,
      "actual_speed": 0,
      "pwm_duty": 8192,
      "pid_enabled": false
    },
    {
      "index": 2,
      "state": "idle",
      "target_speed": 0,
      "actual_speed": 0,
      "pwm_duty": 8192,
      "pid_enabled": false
    },
    {
      "index": 3,
      "state": "idle",
      "target_speed": 0,
      "actual_speed": 0,
      "pwm_duty": 8192,
      "pid_enabled": false
    }
  ]
}
```

**Files to Modify:**
- `main/mqtt.c` - Add status publishing function
- `main/main.h` - Define status structures

---

### Phase 4.1: MQTT Auto-Reconnection (CRITICAL)

**Priority:** 🔴 CRITICAL  
**Estimated Effort:** 2 hours  
**Dependencies:** Phase 1, Phase 2

- [ ] Implement connection state monitoring variable
- [ ] Add exponential backoff for reconnection attempts (1s, 2s, 4s, 8s, max 60s)
- [ ] Re-subscribe to all topics after successful reconnection
- [ ] Queue critical messages during disconnection (optional: simple ring buffer)
- [ ] Publish "online" status on reconnect
- [ ] Add connection state to status reports

**Implementation Details:**

**Connection State Management:**
```c
typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED
} mqtt_connection_state_t;

extern mqtt_connection_state_t mqtt_state;
extern uint32_t reconnect_attempt;
```

**Exponential Backoff:**
```c
// In mqtt_event_handler()
case MQTT_EVENT_DISCONNECTED:
    mqtt_state = MQTT_STATE_DISCONNECTED;
    reconnect_attempt++;
    uint32_t delay_ms = (1 << reconnect_attempt) * 1000; // 1s, 2s, 4s...
    if (delay_ms > 60000) delay_ms = 60000; // Max 60s
    esp_mqtt_client_reconnect(client);
    break;
    
case MQTT_EVENT_CONNECTED:
    mqtt_state = MQTT_STATE_CONNECTED;
    reconnect_attempt = 0;
    // Re-subscribe to topics
    esp_mqtt_client_subscribe(client, topic_control, 2);
    esp_mqtt_client_subscribe(client, topic_config, 2);
    // Publish online status
    mqtt_publish_status("online");
    break;
```

**Files to Modify:**
- `main/mqtt.c` - Enhance `mqtt_event_handler()` with reconnection logic

---

## ❌ OUT OF SCOPE (POSTPONED)

### Phase 3: Security Enhancement (POSTPONED)
The following features are **NOT included** in this version and will be addressed in future releases:

- ❌ MQTT TLS/SSL Support (`mqtts://` on port 8883)
- ❌ CA certificate validation
- ❌ Client certificate authentication
- ❌ Certificate storage in NVS/SPIFFS
- ❌ JWT token authentication for EMQX
- ❌ Encrypted NVS for credential storage
- ❌ Credential rotation mechanism

**Note:** Current implementation uses **plain TCP MQTT** (port 1883) only. Ensure EMQX broker accepts non-TLS connections.

---

### Phase 4.3-4.4: Advanced Features (POSTPONED)
The following features are **NOT included** in this version:

- ❌ OTA Firmware Updates
- ❌ Firmware signature verification
- ❌ Rollback on failure
- ❌ Batch command support (multiple motors in one command)
- ❌ Synchronized motor control
- ❌ Sequence command support (delayed execution)

---

### Phase 5: Monitoring & Diagnostics (POSTPONED)
The following features are **NOT included** in this version:

- ❌ Structured logging (JSON format)
- ❌ Log level control via MQTT
- ❌ Remote log streaming
- ❌ Ping command for latency test
- ❌ Get config command to dump current config
- ❌ Remote reset command
- ❌ Factory reset command

---

## File Modification Summary

| File | Phase 1 | Phase 2 | Phase 4.1 | Description |
|------|---------|---------|-----------|-------------|
| `main/main.h` | ✅ | ✅ | ✅ | Add config structures, error codes, function declarations |
| `main/main.c` | ✅ | - | - | Minor: init order adjustments |
| `main/wifi.c` | ✅ | - | - | NVS config load/save, serial commands |
| `main/mqtt.c` | ✅ | ✅ | ✅ | JSON protocol, reconnection, responses |
| `main/pid.c` | - | ✅ | - | Update to send JSON responses |
| `main/pwm.c` | - | - | - | No changes required |
| `main/pcnt.c` | - | - | - | No changes required |
| `sdkconfig` | ✅ | - | - | Ensure cJSON is enabled |

---

## Testing Checklist

### Unit Tests
- [ ] NVS read/write operations for WiFi config
- [ ] NVS read/write operations for MQTT config
- [ ] JSON parsing - valid commands
- [ ] JSON parsing - malformed commands (error handling)
- [ ] Response generation with request_id correlation
- [ ] Error code to message mapping
- [ ] Device ID generation from MAC

### Integration Tests
- [ ] WiFi connection with NVS-stored credentials
- [ ] WiFi reconnection after signal loss
- [ ] MQTT connection with NVS-stored broker config
- [ ] MQTT reconnection after broker restart
- [ ] Command execution: set_speed with JSON
- [ ] Command execution: stop with JSON
- [ ] Command execution: emergency_stop
- [ ] Status report generation and publishing
- [ ] Request/response correlation (request_id matching)

### Django Integration Tests
- [ ] Device registration flow (manual or auto-discovery)
- [ ] Command publish from Django → ESP32 execution
- [ ] Response receive on Django from ESP32
- [ ] Request/response correlation tracking
- [ ] Status report reception and parsing
- [ ] Concurrent command handling (multiple motors)

---

## Timeline Estimate (MVP v2)

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 1: Configuration | 1-2 days | 1-2 days |
| Phase 2: Django Protocol | 2-3 days | 3-5 days |
| Phase 4.1: Auto-Reconnection | 0.5 day | **3.5-5.5 days** |
| **Testing & Integration** | 1-2 days | **4.5-7.5 days** |

**Total MVP Duration:** 5-8 days (approximately 1 work week)

---

## MQTT Topic Structure

| Topic Pattern | Direction | Purpose |
|--------------|-----------|---------|
| `{device_id}/control` | ESP32 ← Django | Receive commands |
| `{device_id}/response` | ESP32 → Django | Command responses |
| `{device_id}/status` | ESP32 → Django | Device status reports |
| `{device_id}/data` | ESP32 → Django | Real-time data (PWM/PCNT updates) |
| `{device_id}/heartbeat` | ESP32 → Django | Keepalive (1s interval) |

**Example with device_id = "esp32_a1b2c3d4":**
- `esp32_a1b2c3d4/control`
- `esp32_a1b2c3d4/response`
- `esp32_a1b2c3d4/status`

---

## Dependencies

### ESP-IDF Components (Already Available)
- `mqtt` - ESP-MQTT client
- `json` - cJSON library
- `nvs_flash` - Non-volatile storage
- `esp_wifi` - WiFi stack

### External Requirements
- EMQX Broker (v4.4+ or v5.x) with **plain MQTT enabled** (port 1883)
- Django + MQTT integration (e.g., `paho-mqtt` Python library)

---

## Current Code Issues to Fix

### Issue 1: PWM Channel Mismatch
**Location:** `main/pwm.c`, line 20  
**Problem:** Loop `for(int i = 0; i < 4; i++)` only initializes 4 channels, but defines exist for 6 channels  
**Fix:** Either change loop to `i < 6` or remove unused channel definitions

### Issue 2: cmd_params Stack Variable
**Location:** `main/mqtt.c`, line 20  
**Problem:** Local struct passed to task by reference may go out of scope  
**Fix:** Use heap allocation or pass by value

```c
// Current (risky):
cmd_params params = {speed, duration, index};
xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)&params, 1, NULL);

// Fixed (heap allocation):
cmd_params *params = malloc(sizeof(cmd_params));
*params = (cmd_params){speed, duration, index};
xTaskCreate(control_cmd, "CMD_TASK", 4096, params, 1, NULL);
// Remember to free() in control_cmd
```

---

## Notes

1. **cJSON Library:** Already included in ESP-IDF (`#include "cJSON.h"` in main.h)
2. **Current MQTT broker:** Hardcoded to `192.168.31.74:1883` - will be moved to NVS
3. **WiFi credentials:** Hardcoded in `main.h` lines 27-28 - will be moved to NVS
4. **Protocol:** Moving from string-based (`cmd_0_200_10`) to JSON-based for Django compatibility
5. **Security:** Using plain MQTT (port 1883) - TLS postponed to future release
6. **Device Discovery:** Manual configuration via serial or hardcoded defaults - auto-discovery postponed

---

**Document Version:** v2.0 (MVP Scope)  
**Last Updated:** 2026-02-06  
**Next Review:** After Phase 2 completion
