#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 COCO 官方 CDN 并发下载选定图片到 dataset/images/。
- val2017:     http://images.cocodataset.org/val2017/<id>.jpg
- train2017:   http://images.cocodataset.org/train2017/<id>.jpg
文件名: <image_id>_<person_count>.jpg
"""
import os, csv, random, urllib.request, concurrent.futures, sys, time

WORK = os.path.dirname(os.path.abspath(__file__))
DATASET = os.path.dirname(WORK)          # .../dataset
IMGDIR = os.path.join(DATASET, "images")
os.makedirs(IMGDIR, exist_ok=True)

random.seed(42)
UA = {"User-Agent": "Mozilla/5.0 (research-data-download)"}

def pick_val_samples(csv_path, seed=42):
    """从 selected_person_images.csv 按密度桶挑选样本。"""
    scattered, cluster, crowded = [], [], []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            c = int(r["person_count"])
            if 1 <= c <= 3:
                scattered.append((r["image_id"], c))
            elif 4 <= c <= 9:
                cluster.append((r["image_id"], c))
            elif 10 <= c <= 13:
                crowded.append((r["image_id"], c))
    rng = random.Random(seed)
    rng.shuffle(scattered); rng.shuffle(cluster); rng.shuffle(crowded)
    sel = []
    sel += scattered[:8]     # 分散 1-3 人
    sel += cluster[:7]       # 局部聚集 4-9 人
    sel += crowded[:5]       # 较拥挤 10-13 人
    return sel

def pick_train_dense(csv_path, limit=10):
    with open(csv_path, newline="", encoding="utf-8") as f:
        rows = [(r["image_id"], int(r["person_count"])) for r in csv.DictReader(f)]
    rows.sort(key=lambda x: x[1], reverse=True)
    return rows[:limit]

def download_one(item):
    image_id, count = item
    split = "train2017" if image_id in TRAIN_IDS else "val2017"
    url = f"http://images.cocodataset.org/{split}/{image_id}.jpg"
    out = os.path.join(IMGDIR, f"{image_id}_{count}.jpg")
    if os.path.exists(out) and os.path.getsize(out) > 1000:
        return (image_id, "exists")
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=60) as resp, open(out, "wb") as f:
                f.write(resp.read())
            return (image_id, "ok")
        except Exception as e:
            if attempt == 2:
                return (image_id, f"fail:{e}")
            time.sleep(2)

def main():
    global TRAIN_IDS
    val_csv = os.path.join(WORK, "selected_person_images.csv")
    train_csv = os.path.join(WORK, "train2017_dense.csv")
    val_items = pick_val_samples(val_csv)
    train_items = pick_train_dense(train_csv, limit=10)
    TRAIN_IDS = {i for i, _ in train_items}
    all_items = val_items + train_items
    print("selected", len(val_items), "val2017 +", len(train_items), "train2017 =", len(all_items))
    for i, c in all_items:
        print("  ", i, c, "train2017" if i in TRAIN_IDS else "val2017")
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        for res in ex.map(download_one, all_items):
            results.append(res)
            print("downloaded", res[0], res[1])
    ok = [r for r in results if r[1] in ("ok", "exists")]
    fail = [r for r in results if r[1] != "ok" and r[1] != "exists"]
    print("\nOK:", len(ok), " FAIL:", len(fail))
    for r in fail:
        print("  FAIL", r)

if __name__ == "__main__":
    main()