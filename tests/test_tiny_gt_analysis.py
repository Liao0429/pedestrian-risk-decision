# -*- coding: utf-8 -*-
"""scripts/analyze_images500_tiny_gt.py 的单元测试。

验证：文件名解析、标签 zip 索引构建、person 框过滤、Markdown 表格渲染，
以及一个最小端到端跑批（合成 2 张图 + 标签 zip + 检测结果，不依赖真实数据）。
"""
import json
import os
import sys
import zipfile

import numpy as np
import cv2
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts"))

from analyze_images500_tiny_gt import (  # noqa: E402
    build_label_index,
    format_table,
    main,
    parse_image_name,
    read_person_boxes,
)


def test_parse_image_name_valid():
    assert parse_image_name("000000498796_13.jpg") == ("000000498796", 13)
    assert parse_image_name("000000001268_4.png") == ("000000001268", 4)
    assert parse_image_name("000000001268_4.JPG") == ("000000001268", 4)


def test_parse_image_name_invalid():
    assert parse_image_name("000000498796.jpg") is None
    assert parse_image_name("random.txt") is None
    assert parse_image_name("000000498796_13") is None


def test_build_label_index_and_read_person_boxes(tmp_path):
    # 构造一个迷你 labels zip：一个 person 类、一个其他类、一个无标注 id
    zip_path = tmp_path / "labels.zip"
    with zipfile.ZipFile(zip_path, "w") as z:
        z.writestr(
            "coco/labels/train2017/000000000001.txt",
            "0 0.5 0.5 0.1 0.2\n1 0.5 0.5 0.1 0.2\n",  # 第 2 行是 person 之外类别
        )
        z.writestr("coco/labels/val2017/000000000002.txt", "0 0.5 0.5 0.3 0.4\n")
        z.writestr("coco/labels/train2017/000000000003.txt", "2 0.5 0.5 0.1 0.2\n")

    index = build_label_index(str(zip_path))
    assert index["000000000001"] == "coco/labels/train2017/000000000001.txt"
    assert index["000000000002"] == "coco/labels/val2017/000000000002.txt"
    assert "000000000003" in index
    assert "000000009999" not in index

    boxes = read_person_boxes(index, str(zip_path), "000000000001")
    assert boxes == [(0.5, 0.5, 0.1, 0.2)]  # 只保留 person 类
    assert read_person_boxes(index, str(zip_path), "000000000003") == []
    assert read_person_boxes(index, str(zip_path), "000000009999") is None


def test_format_table():
    headers = ["a", "b"]
    rows = [[1, "x"], [2, "y"]]
    out = format_table(headers, rows)
    lines = out.splitlines()
    assert lines[0] == "| a | b |"
    assert lines[1] == "|---|---|"
    assert lines[2] == "| 1 | x |"
    assert lines[3] == "| 2 | y |"


def test_format_table_short_row_padded():
    out = format_table(["a", "b", "c"], [[1, "x"]])
    assert "| 1 | x |  |" in out


def test_main_end_to_end(tmp_path):
    # 合成 3 张图：图 1 两个正常大小 person，图 2 一个微小 person，图 3 一个中等 person
    img_dir = tmp_path / "images"
    img_dir.mkdir()
    synth_img = np.full((100, 100, 3), 255, dtype=np.uint8)
    for name in ("000000000001_2.jpg", "000000000002_1.jpg", "000000000003_1.jpg"):
        cv2.imwrite(str(img_dir / name), synth_img)

    zip_path = tmp_path / "labels.zip"
    with zipfile.ZipFile(zip_path, "w") as z:
        # 图 1：两个 person，高度归一化 0.6 / 0.1（第二个 <20px）
        z.writestr("coco/labels/train2017/000000000001.txt", "0 0.5 0.5 0.4 0.6\n0 0.5 0.9 0.1 0.1\n")
        # 图 2：一个微小 person（高度 10px）
        z.writestr("coco/labels/train2017/000000000002.txt", "0 0.5 0.5 0.05 0.1\n")
        # 图 3：一个中等 person（高度 50px）
        z.writestr("coco/labels/train2017/000000000003.txt", "0 0.5 0.5 0.2 0.5\n")

    results_path = tmp_path / "results.json"
    with open(results_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "results": [
                    {"filename": "000000000001_2", "n": 1, "level": "SAFE"},
                    {"filename": "000000000002_1", "n": 0, "level": "SAFE"},
                    {"filename": "000000000003_1", "n": 1, "level": "SAFE"},
                ]
            },
            f,
        )

    out_md = tmp_path / "tiny_gt_analysis.md"
    out_json = tmp_path / "tiny_gt_summary.json"

    rc = main(
        [
            "--images-dir", str(img_dir),
            "--labels-zip", str(zip_path),
            "--results-json", str(results_path),
            "--out-md", str(out_md),
            "--out-json", str(out_json),
        ]
    )
    assert rc == 0
    assert out_md.is_file()
    assert out_json.is_file()

    s = json.loads(out_json.read_text(encoding="utf-8"))
    assert s["images_total"] == 3
    assert s["gt_total"] == 4
    assert s["gt_consistency"]["ok"] is True
    # 全局 <20px 数量：图 1 的第二个（10px）+ 图 2 的（10px）= 2
    assert s["tiny_height_counts"]["20"] == 2
    # 高度序列 [60, 10, 10, 50]，中位数 = (10+50)/2 = 30
    assert s["global_height"]["median_px"] == pytest.approx(30.0, abs=0.1)
    # 3 张图满足相关性计算条件（>=3 对）
    assert s["correlation"]["tiny_frac_vs_rate"] is not None

    md = out_md.read_text(encoding="utf-8")
    assert "500/500" not in md  # 不是真实数据集
    assert "GT 数据质量校验" in md
    assert "Pearson" in md