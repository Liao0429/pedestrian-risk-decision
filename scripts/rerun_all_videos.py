# -*- coding: utf-8 -*-
"""
真实数据集全量视频增强实验（复现脚本）
- 遍历 dataset/videos50/ 下全部 111 条真实 Mixkit 视频
- 用 crowd_test_enhanced.py 的 process_video（Kalman + 谱特征 + FSM）逐条处理
- 汇总每视频时间线 JSON 的等级分布到 results/rerun/video_enhanced/_summary.json
运行：python scripts/rerun_all_videos.py
"""
import sys, os, glob, json, time

PROJECT = r"H:/PedestrianRiskDecision"
os.chdir(PROJECT)
sys.path.insert(0, os.path.join(PROJECT, "scripts"))

from ultralytics import YOLO  # noqa: E402
from crowd_test_enhanced import process_video, load_config  # noqa: E402

def main():
    weights = "yolov8n.pt"
    video_dir = "dataset/videos50"
    out_root = "results/rerun/video_enhanced"
    os.makedirs(out_root, exist_ok=True)

    videos = sorted(glob.glob(os.path.join(video_dir, "*.mp4")))
    print(f"[rerun] 发现 {len(videos)} 条视频", flush=True)
    cfg = load_config("config/default_config.json")
    edge_dist = cfg.get("edge_dist_m", 2.0)

    model = YOLO(weights)
    summary = {}
    t_start = time.time()
    for i, video in enumerate(videos, 1):
        name = os.path.splitext(os.path.basename(video))[0]
        vout = os.path.join(out_root, name)
        t0 = time.time()
        try:
            # use_spectral=True：启用图拉普拉斯谱特征增强路径
            process_video(model, video, vout, conf=0.25, imgsz=640, H=None,
                          scale_mpp=None, device=0, edge_dist=edge_dist,
                          cfg=cfg, use_spectral=True, classes=[0])
            tl = os.path.join(vout, f"{name}_timeline.json")
            dur = round(time.time() - t0, 1)
            summary[name] = {"status": "ok", "seconds": dur, "timeline": tl}
            print(f"[{i}/{len(videos)}] OK {name} {dur}s", flush=True)
        except Exception as e:
            summary[name] = {"status": "error", "error": str(e)}
            print(f"[{i}/{len(videos)}] FAIL {name}: {e}", flush=True)

    with open(os.path.join(out_root, "_summary.json"), "w", encoding="utf-8") as f:
        json.dump({"total": len(videos),
                   "total_seconds": round(time.time() - t_start, 1),
                   "results": summary}, f, ensure_ascii=False, indent=2)
    print(f"[rerun] 完成，共 {len(videos)} 条，耗时 {round(time.time()-t_start,1)}s", flush=True)

if __name__ == "__main__":
    main()