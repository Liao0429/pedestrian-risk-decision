#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""论文级专业图表生成（科研风格，300 DPI，统一配色）。
数据全部来自真实实验产物（111 条视频 timeline / 基线 / 消融 / 对比汇总）。
输出 docs/figures/ 下 7 张图：
- fig5_111videos_level.png   等级分布柱状图（帧数+百分比）
- fig6_fiedler_boxplot.png   Fiedler 箱线图（连通帧分布 + Spearman rho）
- fig7_switches_hist.png     基线 vs 增强切换次数直方图
- fig8_fiedler_trend.png    Fiedler 均值趋势 + 误差棒
- fig_ablation_pro.png       消融双轴图（切换次数柱状 + DANGER 帧折线）
- fig_danger_compare.png       DANGER 视频数/帧数 基线 vs 增强 双面板
- fig_level_share_compare.png 等级帧占比堆叠对比（基线 vs 增强）

运行：python scripts/plot_professional_figures.py
"""
import json, os, glob, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

# ---------- 全局风格 ----------
plt.rcParams.update({
    "font.sans-serif": ["Microsoft YaHei", "SimHei", "DejaVu Sans"],
    "font.size": 11,
    "axes.unicode_minus": False,
    "axes.edgecolor": "#444444",
    "axes.linewidth": 0.8,
    "figure.dpi": 100,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
})
DPI = 300
C_SAFE, C_WARN, C_DANGER = "#4dac26", "#f1c40f", "#d7191c"
C_BASE, C_ENH = "#95a5a6", "#2166ac"
C_ABL = ["#bdbdbd", "#9ecae1", "#4292c6", "#08519c"]

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(PROJECT)
FIG = "docs/figures"
os.makedirs(FIG, exist_ok=True)


# ---------- 工具 ----------
def rankdata(a):
    s = sorted(a)
    rank = {}
    n = len(a)
    i = 0
    while i < n:
        j = i
        while j < n and s[j] == s[i]:
            j += 1
        avg = (i + j - 1) / 2.0
        for k in range(i, j):
            rank[s[k]] = avg
        i = j
    return [rank[v] for v in a]


def spearman(x, y):
    rx, ry = rankdata(x), rankdata(y)
    n = len(x)
    mx, my = sum(rx) / n, sum(ry) / n
    cov = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    varx = sum((a - mx) ** 2 for a in rx)
    vary = sum((b - my) ** 2 for b in ry)
    if varx == 0 or vary == 0:
        return 0.0
    return cov / math.sqrt(varx * vary)


def load_frames():
    """加载 111 条 timeline 全部帧，返回 list[dict] 和按等级分组的 fiedler。"""
    frames = []
    for tl in glob.glob("results/rerun/video_enhanced/*/*_timeline.json"):
        for f in json.load(open(tl, encoding="utf-8")):
            frames.append(f)
    return frames


def load_base_switches():
    out = []
    for bf in glob.glob("results/rerun/video_baseline/*/*_baseline.json"):
        d = json.load(open(bf, encoding="utf-8"))
        if isinstance(d, dict) and "switches" in d:
            out.append(int(d["switches"]))
    return out


def load_enh_switches():
    out = []
    for tl in glob.glob("results/rerun/video_enhanced/*/*_timeline.json"):
        data = json.load(open(tl, encoding="utf-8"))
        lv = [f["level"] for f in data]
        out.append(sum(1 for i in range(1, len(lv)) if lv[i] != lv[i - 1]))
    return out


def fmt(v):
    return f"{v:,}".replace(",", ",")


# ---------- 数据加载 ----------
frames = load_frames()
LEVELS = ("SAFE", "WARNING", "DANGER")
LV_NUM = {k: i for i, k in enumerate(LEVELS)}
dist = {k: sum(1 for f in frames if f["level"] == k) for k in LEVELS}
total = len(frames)
base_sw = load_base_switches()
enh_sw = load_enh_switches()
cmp_sum = json.load(open("results/rerun/_comparison_summary.json", encoding="utf-8"))
abl = json.load(open("results/rerun/_ablation.json", encoding="utf-8"))
combos = abl["combos"]
abl_sw = {c: [r[c]["switches"] for r in abl["results"].values()] for c in combos}
abl_dang = {c: sum(r[c]["dist"].get("DANGER", 0) for r in abl["results"].values()) for c in combos}
abl_sw_mean = {c: sum(v) / len(v) for c, v in abl_sw.items()}

# Spearman: fiedler vs 等级编号（全部帧，含 0）
fied_all = [float(f.get("fiedler", 0.0)) for f in frames]
lv_all = [LV_NUM[f["level"]] for f in frames]
rho = spearman(fied_all, lv_all)

# 分组 fiedler（全部帧 + 连通帧）
fied_by_level = {k: [float(f.get("fiedler", 0.0)) for f in frames if f["level"] == k] for k in LEVELS}
fied_pos_by_level = {k: [x for x in v if x > 0] for k, v in fied_by_level.items()}
mean_all = {k: sum(v) / len(v) if v else 0.0 for k, v in fied_by_level.items()}
std_all = {k: (sum((x - mean_all[k]) ** 2 for x in v) / len(v)) ** 0.5 if v else 0.0 for k, v in fied_by_level.items()}


# ---------- 图1：111 条等级分布 ----------
fig, ax = plt.subplots(figsize=(6.2, 4.2))
x = [0, 1, 2]
bars = ax.bar(x, [dist[k] for k in LEVELS], width=0.58,
              color=[C_SAFE, C_WARN, C_DANGER], edgecolor="black", linewidth=0.6)
for xi, k in zip(x, LEVELS):
    v = dist[k]
    ax.text(xi, v + 500, f"{fmt(v)}\n({v / total * 100:.1f}%)",
            ha="center", va="bottom", fontsize=10.5)
ax.set_xticks(x)
ax.set_xticklabels(["SAFE\n低风险", "WARNING\n中风险", "DANGER\n高风险"])
ax.set_ylabel("帧数")
ax.set_ylim(0, max(dist.values()) * 1.16)
ax.set_title("111 条真实视频等级分布（含谱特征 FSM，45021 帧）", fontsize=12)
ax.grid(axis="y", alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
ax.text(0.02, 0.96, f"DANGER 视频 {cmp_sum.get('enh_danger', '?')}/111 · 全程 SAFE {cmp_sum.get('enh_safe', '?')}",
        transform=ax.transAxes, fontsize=9.5, color="#555555", va="top")
fig.tight_layout()
fig.savefig(f"{FIG}/fig5_111videos_level.png", dpi=DPI)
plt.close(fig)

# ---------- 图2：Fiedler 箱线图（连通帧）+ Spearman ----------
fig, ax = plt.subplots(figsize=(6.4, 4.4))
bp = ax.boxplot([fied_pos_by_level[k] for k in LEVELS],
                tick_labels=["SAFE", "WARNING", "DANGER"],
                patch_artist=True, showfliers=False, widths=0.55,
                medianprops=dict(color="black", linewidth=1.4),
                boxprops=dict(linewidth=0.9))
for patch, color in zip(bp["boxes"], [C_SAFE, C_WARN, C_DANGER]):
    patch.set_facecolor(color)
    patch.set_alpha(0.75)
for i, k in enumerate(LEVELS, start=1):
    v = fied_pos_by_level[k]
    if v:
        m = sum(v) / len(v)
        ax.text(i + 0.09, m, f"均值 {m:.3f}\n(n={len(v)})", fontsize=8.5,
                va="center", color="#333333")
ax.set_ylabel("Fiedler 特征值（连通帧）")
ax.set_yscale("symlog", linthresh=0.05)
ax.set_title("Fiedler 随风险等级分布（111 条真实视频）", fontsize=12)
ax.grid(axis="y", alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
ax.text(0.02, 0.97, f"Spearman ρ = {rho:.3f}（45021 帧，含 fiedler=0）",
        transform=ax.transAxes, fontsize=10, color="#08519c", va="top", fontweight="bold")
fig.tight_layout()
fig.savefig(f"{FIG}/fig6_fiedler_boxplot.png", dpi=DPI)
plt.close(fig)

# ---------- 图3：切换次数直方图（基线 vs 增强） ----------
fig, ax = plt.subplots(figsize=(7.2, 4.4))
bmax = max(max(base_sw), max(enh_sw))
bins = range(0, bmax + 2)
ax.hist(base_sw, bins=bins, alpha=0.55, color=C_BASE, edgecolor="black", linewidth=0.5,
        label=f"基线（总 {sum(base_sw)} 次，均值 {sum(base_sw)/len(base_sw):.1f}）")
ax.hist(enh_sw, bins=bins, alpha=0.75, color=C_ENH, edgecolor="black", linewidth=0.5,
        label=f"增强（总 {sum(enh_sw)} 次，均值 {sum(enh_sw)/len(enh_sw):.1f}）")
red = (1 - sum(enh_sw) / max(sum(base_sw), 1)) * 100
ax.set_xlabel("每条视频等级切换次数")
ax.set_ylabel("视频条数")
ax.set_title(f"增强路径 vs 基线：等级切换次数分布（降幅 {red:.1f}%）", fontsize=12)
ax.legend(frameon=False, fontsize=9.5)
ax.grid(axis="y", alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
fig.tight_layout()
fig.savefig(f"{FIG}/fig7_switches_hist.png", dpi=DPI)
plt.close(fig)

# ---------- 图4：Fiedler 均值趋势 + 误差棒 ----------
fig, ax = plt.subplots(figsize=(6.0, 4.2))
xs = [0, 1, 2]
means = [mean_all[k] for k in LEVELS]
stds = [std_all[k] for k in LEVELS]
ax.errorbar(xs, means, yerr=stds, fmt="o-", color=C_DANGER, ecolor="#999999",
            elinewidth=1.2, capsize=5, linewidth=2.4, markersize=8)
for x, m, s in zip(xs, means, stds):
    ax.text(x + 0.06, m + s * 0.55, f"{m:.3f}\n±{s:.2f}", fontsize=9.5, va="center")
ax.set_xticks(xs)
ax.set_xticklabels(["SAFE\n低风险", "WARNING\n中风险", "DANGER\n高风险"])
ax.set_ylabel("Fiedler 特征值均值（含 0）")
ax.set_title("谱连通度随风险等级单调上升", fontsize=12)
ax.grid(alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
ax.text(0.02, 0.95, f"均值 0.017 → 0.214 → 1.078", transform=ax.transAxes,
        fontsize=10, color="#08519c", va="top", fontweight="bold")
fig.tight_layout()
fig.savefig(f"{FIG}/fig8_fiedler_trend.png", dpi=DPI)
plt.close(fig)

# ---------- 图5：消融双轴图 ----------
fig, ax1 = plt.subplots(figsize=(7.6, 4.4))
labels_short = ["A\n基线", "B\n+Kalman", "C\n+FSM\n(无谱)", "D\n完整"]
sw_means = [abl_sw_mean[c] for c in combos]
dang_vals = [abl_dang[c] for c in combos]
bars = ax1.bar(labels_short, sw_means, color=C_ABL, edgecolor="black", linewidth=0.6, width=0.6)
for b, v in zip(bars, sw_means):
    ax1.text(b.get_x() + b.get_width() / 2, v + 0.25, f"{v:.1f}", ha="center", fontsize=10)
ax1.set_ylabel("平均等级切换次数")
ax1.set_ylim(0, max(sw_means) * 1.18)
ax1.grid(axis="y", alpha=0.25, linestyle="--")
ax1.spines[["top", "right"]].set_visible(False)
ax2 = ax1.twinx()
ax2.plot(labels_short, dang_vals, "o--", color="#d7191c", linewidth=2, markersize=8, label="DANGER 帧合计")
for xi, v in zip(range(4), dang_vals):
    ax2.text(xi + 0.07, v + 28, f"{v}", fontsize=9.5, color="#d7191c", va="center")
ax2.set_ylabel("DANGER 帧合计（6 条视频）")
ax2.set_ylim(0, max(dang_vals) * 1.22)
ax2.spines[["top"]].set_visible(False)
ax2.legend(loc="upper left", frameon=False, fontsize=9.5)
ax1.set_title("消融实验：切换稳定性与 DANGER 识别（6 条代表性视频）", fontsize=12)
ax1.text(0.02, 0.92, "D 组在切换不增的前提下 DANGER 帧 +152（+10.2%）",
         transform=ax1.transAxes, fontsize=9.5, color="#08519c", va="top")
fig.tight_layout()
fig.savefig(f"{FIG}/fig_ablation_pro.png", dpi=DPI)
plt.close(fig)

# ---------- 图6：DANGER 视频数/帧数 对比（双面板） ----------
fig, axes = plt.subplots(1, 2, figsize=(8.6, 4.0))
ax = axes[0]
bd, ed = cmp_sum["base_danger"], cmp_sum["enh_danger"]
bars = ax.bar([0, 1], [bd, ed], width=0.5, color=[C_BASE, C_ENH], edgecolor="black", linewidth=0.6)
for b, v in zip(bars, [bd, ed]):
    ax.text(b.get_x() + b.get_width() / 2, v + 1.2, str(v), ha="center", fontsize=12)
ax.set_xticks([0, 1]); ax.set_xticklabels(["基线", "增强"])
ax.set_ylabel("出现 DANGER 的视频条数")
ax.set_title("DANGER 视频数（/111）", fontsize=11)
ax.set_ylim(0, max(bd, ed) * 1.2)
ax.grid(axis="y", alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
ax = axes[1]
bd_f, ed_f = cmp_sum["base_lv"]["DANGER"], cmp_sum["enh_lv"]["DANGER"]
bars = ax.bar([0, 1], [bd_f, ed_f], width=0.5, color=[C_BASE, C_ENH], edgecolor="black", linewidth=0.6)
for b, v in zip(bars, [bd_f, ed_f]):
    ax.text(b.get_x() + b.get_width() / 2, v + 350, fmt(v), ha="center", fontsize=11)
ax.set_xticks([0, 1]); ax.set_xticklabels(["基线", "增强"])
ax.set_ylabel("DANGER 帧数")
ax.set_title("DANGER 帧数（45021 帧）", fontsize=11)
ax.set_ylim(0, ed_f * 1.2)
ax.grid(axis="y", alpha=0.25, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
fig.suptitle("谱特征+FSM 增强链路的高风险识别提升", fontsize=12)
fig.tight_layout()
fig.savefig(f"{FIG}/fig_danger_compare.png", dpi=DPI)
plt.close(fig)

# ---------- 图7：等级帧占比堆叠对比 ----------
fig, ax = plt.subplots(figsize=(6.6, 4.4))
base_lv = cmp_sum["base_lv"]
enh_lv = cmp_sum["enh_lv"]
base_total_lv = sum(base_lv.values())
enh_total_lv = sum(enh_lv.values())
share_base = [base_lv[k] / base_total_lv * 100 for k in LEVELS]
share_enh = [enh_lv[k] / enh_total_lv * 100 for k in LEVELS]
x = [0, 1]
bottom_base = bottom_enh = 0
for k, color in zip(LEVELS, [C_SAFE, C_WARN, C_DANGER]):
    ax.bar(x[0], share_base[LV_NUM[k]], bottom=bottom_base, color=color, edgecolor="black", linewidth=0.4, width=0.48)
    ax.bar(x[1], share_enh[LV_NUM[k]], bottom=bottom_enh, color=color, edgecolor="black", linewidth=0.4, width=0.48)
    bottom_base += share_base[LV_NUM[k]]
    bottom_enh += share_enh[LV_NUM[k]]
for i, (sb, se) in enumerate(zip(share_base, share_enh)):
    if se > 6:
        ax.text(x[1], bottom_enh - se / 2, f"{se:.1f}%", ha="center", va="center", fontsize=9, color="black")
    if sb > 6:
        ax.text(x[0], bottom_base - sb / 2, f"{sb:.1f}%", ha="center", va="center", fontsize=9, color="black")
ax.set_xticks(x)
ax.set_xticklabels(["基线\n（逐帧 AND 规则）", "增强\n（谱特征+FSM）"])
ax.set_ylabel("帧数占比（%）")
ax.set_ylim(0, 100)
ax.set_title("等级帧占比：基线 vs 增强", fontsize=12)
ax.legend(labels=["SAFE", "WARNING", "DANGER"], loc="lower right", frameon=False, fontsize=9.5)
ax.grid(axis="y", alpha=0.2, linestyle="--")
ax.spines[["top", "right"]].set_visible(False)
ax.text(0.02, 0.95, "增强后 DANGER 占比 4.9% → 26.1%", transform=ax.transAxes,
        fontsize=9.5, color="#08519c", va="top")
fig.tight_layout()
fig.savefig(f"{FIG}/fig_level_share_compare.png", dpi=DPI)
plt.close(fig)

print("专业图表已生成到 docs/figures/")
print("  - fig5_111videos_level.png / fig6_fiedler_boxplot.png / fig7_switches_hist.png / fig8_fiedler_trend.png")
print("  - fig_ablation_pro.png / fig_danger_compare.png / fig_level_share_compare.png")
print(f"Spearman ρ = {rho:.4f}")