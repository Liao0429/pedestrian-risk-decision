#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
crowd_test_enhanced.py - 增强版测试脚本
集成：
  1. 图拉普拉斯谱特征(compute_laplacian_features: fiedler/谱半径/代数连通度)
  2. Kalman 多目标平滑(kalman_tracker)
  3. FSM 风险决策状态机(risk_fsm, 滞回+保持时间)
支持：视频输入，逐帧处理输出时序结果。
用法示例：
  python crowd_test_enhanced.py --input dataset/videos50/xxx.mp4 --output results/video_test --spectral --device 0
"""
import argparse, json, os, sys, time
import numpy as np

# 延迟导入：cv2/YOLO 仅 CLI 检测需要，缺失时置 None，main() 统一报错
try:
    import cv2
except ImportError:  # pragma: no cover
    cv2 = None

try:
    from ultralytics import YOLO
except ImportError:  # pragma: no cover
    YOLO = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from crowd_test import (compute_features, evaluate_level, get_homography,
                        load_homography, map_to_ground, load_config, EDGE_DIST_M)
from src.algorithms.graph_laplacian import compute_laplacian_features
from src.algorithms.kalman_tracker import KalmanTracker
from src.algorithms.risk_fsm import RiskFSM, RiskFSMConfig

def detect_pedestrians(model, frame, conf, imgsz, device, H, scale_mpp, classes=None):
    """检测目标类别（默认 person），返回 (boxes_xyxy, detections, ground_pts)。"""
    results = model.predict(frame, conf=conf, imgsz=imgsz, classes=classes or [0], device=device, verbose=False)
    boxes_xyxy, detections, pts = [], [], []
    if results and len(results) > 0:
        r = results[0]
        if r.boxes is not None and len(r.boxes) > 0:
            xyxy = r.boxes.xyxy.cpu().numpy()
            confs = r.boxes.conf.cpu().numpy()
            for (x1, y1, x2, y2), c in zip(xyxy, confs):
                boxes_xyxy.append(((x1, y1, x2, y2), float(c)))
                detections.append({"bbox": [float(x1), float(y1), float(x2), float(y2)], "conf": float(c)})
                foot_x = (x1 + x2) / 2.0
                foot_y = y2
                if H is not None:
                    gx, gy = map_to_ground(foot_x, foot_y, H)
                else:
                    gx, gy = foot_x * scale_mpp, foot_y * scale_mpp
                pts.append((gx, gy))
    return boxes_xyxy, detections, pts

def draw_frame(img, boxes_xyxy, level, feat):
    img = img.copy()
    h, w = img.shape[:2]
    color = (0, 255, 0)
    if level == "WARNING":
        color = (0, 255, 255)
    elif level == "DANGER":
        color = (0, 0, 255)
    for (x1, y1, x2, y2), c in boxes_xyxy:
        cv2.rectangle(img, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
        cv2.putText(img, "person %.2f" % c, (int(x1), int(y1) - 5),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
    info = "CROWD:%s n=%d rho=%.2f r_gcc=%.2f" % (level, feat.get("n", 0),
                                                   feat.get("density", 0.0),
                                                   feat.get("r_gcc", 0.0))
    if "fiedler" in feat:
        info += " fiedler=%.3f" % feat["fiedler"]
    cv2.rectangle(img, (0, 0), (w, 45), color, -1)
    cv2.putText(img, info, (8, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 2)
    return img

def process_video(model, video_path, out_dir, conf, imgsz, H, scale_mpp, device,
                   edge_dist, cfg, use_spectral, classes=None):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print("[ERROR] 无法打开视频:", video_path, file=sys.stderr)
        return
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    if H is None and scale_mpp is None:
        H = get_homography(width, height)
        print("[INFO] 未指定 --homography/--scale，使用默认 IPM 标定 H（与基线/C++ 端一致）", file=sys.stderr)
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(video_path))[0]
    out_path = os.path.join(out_dir, base + "_enhanced.mp4")
    writer = cv2.VideoWriter(out_path, cv2.VideoWriter_fourcc(*"mp4v"), fps, (width, height))

    tracker = KalmanTracker(dt=1.0 / max(fps, 1), gate_dist=edge_dist)
    fsm_cfg = RiskFSMConfig(**(cfg.get("fsm", {}) if cfg else {}))
    fsm = RiskFSM(fsm_cfg)

    timeline = []
    frame_id = 0
    LEVELS = ["SAFE", "WARNING", "DANGER"]
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        boxes_xyxy, detections, raw_pts = detect_pedestrians(
            model, frame, conf, imgsz, device, H, scale_mpp, classes)

        # Kalman 平滑脚点
        states = tracker.update(raw_pts)   # {id: (x, y, vx, vy)}
        smooth_pts = [(s[0], s[1]) for s in states.values()]

        # 复杂网络特征 + 谱特征
        n, m, density, r_gcc, clustering = compute_features(smooth_pts, edge_dist)
        feat = {"n": n, "m": m, "density": density, "r_gcc": r_gcc, "clustering": clustering}
        if use_spectral and smooth_pts:
            try:
                spec = compute_laplacian_features(smooth_pts, edge_dist)
                for k in ["fiedler", "spectral_radius", "algebraic_connectivity", "eig_entropy"]:
                    feat[k] = spec.get(k, 0.0)
            except Exception:
                pass

        # 单帧等级 -> FSM 稳定等级（RiskFSM.update 期望 Mapping，传 dict）
        raw_level = evaluate_level(n, density, r_gcc, cfg)
        try:
            now_ms = int(frame_id * 1000.0 / max(fps, 1))
            # 传 dict 而非 SimpleNamespace：RiskFSM 按 Mapping 读取 features
            fsm.update({"n": n, "density": density, "r_gcc": r_gcc}, now_ms)
            # 注意：RiskFSM.level 是属性（返回字符串），不是方法
            lv = fsm.level
            stable_level = LEVELS[lv] if isinstance(lv, int) else str(lv)
        except Exception as e:
            # 兜底：FSM 异常时退回单帧规则判定，但打印告警避免静默
            print("[WARN] FSM 更新失败，回退单帧判定: %s" % e, file=sys.stderr)
            stable_level = raw_level

        timeline.append({"frame": frame_id, "level": stable_level, "n": n,
                         "density": density, "r_gcc": r_gcc,
                         "fiedler": feat.get("fiedler", 0.0)})
        annotated = draw_frame(frame, boxes_xyxy, stable_level, feat)
        writer.write(annotated)
        frame_id += 1
        if frame_id % 30 == 0:
            print("  [%d] n=%d level=%s" % (frame_id, n, stable_level), flush=True)

    cap.release()
    writer.release()
    tl_path = os.path.join(out_dir, base + "_timeline.json")
    with open(tl_path, "w", encoding="utf-8") as f:
        json.dump(timeline, f, ensure_ascii=False, indent=2)
    # 等级统计
    dist = {}
    for it in timeline:
        dist[it["level"]] = dist.get(it["level"], 0) + 1
    print("视频处理完成:", out_path, "帧数:", frame_id, "等级分布:", dist)
    print("时间线已保存:", tl_path)

def main():
    if cv2 is None:
        sys.exit("[crowd_test_enhanced] 缺少 opencv，请先安装: pip install opencv-python-headless")
    if YOLO is None:
        sys.exit("[crowd_test_enhanced] 缺少 ultralytics，请先安装: pip install ultralytics")
    parser = argparse.ArgumentParser(description="crowd_test 增强版：谱特征 + Kalman + FSM")
    parser.add_argument("--input", required=True, help="视频文件路径")
    parser.add_argument("--output", default="output_enhanced")
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--weights", default="yolov8n.pt")
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--homography", default=None)
    parser.add_argument("--scale", type=float, default=None, help="无 H 时的近似比例尺(米/像素)；默认用真实 IPM")
    parser.add_argument("--device", default=None)
    parser.add_argument("--classes", default="0",
                        help="检测目标类别，逗号分隔的 COCO id 列表（默认 0=person；如 2,5,7,3=车辆；16=狗；15=猫）")
    parser.add_argument("--edge-dist", type=float, default=EDGE_DIST_M)
    parser.add_argument("--config", default=None)
    parser.add_argument("--spectral", action="store_true", help="启用图拉普拉斯谱特征")
    args = parser.parse_args()
    classes = [int(x) for x in args.classes.split(",") if x.strip()]

    cfg = load_config(args.config)
    edge_dist = cfg.get("edge_dist_m", args.edge_dist)
    os.makedirs(args.output, exist_ok=True)
    model = YOLO(args.weights)
    H = None
    if args.homography:
        H = load_homography(args.homography)
    if not (os.path.isfile(args.input) and args.input.lower().endswith(
            (".mp4", ".avi", ".mov", ".mkv", ".webm"))):
        print("[ERROR] 增强版当前支持视频文件(--input video.mp4)，图片请用 crowd_test.py", file=sys.stderr)
        return 1
    process_video(model, args.input, args.output, args.conf, args.imgsz, H,
                  args.scale, args.device, edge_dist, cfg, args.spectral, args.classes)

if __name__ == "__main__":
    main()