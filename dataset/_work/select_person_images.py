#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 Ultralytics coco2017labels.zip 的 val2017 YOLO 标签中,
统计每张图片的 person(class 0) 数量, 并按密度分桶输出挑选清单。
"""
import os, sys, csv, collections, zipfile

WORK = os.path.dirname(os.path.abspath(__file__))
ZIP = os.path.join(WORK, "coco2017labels.zip")
LABELS_DIR = os.path.join(WORK, "labels_extract", "labels", "val2017")

def extract(zip_path, out_dir):
    if os.path.isdir(out_dir):
        return
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(out_dir)
    print("extracted to", out_dir)

def count_persons(label_path):
    """YOLO txt 每行: cls cx cy w h, class 0 == person"""
    n = 0
    try:
        with open(label_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                try:
                    if int(float(parts[0])) == 0:
                        n += 1
                except (ValueError, IndexError):
                    continue
    except Exception:
        return None
    return n

def main():
    extract(ZIP, os.path.join(WORK, "labels_extract"))
    if not os.path.isdir(LABELS_DIR):
        print("labels dir not found:", LABELS_DIR)
        sys.exit(1)

    rows = []  # (image_id, person_count, label_path)
    for fn in os.listdir(LABELS_DIR):
        if not fn.endswith(".txt"):
            continue
        image_id = fn[:-4]
        c = count_persons(os.path.join(LABELS_DIR, fn))
        if c is None:
            continue
        rows.append((image_id, c))

    rows.sort(key=lambda r: r[1], reverse=True)
    print("total val2017 label files with person:", sum(1 for _, c in rows if c > 0))
    print("max persons in one image:", rows[0] if rows else None)

    # 分桶
    scattered = [r for r in rows if 1 <= r[1] <= 3]
    cluster   = [r for r in rows if 4 <= r[1] <= 12]
    crowded   = [r for r in rows if r[1] >= 15]

    print("scattered(1-3):", len(scattered), " cluster(4-12):", len(cluster), " crowded(>=15):", len(crowded))

    # 输出挑选清单: 每桶按人数从多到少选若干
    out_csv = os.path.join(WORK, "selected_person_images.csv")
    with open(out_csv, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["image_id", "person_count", "density_bucket"])
        for r in crowded:
            w.writerow([r[0], r[1], "crowded"])
        for r in cluster:
            w.writerow([r[0], r[1], "cluster"])
        for r in scattered:
            w.writerow([r[0], r[1], "scattered"])
    print("wrote", out_csv)

if __name__ == "__main__":
    main()