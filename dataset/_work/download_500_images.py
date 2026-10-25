#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
批量下载 500 张 COCO 真实含行人图片（论文数据量要求）。
- val2017: 从 selected_person_images.csv 按密度桶抽样 450 张
  - 分散(1-3人): 200 张
  - 局部聚集(4-9人): 150 张
  - 较拥挤(10-13人): 100 张
- train2017: 从 train2017_dense.csv 选 50 张（14~19 人密集）
输出: dataset/images500/<image_id>_<person_count>.jpg
"""
import os, csv, random, urllib.request, concurrent.futures, sys, time

WORK = os.path.dirname(os.path.abspath(__file__))
DATASET = os.path.dirname(WORK)
IMGDIR = os.path.join(DATASET, "images500")
os.makedirs(IMGDIR, exist_ok=True)

TARGET_VAL = 450
TARGET_TRAIN = 50
TARGET_TOTAL = TARGET_VAL + TARGET_TRAIN

UA = {"User-Agent": "Mozilla/5.0 (research-data-download)"}

def load_val(csv_path):
    scattered, cluster, crowded = [], [], []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            c = int(r["person_count"])
            if 1 <= c <= 3: scattered.append((r["image_id"], c))
            elif 4 <= c <= 9: cluster.append((r["image_id"], c))
            elif 10 <= c <= 13: crowded.append((r["image_id"], c))
    return scattered, cluster, crowded

def load_train_dense(csv_path, limit):
    with open(csv_path, newline="", encoding="utf-8") as f:
        rows = [(r["image_id"], int(r["person_count"])) for r in csv.DictReader(f)]
    rows.sort(key=lambda x: x[1], reverse=True)
    return rows[:limit]

def download_one(item):
    image_id, count, split = item
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
    val_csv = os.path.join(WORK, "selected_person_images.csv")
    train_csv = os.path.join(WORK, "train2017_dense.csv")
    scattered, cluster, crowded = load_val(val_csv)
    rng = random.Random(123)
    rng.shuffle(scattered); rng.shuffle(cluster); rng.shuffle(crowded)

    # 按目标数量抽取（若不足则全取）
    n_scat = min(200, len(scattered))
    n_clu  = min(150, len(cluster))
    n_crow = min(100, len(crowded))
    # 若分散不足，用 cluster 补
    deficit = TARGET_VAL - (n_scat + n_clu + n_crow)
    if deficit > 0:
        pool = cluster[n_clu:] + crowded[n_crow:] + scattered[n_scat:]
        rng.shuffle(pool)
        extra = pool[:deficit]
    else:
        extra = []
    val_items = scattered[:n_scat] + cluster[:n_clu] + crowded[:n_crow] + extra
    train_items = load_train_dense(train_csv, TARGET_TRAIN)

    items = [(i, c, "val2017") for i, c in val_items] + \
            [(i, c, "train2017") for i, c in train_items]
    print(f"准备下载: val2017={len(val_items)} train2017={len(train_items)} 总={len(items)}")
    print(f"人数范围: val 1-13人, train 14-19人")

    ok, fail = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
        for res in ex.map(download_one, items):
            if res[1] in ("ok", "exists"):
                ok.append(res)
            else:
                fail.append(res)
            if len(ok) % 50 == 0:
                print(f"  进度: 成功 {len(ok)} 失败 {len(fail)}")
    print(f"\n完成: 成功 {len(ok)} 失败 {len(fail)}")
    for r in fail:
        print("  FAIL", r)

if __name__ == "__main__":
    main()