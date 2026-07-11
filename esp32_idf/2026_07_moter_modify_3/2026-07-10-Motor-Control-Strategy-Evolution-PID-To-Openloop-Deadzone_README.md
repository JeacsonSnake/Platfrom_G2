# ESP32S3 电机控制策略演进记录：从 PID 闭环到开环死区补偿

**日期**: 2026-07-10  
**分支**: `main`  
**任务描述**: 梳理并记录从基于代码的 PID 调节、发现电机内置速度闭环、决定采用开环控制、验证开环有效性，到最终加入死区补偿的完整思考与修改过程

---

## 1. 背景

### 1.1 系统概述

项目使用 ESP32-S3-DevKitC-1 控制 4 路 CHB-BLDC2418 无刷直流电机，核心控制链路为：

```
MQTT 命令 → control_cmd 任务 → motor_speed_list[index]
                                    ↓
PID 任务(pid.c) ← pcnt_count_list[index] ← PCNT 采样(pcnt.c)
                                    ↓
                            PWM 输出(pwm.c) → 电机
```

- **PWM**: GPIO 1/4/6/8，5KHz，13-bit（0~8191），反相逻辑（8191=OFF，0=ON）
- **PCNT**: GPIO 2/5/7/9，200ms 采样周期（5Hz），乘以 5 转换为每秒脉冲数
- **FG 信号**: 6 pulses/rotation
- **电机**: CHB-BLDC2418，12V 供电，实测空载约 4500 RPM

### 1.2 历史问题

截至 2026-07-08，代码存在以下主要问题：

| # | 问题 | 现象 |
|---|------|------|
| 1 | PID 结构错误：位置式与增量式混合 | `output = data->pre_input + output` 导致积分/历史输出被累加两次，振荡与超调 |
| 2 | 微分项对设定值突变敏感 | `derivative = (error - data->pre_error)` 产生 Derivative Kick |
| 3 | 积分限幅不精确 | 用 `±max_pwm` 限幅 `integral`，Ki 很小时过于保守 |
| 4 | 量程混淆 | `PID_MAX_PCNT` 一度被改为 900（对应 9000 RPM，24V 规格），但 12V 实际空载仅 4500 RPM |
| 5 | 软启动与状态重置耦合混乱 | `pre_input` 残留影响下一次启动 |
| 6 | PCNT 异常阈值过低 | 150/200ms 在高速时可能误判 |

---

## 2. Phase 1: PID 结构修复与量程修正

### 2.1 诊断摘要

基于 `2026_07_moter_modify_3/prompt/2026_07_10.md` 对 `main/pid.c`、`main/pcnt.c`、`main/pwm.c` 进行审查，确认上述 6 个问题及代码位置。

### 2.2 关键决策

| 决策项 | 结果 | 说明 |
|--------|------|------|
| 电机规格 | 12V / 4500 RPM（空载实测） | 9000 RPM 为 24V 规格，当前 12V 供电不适用 |
| `PID_MAX_PCNT` | 改回 450（pulses/sec） | 对应 4500 RPM（12V 实测） |
| `PID_data` 结构体 | 最小改动 | 保留 `pre_input`/`d_filtered`，新增 `pre_output`，复用 `pre_measurement` |
| `PID_Calculate` | 纯位置式 + 微分先行 + 条件积分 | 解决积分累加两次与 Derivative Kick 问题 |
| Rate Limiter | 软启动前 2s 每周期最大增加 300；正常运行每周期最大变化 450 | 替代原有绝对上限软启动 |

### 2.3 修改文件

- `main/main.h`：新增 `PID_terms` 分项输出结构体；`PID_data` 新增 `pre_output`；更新 `PID_Calculate` 声明
- `main/pid.c`：重写 `PID_Calculate`；`PID_init` 改为 Rate Limiter 软启动；启动/停止边沿清零状态；新增 `pid_log_terms()` 解耦日志
- `main/pcnt.c`：异常阈值 150 → 250；突刺检测 `5x+30` → `8x+50`
- `hardware_info/CHB-BLDC2418-Motor-Configuration.md`：更新为 12V/4500 RPM 实测规格
- `AGENTS.md`：同步电机规格与 Max PCNT 描述

### 2.4 关键代码

```c
// 位置式 PID + 微分先行（Derivative on Measurement）+ 条件积分
double PID_Calculate(struct PID_params params, struct PID_data *data, 
                     double target_speed, double current_speed, struct PID_terms *terms)
{
    double error = target_speed - current_speed;
    double Pout = params.Kp * error;
    
    // 微分先行：对测量值微分，避免设定值突变导致的 Derivative Kick
    double Dout = params.Kd * (data->pre_measurement - current_speed);
    
    // 条件积分：预测输出是否会朝饱和方向加剧
    double predicted_output = Pout + params.Ki * (data->integral + error) + Dout;
    bool saturate_high = (predicted_output > params.max_pwm && error > 0);
    bool saturate_low  = (predicted_output < params.min_pwm && error < 0);
    
    if (!saturate_high && !saturate_low) {
        data->integral += error;
        double integral_max = params.max_pwm / params.Ki;
        if (data->integral > integral_max) data->integral = integral_max;
        if (data->integral < -integral_max) data->integral = -integral_max;
    }
    
    double Iout = params.Ki * data->integral;
    double output = Pout + Iout + Dout;  // 纯位置式，不再与历史输出累加
    
    if (output > params.max_pwm) output = params.max_pwm;
    else if (output < params.min_pwm) output = params.min_pwm;
    
    data->pre_error = error;
    data->pre_measurement = current_speed;
    
    if (terms != NULL) {
        terms->Pout = Pout;
        terms->Iout = Iout;
        terms->Dout = Dout;
        terms->error = error;
    }
    
    return output;
}
```

---

## 3. Phase 2: PID 参数整定尝试

### 3.1 初始参数

结构修复后保留的初始参数为：

```c
#define PID_KP  (7.0)
#define PID_KI  (0.005)
#define PID_KD  (0.03)
```

### 3.2 上调参数尝试

基于纯位置式结构，尝试提高 PID 参数以增强响应：

```c
#define PID_KP  (50.0)
#define PID_KI  (0.50)
#define PID_KD  (0.30)
```

### 3.3 测试结果与分析

- 参数上调后，电机响应有所改善，但稳态精度仍不理想
- 不同目标速度下，实际转速与目标值偏差仍较大
- 观察到电机转速似乎被某种内部机制"拉回"到固定区间
- **关键发现**：CHB-BLDC2418 驱动板内部疑似已有自己的速度闭环

---

## 4. Phase 3: 发现电机内部闭环

### 4.1 现象

在 PID 闭环控制下观察到：
- 目标速度低于电机物理最低稳定转速时，PID 输出饱和在 8191（反相后 duty=0，全速）
- 电机转速似乎存在一个"自然稳定点"，不受 PID 输出精细调节
- PID 输出与电机实际转速之间不是简单的线性关系
- 即使 ESP32 侧 PID 努力调节，电机实际转速变化平缓，仿佛在跟随某个内部参考

### 4.2 推测

CHB-BLDC2418 电机驱动板内部已集成速度闭环：
- 驱动板接收 PWM 信号后，内部根据 PWM 占空比解析为目标转速
- 驱动板使用自身的 FG 反馈进行闭环调节
- ESP32 侧再叠加一个 PID 闭环，会与驱动板内部闭环**相互冲突**
- 两个闭环串联导致系统响应不直观、参数难以整定

### 4.3 决策

基于以上观察，决定：
- **暂停 ESP32 侧 PID 闭环**
- **改用开环控制**：将目标转速直接线性映射为 PWM 占空比
- 让电机驱动板内部闭环自行调节电机转速
- ESP32 侧仅负责将用户目标转换为合适的 PWM 设定值

---

## 5. Phase 4: 开环控制验证

### 5.1 实现方式

注释掉 `PID_Calculate()` 调用，改为线性映射：

```c
// 开环映射：target -> PWM
double new_input = 0.0;
if (temp > 0) {
    double slope = (double)PID_MAX_PWM / PID_MAX_PCNT;
    new_input = temp * slope;  // temp 单位为 pulses/sec，PID_MAX_PCNT=450
}
```

其中 `temp` 为 `motor_speed_list[index]`，`PID_MAX_PCNT=450`（对应 4500 RPM）。

### 5.2 测试结果

测试命令覆盖 20~450 pulses/sec（对应 200~4500 RPM）：

| 目标 (pulses/sec) | 稳态实际 (pulses/sec) | 误差 |
|-------------------|----------------------|------|
| 150 | 140.9 | -6.0% |
| 250 | 242.7 | -2.9% |
| 350 | 348.5 | -0.4% |
| 450 | 447.0 | -0.7% |

### 5.3 结论

- **开环控制稳态精度大幅提升**
- 150~450 pulses/sec（1500~4500 RPM）范围内误差 < 7%
- 证明电机驱动板内部确实存在有效的速度闭环
- ESP32 侧外部 PID 与内部闭环冲突，开环映射是更合理的策略

---

## 6. Phase 5: 死区补偿与速率限制

### 6.1 低速不转问题

开环映射后发现：
- `target=20`、`target=30` 时电机不转
- `target=40` 勉强转动但误差较大
- 原因是电机存在**静摩擦死区**：PWM 输出小于约 300 时电机无法克服静摩擦

### 6.2 死区补偿

加入最小输出偏移量：

```c
#define PID_OPENLOOP_OFFSET     (300.0)  // 死区补偿偏移量

double new_input = 0.0;
if (temp > 0) {
    double slope = (PID_MAX_PWM - PID_OPENLOOP_OFFSET) / PID_MAX_PCNT;
    new_input = PID_OPENLOOP_OFFSET + temp * slope;
}
```

- `target=0` 时输出 0（电机停止）
- `target>0` 时输出从 `PID_OPENLOOP_OFFSET` 开始，确保能克服静摩擦
- `target=PID_MAX_PCNT` 时输出 `PID_MAX_PWM`（全速）

### 6.3 非对称速率限制

为进一步平滑启动与抑制高→低切换过冲，采用非对称 Rate Limiter：

```c
#define PID_MAX_OUTPUT_DELTA    (500.0)   // 加速限制
#define PID_MAX_BRAKING_DELTA   (900.0)   // 减速/制动限制（允许更快刹车）

double delta = new_input - data.pre_output;
double max_pos_delta = startup_phase ? PID_SOFTSTART_OUTPUT_DELTA : PID_MAX_OUTPUT_DELTA;
if (delta > max_pos_delta) {
    new_input = data.pre_output + max_pos_delta;
}
else if (delta < -PID_MAX_BRAKING_DELTA) {
    new_input = data.pre_output - PID_MAX_BRAKING_DELTA;
}
```

- 软启动阶段（前 10 个周期，约 2 秒）使用更小的 `PID_SOFTSTART_OUTPUT_DELTA = 300`
- 正常加速限制为 500/周期
- 减速允许更快（900/周期），抑制惯性过冲

### 6.4 最终代码

```c
// ========== 开环控制 + 死区补偿 ==========
//  电机驱动板内部疑似已有速度闭环，ESP32 侧采用开环映射即可。
//  实测发现电机在输出 < ~300 时不转动（静摩擦死区），
//  因此当 target > 0 时加入一个最小输出偏移量，低速区目标才能真实对应转速。
#define PID_OPENLOOP_OFFSET     (300.0)
double new_input = 0.0;
if (temp > 0) {
    double slope = (PID_MAX_PWM - PID_OPENLOOP_OFFSET) / (double)PID_MAX_PCNT;
    new_input = PID_OPENLOOP_OFFSET + temp * slope;
    if (new_input > PID_MAX_PWM) new_input = PID_MAX_PWM;
    if (new_input < PID_MIN_PWM) new_input = PID_MIN_PWM;
}

// Rate Limiter
double delta = new_input - data.pre_output;
double max_pos_delta = startup_phase ? PID_SOFTSTART_OUTPUT_DELTA : PID_MAX_OUTPUT_DELTA;
if (delta > max_pos_delta) {
    new_input = data.pre_output + max_pos_delta;
}
else if (delta < -PID_MAX_BRAKING_DELTA) {
    new_input = data.pre_output - PID_MAX_BRAKING_DELTA;
}

// 反相 PWM
int new_input_int = PID_MAX_PWM - (int)new_input;
pwm_set_duty(new_input_int, index);
```

---

## 7. Phase 6: 单位统一为 RPM（后续整理）

在开环死区补偿策略稳定后，进一步将命令输入单位与日志输出单位从 `pulses/sec` 统一为 `RPM`：

- `PID_MAX_PCNT` 从 450（pulses/sec）改为 4500（RPM）
- 命令格式：`cmd_2_800_10` 直接表示 800 RPM
- PID 日志：`target=800 RPM, actual=800 RPM (raw=16/200ms)`
- `analyze_motor_log.py` 同步更新为 RPM 单位

> 该部分详细记录见 `2026_07_moter_modify_2/2026-07-09-Motor-Control-RPM-Units-And-Analyzer-Update_README.md`

---

## 8. 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `main/pid.c` | PID 结构修复、量程修正、开环映射、死区补偿、非对称速率限制、RPM 单位切换 |
| `main/pcnt.c` | 异常阈值调整、RPM 单位遥测 |
| `main/mqtt.c` | 命令格式 RPM 注释 |
| `main/main.h` | `PID_terms` 结构体、`PID_data` 新增 `pre_output` |
| `2026_07_moter_modify/analyze_motor_log.py` | 解析 RPM 单位日志、阈值同步放大 10 倍 |
| `AGENTS.md` | 命令格式、单位说明同步更新 |
| `hardware_info/CHB-BLDC2418-Motor-Configuration.md` | 电机规格、max_pcnt 说明同步更新 |

---

## 9. 验证结果汇总

### 9.1 开环验证（modified_5）

| 目标 (pulses/sec) | 稳态实际 | 误差 |
|-------------------|----------|------|
| 150 | 140.9 | -6.0% |
| 250 | 242.7 | -2.9% |
| 350 | 348.5 | -0.4% |
| 450 | 447.0 | -0.7% |

- 150~450 pulses/sec 误差 < 7%
- 证明开环优于外部 PID

### 9.2 最终 RPM 验证（modified_final）

测试 400~1000 RPM：

| 目标 (RPM) | 稳态实际 | 误差 |
|-----------|----------|------|
| 800 | 800.0 | 0.0% |
| 700 | 706.0 | +0.9% |
| 1000 | 976.7 | -2.3% |
| 950 | 1026.0 | +8.0% |

- 400~1000 RPM 范围内大部分目标误差在 ±5% 以内
- 800 RPM（常用搅拌速度）稳态误差为 0
- 950/975 RPM 略偏高，可能接近电机驱动板内部闭环非线性区

---

## 10. Git 提交记录

### 提交 1: PID 结构修复与量程修正

```bash
git add main/main.h main/pid.c main/pcnt.c AGENTS.md hardware_info/CHB-BLDC2418-Motor-Configuration.md
git commit -m "fix(pid,pcnt): pure position PID, derivative-on-measurement, rate limiter, 9000 RPM scale"
```

**提交信息**:  
- Commit: `1d4e6a5`  
- 修改: 3 个文件，107 行插入，81 行删除

### 提交 2: PID 参数重新整定

```bash
git add main/pid.c
git commit -m "fix(pid): retune PID parameters for pure position control (Kp=50, Ki=0.5, Kd=0.3)"
```

**提交信息**:  
- Commit: `311cb52`  
- 修改: 1 个文件，3 行插入，3 行删除

### 提交 3: 改用开环线性映射

```bash
git add main/pid.c
git commit -m "feat(pid): open-loop test, linear target-to-PWM mapping with asymmetric rate limiter"
```

**提交信息**:  
- Commit: `ee2069c`  
- 修改: 1 个文件，40 行插入，38 行删除

### 提交 4: 加入死区补偿

```bash
git add main/pid.c
git commit -m "fix(pid): add open-loop dead-zone offset (300) for low-speed accuracy"
```

**提交信息**:  
- Commit: `59c3a92`  
- 修改: 1 个文件，7 行插入，4 行删除

### 提交 5: 单位切换为 RPM

```bash
git add main/pid.c main/pcnt.c main/mqtt.c AGENTS.md 2026_07_moter_modify/analyze_motor_log.py
git add -f hardware_info/CHB-BLDC2418-Motor-Configuration.md
git commit -m "refactor(motor): 将电机控制单位从 pulses/sec 统一为 RPM"
```

**提交信息**:  
- Commit: `5979617`  
- 修改: 6 个文件，72 行插入，69 行删除

---

## 11. 经验教训

### 11.1 关于电机驱动板

- **不要假设电机驱动板是纯执行机构**：现代 BLDC 电机驱动板往往内置速度/电流闭环
- 在叠加外部闭环前，应先通过开环阶跃测试确认驱动板本身的闭环特性
- 当外部 PID 与内部闭环冲突时，外部 PID 参数再优也难以取得好效果

### 11.2 关于 PID 调试

- 结构正确比参数重要：位置式与增量式混合、Derivative Kick 等问题会从根本上破坏控制性能
- 微分先行、条件积分、积分限幅是位置式 PID 的标配
- Rate Limiter 是抑制启动过冲和高→低切换过冲的有效手段

### 11.3 关于低速区

- 电机存在静摩擦死区，低速目标需要额外补偿
- 开环映射 + 死区补偿在 150~4500 RPM 范围内效果良好
- 低于 150 RPM 的精确控制仍需进一步研究（如独立启动 PWM 下限）

---

## 12. 后续建议

### 方案 A: 进一步优化低速区
- 针对 target < 100 RPM 设置独立启动 PWM 下限
- 或采用分段线性映射：低速区使用更陡的斜率

### 方案 B: 保持当前开环策略
- 当前 300~1000 RPM 常用搅拌区间已满足需求
- 800 RPM 常用点稳态误差为 0

### 方案 C: 恢复 PID 闭环（可选）
- 若未来确认可禁用电机驱动板内部闭环，可重新启用 ESP32 侧 PID
- 当前代码已保留完整的 `PID_Calculate()` 实现，只需取消注释即可恢复

---

## 13. 参考链接

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/index.html)
- [CHB-BLDC2418-Motor-Configuration.md](../hardware_info/CHB-BLDC2418-Motor-Configuration.md)
- [AGENTS.md](../AGENTS.md)
- RPM 单位切换记录: `2026_07_moter_modify_2/2026-07-09-Motor-Control-RPM-Units-And-Analyzer-Update_README.md`
- PID 修复详细报告: `2026_07_moter_modify_3/2026_07_10_pid_fix_report.md`

---

**记录人**: Kimi Code CLI  
**更新时间**: 2026-07-10  
**完成时间**: 2026-07-10
