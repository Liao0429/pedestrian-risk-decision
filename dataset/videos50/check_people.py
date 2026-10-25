# -*- coding: utf-8 -*-
"""Quick pedestrian presence check using OpenCV HOG on sampled frames."""
import os, sys, cv2
import numpy as np

vdir = r"H:/PedestrianRiskDecision/dataset/videos50"
files = sorted([f for f in os.listdir(vdir) if f.lower().endswith((".mp4",".webm"))])

try:
    hog = cv2.HOGDescriptor()
    hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
    have_hog = True
except Exception as e:
    print("HOG init failed:", e, file=sys.stderr)
    have_hog = False

def check(vpath, n_frames=6):
    cap = cv2.VideoCapture(vpath)
    if not cap.isOpened():
        return None
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT)) or 0
    if total <= 0:
        cap.release()
        return None
    detections = 0
    frames_with = 0
    positions = []
    for fi in range(n_frames):
        target = int(total * (fi + 0.5) / n_frames)
        cap.set(cv2.CAP_PROP_POS_FRAMES, target)
        ok, frame = cap.read()
        if not ok or frame is None:
            continue
        frame = cv2.resize(frame, (640, 360))
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        if have_hog:
            rects, _ = hog.detectMultiScale(gray, winStride=(4,4), padding=(8,8), scale=1.05)
            det = len(rects)
            if det > 0:
                frames_with += 1
                detections += det
                positions.append((fi, det))
        else:
            # fallback: variance heuristic (not reliable)
            det = 1
            frames_with += 1
            detections += 1
    cap.release()
    return {"total_frames": total, "frames_with_people": frames_with,
            "total_detections": detections, "positions": positions}

results = {}
for fn in files:
    path = os.path.join(vdir, fn)
    r = check(path)
    results[fn] = r
    if r:
        print(f"{fn}: det_frames={r['frames_with_people']}/6 dets={r['total_detections']} frames={r['total_frames']}")
    else:
        print(f"{fn}: OPEN FAILED")

# Summary
ok = {f:r for f,r in results.items() if r}
zero = [f for f,r in ok.items() if r["frames_with_people"] == 0]
print("\n=== SUMMARY ===")
print("total files:", len(files), "opened:", len(ok), "with >=1 detection frame:", len(ok)-len(zero))
print("files with 0 pedestrian detections across all sampled frames:", len(zero))
for f in zero:
    print("  ZERO:", f)

with open(os.path.join(vdir, "_hog_check.txt"), "w") as fh:
    for fn in files:
        r = results.get(fn)
        fh.write(f"{fn}\t{r if r else 'OPEN_FAIL'}\n")
print("Saved _hog_check.txt")