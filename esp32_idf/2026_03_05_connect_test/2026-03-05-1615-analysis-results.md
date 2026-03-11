# ESP32-S3 MQTT Connection Stability Analysis Report

**Analysis Date**: 2026-03-05  
**Log File**: `esp32_log_20260305_161517.txt`  
**Test Duration**: ~33.32 minutes  
**Device Boot Time**: 2026-03-05 16:15:17

---

## 1. Executive Summary

This report analyzes a 33-minute continuous operation log of the ESP32-S3 motor control IoT device. The device experienced **severe MQTT connection instability** with multiple heartbeat message bursts indicating potential task blocking issues.

| Metric | Value |
|--------|-------|
| Total Connections | 9 |
| Total Disconnections | 25 |
| Connection Retention Rate | 36.00% |
| TCP_TRANSPORT_ERROR Count | 60 |
| PING_OR_UNKNOWN_ERROR Count | 9 |
| WiFi Connection | Stable (IP: 192.168.110.180, RSSI: -69dBm) |
| NTP Time Sync | Successful |

---

## 2. Critical Issue: Heartbeat Message Burst

### 2.1 Problem Description

Multiple instances of **heartbeat message bursts** were observed where several heartbeat messages were logged in rapid succession (within milliseconds), violating the expected 30-second interval.

### 2.2 Observed Burst Events

| Timestamp | Burst Count | msg_id Sequence | Interval from Previous Normal Heartbeat |
|-----------|-------------|-----------------|-----------------------------------------|
| 16:36:28 | 8 messages | 15191, 38542, 50889, 48995, 23239, 62833, 56062, 24086, 63831 | ~4.5 min (abnormal) |
| 16:38:45 | 3 messages | 56669, 13695, 42240 | ~1.7 min (abnormal) |
| 16:40:29 | 2 messages | 45634, 54870 | ~1.9 min (abnormal) |
| 16:44:22 | 2 messages | 41210, 62881 | ~5.3 min (abnormal) |
| 16:47:29 | 3 messages | 62528, 40112, 4822 | ~5.8 min (abnormal) |

### 2.3 Root Cause Analysis

The heartbeat burst pattern indicates the `mqtt_heartbeat_task` was **blocked or suspended** during the interval, and upon resuming, it appears that:

1. **MQTT Client Internal Queue Accumulation**: The `esp_mqtt_client_publish()` function with QoS=1 may have queued messages internally during network congestion
2. **Task Blocking**: The heartbeat task may have been blocked by mutex contention or network operations
3. **Log Buffer Flush**: Serial output buffering may have delayed log display

### 2.4 Key Evidence

```
Normal pattern (30s interval):
16:26:33 msg_id=51379
16:27:03 msg_id=32388 (30s later)
16:27:33 msg_id=19554 (30s later)

Burst pattern (abnormal):
16:31:33 msg_id=43820 (normal)
16:36:28 msg_id=15191 (4.5 min gap - then burst of 8 messages)
```

---

## 3. Connection Stability Analysis

### 3.1 Disconnection Timeline

| Phase | Time Range | Disconnections | Avg Connection Duration |
|-------|------------|----------------|------------------------|
| Initial Stable | 16:15 - 16:30 | 1 | 15 min |
| Critical Period | 16:36 - 16:44 | 11 | < 2 min |
| Recovery | 16:44 - 16:48 | 2 | ~3 min |

### 3.2 Error Type Distribution

```
TCP Transport Errors: 60 occurrences (87.0%)
- select() timeout: 22+
- Connection reset by peer: Multiple
- Failed to open connection: Multiple

Ping/Protocol Errors: 9 occurrences (13.0%)
- No PING_RESP: 9
```

### 3.3 Critical Event Sequence

```
16:36:59 - PING timeout disconnect
16:38:45 - TCP connection reset
16:38:58 - TCP transport error (select timeout)
16:39:11 - TCP transport error cascade
16:40:29 - PING timeout disconnect
16:41:46 - PING timeout disconnect
16:42:19-16:43:16 - Multiple connection failures
```

---

## 4. Network Environment Analysis

### 4.1 WiFi Signal Quality

| Metric | Value | Assessment |
|--------|-------|------------|
| RSSI | -69 dBm | Moderate (borderline weak) |
| Security | WPA2-PSK | OK |
| Channel | 1 | OK |
| PHY | bgn | OK |

### 4.2 Network Path

```
ESP32-S3 (192.168.110.180) 
    -> WiFi AP (WeShare-6148)
    -> VMware NAT Gateway
    -> EMQX Broker (192.168.110.31:1883)
```

**Assessment**: VMware NAT and moderate WiFi signal contribute to TCP transport instability.

---

## 5. Recommendations

### 5.1 Immediate Actions

1. **Optimize MQTT Keepalive Settings**
   ```c
   // Reduce keepalive interval for faster detection
   .session.keepalive = 30  // from 60s
   ```

2. **Add Heartbeat Send Timeout**
   ```c
   // Monitor heartbeat publish time to detect blocking
   uint32_t start_tick = xTaskGetTickCount();
   int msg_id = esp_mqtt_client_publish(...);
   if (xTaskGetTickCount() - start_tick > pdMS_TO_TICKS(5000)) {
       ESP_LOGW(TAG, "Heartbeat publish blocked for >5s");
   }
   ```

3. **Implement Connection Quality Metrics**
   - Track actual publish latency
   - Monitor task execution timing

### 5.2 Long-term Improvements

1. **Network Infrastructure**
   - Move ESP32 closer to router (improve RSSI above -65dBm)
   - Consider direct network connection vs VMware NAT

2. **MQTT Configuration**
   - Enable TCP keepalive at socket level
   - Adjust buffer sizes based on network quality
   - Consider QoS 0 for heartbeat to reduce ACK dependency

3. **Diagnostic Enhancements**
   - Add task watchdog monitoring
   - Log system tick count with each heartbeat
   - Monitor free heap and task stack usage

---

## 6. Conclusion

The test reveals **critical MQTT connection instability** with only 36% connection retention rate. The heartbeat burst phenomenon suggests task blocking or MQTT client internal queue accumulation during network congestion.

**Priority Actions**:
1. Address WiFi signal strength (currently -69dBm)
2. Optimize MQTT keepalive and timeout settings
3. Investigate MQTT client publish blocking behavior
4. Consider bypassing VMware NAT for direct network access

**Next Steps**:
- Implement heartbeat timing diagnostics
- Test with improved physical positioning
- Monitor task execution latency

---

**Report Generated**: 2026-03-05  
**Log Duration**: 33.32 minutes  
**Total Log Lines**: 515
