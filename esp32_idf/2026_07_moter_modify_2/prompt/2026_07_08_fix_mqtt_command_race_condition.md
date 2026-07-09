**# Harness Prompt: 修复 MQTT 命令传输延迟/乱序导致的电机调速切换过冲

## 项目背景

- **项目**: ESP32-S3 电机控制 IoT 项目
- **工作目录**: `Platform_G2\esp32_idf`
- **技术栈**: ESP-IDF 5.5.2, FreeRTOS, C11
- **电机**: CHB-BLDC2418, 4 通道 PWM 控制（GPIO 1/4/6/8）
- **通信**: MQTT over WiFi，命令格式 `cmd_<motor_index>_<speed>_<duration>`
- **当前GIT分支**: `main`

## 当前问题

在高转速命令切换到低转速命令时（如 450 → 50），电机经常出现**先高速冲出再回落**的现象。具体表现为：

```text
target=50/s, actual=0/s,   pid_out=8191, pwm_duty=0     ← 第一拍全速
target=50/s, actual=400/s, pid_out=5732, pwm_duty=2462  ← 下一测到高速
```

已确认：
1. **不是硬件电容问题**；
2. **不是 PID 参数问题**：已尝试 Rate Limiter、条件积分抗饱和、Kp 回调、PID 状态清零；
3. **根本原因**: `MQTT broker` **传输延迟/乱序** + `control_cmd` **任务并发**。
4. 之前所作出的相关修改可参考 `esp32_idf\2026_07_moter_modify` 文件夹下依照时间顺序的**所有 `.md` 文件**内容。

## 问题根因分析（需确认）

当前 `main/mqtt.c` 中每次收到 `cmd_` 消息都会创建一个新的 `control_cmd` 任务：

```c
// main/mqtt.c:142-169
void message_compare(char *msg) {
    ...
    else if(strncmp(msg, "cmd_", 4) == 0) {
        int index, speed, duration;
        sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
        cmd_params *params = malloc(sizeof(cmd_params));
        if (params != NULL) {
            params->speed = speed;
            params->duration = duration;
            params->index = index;
            if (xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create control task");
                free(params);
            }
        }
    }
}
```

`control_cmd` 实现（`main/pid.c:218-225`）：

```c
void control_cmd(void *params) {
    cmd_params* local_params = (cmd_params*)params;
    int local_speed = local_params->speed;
    int local_duration = local_params->duration;
    int local_index = local_params->index;
    free(local_params);
    motor_speed_list[local_index] = local_speed;
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    pwm_set_duty(8191, local_index);
    vTaskDelete(NULL);
}
```

由于 MQTT broker 传输延迟/乱序，设备端可能连续收到 `cmd_2_450_10` 和 `cmd_2_50_10`，两个任务并发执行：
- 任务 A：设置 `motor_speed_list[2] = 450`，等待 10 秒
- 任务 B：设置 `motor_speed_list[2] = 50`，等待 10 秒
- 任务 A 10 秒后把 `motor_speed_list[2]` 改回 0

结果是 `motor_speed_list` 被反复覆盖，低转速命令无法正确终止高转速命令。

## 目标

修复 MQTT 命令处理逻辑，确保：
1. 同一电机同时只能有一个 `control_cmd` 任务在运行；
2. 新命令到达时，旧命令任务应立即停止，电机转速应平滑过渡到新目标；
3. 命令结束时的清理逻辑（`motor_speed_list=0`、`pwm_set_duty(8191)`等）不会被其他任务覆盖；
4. 保持原有命令格式和 MQTT 主题不变。

## 建议修复方向

### 方案（推荐，需确认）：维护每个电机的当前任务句柄

在 `main/mqtt.c` 中为每个电机维护一个 `TaskHandle_t`：

```c
static TaskHandle_t cmd_task_handle[4] = {NULL};

void message_compare(char *msg) {
    ...
    else if(strncmp(msg, "cmd_", 4) == 0) {
        int index, speed, duration;
        sscanf(msg, "cmd_%d_%d_%d", &index, &speed, &duration);
        
        // 先停止同电机的旧任务
        if (index >= 0 && index < 4 && cmd_task_handle[index] != NULL) {
            vTaskDelete(cmd_task_handle[index]);
            cmd_task_handle[index] = NULL;
            motor_speed_list[index] = 0;
            pwm_set_duty(8191, index);
        }
        
        cmd_params *params = malloc(sizeof(cmd_params));
        if (params != NULL) {
            params->speed = speed;
            params->duration = duration;
            params->index = index;
            if (xTaskCreate(control_cmd, "CMD_TASK", 4096, (void*)params, 1, &cmd_task_handle[index]) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create control task");
                free(params);
                cmd_task_handle[index] = NULL;
            }
        }
    }
}
```

同时修改 `control_cmd` 在任务结束时清空句柄：

```c
void control_cmd(void *params) {
    ...
    vTaskDelay(local_duration * 1000 / portTICK_PERIOD_MS);
    motor_speed_list[local_index] = 0;
    pwm_set_duty(8191, local_index);
    cmd_task_handle[local_index] = NULL;  // 新增
    vTaskDelete(NULL);
}
```

## 需要修改的文件（需确认）

1. `main/mqtt.c` —— 命令接收与任务创建逻辑
2. `main/pid.c` —— `control_cmd()` 任务结束时的句柄清理
3. `main/main.h` —— 如有新增全局变量或函数声明
4. 可能需要在 `main/mqtt.c` 中 `#include "pwm.h"` 或直接使用 `pwm_set_duty`

## 测试与验证 （提示用户在指定文件夹保存串口日志后回发以进行相关验证）

1. 编译烧录后，抓取日志，存储到 `2026_07_moter_modify/` 内部指定数据管理文件夹下并用 `analyze_motor_log.py` 分析；
2. 观察串口日志，确认 `target=50` 的第一拍 `pid_out` 不再等于 8191；
3. 观察电机实际转速，确认不再先高速冲出。

## 注意事项

- 不要修改 MQTT 主题和命令格式；
- 不要降低 PID 任务的实时性；
- `vTaskDelete` 被删除的任务不会执行到 `vTaskDelete(NULL)` 之后的代码，所以句柄清理要么在删除前完成，要么使用任务通知机制让旧任务自行退出；
- 若采用 `vTaskDelete` 强制删除，需确保旧任务没有持有必须释放的资源（当前 `control_cmd` 只操作 `motor_speed_list` 和 PWM，无其他资源）；
- 建议配合 PID 已有的 `target=0` 清零逻辑一起工作；
- 注意提交本地 git commit 并清晰专业地注明所作修改。

## 参考文档

- `2026_07_moter_modify/modified_final/2026_07_08_heat_detect_FIN_README.md` —— 本次优化的最终总结报告
- `AGENTS.md` —— 项目整体规范
- `hardware_info/CHB-BLDC2418-Motor-Configuration.md` —— 电机规格
**