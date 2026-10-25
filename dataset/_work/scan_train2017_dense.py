#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 coco2017labels.zip 流式扫描 train2017, 统计每图 person(class0) 数量, 输出 >=20 人的图片清单。"""
import os, csv, zipfile

WORK = os.path.dirname(os.path.abspath(__file__))
ZIP = os.path.join(WORK, "coco2017labels.zip")
OUT = os.path.join(WORK, "train2017_dense.csv")
PREFIX = "coco/labels/train2017/"
MIN_PERSONS = 10

def count_persons_in_bytes(data: bytes) -> int:
    n = 0
    for line in data.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if not parts:
            continue
        try:
            if int(float(parts[0])) == 0:
                n += 1
        except (ValueError, IndexError):
            continue
    return n

def main():
    rows = []
    processed = 0
    with zipfile.ZipFile(ZIP) as z:
        names = [n for n in z.namelist() if n.startswith(PREFIX) and n.endswith(".txt")]
        print("train2017 label files:", len(names))
        for i, name in enumerate(names):
            processed += 1
            image_id = name[len(PREFIX):-4]
            try:
                c = count_persons_in_bytes(z.read(name))
            except Exception:
                continue
            if c >= MIN_PERSONS:
                rows.append((image_id, c))
            if (i + 1) % 20000 == 0:
                print("  processed", i + 1, "...")
    print("TOTAL processed files:", processed)
    rows.sort(key=lambda r: r[1], reverse=True)
    print("images with >=%d persons: %d" % (MIN_PERSONS, len(rows)))
    print("top 20:", rows[:20])
    with open(OUT, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["image_id", "person_count"])
        for image_id, c in rows:
            w.writerow([image_id, c])
    print("wrote", OUT)

if __name__ == "__main__":
    main()