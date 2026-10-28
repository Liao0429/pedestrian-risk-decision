#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 FSM 决策过程综合时序图（P2-2：算法原理时序曲线）。

输入：单条真实视频的逐帧 timeline JSON（含 frame/level/n/density/r_gcc/fiedler）。
输出：docs/figures/fig9_fsm_timeline_<name>.png —— 4 子图：
  1. 风险等级色带（SAFE/WARNING/DANGER 随时间，含切换竖线标注）
  2. 人数 n（检测到行人总数）
  3. r_gcc（最大连通分量占比，0~1）
  4. Fiedler 代数连通度（图拓扑连通性指标）

用途：论文算法章节 / 答辩讲解 FSM 如何把逐帧抖动收敛为稳定等级。

运行：
    python scripts/plot_fsm_timeline.py                          # 默认 dense 人群视频
    python scripts/plot_fsm_timeline.py results/xxx/timeline.json [输出名]

依赖：matplotlib、numpy（仅标准库 + matplotlib，无 OpenCV 依赖）。
路径全部基于仓库根目录相对定位，可在任意位置运行。
"""
import json
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ---------- 全局风格 ----------
plt.rcParams.update({
    "font.sans-serif": ["DejaVu Sans", "Arial", "Microsoft YaHei"],
    "font.size": 10,
    "axes.unicode_minus": False,
    "axes.edgecolor": "#444444",
    "axes.linewidth": 0.8,
    "figure.dpi": 100,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
})

C_SAFE = "#4dac26"
C_WARN = "#f1c40f"
C_DANGER = "#d7191c"
C_BLUE = "#2166ac"
C_GRAY = "#666666"

LEVELS = ("SAFE", "WARNING", "DANGER")
LV_COLOR = {k: c for k, c in zip(LEVELS, (C_SAFE, C_WARN, C_DANGER))}
LV_NUM = {k: i for i, k in enumerate(LEVELS)}


def find_timeline(proj):
    """优先使用最新重跑（rerun/bench_dense_enhanced），回退到早期 video_enhanced_fsm。"""
    candidates = [
        "results/rerun/bench_dense_enhanced/mixkit_13192_timeline.json",
        "results/video_enhanced_fsm/mixkit_13192_timeline.json",
        "results/video_enhanced/mixkit_13192_timeline.json",
    ]
    for rel in candidates:
        p = os.path.join(proj, rel)
        if os.path.exists(p):
            return p
    return None


def plot_timeline(frames, out_png, title=""):
    frames = sorted(frames, key=lambda f: int(f["frame"]))
    xs = [f["frame"] for f in frames]
    lv = [f["level"] for f in frames]
    n = [float(f.get("n", 0.0)) for f in frames]
    r_gcc = [float(f.get("r_gcc", 0.0)) for f in frames]
    fiedler = [float(f.get("fiedler", 0.0)) for f in frames]

    # 等级切换帧
    switch_frames = []
    for i in range(1, len(frames)):
        if lv[i] != lv[i - 1]:
            switch_frames.append(xs[i])
    switch_count = len(switch_frames)

    lv_idx = [LV_NUM.get(k, 0) for k in lv]

    fig, axes = plt.subplots(4, 1, figsize=(11, 9), sharex=True,
                             gridspec_kw={"height_ratios": [1.4, 1, 1, 1], "hspace": 0.12})

    # ---- 子图1：风险等级色带 ----
    ax = axes[0]
    for k in LEVELS:
        idx = LV_NUM[k]
        seg_x = [x for x, y in zip(xs, lv_idx) if y == idx]
        if not seg_x:
            continue
        runs = []
        start = prev = seg_x[0]
        for x in seg_x[1:]:
            if x != prev + 1:
                runs.append((start, prev))
                start = x
            prev = x
        runs.append((start, prev))
        for a0, a1 in runs:
            ax.fill_between([a0, a1 + 1], idx, step="post", color=LV_COLOR[k], alpha=0.9)
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["SAFE", "WARNING", "DANGER"])
    ax.set_ylim(-0.3, 2.3)
    ax.set_ylabel("Risk level")
    for sf in switch_frames:
        ax.axvline(sf, color=C_GRAY, linestyle="--", linewidth=0.7, alpha=0.6)
    ax.set_title(title, fontsize=12)
    ax.text(0.985, 0.90,
            f"FSM switches: {switch_count}",
            transform=ax.transAxes, ha="right", va="top", fontsize=10,
            bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="#888888", alpha=0.85))

    # ---- 子图2：人数 n ----
    ax = axes[1]
    ax.plot(xs, n, color=C_BLUE, linewidth=1.2, drawstyle="steps-post")
    ax.fill_between(xs, n, step="post", color=C_BLUE, alpha=0.15)
    ax.set_ylabel("People count")
    ax.grid(axis="y", alpha=0.25, linestyle="--")
    ax.spines[["top", "right"]].set_visible(False)

    # ---- 子图3：r_gcc ----
    ax = axes[2]
    ax.plot(xs, r_gcc, color="#8c2d04", linewidth=1.2, drawstyle="steps-post")
    ax.fill_between(xs, r_gcc, step="post", color="#8c2d04", alpha=0.12)
    ax.set_ylabel("r_GCC")
    ax.set_ylim(-0.02, 1.02)
    ax.grid(axis="y", alpha=0.25, linestyle="--")
    ax.spines[["top", "right"]].set_visible(False)

    # ---- 子图4：Fiedler ----
    ax = axes[3]
    ax.plot(xs, fiedler, color=C_DANGER, linewidth=1.2, drawstyle="steps-post")
    ax.fill_between(xs, fiedler, step="post", color=C_DANGER, alpha=0.12)
    ax.set_ylabel("Fiedler λ₂")
    ax.set_xlabel("Frame")
    ax.grid(axis="y", alpha=0.25, linestyle="--")
    ax.spines[["top", "right"]].set_visible(False)

    handles = [
        mpatches.Patch(color=C_SAFE, label="SAFE"),
        mpatches.Patch(color=C_WARN, label="WARNING"),
        mpatches.Patch(color=C_DANGER, label="DANGER"),
    ]
    fig.legend(handles=handles, loc="upper center", ncol=3, frameon=False,
               bbox_to_anchor=(0.5, 1.0), fontsize=10)

    fig.savefig(out_png, dpi=300)
    plt.close(fig)
    return switch_count, len(frames)


def main():
    proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(proj)
    fig_dir = os.path.join(proj, "docs", "figures")
    os.makedirs(fig_dir, exist_ok=True)

    if len(sys.argv) >= 2 and sys.argv[1].endswith(".json"):
        tl_path = sys.argv[1] if os.path.isabs(sys.argv[1]) else os.path.join(proj, sys.argv[1])
    else:
        tl_path = find_timeline(proj)
    if not tl_path or not os.path.exists(tl_path):
        print(f"!! timeline 不存在: {tl_path}")
        return 1

    out_name = sys.argv[2] if len(sys.argv) >= 3 else "fig9_fsm_timeline_13192.png"
    out_png = os.path.join(fig_dir, out_name)

    data = json.load(open(tl_path, encoding="utf-8"))
    title = "FSM risk-level timeline (mixkit-13192, dense crowd)"
    switches, total = plot_timeline(data, out_png, title=title)

    dist = {}
    for f in data:
        dist[f["level"]] = dist.get(f["level"], 0) + 1
    print(f"输出图片: {out_png}")
    print(f"帧数: {total}, FSM 切换: {switches} 次")
    print("等级分布:", {k: dist.get(k, 0) for k in LEVELS})
    return 0


if __name__ == "__main__":
    sys.exit(main())
