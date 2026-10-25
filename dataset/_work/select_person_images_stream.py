#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""流式读取 coco2017labels.zip 中的 val2017 标签, 统计每图 person(class 0) 数量并分桶。"""
import os, csv, zipfile

WORK = os.path.dirname(os.path.abspath(__file__))
ZIP = os.path.join(WORK, "coco2017labels.zip")
OUT_CSV = os.path.join(WORK, "selected_person_images.csv")

PREFIX = "coco/labels/val2017/"

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
    stats = {"scattered": 0, "cluster": 0, "crowded": 0}
    rows = []  # (image_id, person_count, bucket)
    with zipfile.ZipFile(ZIP) as z:
        names = [n for n in z.namelist() if n.startswith(PREFIX) and n.endswith(".txt")]
        print("val2017 label files in zip:", len(names))
        for i, name in enumerate(names):
            image_id = name[len(PREFIX):-4]
            try:
                c = count_persons_in_bytes(z.read(name))
            except Exception:
                continue
            if c <= 0:
                continue
            if c >= 15:
                bucket = "crowded"
            elif c >= 4:
                bucket = "cluster"
            else:
                bucket = "scattered"
            stats[bucket] += 1
            rows.append((image_id, c, bucket))
            if (i + 1) % 20000 == 0:
                print("  processed", i + 1, "files...")
    rows.sort(key=lambda r: r[1], reverse=True)
    print("buckets:", stats)
    print("total images with >=1 person:", len(rows))

    with open(OUT_CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["image_id", "person_count", "density_bucket"])
        for image_id, c, bucket in rows:
            w.writerow([image_id, c, bucket])
    print("wrote", OUT_CSV, "rows:", len(rows))

if __name__ == "__main__":
    main()