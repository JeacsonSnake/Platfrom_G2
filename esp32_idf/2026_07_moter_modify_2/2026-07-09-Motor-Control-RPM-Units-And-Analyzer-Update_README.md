# ESP32S3 电机控制单位切换至 RPM 实现记录

**日期**: 2026-07-09  
**分支**: `main`  
**任务描述**: 将电机控制系统的输入单位与日志输出单位从 `pulses/sec` 统一为 `RPM`，并同步更新 `analyze_motor_log.py` 解析脚本与相关文档

---

## 1. 背景

### 历史问题回顾
- 电机控制命令 `cmd_<index>_<speed>_<duration>` 中的 `speed` 一直以 `pulses/sec`（每秒脉冲数）为单位
- 日志中 `target` 与 `actual` 同样显示为 `pulses/sec`，操作人员需要心算换算：`RPM = pulses/sec × 10`
- 常用搅拌转速为 **300~1000 RPM**，其中 **800 RPM** 居多，每次下发命令前都需要将 RPM 除以 10，容易出错
- `analyze_motor_log.py` 解析脚本、报告、图表均基于 `pulses/sec`，不直观

### 本次任务目标
- 命令输入直接以 **RPM** 为单位，无需换算
- 串口日志与 MQTT 遥测中 `target` / `actual` 显示为 RPM
- `raw=XX/200ms` 保持原始脉冲数不变，便于调试
- 同步更新日志解析脚本与项目文档

---

## 2. 单位换算关系

电机 FG 信号为 **6 pulses/rotation**：

```
RPM = pulses/sec × (60 / 6) = pulses/sec × 10
pulses/sec = RPM / 10
```

200ms 采样窗口内：

```
actual_rpm = pcnt_count_per_200ms × 5 × 10
           = pcnt_count_per_200ms × 50
```

12V 供电下实测空载转速约为 **4500 RPM**，对应 **450 pulses/sec**。

---

## 3. 实现方案

### 3.1 需求分析

| 需求项 | 说明 |
|--------|------|
| 命令输入 | `cmd_M_<speed_rpm>_<duration>`，例如 `cmd_2_800_10` 表示电机 2 以 800 RPM 运行 10 秒 |
| PID 日志 | `target=X RPM, actual=Y RPM (raw=Z/200ms)` |
| PCNT 日志 | `PCNT=Y RPM (raw=Z/200ms), target=X RPM` |
| MQTT 遥测 | 运行状态由 `pcnt_count_%d_%d` 改为 `pcnt_rpm_%d_%d` |
| 解析脚本 | `analyze_motor_log.py` 正则、阈值、图表、报告全部适配 RPM |
| 文档 | `AGENTS.md`、`CHB-BLDC2418-Motor-Configuration.md` 同步更新 |

### 3.2 当前控制策略

代码仍处于 **开环控制 + 死区补偿** 状态：
- 电机驱动板内部疑似已有速度闭环，ESP32 侧外部 PID 会与内部闭环冲突
- 因此 ESP32 侧采用开环线性映射：`PWM_output = PID_OPENLOOP_OFFSET + target_rpm × slope`
- 死区补偿偏移量 `PID_OPENLOOP_OFFSET = 300`，解决低速不转问题
- 非对称速率限制：加速 ≤ 500/周期，减速 ≤ 900/周期

---

## 4. 修改文件

### 4.1 `main/pid.c`

#### 量程宏定义

```c
#define PID_MAX_PCNT            (4500)  // 最大转速：12V 供电下实际空载约 4500 RPM
```

#### 实际转速换算

```c
// pcnt_count_list is raw pulses per 200ms; RPM = pulses/200ms * 5 * 10 = pulses/200ms * 50
double actual_rpm = pcnt_count_list[index] * 50.0;
```

#### 开环映射

```c
if (temp > 0) {
    double slope = (PID_MAX_PWM - PID_OPENLOOP_OFFSET) / (double)PID_MAX_PCNT;
    new_input = PID_OPENLOOP_OFFSET + temp * slope;
    if (new_input > PID_MAX_PWM) {
        new_input = PID_MAX_PWM;
    }
    if (new_input < PID_MIN_PWM) {
        new_input = PID_MIN_PWM;
    }
}
```

#### PID 日志格式

```c
ESP_LOGI(TAG, "Motor %d PID: target=%.0f RPM, actual=%.0f RPM (raw=%d/200ms), pid_out=%.0f, pwm_duty=%d, ss=%d",
         index, temp, actual_rpm, pcnt_count_list[index], new_input, new_input_int, startup_counter);
```

### 4.2 `main/pcnt.c`

#### RPM 换算与遥测

```c
// 将200ms原始值转换为 RPM: RPM = pulses/200ms * 5 * (60/6) = pulses/200ms * 50
int actual_rpm = pcnt_count_list[index] * 50;
int target_rpm = (int)motor_speed_list[index];

char buff[64];
// MQTT发布 RPM 值（0-4500范围）（QoS 0，非阻塞）
sprintf(buff, "pcnt_rpm_%d_%d", index, actual_rpm);
mqtt_publish_safe(mqtt_telemetry_topic, buff, strlen(buff), 0, 0);

ESP_LOGI(TAG, "Motor %d running, PCNT=%d RPM (raw=%d/200ms), target=%d RPM", 
         index, actual_rpm, pcnt_count_list[index], target_rpm);
```

### 4.3 `main/mqtt.c`

#### 命令格式注释

```c
else if(strncmp(msg, "cmd_", 4) == 0)
{
    // 命令格式: cmd_<motor_index>_<speed_rpm>_<duration_seconds>
    // 例如 cmd_2_800_10 表示电机2以 800 RPM 运行 10 秒
    int index, speed, duration;
    sscanf(msg, "cmd_%d_%d_%d",  &index, &speed, &duration);
    ...
}
```

### 4.4 `2026_07_moter_modify/analyze_motor_log.py`

#### 正则表达式

```python
PID_RE = re.compile(
    r"\[(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\].*?"
    r"PID_EVENT: Motor (?P<motor>\d+) PID: target=(?P<target>\d+) RPM, "
    r"actual=(?P<actual>-?\d+) RPM \(raw=(?P<raw>-?\d+)/200ms\), "
    r"(?:err=[\d.-]+, P=[\d.-]+, I=[\d.-]+, D=[\d.-]+, )?"
    r"pid_out=(?P<pid_out>[\d.]+), pwm_duty=(?P<pwm_duty>\d+), (?:ss|startup)=(?P<startup>\d+)"
)
```

#### 异常值阈值同步放大 10 倍

| 项目 | 旧阈值 (pulses/sec) | 新阈值 (RPM) |
|------|---------------------|--------------|
| 异常值过滤 | 540 | 5400 |
| 切换过冲判定 | >50 | >500 |
| 高速区饱和 | ≥450 | ≥4500 |
| 惯性判定最小速度 | 10 | 100 |
| 稳态误差有效目标 | >10 | >100 |

#### 图表与报告

- 横轴/纵轴标签由 `Target Speed (pulses/sec)` 改为 `Target Speed (RPM)` / `Steady Actual Speed (RPM)`
- 瞬态响应图默认候选段由 `target=50` 改为 `target=500`
- 报告结论中速度范围由 `15~475 pulses/sec` 改为 `150~4750 RPM`

### 4.5 `AGENTS.md`

- 命令格式说明更新为 `cmd_<index>_<speed_rpm>_<duration>`
- MQTT 测试示例更新为 `cmd_0_800_5`（800 RPM，常用搅拌速度）
- Max PCNT / FG 信号说明补充 RPM 与 pulses/sec 的换算关系

### 4.6 `hardware_info/CHB-BLDC2418-Motor-Configuration.md`

- `Recommended PID max_pcnt` 说明改为 4500 RPM（或 450 pulses/sec）
- PID 配置示例中 `.max_pcnt = 4500`
- 变更对照表与校验清单同步更新

---

## 5. 日志格式对比

### 修改前

```
Motor 2 PID: target=80/s, actual=75/s (raw=15/200ms), pid_out=1403.0, pwm_duty=6788, ss=0
Motor 2 running, PCNT=75/s (raw=15/200ms), target=80/s
```

### 修改后

```
Motor 2 PID: target=800 RPM, actual=750 RPM (raw=15/200ms), pid_out=1403.0, pwm_duty=6788, ss=0
Motor 2 running, PCNT=750 RPM (raw=15/200ms), target=800 RPM
```

> 控制物理输出完全相同，仅输入/显示单位变化。

---

## 6. 验证与测试

### 6.1 编译烧录
- 在正确配置 ESP-IDF 5.5.2 的环境中成功编译并烧录
- 当前控制策略仍为开环 + 死区补偿

### 6.2 日志分析
- 新日志存放于：`2026_07_moter_modify_3/modified_final/`
- 使用更新后的 `2026_07_moter_modify/analyze_motor_log.py` 进行解析
- 解析脚本 `py_compile` 与正则 smoke test 通过
- 分析结果符合预期，无需进一步修改解析脚本

### 6.3 常用命令示例

```bash
# 800 RPM 运行 5 秒（常用搅拌速度）
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_800_5"

# 300 RPM 运行 10 秒
mosquitto_pub -h 192.168.110.31 -t "esp32_1/control" -m "cmd_0_300_10"
```

---

## 7. Git 提交记录

### 提交：电机控制单位统一为 RPM

```bash
# 添加修改文件（排除 django_backend/db.sqlite3 与 __pycache__）
git add main/pid.c main/pcnt.c main/mqtt.c AGENTS.md 2026_07_moter_modify/analyze_motor_log.py
git add -f hardware_info/CHB-BLDC2418-Motor-Configuration.md

# 提交
git commit -m "refactor(motor): 将电机控制单位从 pulses/sec 统一为 RPM

- main/pid.c: PID_MAX_PCNT 改为 4500 (RPM)，实际转速 actual_rpm = raw * 50
- main/pid.c: PID 日志 target/actual 单位改为 RPM，raw=XX/200ms 保持不变
- main/pcnt.c: 运行日志与 MQTT 遥测从 pulses/sec 改为 RPM（pcnt_rpm_%d_%d）
- main/mqtt.c: 明确 cmd_<index>_<speed_rpm>_<duration> 命令格式
- 2026_07_moter_modify/analyze_motor_log.py: 正则与报告全部适配 RPM 单位
- AGENTS.md / hardware_info/CHB-BLDC2418-Motor-Configuration.md: 同步更新单位说明

换算关系: RPM = pulses/sec * 10 (6 PPR)"
```

**提交信息**:  
- Commit: `5979617`  
- 修改: 6 个文件，72 行插入，69 行删除

---

## 8. 使用说明

### 8.1 命令格式

```
cmd_<motor_index>_<speed_rpm>_<duration_seconds>
```

- `motor_index`: 0~3
- `speed_rpm`: 目标转速，单位 RPM（0~4500）
- `duration_seconds`: 运行时长，单位秒

### 8.2 日志查看

```powershell
idf.py -p COM9 monitor
```

关注 `PID_EVENT` 与 `PCNT` 标签日志，确认 `target` / `actual` 显示为 RPM，`raw` 仍为 200ms 原始脉冲数。

### 8.3 报告生成

```bash
cd 2026_07_moter_modify
python analyze_motor_log.py --log ../2026_07_moter_modify_3/modified_final/<your_log>.txt --fig-dir ../2026_07_moter_modify_3/modified_final
```

---

## 9. 问题解决记录

### 问题：输入单位不直观
**现象**: 常用搅拌转速 800 RPM 需要下发 `cmd_2_80_10`，容易误输入为 800 导致电机超速  
**原因**: 命令与日志单位使用 `pulses/sec`，操作人员需要额外换算  
**解决**: 将 `motor_speed_list[]` 统一解释为 RPM，命令、日志、遥测、解析脚本全部使用 RPM

### 问题：解析脚本与日志单位不匹配
**现象**: 若日志改为 RPM 后，旧版 `analyze_motor_log.py` 无法解析  
**原因**: 正则表达式匹配 `target=XXX/s, actual=YYY/s`  
**解决**: 更新正则表达式为 `target=XXX RPM, actual=YYY RPM`，并将所有阈值、标签、结论同步放大 10 倍

### 问题：hardware_info 目录被 gitignore
**现象**: `git add hardware_info/CHB-BLDC2418-Motor-Configuration.md` 时提示路径被忽略  
**原因**: `.gitignore` 中配置了 `hardware_info`  
**解决**: 使用 `git add -f` 强制添加该文档文件（仅该 Markdown 文件，目录内其他文件仍被忽略）

---

## 10. 后续建议

### 方案 A: 低速精度进一步优化
- 当前死区补偿偏移量 `PID_OPENLOOP_OFFSET = 300`
- target < 100 RPM 时仍可能存在较大稳态误差
- 可针对 target < 100 RPM 设置独立启动 PWM 下限，或继续微调死区补偿

### 方案 B: 恢复 PID 闭环（可选）
- 若后续确认电机驱动板内部闭环可被覆盖/禁用，可重新启用 `PID_Calculate()`
- 当前开环方案在 150~4500 RPM 范围内稳态精度已较好

### 方案 C: 增加单位校验
- 在 MQTT 命令解析处增加 `speed_rpm` 范围检查（0~4500）
- 对负数或超速目标给出明确错误日志

---

## 11. 参考链接

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/index.html)
- [CHB-BLDC2418-Motor-Configuration.md](../hardware_info/CHB-BLDC2418-Motor-Configuration.md)
- [AGENTS.md](../AGENTS.md)
- 历史优化记录: `2026_07_moter_modify_2/2026-07-09-mqtt-stability-and-pcnt-median-filter_README.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-07-09 21:40  
**完成时间**: 2026-07-09
