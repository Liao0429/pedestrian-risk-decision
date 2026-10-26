# -*- coding: utf-8 -*-
"""
视频增强算法"前后对比"——基线版本（算法前）
- 逐帧检测 + 计算网络特征（n/density/r_gcc），按 RiskRule 阈值直接判定等级（无 FSM 时序平滑、无 Kalman、无谱特征）
- 输出每帧等级序列 + 切换次数 + 等级分布，用于与增强版（FSM）对比
运行：python scripts/rerun_video_baseline.py [--videos v1 v2 ...]（默认全部）
"""
import sys, os, json, glob, argparse
from collections import Counter
from pathlib import Path

PROJECT = str(Path(__file__).resolve().parent.parent)
os.chdir(PROJECT)
sys.path.insert(0, os.path.join(PROJECT, "scripts"))

import cv2  # noqa: E402
from ultralytics import YOLO  # noqa: E402
from crowd_test_enhanced import detect_pedestrians, compute_features, load_config  # noqa: E402
from crowd_test import get_homography  # noqa: E402


def rule_level(n, rho, gcc, cfg):
    """无 FSM 的逐帧规则判定（算法前基线），与 C++ RiskEngine 阈值一致。"""
    d = cfg["danger"]
    w = cfg["warning"]
    if n >= d["n"] or rho >= d["rho"] or gcc >= d["gcc"]:
        return "DANGER"
    if n >= w["n"] or rho >= w["rho"] or gcc >= w["gcc"]:
        return "WARNING"
    return "SAFE"


def process_baseline(model, video, out_dir, cfg, edge_dist, device=0):
    os.makedirs(out_dir, exist_ok=True)
    cap = cv2.VideoCapture(video)
    name = os.path.splitext(os.path.basename(video))[0]
    ok, frame = cap.read()
    if not ok:
        return None
    H = get_homography(frame.shape[1], frame.shape[0])  # 真实 IPM（与增强路径一致）
    levels, feats = [], []
    while frame is not None:
        _, _, pts = detect_pedestrians(model, frame, 0.25, 640, device, H, None, [0])
        n, m, density, r_gcc, clustering = compute_features(pts, edge_dist)
        lvl = rule_level(n, density, r_gcc, cfg)
        levels.append(lvl)
        feats.append({"n": n, "density": density, "r_gcc": r_gcc, "level": lvl})
        ok, frame = cap.read()
        if not ok:
            frame = None
    switches = sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1])
    dist = dict(Counter(levels))
    result = {"video": name, "frames": len(levels), "switches": switches,
            "level_distribution": dist, "timeline": feats}
    with open(os.path.join(out_dir, f"{name}_baseline.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--videos", nargs="*", default=None, help="指定视频名（不含.mp4）；默认全部")
    args = ap.parse_args()
    cfg = load_config("config/default_config.json")
    edge_dist = cfg.get("edge_dist_m", 2.0)
    model = YOLO("yolov8n.pt")
    videos = sorted(glob.glob("dataset/videos50/*.mp4"))
    if args.videos:
        videos = [v for v in videos if os.path.splitext(os.path.basename(v))[0] in args.videos]
    out_root = "results/rerun/video_baseline"
    os.makedirs(out_root, exist_ok=True)
    summary = {}
    for i, v in enumerate(videos, 1):
        name = os.path.splitext(os.path.basename(v))[0]
        r = process_baseline(model, v, os.path.join(out_root, name), cfg, edge_dist)
        summary[name] = {"frames": r["frames"], "switches": r["switches"],
                         "level_distribution": r["level_distribution"]}
        print(f"[{i}/{len(videos)}] {name} switches={r['switches']} "
              f"dist={r['level_distribution']}", flush=True)
    with open(os.path.join(out_root, "_summary.json"), "w", encoding="utf-8") as f:
        json.dump({"total": len(videos), "results": summary}, f, ensure_ascii=False, indent=2)
    print("DONE_BASELINE")


if __name__ == "__main__":
    main()