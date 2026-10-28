#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""真实数据聚合统计与数据分析图表（论文 7.4/7.6 数据挖掘部分）。
数据来源：results/rerun/video_enhanced/*/*_timeline.json（111 条真实视频，新代码 w4=0.1）
输出：
- results/rerun/_aggregate.json     111 条聚合统计（等级分布/切换/谱特征相关性）
- docs/figures/fig5_111videos_level.png     111 条等级分布
- docs/figures/fig6_fiedler_boxplot.png     fiedler 按风险等级箱线图（谱-风险相关性）
- docs/figures/fig7_switches_hist.png       增强 vs 基线切换次数分布
- docs/figures/fig8_fiedler_trend.png      fiedler 均值随等级趋势线
运行：python scripts/plot_data_analysis.py
"""
import json, os, glob
import statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(PROJECT)
FIG = "docs/figures"
os.makedirs(FIG, exist_ok=True)

OUT = "results/rerun/video_enhanced"
timelines = sorted(glob.glob(f"{OUT}/*/*_timeline.json"))
assert timelines, "未找到 timeline，请先运行 scripts/rerun_all_videos.py"

# ---------- 1. 聚合统计 ----------
agg = {"total_videos": len(timelines), "total_frames": 0,
       "levels": {"SAFE": 0, "WARNING": 0, "DANGER": 0},
       "switches": [], "videos_with_danger": 0,
       "fiedler_by_level": {"SAFE": [], "WARNING": [], "DANGER": []},
       "per_video": {}}
for tl in timelines:
    data = json.load(open(tl, encoding="utf-8"))
    name = os.path.basename(os.path.dirname(tl))
    levels = [f["level"] for f in data]
    dist = {k: levels.count(k) for k in ("SAFE", "WARNING", "DANGER")}
    sw = sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1])
    agg["total_frames"] += len(levels)
    for k in agg["levels"]:
        agg["levels"][k] += dist[k]
    agg["switches"].append(sw)
    if dist["DANGER"] > 0:
        agg["videos_with_danger"] += 1
    for f in data:
        lv = f["level"]
        fied = f.get("fiedler", 0.0)
        agg["fiedler_by_level"][lv].append(float(fied))
    agg["per_video"][name] = {"frames": len(levels), "dist": dist, "switches": sw}

for k in ("SAFE", "WARNING", "DANGER"):
    arr = agg["fiedler_by_level"][k]
    pos = [x for x in arr if x > 0]
    agg["fiedler_by_level"][k] = {"count": len(arr),
                                  "mean": round(float(st.mean(arr)), 4) if arr else 0.0,
                                  "median": round(float(st.median(arr)), 4) if arr else 0.0,
                                  "std": round(float(st.pstdev(arr)), 4) if arr else 0.0,
                                  "positive_count": len(pos),
                                  "positive_mean": round(float(st.mean(pos)), 4) if pos else 0.0}

sw_list = agg["switches"]
agg["switches"] = {"list": sw_list,
                 "total": int(sum(sw_list)),
                 "mean": round(float(st.mean(sw_list)), 4),
                 "median": float(st.median(sw_list)),
                 "min": int(min(sw_list)),
                 "max": int(max(sw_list))}
with open("results/rerun/_aggregate.json", "w", encoding="utf-8") as f:
    json.dump(agg, f, ensure_ascii=False, indent=2)
print(f"聚合完成：{agg['total_videos']} 条视频 / {agg['total_frames']} 帧")
print(f"等级分布：{agg['levels']}")
print(f"切换总次数 {agg['switches']['total']}，均值 {agg['switches']['mean']}，中位数 {agg['switches']['median']}")
print(f"出现 DANGER 视频 {agg['videos_with_danger']} 条")

# ---------- 2. 图5：111 条等级分布 ----------
fig, ax = plt.subplots(figsize=(7, 5))
labels = ["SAFE", "WARNING", "DANGER"]
vals = [agg["levels"][k] for k in labels]
colors = ["#2ecc71", "#f1c40f", "#e74c3c"]
bars = ax.bar(labels, vals, color=colors, edgecolor="black", width=0.55)
for b, v in zip(bars, vals):
    ax.text(b.get_x() + b.get_width() / 2, v + 400, f"{v}\n({v/agg['total_frames']*100:.1f}%)",
            ha="center", fontsize=11)
ax.set_title(f"111 条真实视频增强路径等级分布（{agg['total_frames']} 帧，含谱特征 FSM）")
ax.set_ylabel("帧数")
ax.set_ylim(0, max(vals) * 1.12)
fig.tight_layout(); fig.savefig(f"{FIG}/fig5_111videos_level.png", dpi=150); plt.close(fig)

# ---------- 3. 图6：fiedler 按等级箱线图 ----------
fied_data = {}
for k in ("SAFE", "WARNING", "DANGER"):
    stats = agg["fiedler_by_level"][k]
    fied_data[k] = stats  # 已聚合为统计量，重新加载原始值
# 重新加载原始 fiedler 值用于箱线图
raw = {"SAFE": [], "WARNING": [], "DANGER": []}
for tl in timelines:
    for f in json.load(open(tl, encoding="utf-8")):
        lv = f["level"]
        fied = f.get("fiedler", 0.0)
        if fied > 0:
            raw[lv].append(float(fied))

fig, ax = plt.subplots(figsize=(7, 5))
bp = ax.boxplot([raw["SAFE"], raw["WARNING"], raw["DANGER"]],
                tick_labels=["SAFE", "WARNING", "DANGER"],
                patch_artist=True, showfliers=False)
for patch, color in zip(bp["boxes"], ["#2ecc71", "#f1c40f", "#e74c3c"]):
    patch.set_facecolor(color)
    patch.set_alpha(0.7)
for i, k in enumerate(["SAFE", "WARNING", "DANGER"], start=1):
    st = agg["fiedler_by_level"][k]
    ax.text(i, st["positive_mean"] + 0.05, f"连通帧均值 {st['positive_mean']:.3f} / 中位 {st['median']:.3f}",
            ha="center", fontsize=9)
ax.set_title("Fiedler 特征值随风险等级分布（111 条真实视频）")
ax.set_ylabel("Fiedler 特征值")
ax.set_yscale("symlog", linthresh=0.01)
ax.grid(axis="y", alpha=0.3)
fig.tight_layout(); fig.savefig(f"{FIG}/fig6_fiedler_boxplot.png", dpi=150); plt.close(fig)

# ---------- 4. 图7：增强 vs 基线切换次数直方图 ----------
base_files = sorted(glob.glob("results/rerun/video_baseline/*/*_baseline.json"))
if base_files:
    base_sw = []
    for bf in base_files:
        data = json.load(open(bf, encoding="utf-8"))
        # 顶层字段带 switches 时直接使用，否则从 timeline 解析
        if isinstance(data, dict) and "switches" in data:
            base_sw.append(int(data["switches"]))
        else:
            tl = data.get("timeline") if isinstance(data, dict) else data
            levels = [f.get("level") or f.get("risk_level") for f in tl]
            levels = [x for x in levels if x in ("SAFE", "WARNING", "DANGER")]
            base_sw.append(sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1]))
    enh_sw = agg["switches"]["list"]
    fig, ax = plt.subplots(figsize=(8, 5))
    bins = max(max(base_sw), max(enh_sw)) + 1
    ax.hist(base_sw, bins=bins, alpha=0.6, label=f"基线（总 {sum(base_sw)} 次）", color="#95a5a6", edgecolor="black")
    ax.hist(enh_sw, bins=bins, alpha=0.6, label=f"增强（总 {sum(enh_sw)} 次）", color="#2ecc71", edgecolor="black")
    ax.set_xlabel("等级切换次数")
    ax.set_ylabel("视频条数")
    ax.set_title("增强路径 vs 基线：等级切换次数分布对比")
    ax.legend()
    fig.tight_layout(); fig.savefig(f"{FIG}/fig7_switches_hist.png", dpi=150); plt.close(fig)
    print(f"基线切换总 {sum(base_sw)}，增强切换总 {sum(enh_sw)}，降幅 {(1-sum(enh_sw)/max(sum(base_sw),1))*100:.1f}%")

# ---------- 5. 图8：fiedler 均值随等级趋势线 ----------
fig, ax = plt.subplots(figsize=(6, 4.5))
xs = [0, 1, 2]
means = [agg["fiedler_by_level"][k]["mean"] for k in ("SAFE", "WARNING", "DANGER")]
ax.text(1.02, means[1], "（含 fiedler=0 全部帧均值）", fontsize=9, color="#7f8c8d", va="center")
ax.plot(xs, means, "o-", color="#e74c3c", linewidth=2.5, markersize=9)
for x, m in zip(xs, means):
    ax.text(x + 0.05, m, f"{m:.3f}", fontsize=11, va="center")
ax.set_xticks(xs)
ax.set_xticklabels(["SAFE\n(低风险)", "WARNING\n(中风险)", "DANGER\n(高风险)"])
ax.set_ylabel("Fiedler 特征值均值")
ax.set_title("谱连通度（Fiedler）随风险等级单调上升")
ax.grid(alpha=0.3)
fig.tight_layout(); fig.savefig(f"{FIG}/fig8_fiedler_trend.png", dpi=150); plt.close(fig)

print("图表已输出到 docs/figures/")