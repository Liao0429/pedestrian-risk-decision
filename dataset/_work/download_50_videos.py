#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量下载 50 条真实行人/人群视频（YouTube 公开视频，通过代理）。
用于论文数据量要求。每条限制时长<180s，360p 以内，减小体积。
输出: dataset/videos50/<video_id>.<ext>
"""
import subprocess, sys, os, time

PYTHON = sys.executable
OUT = r"H:/PedestrianRiskDecision/dataset/videos50"
os.makedirs(OUT, exist_ok=True)
PROXY = "http://127.0.0.1:7890"

TERMS = [
    "行人 街道 人群",
    "行人 过马路 斑马线",
    "街头 人群 人流",
    "street crowd pedestrian",
    "pedestrian walking street",
    "crowd crossing road",
    "people walking sidewalk",
    "busy street people walking",
    "行人 密集 路口",
    "pedestrian crosswalk people",
    "人群 拥挤 街道",
    "walking crowd city",
]

def count_done():
    return sum(1 for f in os.listdir(OUT) if os.path.splitext(f)[1].lower() in (".mp4", ".webm", ".mkv", ".m4a", ".mp3"))

def download(term, n):
    cmd = [
        PYTHON, "-m", "yt_dlp",
        "--proxy", PROXY,
        "-f", "best[height<=360]/best",
        "--match-filter", "duration < 180",
        "--no-overwrites",
        "--no-playlist",
        "-o", os.path.join(OUT, "%(id)s.%(ext)s"),
        f"ytsearch{n}:{term}",
    ]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=300)

def main():
    for term in TERMS:
        done = count_done()
        if done >= 50:
            print(f"已达到 {done} 条，停止", flush=True)
            break
        print(f"[{done}/50] 搜索下载: {term}", flush=True)
        try:
            r = download(term, 12)
            for line in (r.stdout or "").splitlines()[-4:]:
                print("  ", line, flush=True)
            if r.returncode != 0:
                err = [l for l in (r.stderr or "").splitlines() if l.strip()][-3:]
                print("  yt-dlp 返回非零:", err, flush=True)
        except Exception as e:
            print("  下载异常:", e, flush=True)
        time.sleep(2)
    print("完成，共", count_done(), "个视频", flush=True)

if __name__ == "__main__":
    main()