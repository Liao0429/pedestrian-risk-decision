# -*- coding: utf-8 -*-
"""核对零误报视频是否为"真实稀疏/无人"场景（避免暗光人群被漏检误判为零误报）。
方法：对每条零误报视频采样 ~15 帧，统计平均亮度（暗光夜店亮度低）和实际检测人数（真稀疏应 <0.5人/帧）。
运行：python scripts/find_sparse_video.py
"""
import sys, os, json, cv2
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "scripts"))
os.chdir(PROJECT_ROOT)
from ultralytics import YOLO  # noqa: E402
from crowd_test import get_homography  # noqa: E402
from crowd_test_enhanced import detect_pedestrians  # noqa: E402

model = YOLO("yolov8n.pt")

agg = json.load(open("results/rerun/video_enhanced/_aggregate.json", encoding="utf-8"))
zero_safe = [v["name"] for v in agg["per_video"]
            if v["dist"].get("DANGER", 0) == 0 and v["dist"].get("WARNING", 0) == 0]
print(f"待核对零误报视频: {len(zero_safe)} 条")

results = []
for name in zero_safe:
    vp = f"dataset/videos50/{name}.mp4"
    if not os.path.isfile(vp):
        continue
    cap = cv2.VideoCapture(vp)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    ok0, frame0 = cap.read()
    if not ok0:
        cap.release(); continue
    H = get_homography(frame0.shape[1], frame0.shape[0])
    step = max(1, total // 15)
    tb = 0.0; tn = 0.0; sampled = 0
    for f in range(0, total, step):
        cap.set(cv2.CAP_PROP_POS_FRAMES, f)
        ok, frame = cap.read()
        if not ok:
            continue
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        tb += float(gray.mean())
        _, _, pts = detect_pedestrians(model, frame, 0.25, 640, 0, H, None, [0])
        tn += len(pts)
        sampled += 1
    cap.release()
    if sampled:
        results.append({"name": name, "frames": total,
                         "avg_brightness": round(tb / sampled, 1),
                         "avg_detected": round(tn / sampled, 2)})

results.sort(key=lambda x: -x["avg_brightness"])
print("\n=== 零误报视频核对（亮度高 + 检测人少 = 真稀疏/无人） ===")
for r in results:
    flag = "✓真稀疏" if (r["avg_brightness"] > 80 and r["avg_detected"] < 0.5) else "⚠可能暗光/有漏检"
    print(f"  {r['name']}: 亮度{r['avg_brightness']:.0f}  平均检测{r['avg_detected']}人  {flag}")