import cv2, time, pathlib, sys

cap = cv2.VideoCapture("rtsp://192.168.0.136:554/live/0", cv2.CAP_FFMPEG)
if not cap.isOpened():
    print("FAILED open RTSP"); sys.exit(1)

# 热身 10 帧
for _ in range(10):
    ret, frame = cap.read()
    if not ret:
        time.sleep(0.1)

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
out_dir = PROJECT_ROOT / "results" / "edge_board" / "screenshots"
out_dir.mkdir(parents=True, exist_ok=True)
frames = []
start = time.time()
for i in range(60):
    ret, frame = cap.read()
    if ret:
        frames.append(frame)
        if i in [5, 25, 45, 58]:
            cv2.imwrite(str(out_dir / f"board_live_frame{i:02d}.png"), frame)
elapsed = time.time() - start
fps = len(frames) / elapsed if elapsed > 0 else 0
print(f"抓帧 {len(frames)}/60, 耗时 {elapsed:.2f}s, RTSP 帧率约 {fps:.1f} FPS")
print(f"画面尺寸: {frames[0].shape if frames else '无'}")
print(f"截图目录: {out_dir}")
cap.release()