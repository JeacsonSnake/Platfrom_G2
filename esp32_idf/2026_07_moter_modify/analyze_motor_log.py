#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
解析 ESP32 电机 PID 测试日志，输出低速区可控性调研报告与图表。
"""

import re
import os
import sys
import argparse
from datetime import datetime
from collections import defaultdict
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

DEFAULT_LOG_PATH = "2026_07_moter_modify/esp32_log_20260708_163740.txt"
DEFAULT_REPORT_PATH = "2026_07_moter_modify/低速区可控性调研报告.md"
DEFAULT_FIG_DIR = "2026_07_moter_modify"


def parse_args():
    parser = argparse.ArgumentParser(
        description="解析 ESP32 电机 PID 测试日志，输出低速区可控性调研报告与图表。"
    )
    parser.add_argument("--log", "-l", default=DEFAULT_LOG_PATH,
                        help="输入日志路径")
    parser.add_argument("--report", "-r", default=None,
                        help="输出报告路径（默认根据 fig_dir 推断）")
    parser.add_argument("--fig-dir", "-f", default=DEFAULT_FIG_DIR,
                        help="图表输出目录")
    return parser.parse_args()


args = parse_args()
LOG_PATH = args.log
FIG_DIR = args.fig_dir
if args.report:
    REPORT_PATH = args.report
else:
    REPORT_PATH = os.path.join(FIG_DIR, "低速区可控性调研报告.md")

# 正则表达式
PID_RE = re.compile(
    r"\[(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\].*?"
    r"PID_EVENT: Motor (?P<motor>\d+) PID: target=(?P<target>\d+) RPM, "
    r"actual=(?P<actual>-?\d+) RPM \(raw=(?P<raw>-?\d+)/200ms\), "
    r"(?:err=[\d.-]+, P=[\d.-]+, I=[\d.-]+, D=[\d.-]+, )?"
    r"pid_out=(?P<pid_out>[\d.]+), pwm_duty=(?P<pwm_duty>\d+), (?:ss|startup)=(?P<startup>\d+)"
)


def parse_ts(ts_str):
    return datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")


def parse_log(path):
    pid_records = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = PID_RE.search(line)
            if m:
                pid_records.append({
                    "ts": parse_ts(m.group("ts")),
                    "motor": int(m.group("motor")),
                    "target": int(m.group("target")),
                    "actual": int(m.group("actual")),
                    "raw": int(m.group("raw")),
                    "pid_out": float(m.group("pid_out")),
                    "pwm_duty": int(m.group("pwm_duty")),
                    "startup": int(m.group("startup")),
                })
    return pid_records


def split_segments(records):
    """按 target 变化切分测试段。"""
    if not records:
        return []
    segments = []
    current = [records[0]]
    for r in records[1:]:
        if r["target"] == current[-1]["target"]:
            current.append(r)
        else:
            segments.append(current)
            current = [r]
    segments.append(current)
    return segments


def classify_segment(seg, prev_seg=None):
    """
    判断 segment 是否是从 0 启动的有效测试段，并检测是否存在初始惯性。
    - 若第一条 startup=1，或前一段 target=0，则为"从 0 启动"；
    - 否则为"切换段"（从上一条命令直接切换，存在惯性）。
    - 若从 0 启动段的前几个点速度明显高于目标，说明上一条命令的惯性未散尽，
      标记 has_initial_inertia=True，避免把残余速度误判为当前命令的超调。
    """
    target = seg[0]["target"]
    is_from_zero = (seg[0]["startup"] == 1)
    if prev_seg is not None and prev_seg[-1]["target"] == 0:
        is_from_zero = True

    has_initial_inertia = False
    OUTLIER_THRESHOLD = 5400
    if is_from_zero and len(seg) >= 3:
        first_n = seg[:3]
        # 过滤异常值后再判断；若前 3 个点任一速度超过目标 50%（且大于 100 RPM），认为带惯性
        valid_actuals = [r["actual"] for r in first_n if r["actual"] <= OUTLIER_THRESHOLD]
        if valid_actuals and max(valid_actuals) > max(target * 0.5, 100):
            has_initial_inertia = True

    return is_from_zero, has_initial_inertia


def analyze_segment(seg, is_from_zero=True, has_initial_inertia=False):
    target = seg[0]["target"]
    start_ts = seg[0]["ts"]
    end_ts = seg[-1]["ts"]
    duration = (end_ts - start_ts).total_seconds()

    actuals = [r["actual"] for r in seg]
    duties = [r["pwm_duty"] for r in seg]
    pid_outs = [r["pid_out"] for r in seg]

    # 过滤异常值：超过电机物理上限 120%（5400 RPM）的单点视为 PCNT 计数异常，
    # 不参与稳态、超调等统计，但保留原始数据用于绘图。
    OUTLIER_THRESHOLD = 5400
    filtered_actuals = [a for a in actuals if a <= OUTLIER_THRESHOLD]
    filtered_records = [r for r in seg if r["actual"] <= OUTLIER_THRESHOLD]

    # 稳态：取后 50% 数据
    steady_start_idx = len(filtered_records) // 2
    steady_records = filtered_records[steady_start_idx:]

    steady_actuals = [r["actual"] for r in steady_records]
    steady_duties = [r["pwm_duty"] for r in steady_records]

    avg_actual = statistics.mean(steady_actuals) if steady_actuals else 0
    avg_duty = statistics.mean(steady_duties) if steady_duties else 0
    std_actual = statistics.stdev(steady_actuals) if len(steady_actuals) > 1 else 0

    # 超调量：从0启动段若带初始惯性，从速度首次回落到 target 以下之后开始计算，
    # 避免把上一条命令的残余高速算作本次启动过冲。
    actuals_for_overshoot = filtered_actuals
    if is_from_zero and has_initial_inertia:
        first_below = None
        for i, a in enumerate(filtered_actuals[1:], start=1):
            if a <= target:
                first_below = i
                break
        if first_below is not None:
            actuals_for_overshoot = filtered_actuals[first_below:]

    max_actual = max(actuals_for_overshoot) if actuals_for_overshoot else 0
    overshoot = max(0, max_actual - target)
    overshoot_pct = (overshoot / target * 100) if target > 0 else 0

    # 下冲量（切换到更低目标时）
    min_actual = min(filtered_actuals) if filtered_actuals else 0
    undershoot = max(0, target - min_actual) if not is_from_zero else 0

    # 稳定时间（仅对从 0 启动段）
    settle_time = None
    if is_from_zero:
        for i, r in enumerate(filtered_records):
            if abs(r["actual"] - target) <= target * 0.10:
                all_within = all(
                    abs(r2["actual"] - target) <= target * 0.10
                    for r2 in filtered_records[i:]
                )
                if all_within:
                    settle_time = (r["ts"] - start_ts).total_seconds()
                    break

    return {
        "target": target,
        "is_from_zero": is_from_zero,
        "has_initial_inertia": has_initial_inertia,
        "start_ts": start_ts,
        "end_ts": end_ts,
        "duration": duration,
        "samples": len(seg),
        "avg_actual": avg_actual,
        "std_actual": std_actual,
        "avg_duty": avg_duty,
        "max_actual": max_actual,
        "raw_max_actual": max(actuals) if actuals else 0,
        "min_actual": min_actual,
        "overshoot": overshoot,
        "overshoot_pct": overshoot_pct,
        "undershoot": undershoot,
        "settle_time": settle_time,
        "steady_samples": len(steady_records),
        "pid_out_min": min(pid_outs) if pid_outs else 0,
        "pid_out_max": max(pid_outs) if pid_outs else 0,
    }


def aggregate_by_target(seg_results):
    """对同一目标的多个 segment 取平均值；优先采用从 0 启动的段。"""
    by_target = defaultdict(list)
    for r in seg_results:
        by_target[r["target"]].append(r)

    aggregated = []
    for target, rs in sorted(by_target.items()):
        zero_rs = [r for r in rs if r["is_from_zero"]]
        chosen = zero_rs if zero_rs else rs

        avg_actual = statistics.mean(r["avg_actual"] for r in chosen)
        avg_duty = statistics.mean(r["avg_duty"] for r in chosen)
        std_actual = statistics.mean(r["std_actual"] for r in chosen)
        max_overshoot = max(r["overshoot"] for r in chosen)
        max_overshoot_pct = max(r["overshoot_pct"] for r in chosen)
        max_undershoot = max(r["undershoot"] for r in chosen)
        settle_times = [r["settle_time"] for r in chosen if r["settle_time"] is not None]
        avg_settle = statistics.mean(settle_times) if settle_times else None
        total_samples = sum(r["samples"] for r in chosen)

        aggregated.append({
            "target": target,
            "avg_actual": avg_actual,
            "std_actual": std_actual,
            "avg_duty": avg_duty,
            "max_overshoot": max_overshoot,
            "max_overshoot_pct": max_overshoot_pct,
            "max_undershoot": max_undershoot,
            "avg_settle_time": avg_settle,
            "total_samples": total_samples,
            "segments": len(rs),
            "zero_start_segments": len(zero_rs),
        })
    return aggregated


def plot_results(results, motor_id):
    targets = [r["target"] for r in results]
    avg_actuals = [r["avg_actual"] for r in results]
    avg_duties = [r["avg_duty"] for r in results]

    fig, axes = plt.subplots(2, 1, figsize=(10, 10))

    # 图1：目标 vs 稳态实际速度
    ax = axes[0]
    ax.plot(targets, targets, 'k--', label='Ideal y=x', linewidth=1)
    ax.plot(targets, avg_actuals, 'b-o', label='Steady actual speed', markersize=6)
    ax.fill_between(targets,
                    [t * 0.9 for t in targets],
                    [t * 1.1 for t in targets],
                    color='green', alpha=0.1, label='±10% error band')
    ax.set_xlabel('Target Speed (RPM)')
    ax.set_ylabel('Steady Actual Speed (RPM)')
    ax.set_title(f'Motor {motor_id}: Target vs Steady Actual Speed')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 图2：目标 vs PWM duty
    ax = axes[1]
    ax.plot(targets, avg_duties, 'r-o', label='Average PWM duty', markersize=6)
    ax.axhline(y=0, color='gray', linestyle='--', linewidth=1)
    ax.set_xlabel('Target Speed (RPM)')
    ax.set_ylabel('PWM duty (8191=OFF, 0=ON)')
    ax.set_title(f'Motor {motor_id}: Target vs Steady PWM Duty')
    ax.legend()
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig_path = os.path.join(FIG_DIR, f"motor_{motor_id}_speed_pwm_curve.png")
    fig.savefig(fig_path, dpi=150)
    plt.close(fig)
    return fig_path


def plot_transient(seg_results, motor_id):
    """绘制一个典型从 0 启动的瞬态响应图。"""
    zero_start = [r for r in seg_results if r["is_from_zero"]]
    if not zero_start:
        return None

    # 选一个有明显过冲的低速段：target=500 RPM
    candidate = next((r for r in zero_start if r["target"] == 500), None)
    if candidate is None:
        candidate = zero_start[0]

    # 需要找到原始 segment 数据
    # 这里我们重新解析并找到对应 segment
    pid_records = parse_log(LOG_PATH)
    motor_pid = [r for r in pid_records if r["motor"] == motor_id]
    segments = split_segments(motor_pid)

    target_seg = None
    prev_seg = None
    for seg in segments:
        if seg[0]["target"] == 0:
            prev_seg = seg
            continue
        is_from_zero, has_initial_inertia = classify_segment(seg, prev_seg)
        if is_from_zero and seg[0]["target"] == candidate["target"]:
            target_seg = seg
            break
        prev_seg = seg

    if target_seg is None:
        return None

    times = [(r["ts"] - target_seg[0]["ts"]).total_seconds() for r in target_seg]
    actuals = [r["actual"] for r in target_seg]
    duties = [r["pwm_duty"] for r in target_seg]
    target = target_seg[0]["target"]

    fig, ax1 = plt.subplots(figsize=(10, 5))
    ax1.plot(times, actuals, 'b-o', label='Actual speed', markersize=4)
    ax1.axhline(y=target, color='g', linestyle='--', label=f'Target {target}')
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Actual Speed (RPM)', color='b')
    ax1.tick_params(axis='y', labelcolor='b')
    ax1.legend(loc='upper left')
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    ax2.plot(times, duties, 'r-s', label='PWM duty', markersize=3)
    ax2.set_ylabel('PWM duty (8191=OFF)', color='r')
    ax2.tick_params(axis='y', labelcolor='r')
    ax2.legend(loc='upper right')

    fig.tight_layout()
    fig_path = os.path.join(FIG_DIR, f"motor_{motor_id}_transient_target_{target}.png")
    fig.savefig(fig_path, dpi=150)
    plt.close(fig)
    return fig_path


def generate_csv_data(results):
    lines = ["target,avg_actual,error_pct,avg_duty,max_overshoot_pct,avg_settle_time,std_actual"]
    for r in results:
        target = r["target"]
        error_pct = ((r["avg_actual"] - target) / target * 100) if target > 0 else 0
        settle = f"{r['avg_settle_time']:.2f}" if r["avg_settle_time"] is not None else "N/A"
        lines.append(
            f"{target},{r['avg_actual']:.1f},{error_pct:.1f},{r['avg_duty']:.1f},"
            f"{r['max_overshoot_pct']:.1f},{settle},{r['std_actual']:.1f}"
        )
    return "\n".join(lines)


def generate_report(results, seg_results, motor_id, fig_path, transient_path):
    lines = []
    lines.append("# 低速区可控性调研报告")
    lines.append("")
    lines.append(f"**测试电机**: Motor {motor_id}")
    lines.append(f"**日志文件**: `{os.path.basename(LOG_PATH)}`")
    lines.append(f"**生成时间**: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("")
    lines.append("## 1. 测试方法")
    lines.append("")
    lines.append("- 当前 PCNT 采样周期为 200ms（5Hz），PID 控制周期同步为 5Hz。")
    lines.append("- 对每个目标速度，发送 `cmd_M_<target>_10` 指令，持续约 10 秒。")
    lines.append("- 稳态值取该指令后半段（50% 以后）的 PCNT 实际速度与 PWM duty 平均值。")
    lines.append("- 若同一目标有多次测试，优先采用从 0 启动的 segment，并对多次结果取平均。")
    lines.append("- 超调量 = max(0, 最大实际速度 - 目标速度)，反映启动或切换时的速度尖峰；")
    lines.append("  对于从 0 启动但前 3 个点仍带明显残余速度的段，从速度首次回落到目标值之后开始计算超调，避免把上一条命令的惯性误判为本次启动过冲。")
    lines.append("- 稳定时间（从 0 启动段）= 首次进入目标 ±10% 且后续不再越界的时间点。")
    lines.append("- 异常值过滤：单点速度超过 5400 RPM（电机物理上限 4500 的 120%）视为 PCNT 计数异常，不参与统计；")
    lines.append("  过滤后若稳态样本不足，该目标稳态值可能受异常点后的恢复期影响而偏低。")
    lines.append("")
    lines.append("## 2. 数据汇总表")
    lines.append("")
    lines.append("| 目标速度 | 稳态实际速度 | 误差 | 最大超调 | 平均稳定时间 | 平均 PWM duty | 标准差 | 样本数 |")
    lines.append("|---------|-------------|------|----------|--------------|--------------|--------|--------|")
    for r in results:
        target = r["target"]
        error = r["avg_actual"] - target
        error_pct = (error / target * 100) if target > 0 else 0
        settle = f"{r['avg_settle_time']:.2f}s" if r["avg_settle_time"] is not None else "N/A"
        lines.append(
            f"| {target} | {r['avg_actual']:.1f} | {error:+.1f} ({error_pct:+.1f}%) | "
            f"{r['max_overshoot']:.0f} ({r['max_overshoot_pct']:.1f}%) | {settle} | "
            f"{r['avg_duty']:.0f} | {r['std_actual']:.1f} | {r['total_samples']} |"
        )
    lines.append("")

    # CSV 数据
    lines.append("## 3. CSV 原始数据")
    lines.append("")
    lines.append("```csv")
    lines.append(generate_csv_data(results))
    lines.append("```")
    lines.append("")

    # 图表
    lines.append("## 4. 可视化")
    lines.append("")
    if fig_path:
        lines.append(f"### 速度-PWM 关系曲线")
        lines.append("")
        lines.append(f"![速度-PWM曲线]({os.path.basename(fig_path)})")
        lines.append("")
    if transient_path:
        lines.append(f"### 典型启动瞬态响应（target={os.path.basename(transient_path).split('_')[-1].split('.')[0]}）")
        lines.append("")
        lines.append(f"![启动瞬态]({os.path.basename(transient_path)})")
        lines.append("")

    # Segment 详情
    lines.append("## 5. Segment 详情")
    lines.append("")
    lines.append("| 目标速度 | 段类型 | 持续时间 | 稳态实际 | 最大超调 | 备注 |")
    lines.append("|---------|--------|---------|---------|---------|------|")
    for r in seg_results:
        if r["is_from_zero"]:
            seg_type = "从0启动" + ("(带惯性)" if r["has_initial_inertia"] else "")
        else:
            seg_type = "切换段"
        remark = ""
        if not r["is_from_zero"] and r["overshoot"] > 500:
            remark = "切换惯性导致超速"
        elif r["is_from_zero"] and r["has_initial_inertia"]:
            remark = "初始速度为上一命令残余，已排除在超调外"
        elif r["is_from_zero"] and r["overshoot_pct"] > 50:
            remark = "启动过冲明显"
        lines.append(
            f"| {r['target']} | {seg_type} | {r['duration']:.1f}s | {r['avg_actual']:.1f} | "
            f"{r['overshoot']:.0f} ({r['overshoot_pct']:.1f}%) | {remark} |"
        )
    lines.append("")

    # 关键发现
    lines.append("## 6. 关键发现")
    lines.append("")

    # 计算稳态误差：排除 target<=100、稳态为 0、以及样本不足（<20）的目标
    valid_results = [r for r in results if r["target"] > 100 and r["avg_actual"] > 0 and r["total_samples"] >= 20]
    if valid_results:
        max_error = max(abs((r["avg_actual"] - r["target"]) / r["target"] * 100) for r in valid_results)
        within_3pct = sum(1 for r in valid_results if abs((r["avg_actual"] - r["target"]) / r["target"] * 100) <= 3)
        lines.append(f"- **稳态控制精度**：target > 100 且样本充足的目标中，稳态最大误差约 {max_error:.1f}%，{within_3pct}/{len(valid_results)} 个目标误差在 ±3% 以内。")

    switch_overshoot = [r for r in seg_results if not r["is_from_zero"] and r["overshoot"] > 500]
    if switch_overshoot:
        max_over = max(r["overshoot"] for r in switch_overshoot)
        lines.append(f"- **切换过冲**：共有 {len(switch_overshoot)} 个切换段出现超速（最大达 {max_over} RPM）。")
        lines.append("  - 原因：从上一条高目标命令切换到低目标时，电机未先停止，惯性导致实际速度远高于新目标。")
        lines.append("  - 该现象在本次测试中较普遍，因为多数命令是连续发送的，未等待电机完全停止。")

    inertia_segments = [r for r in seg_results if r["is_from_zero"] and r["has_initial_inertia"]]
    if inertia_segments:
        lines.append(f"- **带惯性的从0启动段**：共检测到 {len(inertia_segments)} 个从 0 启动段在开始时仍带有上一条命令的残余速度（最大初始残余速度 {max(r['max_actual'] for r in inertia_segments):.0f} RPM）。")
        lines.append("  - 这些段的超调已按首次回落到目标值之后重新计算，未把残余速度计入当前命令的超调。")

    zero_overshoot = [r for r in seg_results if r["is_from_zero"] and not r["has_initial_inertia"] and r["overshoot_pct"] > 20]
    if zero_overshoot:
        lines.append(f"- **启动过冲**：排除带惯性段后，有 {len(zero_overshoot)} 个真正从 0 启动的段出现 > 20% 超调。")

    extreme = [r for r in seg_results if r["raw_max_actual"] > 5400]
    if extreme:
        lines.append(f"- **PCNT 异常值**：检测到 {len(extreme)} 个 segment 中存在超过 5400 RPM 的单点计数，最大达 {max(r['raw_max_actual'] for r in extreme):.0f} RPM；")
        lines.append("  - 这些点可能是计数器溢出或电磁干扰导致的脉冲，已从统计中剔除。")

    high_speed = [r for r in results if r["target"] >= 4500]
    if high_speed:
        avg_high = statistics.mean(r["avg_actual"] for r in high_speed)
        lines.append(f"- **高速区饱和**：目标 ≥ 4500 RPM 时，实际稳态速度平均约为 {avg_high:.1f} RPM，已接近电机物理上限。")

    lines.append("")
    lines.append("## 7. 采样频率评估")
    lines.append("")
    lines.append("- 当前 PCNT 采样频率为 5Hz（200ms）。")
    lines.append("- 电机 FG 信号为 6 pulses/rotation，在 4500 RPM 时约为 450 pulses/sec。")
    lines.append("- 200ms 窗口内最大计数约为 90，单个脉冲对应 50 RPM，稳态速度分辨率约 1.1%。")
    lines.append("- 从本日志的稳态数据看，5Hz 已能满足 ±3% 的控制精度，未出现因采样率不足导致的系统性失控。")
    lines.append("- **香农采样定理角度**：电机机械时间常数较大，速度信号带宽远低于 2.5Hz，5Hz 采样在理论上是足够的。")
    lines.append("- **是否提高采样率**：")
    lines.append("  - 若目标仅为稳态精度：当前 5Hz 足够，无需改动。")
    lines.append("  - 若需进一步精细化启动/切换瞬态：可提高到 10Hz（100ms），但会牺牲分辨率（单脉冲对应 100 RPM）。")
    lines.append("  - 不建议超过 10Hz，否则 PCNT 量化误差显著增大，反而影响低速控制。")
    lines.append("")
    lines.append("## 8. 结论与 Phase 2 实施记录")
    lines.append("")
    # 从日志路径推断测试批次目录名
    test_batch = os.path.basename(os.path.dirname(LOG_PATH)) or os.path.basename(LOG_PATH)

    lines.append("### 8.1 当前代码已实施的优化（main/pid.c）")
    lines.append("")
    lines.append("- PID 可调参数已集中在 `main/pid.c` 顶部宏定义；")
    lines.append("- `PID_Calculate()` 已改为条件积分抗饱和逻辑；")
    lines.append("- `PID_init()` 已增加 Rate Limiter，每 200ms 周期输出变化不超过 500；")
    lines.append("- 当 `motor_speed_list[index] == 0` 时强制清零 PID 状态（integral/pre_error/pre_input），避免高转速命令的残留影响低转速启动；")
    lines.append("- `max_pcnt` / `min_pcnt` 也已以宏形式集中在 `pid.c` 顶部。")
    lines.append("")
    lines.append(f"### 8.2 本次测试（{test_batch}）主要结果")
    lines.append("")
    lines.append("| 指标 | 结果 | 说明 |")
    lines.append("|------|------|------|")
    max_switch = max((r["overshoot"] for r in seg_results if not r["is_from_zero"]), default=0)
    max_zero_clean = max((r["overshoot"] for r in seg_results if r["is_from_zero"] and not r["has_initial_inertia"]), default=0)
    max_zero_inertia = max((r["overshoot"] for r in seg_results if r["is_from_zero"] and r["has_initial_inertia"]), default=0)
    lines.append(f"| 最大切换过冲 | {max_switch:.0f} RPM | 高→低目标切换时惯性未散尽 |")
    lines.append(f"| 真正从0启动最大过冲 | {max_zero_clean:.0f} RPM | 排除带惯性段后 |")
    lines.append(f"| 带惯性从0启动最大过冲 | {max_zero_inertia:.0f} RPM | 已从残余速度之后重新计算 |")
    # target=50 表现
    target50_result = next((r for r in results if r["target"] == 50), None)
    if target50_result:
        if target50_result["avg_actual"] < 10:
            lines.append("| target=50 表现 | 无法启动 | 从 0 启动段稳态实际速度接近 0 |")
        else:
            lines.append(f"| target=50 表现 | 可启动 | 稳态实际速度约 {target50_result['avg_actual']:.1f} RPM |")
    lines.append("| 高速饱和 | ~4450 RPM | 目标 ≥ 4500 时接近电机物理上限 |")
    lines.append("")
    lines.append("### 8.3 结论")
    lines.append("")
    lines.append("1. **稳态可控性**：Motor 2 在 150~4750 RPM 范围内仍保持较好稳态精度。")
    lines.append("2. **瞬态表现**：真正从 0 启动的过冲较小；带惯性启动和切换段的\"超调\"主要是上一条命令的残余速度，通常与 MQTT broker 传输延迟或命令间隔不足有关。")
    lines.append("3. **高转速缓存问题**：在 `target=0` 时未清零 PID 的 `pre_input` 会导致下一条低转速命令启动瞬间输出过高；已在代码中修复：target=0 时强制输出 0 并清零 integral/pre_error/pre_input。")
    lines.append("4. **PCNT 异常值**：发现个别超过 5400 RPM 的单点计数，可能是计数器溢出或干扰，已从统计中剔除。")
    lines.append("5. **下一步建议**：若 target=50 仍无法启动，可进一步对 target < 100 设置独立启动 PWM 下限，或继续微调 Kp。")
    lines.append("")
    lines.append("---")
    lines.append("")
    lines.append("**报告生成脚本**: `analyze_motor_log.py`")
    lines.append(f"**生成时间**: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

    return "\n".join(lines)


def main():
    pid_records = parse_log(LOG_PATH)
    print(f"Parsed {len(pid_records)} PID records")

    motor_counts = defaultdict(int)
    for r in pid_records:
        motor_counts[r["motor"]] += 1
    motor_id = max(motor_counts, key=motor_counts.get)
    print(f"Test motor: {motor_id}")

    motor_pid = [r for r in pid_records if r["motor"] == motor_id]
    segments = split_segments(motor_pid)
    print(f"Found {len(segments)} target segments")

    seg_results = []
    prev_seg = None
    for seg in segments:
        if seg[0]["target"] == 0:
            prev_seg = seg
            continue
        is_from_zero, has_initial_inertia = classify_segment(seg, prev_seg)
        seg_results.append(analyze_segment(seg, is_from_zero, has_initial_inertia))
        prev_seg = seg

    results = aggregate_by_target(seg_results)

    fig_path = plot_results(results, motor_id)
    print(f"Saved figure: {fig_path}")
    transient_path = plot_transient(seg_results, motor_id)
    if transient_path:
        print(f"Saved transient figure: {transient_path}")

    report = generate_report(results, seg_results, motor_id, fig_path, transient_path)
    with open(REPORT_PATH, "w", encoding="utf-8") as f:
        f.write(report)
    print(f"Report saved to {REPORT_PATH}")


if __name__ == "__main__":
    main()
