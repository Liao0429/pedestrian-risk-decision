#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按沉稳配色主题重制全部数据图表 + 首屏主视觉。

原理：不改动任何原始绘图脚本的数据逻辑，只在源码层把硬编码 hex 映射到
palette.py 的主题色，然后 exec 执行原脚本，因此数据口径与原脚本完全一致。
这样 16 张图的数据来源、统计口径与原脚本 100% 一致。

运行：
    G:/DevTools/python/python.exe docs/figures/_style/make_figures.py
"""
import os, re, sys, json, glob

ROOT = r"G:\PedestrianRiskDecision"
STYLE = os.path.join(ROOT, "docs", "figures", "_style")
sys.path.insert(0, STYLE)
os.chdir(ROOT)

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from palette import apply, colors, level_colors, get, cmap_seq, cmap_div

FIG = os.path.join(ROOT, "docs", "figures")
os.makedirs(FIG, exist_ok=True)

# 原绘图脚本会重写 results/rerun/_aggregate.json（median 被 float 化、_comment 溯源注释丢失），
# 重出图前做快照、跑完还原，避免污染数据产物。
AGG = os.path.join(ROOT, "results", "rerun", "_aggregate.json")
_AGG_SNAPSHOT = open(AGG, "rb").read() if os.path.exists(AGG) else None

# 旧 hex -> 主题语义
def build_map(theme):
    T = get(theme)
    lc = level_colors(theme)
    P = colors(theme)
    return {
        "#2ecc71": lc["safe"], "#4dac26": lc["safe"],
        "#f1c40f": lc["warning"],
        "#e74c3c": lc["danger"], "#d7191c": lc["danger"], "#8c2d04": lc["danger"],
        "#2166ac": P[0], "#4292c6": P[2], "#3498db": P[0],
        "#9b59b6": P[6],
        "#9ecae1": P[6], "#08519c": P[1],
        "#95a5a6": T.text_muted, "#7f8c8d": T.text_muted, "#bdbdbd": T.grid, "#999999": T.grid,
        "#333333": T.text, "#444444": T.axis, "#555555": T.text_muted, "#666666": T.text_muted, "#888888": T.text_muted,
    }

SOURCES = [
    ("scripts/plot_data_analysis.py", "agg"),
    ("scripts/plot_professional_figures.py", "pro"),
    ("scripts/plot_results.py", "res"),
    ("scripts/plot_fsm_timeline.py", "fsm"),
]

def patch(src, m, suffix):
    src = re.sub(r"#[0-9a-fA-F]{6}", lambda x: m.get(x.group(0).lower(), x.group(0)), src)
    if suffix:
        src = re.sub(r"\.png([\"'])", "_" + suffix + r".png\1", src)
    return src

def run_all(theme, suffix):
    # plot_fsm_timeline.py 末尾有 sys.exit(main())，exec 时会把整个进程干掉
    sys.exit = lambda *a, **k: None
    apply(theme)
    m = build_map(theme)
    print(f"\n===== theme={theme} suffix={suffix} =====")
    for rel, tag in SOURCES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            print(f"  [skip] {rel} 不存在"); continue
        raw = open(path, encoding="utf-8").read()
        code = patch(raw, m, suffix)
        g = {"__name__": "__main__", "__file__": path}
        try:
            exec(compile(code, path, "exec"), g)
            print(f"  [ok] {rel}")
        except Exception as e:
            print(f"  [FAIL] {rel}: {type(e).__name__}: {e}")

if "--hero-only" not in sys.argv:
    # 只生成沉稳主题，直接覆盖主文件名（不带后缀）
    run_all("steady", "")

# ---------------- hero_overview.png ----------------
def hero():
    apply("steady")
    T = get("steady")
    lc = level_colors("steady")
    P = colors("steady")
    PB = colors("steady")
    lcB = level_colors("steady")

    summary = json.load(open(os.path.join(ROOT, "results", "images500_gt_eval", "summary.json"), encoding="utf-8"))

    fig = plt.figure(figsize=(16, 6.2))
    gs = fig.add_gridspec(1, 3, width_ratios=[1.25, 1.0, 1.35], wspace=0.16)

    # 左：检测效果示例
    ax1 = fig.add_subplot(gs[0, 0])
    cands = sorted(glob.glob(os.path.join(ROOT, "results", "rerun", "images500_baseline", "vis", "*.jpg")))
    picked = None
    for c in cands:
        if "crowd" in os.path.basename(c):
            picked = c; break
    if picked is None and cands:
        picked = cands[0]
    if picked:
        import matplotlib.image as mpimg
        img = mpimg.imread(picked)
        ax1.imshow(img)
        ax1.set_xticks([]); ax1.set_yticks([])
        for s in ax1.spines.values():
            s.set_edgecolor(T.grid); s.set_linewidth(1.2)
        ax1.set_title("边缘端实时检测输出（真机同源模型）", fontsize=11.5, color=T.text, pad=10)
        ax1.text(0.5, -0.06, os.path.basename(picked), transform=ax1.transAxes,
                 ha="center", va="top", fontsize=8.5, color=T.text_muted)
    else:
        ax1.text(0.5, 0.5, "检测效果示例\n（未找到 vis 图）", ha="center", va="center", fontsize=12, color=T.text_muted)
        ax1.axis("off")

    # 中：核心指标卡片
    ax2 = fig.add_subplot(gs[0, 1]); ax2.axis("off")
    ax2.set_xlim(0, 1); ax2.set_ylim(0, 1)
    cards = [
        ("检出率", f"{summary['overall_detection_rate_pct']:.2f}%", "500 张真实 COCO 图 / GT 3089", PB[0]),
        ("计数 MAE", f"{summary['mae']:.3f}", "人数估计平均绝对误差", PB[2]),
        ("Pearson r", f"{summary['pearson_r']:.4f}", "估计人数与真值相关性", PB[4]),
    ]
    y = 0.86
    for label, val, sub, col in cards:
        ax2.add_patch(plt.Rectangle((0.02, y - 0.20), 0.96, 0.22,
                                    facecolor=T.panel, edgecolor=col, linewidth=2.2))
        ax2.text(0.06, y - 0.035, label, fontsize=10.5, color=T.text_muted, va="center")
        ax2.text(0.06, y - 0.115, val, fontsize=25, color=col, va="center", fontweight="bold")
        ax2.text(0.06, y - 0.185, sub, fontsize=8.4, color=T.text_muted, va="center")
        y -= 0.30
    ax2.set_title("核心成果（真实数据）", fontsize=13, color=T.text, pad=10)

    # 右：架构简化框图
    ax3 = fig.add_subplot(gs[0, 2]); ax3.axis("off")
    ax3.set_xlim(0, 10); ax3.set_ylim(0, 10)
    ax3.set_title("系统链路：采集 → 感知 → 决策 → 交互", fontsize=13, color=T.text, pad=10)
    boxes = [
        (0.4, 8.0, 9.2, 1.2, "摄像头采集  /  MIPI-CSI", P[1]),
        (0.4, 6.2, 9.2, 1.2, "YOLOv5n + RKNN INT8 量化检测", P[2]),
        (0.4, 4.4, 9.2, 1.2, "IPM 逆透视映射  +  Kalman / ByteTrack 跟踪", P[3]),
        (0.4, 2.6, 9.2, 1.2, "图拉普拉斯谱特征  +  DBSCAN 人群聚类", P[4]),
        (0.4, 0.8, 9.2, 1.2, "FSM 时序风险决策  →  LED / 云台 / 语音", lcB["danger"]),
    ]
    for x, y0, w, h, txt, col in boxes:
        ax3.add_patch(plt.Rectangle((x, y0), w, h, facecolor=T.panel, edgecolor=col, linewidth=2.0))
        ax3.text(x + w / 2, y0 + h / 2, txt, ha="center", va="center", fontsize=9.6, color=T.text)
    for y0 in (7.9, 6.1, 4.3, 2.5):
        ax3.annotate("", xy=(5, y0 - 0.15), xytext=(5, y0 + 0.15),
                     arrowprops=dict(arrowstyle="-|>", color=T.text_muted, lw=1.6))
    ax3.text(5, 9.6, "Luckfox Pico Max / RV1106 · C++ 板端", ha="center", fontsize=9, color=T.text_muted)

    out = os.path.join(FIG, "hero_overview.png")
    fig.savefig(out, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("\nhero_overview.png ->", out)

try:
    hero()
except Exception as e:
    print("hero FAIL:", type(e).__name__, e)

# ---------------- 汇总 ----------------
if _AGG_SNAPSHOT is not None:
    open(AGG, "wb").write(_AGG_SNAPSHOT)

figs = sorted(f for f in glob.glob(os.path.join(FIG, "*.png"))
              if not os.path.basename(f).startswith(("demo_", "palette_preview")))
print(f"\n主图: {len(figs)} 张")
for f in figs: print("  ", os.path.basename(f), f"{os.path.getsize(f)//1024}KB")
print("hero:", os.path.exists(os.path.join(FIG, "hero_overview.png")))
