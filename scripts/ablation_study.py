# -*- coding: utf-8 -*-
"""消融实验（Ablation Study）——论文核心工作量章节。
对代表性视频，分别关闭/开启 Kalman / 谱特征 / FSM，量化各组件的贡献。
配置组合：
  A. 基线（无 Kalman、无 FSM）：逐帧规则判定
  B. +Kalman（仅跟踪平滑，规则判定）
  C. +Kalman + FSM（无谱特征）：量化 FSM 滞回稳定性贡献
  D. +Kalman + 谱特征 + FSM（完整增强）：量化谱特征贡献
输出：每视频每组等级分布/切换次数 + 汇总图 docs/figures/fig_ablation.png
运行：python scripts/ablation_study.py
"""
import sys, os, json, time

# 延迟导入：rule_level 等纯函数不依赖 cv2/YOLO，缺失时仅 main() 报错
try:
    import cv2
except ImportError:  # pragma: no cover
    cv2 = None

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "scripts"))
os.chdir(PROJECT_ROOT)
try:
    from ultralytics import YOLO  # noqa: E402
except ImportError:  # pragma: no cover
    YOLO = None
from crowd_test import get_homography  # noqa: E402
from crowd_test_enhanced import (detect_pedestrians, compute_features,
                                 load_config)  # noqa: E402
from src.algorithms.kalman_tracker import KalmanTracker  # noqa: E402
from src.algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402
from src.algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402

VIDEOS = ["mixkit_134", "mixkit_13192", "mixkit_3428", "mixkit_4519",
          "mixkit_4178", "mixkit_50134"]
# 消融组合（修正：C 组用 FSM 但关闭谱项，D 组开启谱项，使谱特征贡献可量化）
#   元组：(标签, use_kalman, use_spectral, use_fsm)
#   A 基线：逐帧规则判定（无增强）
#   B +Kalman：Kalman 平滑 + 规则判定（无 FSM）
#   C +FSM(无谱特征)：Kalman + FSM（谱项 w4 不生效，fiedler=0）
#   D 完整(增强)：Kalman + 谱特征 + FSM（w4 谱项生效）
COMBOS = [
    ("A 基线(无增强)",    False, False, False),
    ("B +Kalman",         True,  False, False),
    ("C +FSM(无谱特征)",   True,  False,  True),
    ("D 完整(增强)",       True,   True,   True),
]


def rule_level(n, rho, gcc, cfg):
    """规则阈值分级（与 crowd_test.evaluate_level 的 AND 逻辑一致，避免消融基线失真）。"""
    d, w = cfg.get("danger", {}), cfg.get("warning", {})
    if n >= d.get("n", 10) and (rho >= d.get("rho", 0.55) or gcc >= d.get("gcc", 0.80)):
        return "DANGER"
    if n >= w.get("n", 5) and (rho >= w.get("rho", 0.28) or gcc >= w.get("gcc", 0.55)):
        return "WARNING"
    return "SAFE"


def run_one(model, video, H, cfg, edge_dist, use_kalman, use_spectral, use_fsm):
    cap = cv2.VideoCapture(video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    tracker = KalmanTracker(dt=1.0 / max(fps, 1), gate_dist=edge_dist) if use_kalman else None
    fsm = RiskFSM(RiskFSMConfig(**(cfg.get("fsm", {}) if cfg else {}))) if use_fsm else None
    levels = []
    now_ms = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        _, _, pts = detect_pedestrians(model, frame, 0.25, 640, 0, H, None, [0])
        if use_kalman and tracker is not None:
            states = tracker.update(pts)
            smooth = [(s[0], s[1]) for s in states.values()]
        else:
            smooth = pts
        n, m, density, r_gcc, clustering = compute_features(smooth, edge_dist)
        feat = {"n": n, "m": m, "density": density, "r_gcc": r_gcc, "clustering": clustering}
        if use_spectral and smooth:
            try:
                spec = compute_laplacian_features(smooth, edge_dist)
                feat.update({k: spec.get(k, 0.0) for k in
                             ["fiedler", "spectral_radius", "algebraic_connectivity", "eig_entropy"]})
            except Exception:
                pass
        if use_fsm and fsm is not None:
            fsm.update({"n": n, "density": density, "r_gcc": r_gcc,
                        "fiedler": feat.get("fiedler", 0.0)}, now_ms)
            lv = fsm.level
        else:
            lv = rule_level(n, density, r_gcc, cfg)
        levels.append(lv)
        now_ms += int(1000.0 / max(fps, 1))
    cap.release()
    from collections import Counter
    dist = dict(Counter(levels))
    switches = sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1])
    return {"frames": len(levels), "switches": switches, "dist": dist}


def main():
    if cv2 is None:
        sys.exit("[ablation_study] 缺少 opencv，请先安装: pip install opencv-python-headless")
    if YOLO is None:
        sys.exit("[ablation_study] 缺少 ultralytics，请先安装: pip install ultralytics")
    model = YOLO("yolov8n.pt")
    cfg = load_config("config/default_config.json")
    edge_dist = cfg.get("edge_dist_m", 2.0)

    # 预计算每视频 H
    H_cache = {}
    for name in VIDEOS:
        vp = f"dataset/videos50/{name}.mp4"
        cap = cv2.VideoCapture(vp)
        ok, frame = cap.read()
        if ok:
            H_cache[name] = get_homography(frame.shape[1], frame.shape[0])
        cap.release()

    summary = {"videos": VIDEOS, "combos": [c[0] for c in COMBOS], "results": {}}
    for name in VIDEOS:
        summary["results"][name] = {}
        vp = f"dataset/videos50/{name}.mp4"
        for label, uk, us, uf in COMBOS:
            r = run_one(model, vp, H_cache[name], cfg, edge_dist, uk, us, uf)
            summary["results"][name][label] = r
            print(f"{name:14s} {label:16s} 切换{r['switches']:3d} 分布{r['dist']}", flush=True)
        print("---", flush=True)

    os.makedirs("results/rerun", exist_ok=True)
    with open("results/rerun/_ablation.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    # 绘图：每视频切换次数对比
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
    plt.rcParams["axes.unicode_minus"] = False
    labels = [c[0] for c in COMBOS]
    nv = len(VIDEOS)
    fig, axes = plt.subplots(2, 3, figsize=(15, 8))
    axes = axes.flatten()
    for idx, name in enumerate(VIDEOS):
        ax = axes[idx]
        vals = [summary["results"][name][lbl]["switches"] for lbl in labels]
        bars = ax.bar(labels, vals, color=["#95a5a6", "#5dade2", "#af7ac5", "#2ecc71"], edgecolor="black")
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 0.3, str(v), ha="center", fontsize=9)
        ax.set_title(name)
        ax.set_ylabel("等级切换次数")
        ax.tick_params(axis="x", rotation=15)
        ax.set_ylim(0, max(vals) * 1.25)
    fig.suptitle("消融实验：各组件对等级稳定性的贡献（切换次数越低越稳定）", fontsize=14)
    fig.tight_layout()
    os.makedirs("docs/figures", exist_ok=True)
    fig.savefig("docs/figures/fig_ablation.png", dpi=150)
    plt.close(fig)
    print("消融结果已保存 results/rerun/_ablation.json + docs/figures/fig_ablation.png")


if __name__ == "__main__":
    main()