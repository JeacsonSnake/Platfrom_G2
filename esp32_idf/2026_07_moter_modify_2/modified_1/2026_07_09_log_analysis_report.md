# ESP32-S3 电机/MQTT 稳定性测试日志分析报告

## 测试信息

- **日志文件**: `esp32_log_20260709_151200.txt`
- **测试时长**: 22.13 分钟（2026-07-09 15:12:00 ~ 15:34:08）
- **固件版本**: 已合并 `cmd_task_handle[4]` 修复（commit `2a27213`）后的 `modified_1`
- **测试命令**: 以电机 2 为主，执行 `cmd_2_70_300`、70→20、20→10、30、50 等长时/变转速任务
- **硬件环境**: ESP32-S3-DevKitC-1 + CHB-BLDC2418，MQTT broker `192.168.110.31:1883`

## 关键现象统计

| 指标 | 数值 | 说明 |
|------|------|------|
| MQTT 连接次数 | 12 | 频繁重连 |
| MQTT 断开次数 | 28 | 连接保持率仅 42.86% |
| PING_OR_UNKNOWN_ERROR | 40 | 主要表现为 broker 未响应 PING 或底层写入阻塞 |
| TCP_TRANSPORT_ERROR | 30 | `select() timeout`（tls_err=32774） |
| 平均单次连接时长 | 约 1~2 分钟 | 最长一次约 18 分钟（15:12:37 ~ 15:30:56） |
| 心跳发送耗时 | 最长 9360 ms | 多次出现 5~9 s，远超正常 0 ms |

## 问题 1：长时间电机运行期间 MQTT 频繁断连

### 根因分析

1. **QoS 2 遥测/通知风暴**
   - `pwm_set_duty()` 每 200 ms 为每个电机发布一次 PWM 变更通知（QoS 2）。
   - `pcnt_monitor()` 每 200 ms 为每个电机发布一次转速（QoS 2）。
   - `control_cmd()` 在任务创建/结束时发布 `task_create_*` / `task_finished_*`（QoS 2）。
   - QoS 2 需要 PUBLISH → PUBREC → PUBREL → PUBCOMP 四次握手，网络和 MQTT 任务负载极高。
   - 长时电机运行（如 300 s）会累积大量 QoS 2 消息，导致 broker/ESP32 处理拥塞，PING 响应被阻塞，最终触发 `PING_OR_UNKNOWN_ERROR`。

2. **激进的自动重连策略**
   - `reconnect_timeout_ms = 3000`：断连后 3 s 立即重试，容易在弱网/VMware NAT 环境下形成重连风暴。
   - `timeout_ms = 10000`：TCP `select()` 10 s 超时后，健康检查任务又立即 `stop/start` 客户端，进一步加剧。
   - `disable_clean_session = false`：会话持久化使 broker 保留未确认的 QoS 消息，重连时可能一次性涌入大量待处理消息。

3. **MQTT 内部任务优先级过高**
   - 优先级 5 的 MQTT 任务在断连/重连期间运行或阻塞时，会抢占优先级 1 的 PID/PCNT/日志任务，间接造成系统响应变慢。

### 修复措施

- 新增 `mqtt_publish_safe()` 辅助函数：未连接时直接跳过，避免调用任务被阻塞。
- PWM/PCNT/任务生命周期通知全部改为 **QoS 0** 非阻塞发布。
- 心跳仍使用 QoS 1，但同样走 `mqtt_publish_safe()`。
- MQTT 配置调整：
  - `keepalive`: 60 s → **120 s**
  - `disable_clean_session`: false → **true**（clean session，避免 broker 积压）
  - `reconnect_timeout_ms`: 3000 → **8000**
  - `timeout_ms`: 10000 → **15000**
  - MQTT 内部任务优先级: 5 → **4**

## 问题 2：MQTT 断连期间日志输出中断

### 根因分析

- 断连期间，`esp_mqtt_client_publish()`（QoS 2）可能被阻塞或等待队列，导致调用它的 PID/PCNT/`control_cmd` 任务被挂起。
- MQTT 内部任务在 TCP `select()` 超时后快速重试，优先级 5 持续占用 CPU，低优先级日志输出任务被挤压。
- 日志中可见 4~10 s 的 PID/PCNT 日志空白区，与 `select() timeout` / `Writing didn't complete in specified timeout` 时间高度吻合。

### 修复措施

- 通过 `mqtt_publish_safe()` 在断连时直接返回，避免 PID/PCNT 任务因发布而挂起。
- 降低 MQTT 任务优先级，减小对日志任务和电机控制任务的抢占。
- 延长重连间隔和 TCP 超时，减少重连风暴期间的 CPU/网络抖动。

## 问题 3：电机高→低转速切换过冲（回归验证）

### 验证结果

日志中可观察到多次目标转速由高到低切换：

| 时间 | 切换 | pid_out | pwm_duty | 结论 |
|------|------|---------|----------|------|
| 15:13:19 | 70 → 0 | 0 | 8191 | 立即停止，无过冲 |
| 15:14:20 | 70 → 20 | 1658 | 6533 | 目标切换后第一拍未出现 8191 或 0 极端值，PID 状态已正确重置 |
| 15:15:02 | 20 → 10 | 628 | 7564 | 平滑过渡 |

- 所有 0→非零 切换均触发 `soft-start reset`，`startup=1` 时 `pid_out` 从较低值开始。
- 高→低切换时，`pid_out` 保持在与新目标相匹配的合理区间，未出现“上一拍高转速残留导致 PWM 瞬间满开”的现象。

**结论**：在 `modified_1` 固件中，电机高→低转速切换过冲问题已得到控制。`cmd_task_handle[4]` 单任务机制 + PID 软启动/状态清零 + 输出变化率限制共同生效。

## 仍存在的非软件问题

1. **电机未转动但 PCNT 读数异常**
   - 测试初期 `cmd_2_70_300` 启动后，电机实际转速为 0，但 PCNT 持续读到 1500~2000 的噪声脉冲。
   - 可能原因：电机堵转/FG 线受 PWM/电源噪声干扰、12 V 电源不稳、硬件接线问题。
   - 建议：检查电机驱动接线、FG 信号线屏蔽/走线、电源纹波。

2. **PCNT 诊断为高比例 0 采样**
   - 运行期间多次出现 `PCNT诊断: 82%~85%采样为0`。
   - 在低速目标（10~30/s）时，FG 信号稀疏属于正常现象；但结合异常大值，建议排查信号完整性。

## 下一步验证建议

1. 编译并烧录本修复后的固件，复现 `cmd_2_70_300` 长时任务，观察：
   - MQTT 连接保持率是否提升到 80% 以上；
   - 心跳 `elapsed` 是否恢复到 < 1000 ms；
   - 断连期间 PID/PCNT 日志是否不再出现长时间空白。
2. 在 MQTT broker 端抓包，确认 QoS 2 消息风暴是否消失。
3. 若仍频繁断连，可进一步降低 MQTT 任务优先级至 3，或启用 MQTT 5 的 `message_retransmit_timeout`。
4. 针对电机未转/FG 噪声问题，进行硬件层面排查。

## 相关代码变更

- `main/mqtt.c`: 新增 `mqtt_publish_safe()`；调整 MQTT 配置（QoS、keepalive、clean session、超时、优先级）。
- `main/main.h`: 声明 `mqtt_publish_safe()`。
- `main/pwm.c`: PWM 通知改为 QoS 0 + `mqtt_publish_safe()`。
- `main/pcnt.c`: 转速通知改为 QoS 0 + `mqtt_publish_safe()`。
- `main/pid.c`: `control_cmd` 任务创建/结束通知改为 QoS 0 + `mqtt_publish_safe()`。
- `AGENTS.md`: 更新 MQTT 配置参数和任务优先级说明。
