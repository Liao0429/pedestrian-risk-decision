# -*- coding: utf-8 -*-
"""真实数据绘图：图片基线 / 视频增强 / 算法前后对比。
全部数据来自 results/rerun/ 真实运行结果，零造假。
运行：python scripts/plot_results.py （输出 docs/figures/*.png）
"""
import json, os, re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

PROJECT = r"H:/PedestrianRiskDecision"
os.chdir(PROJECT)
FIG = "docs/figures"
os.makedirs(FIG, exist_ok=True)

# ---------- 1. 图片基线 ----------
data = json.load(open("results/rerun/images500_baseline/results.json", encoding="utf-8"))
res = data["results"]
levels = {"SAFE": 0, "WARNING": 0, "DANGER": 0}
sum_det = sum_gt = hd_det = hd_gt = 0
for r in res:
    levels[r["level"]] += 1
    n = r["n"]
    m = re.search(r"_(\d+)$", r["filename"])
    gt = int(m.group(1)) if m else 0
    sum_det += n; sum_gt += gt
    if gt >= 14:
        hd_det += n; hd_gt += gt
overall = sum_det / sum_gt * 100
hd_rate = hd_det / hd_gt * 100
total = data["total_images"]

# 图1：图片等级分布
fig, ax = plt.subplots(figsize=(6, 4.5))
labels = ["SAFE", "WARNING", "DANGER"]
vals = [levels[k] for k in labels]
colors = ["#2ecc71", "#f1c40f", "#e74c3c"]
bars = ax.bar(labels, vals, color=colors, edgecolor="black")
for b, v in zip(bars, vals):
    ax.text(b.get_x() + b.get_width() / 2, v + 5, f"{v}\n({v/total*100:.1f}%)", ha="center", fontsize=10)
ax.set_title(f"图片基线等级分布（{total} 张真实 COCO 图）")
ax.set_ylabel("图片数")
ax.set_ylim(0, max(vals) * 1.15)
fig.tight_layout(); fig.savefig(f"{FIG}/fig1_level_dist.png", dpi=150); plt.close(fig)

# 图2：检出率
fig, ax = plt.subplots(figsize=(6, 4.5))
names = ["总体检出率", "高密度(≥14人)"]
v = [overall, hd_rate]
bars = ax.bar(names, v, color=["#3498db", "#9b59b6"], edgecolor="black", width=0.5)
for b, val in zip(bars, v):
    ax.text(b.get_x() + b.get_width() / 2, val + 1, f"{val:.1f}%", ha="center", fontsize=12)
ax.set_title("图片基线检出率（真实推理）")
ax.set_ylabel("检出率 %")
ax.set_ylim(0, 100)
fig.tight_layout(); fig.savefig(f"{FIG}/fig2_detect_rate.png", dpi=150); plt.close(fig)

# ---------- 2. 视频增强聚合 ----------
agg = json.load(open("results/rerun/video_enhanced/_aggregate.json", encoding="utf-8"))
elv = agg["level_distribution"]
eframes = agg["total_frames"]
n = agg["total_ok"]

# ---------- 3. 前后对比 ----------
cmp = json.load(open("results/rerun/_comparison_summary.json", encoding="utf-8"))
b_sw = cmp["base_sw"] / n; e_sw = cmp["enh_sw"] / n
b_danger = cmp["base_danger"]; e_danger = cmp["enh_danger"]
b_safe = cmp["base_safe"]; e_safe = cmp["enh_safe"]

# 图3：前后对比（切换次数 / DANGER视频数 / 零误报）
fig, axes = plt.subplots(1, 3, figsize=(14, 4.5))
bc = "#95a5a6"; ec = "#2ecc71"
axes[0].bar(["基线", "增强"], [b_sw, e_sw], color=[bc, ec], edgecolor="black")
axes[0].set_title(f"平均切换次数/视频\n(↓{(1-e_sw/b_sw)*100:.1f}%)")
axes[0].set_ylabel("次数")
for i, val in enumerate([b_sw, e_sw]): axes[0].text(i, val + 0.5, f"{val:.1f}", ha="center")
axes[1].bar(["基线", "增强"], [b_danger, e_danger], color=[bc, ec], edgecolor="black")
axes[1].set_title(f"出现 DANGER 视频数\n(↓{(1-e_danger/b_danger)*100:.1f}%)")
for i, val in enumerate([b_danger, e_danger]): axes[1].text(i, val + 0.5, str(val), ha="center")
axes[2].bar(["基线", "增强"], [b_safe, e_safe], color=[bc, ec], edgecolor="black")
axes[2].set_title(f"全程 SAFE(零误报)视频数\n(↑{(e_safe/b_safe-1)*100:.1f}%)")
for i, val in enumerate([b_safe, e_safe]): axes[2].text(i, val + 0.5, str(val), ha="center")
for a in axes:
    a.set_ylim(0, max(b_sw, e_sw, b_danger, e_danger, b_safe, e_safe) * 1.25)
fig.suptitle(f"增强算法前后对比（{n} 条真实视频）", fontsize=14)
fig.tight_layout(); fig.savefig(f"{FIG}/fig3_before_after.png", dpi=150); plt.close(fig)

# 图4：视频增强等级分布
fig, ax = plt.subplots(figsize=(6, 4.5))
names = ["SAFE", "WARNING", "DANGER"]
vv = [elv.get(k, 0) for k in names]
colors = ["#2ecc71", "#f1c40f", "#e74c3c"]
bars = ax.bar(names, vv, color=colors, edgecolor="black")
for b, val in zip(bars, vv):
    ax.text(b.get_x() + b.get_width() / 2, val + 300, f"{val}\n({val/eframes*100:.1f}%)", ha="center", fontsize=9)
ax.set_title(f"视频增强等级分布（{n} 条视频，{eframes} 帧）")
ax.set_ylabel("帧数")
ax.set_ylim(0, max(vv) * 1.15)
fig.tight_layout(); fig.savefig(f"{FIG}/fig4_video_level.png", dpi=150); plt.close(fig)

print("已生成图表:")
for f in sorted(os.listdir(FIG)):
    print("  ", f)