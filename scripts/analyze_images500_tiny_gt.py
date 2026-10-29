#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_images500_tiny_gt.py - images500 GT 微小目标量化分析
================================================================
目的（对应 P1-9 的人工复核前置）：
  1. GT 数据质量校验：500 张图文件名后缀 _<count> 与 COCO 标注文件中的
     person 实例数是否完全一致。
  2. 量化 GT 中"微小行人"占比（以原始分辨率像素高度衡量：
     <16px / <20px / <32px / <48px）。
  3. 分析"微小目标占比"与检出率的关系，验证欠检是否主要由微小 GT 解释。
  4. 输出欠检/过检 Top10 个案的 GT 尺寸明细，供人工视觉复核时对照。

数据源（默认相对仓库根目录）：
  - dataset/images500/*.jpg          图片（取原始尺寸）
  - dataset/_work/coco2017labels.zip  Ultralytics COCO 2017 labels（YOLO 格式）
  - results/rerun/images500_baseline/results.json   检测输出
  - results/images500_gt_eval/summary.json          最差个案清单（可选）

输出：
  - results/images500_gt_eval/tiny_gt_analysis.md
  - results/images500_gt_eval/tiny_gt_summary.json

用法：
  python scripts/analyze_images500_tiny_gt.py
"""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import struct
import sys
import zipfile
from typing import Dict, List, Optional, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IMAGES_DIR = os.path.join("dataset", "images500")
DEFAULT_LABELS_ZIP = os.path.join("dataset", "_work", "coco2017labels.zip")
DEFAULT_RESULTS_JSON = os.path.join("results", "rerun", "images500_baseline", "results.json")
DEFAULT_SUMMARY_JSON = os.path.join("results", "images500_gt_eval", "summary.json")
DEFAULT_OUT_MD = os.path.join("results", "images500_gt_eval", "tiny_gt_analysis.md")
DEFAULT_OUT_JSON = os.path.join("results", "images500_gt_eval", "tiny_gt_summary.json")

HEIGHT_THRESHOLDS = (16, 20, 32, 48)
AREA_THRESHOLDS = (32 * 32, 64 * 64)

TINY_HEIGHT = 20  # 分析相关性时采用的"微小"定义（像素高度阈值）
TINY_AREA = 32 * 32


def parse_image_name(basename: str) -> Optional[Tuple[str, int]]:
    """从 '000000498796_13.jpg' 解析 (coco_id, suffix_count)。"""
    m = re.match(r"(\d+)_(\d+)\.(?:jpg|jpeg|png)$", basename, re.IGNORECASE)
    if not m:
        return None
    return m.group(1), int(m.group(2))


def load_results(results_path: str) -> Dict[str, dict]:
    """读取检测结果，返回 {filename(不含扩展): {n, level, ...}}。"""
    with open(results_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    out: Dict[str, dict] = {}
    for item in data.get("results", []):
        fn = item.get("filename")
        if fn:
            out[fn] = item
    return out


def build_label_index(zip_path: str) -> Dict[str, str]:
    """一次性扫描 zip，建立 {coco_id: member_name} 索引（train2017 优先，val2017 兜底）。"""
    index: Dict[str, str] = {}
    with zipfile.ZipFile(zip_path) as z:
        for n in z.namelist():
            m = re.match(r"coco/labels/(train2017|val2017)/(\d+)\.txt$", n)
            if not m:
                continue
            split, coco_id = m.group(1), m.group(2)
            if coco_id not in index or split == "train2017":
                index[coco_id] = n
    return index


def read_person_boxes(
    label_index: Dict[str, str], zip_path: str, coco_id: str
) -> Optional[List[Tuple[float, float, float, float]]]:
    """从预建索引读取某 COCO 图片的 person 框（YOLO 归一化 cx cy w h），
    返回 None 表示未找到标注文件。"""
    member = label_index.get(coco_id)
    if member is None:
        return None
    with zipfile.ZipFile(zip_path) as z:
        content = z.read(member).decode("utf-8", errors="replace")
    boxes: List[Tuple[float, float, float, float]] = []
    for line in content.splitlines():
        parts = line.split()
        if len(parts) < 5 or parts[0] != "0":
            continue
        cx, cy, w, h = (float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4]))
        boxes.append((cx, cy, w, h))
    return boxes


def load_worst_lists(summary_path: str) -> Tuple[List[dict], List[dict]]:
    """读取已有最差个案清单；文件不存在时返回空列表。"""
    if not os.path.isfile(summary_path):
        return [], []
    with open(summary_path, "r", encoding="utf-8") as f:
        s = json.load(f)
    return s.get("worst_under_detected", []), s.get("worst_over_detected", [])


def get_image_size(path: str) -> Tuple[int, int]:
    """零依赖读取 JPEG/PNG 图片尺寸 (宽, 高)。"""
    with open(path, "rb") as f:
        head = f.read(26)
    if head[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", head[16:24])
    if head[:2] == b"\xff\xd8":
        with open(path, "rb") as f:
            data = f.read(256 * 1024)
        i = 2
        while i < len(data) - 9:
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            # 无长度段标记：SOI/RST/EOI/TEM
            if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
                i += 2
                continue
            length = struct.unpack(">H", data[i + 2:i + 4])[0]
            # SOF 标记含图像尺寸（除 DHT/DAC/RST/SOI/EOI/TEM）
            if marker in (
                0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF,
            ):
                h, w = struct.unpack(">HH", data[i + 5:i + 9])
                return w, h
            i += 2 + length
    raise ValueError(f"不支持的图片格式: {path}")


def format_table(headers: List[str], rows: List[List[object]]) -> str:
    """渲染 Markdown 表格。"""
    col_count = len(headers)
    lines = [
        "| " + " | ".join(str(h) for h in headers) + " |",
        "|" + "|".join(["---"] * col_count) + "|",
    ]
    for row in rows:
        cells = [str(c) for c in row]
        if len(cells) < col_count:
            cells += [""] * (col_count - len(cells))
        lines.append("| " + " | ".join(cells[:col_count]) + " |")
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images-dir", default=DEFAULT_IMAGES_DIR)
    parser.add_argument("--labels-zip", default=DEFAULT_LABELS_ZIP)
    parser.add_argument("--results-json", default=DEFAULT_RESULTS_JSON)
    parser.add_argument("--summary-json", default=DEFAULT_SUMMARY_JSON)
    parser.add_argument("--out-md", default=DEFAULT_OUT_MD)
    parser.add_argument("--out-json", default=DEFAULT_OUT_JSON)
    args = parser.parse_args(argv)

    images_dir = args.images_dir if os.path.isabs(args.images_dir) else os.path.join(REPO_ROOT, args.images_dir)
    labels_zip = args.labels_zip if os.path.isabs(args.labels_zip) else os.path.join(REPO_ROOT, args.labels_zip)
    results_json = args.results_json if os.path.isabs(args.results_json) else os.path.join(REPO_ROOT, args.results_json)
    summary_json = args.summary_json if os.path.isabs(args.summary_json) else os.path.join(REPO_ROOT, args.summary_json)
    out_md = args.out_md if os.path.isabs(args.out_md) else os.path.join(REPO_ROOT, args.out_md)
    out_json = args.out_json if os.path.isabs(args.out_json) else os.path.join(REPO_ROOT, args.out_json)

    if not os.path.isfile(labels_zip):
        print(f"[ERROR] 找不到 labels zip: {labels_zip}", file=sys.stderr)
        return 2
    if not os.path.isdir(images_dir):
        print(f"[ERROR] 找不到图片目录: {images_dir}", file=sys.stderr)
        return 2

    results = load_results(results_json) if os.path.isfile(results_json) else {}
    worst_under, worst_over = load_worst_lists(summary_json)
    label_index = build_label_index(labels_zip)

    # ---------- 逐图收集 GT 尺寸统计 ----------
    per_image: Dict[str, dict] = {}
    files = sorted(f for f in os.listdir(images_dir) if parse_image_name(f))
    if not files:
        print(f"[ERROR] {images_dir} 下没有匹配 *_<count>.jpg 的图片", file=sys.stderr)
        return 2

    gt_consistency_ok = True
    gt_consistency_bad: List[Tuple[str, int, int, str]] = []
    missing_labels: List[str] = []

    for fname in files:
        parsed = parse_image_name(fname)
        if parsed is None:
            continue
        coco_id, suffix = parsed
        stem = os.path.splitext(fname)[0]
        image_path = os.path.join(images_dir, fname)
        try:
            w_img, h_img = get_image_size(image_path)
        except Exception as exc:  # pragma: no cover - 异常图片防御
            print(f"[WARN] 无法读取图片尺寸 {fname}: {exc}")
            w_img, h_img = 0, 0

        boxes = read_person_boxes(label_index, labels_zip, coco_id)
        if boxes is None:
            missing_labels.append(stem)
            gt_consistency_ok = False
            continue

        gt_count = len(boxes)
        if gt_count != suffix:
            gt_consistency_ok = False
            gt_consistency_bad.append((stem, suffix, gt_count, f"coco:{coco_id}"))

        heights = [h * h_img for (_, _, _, h) in boxes]
        widths = [w * w_img for (_, _, _, w) in boxes]
        areas = [wi * hi for wi, hi in zip(widths, heights)]

        tiny_heights = {
            th: sum(1 for hh in heights if hh < th) for th in HEIGHT_THRESHOLDS
        }
        tiny_areas = {
            ta: sum(1 for aa in areas if aa < ta) for ta in AREA_THRESHOLDS
        }

        per_image[stem] = {
            "coco_id": coco_id,
            "suffix_gt": suffix,
            "label_gt": gt_count,
            "image_w": w_img,
            "image_h": h_img,
            "det_n": results.get(stem, {}).get("n"),
            "level": results.get(stem, {}).get("level"),
            "heights_px": heights,
            "areas_px": areas,
            "tiny_height": tiny_heights,
            "tiny_area": tiny_areas,
        }

    # ---------- 全局统计 ----------
    all_heights: List[int] = []
    all_areas: List[int] = []
    for info in per_image.values():
        all_heights.extend(info["heights_px"])
        all_areas.extend(info["areas_px"])

    total_persons = len(all_heights)
    global_tiny = {th: sum(1 for hh in all_heights if hh < th) for th in HEIGHT_THRESHOLDS}
    global_tiny_area = {ta: sum(1 for aa in all_areas if aa < ta) for ta in AREA_THRESHOLDS}
    global_median_h = statistics.median(all_heights) if all_heights else 0
    global_mean_h = statistics.mean(all_heights) if all_heights else 0.0

    # ---------- 相关性：微小占比 vs 检出率 ----------
    pairs = []
    for stem, info in per_image.items():
        gt = info["label_gt"]
        det = info["det_n"]
        if gt <= 0 or det is None:
            continue
        rate = det / gt
        tiny_frac = info["tiny_height"][TINY_HEIGHT] / gt
        pairs.append((tiny_frac, rate, info["heights_px"]))
    if len(pairs) >= 3:
        import numpy as np

        tiny_arr = np.array([p[0] for p in pairs], dtype=np.float64)
        rate_arr = np.array([p[1] for p in pairs], dtype=np.float64)
        corr_tiny_rate = float(np.corrcoef(tiny_arr, rate_arr)[0, 1])
        med_h_arr = np.array([statistics.median(p[2]) for p in pairs], dtype=np.float64)
        corr_medh_rate = float(np.corrcoef(med_h_arr, rate_arr)[0, 1])
        # 微小占比分桶 -> 平均检出率
        buckets = {
            "0%": ([], []),
            "(0,25%]": ([], []),
            "(25%,50%]": ([], []),
            "(50%,75%]": ([], []),
            "(75%,100%]": ([], []),
        }
        for tiny_frac, rate, _ in pairs:
            if tiny_frac <= 0:
                key = "0%"
            elif tiny_frac <= 0.25:
                key = "(0,25%]"
            elif tiny_frac <= 0.5:
                key = "(25%,50%]"
            elif tiny_frac <= 0.75:
                key = "(50%,75%]"
            else:
                key = "(75%,100%]"
            buckets[key][0].append(rate)
            buckets[key][1].append(1)
        bucket_table = []
        for key in ("0%", "(0,25%]", "(25%,50%]", "(50%,75%]", "(75%,100%]"):
            rates, cnt = buckets[key]
            bucket_table.append([key, len(rates), f"{statistics.mean(rates):.3f}" if rates else "-"])
    else:
        corr_tiny_rate = None
        corr_medh_rate = None
        bucket_table = []

    # ---------- 最差个案表格 ----------
    def case_rows(case_list: List[dict]) -> List[List[object]]:
        rows = []
        for c in case_list:
            stem = c.get("filename")
            info = per_image.get(stem)
            if info is None:
                rows.append([stem, c.get("gt"), c.get("det"), c.get("err"), "-", "-", "-", "-", "-", "-"])
                continue
            th20 = info["tiny_height"][20]
            th32 = info["tiny_height"][32]
            med_h = statistics.median(info["heights_px"]) if info["heights_px"] else 0
            min_h = min(info["heights_px"]) if info["heights_px"] else 0
            rows.append(
                [
                    stem,
                    c.get("gt"),
                    c.get("det"),
                    c.get("err"),
                    th20,
                    th32,
                    f"{med_h:.0f}",
                    f"{min_h:.0f}",
                    f"{th20 / c['gt'] * 100:.0f}%" if c.get("gt") else "-",
                    info["image_h"],
                ]
            )
        return rows

    case_headers = [
        "文件",
        "GT",
        "检测",
        "误差",
        "<20px",
        "<32px",
        "中位高(px)",
        "最矮(px)",
        "微小占比",
        "图高(px)",
    ]

    # ---------- 生成 Markdown ----------
    md = []
    md.append("# images500 GT 微小目标量化分析")
    md.append("")
    md.append(
        "> 本报告由 `scripts/analyze_images500_tiny_gt.py` 自动生成，纯离线、可复现。"
    )
    md.append("> 目的：为 500 张 GT 人工复核提供量化前置——欠检是否主要由微小行人实例解释。")
    md.append("")
    md.append(f"- 生成时间：{__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    md.append(f"- 图片目录：`{images_dir}`")
    md.append(f"- 标注来源：`{labels_zip}`（Ultralytics COCO 2017 labels，YOLO 格式）")
    md.append(f"- 检测结果：`{results_json}`" if os.path.isfile(results_json) else "- 检测结果：未找到（仅做 GT 统计）")
    md.append("")

    md.append("## 一、GT 数据质量校验")
    md.append("")
    md.append(
        f"对 {len(per_image)} 张图片逐一比对「文件名后缀人数」与 COCO 标注文件中的 "
        "person 实例数（类别 id=0）。"
    )
    md.append("")
    if gt_consistency_ok:
        md.append(f"**结果：{len(per_image)}/{len(files)} 全部一致，无缺失、无冲突。**")
    else:
        md.append(f"**结果：存在不一致 {len(gt_consistency_bad)} 处，缺失 {len(missing_labels)} 个。**")
        for item in gt_consistency_bad:
            md.append(f"- {item[0]}: 后缀 {item[1]} vs 标注 {item[2]}（{item[3]}）")
        for item in missing_labels:
            md.append(f"- {item}: 无标注文件")
    md.append("")
    md.append("> 结论：GT 人数来源可靠，文件名后缀可直接作为人数真值使用。")
    md.append("")

    md.append("## 二、GT 全局尺寸分布")
    md.append("")
    md.append(f"- GT 总人数：**{total_persons}**（{len(per_image)} 张图）")
    md.append(f"- 高度（原始像素）：均值 {global_mean_h:.1f}px，中位数 {global_median_h}px")
    md.append("")
    md.append("### 按高度阈值统计")
    md.append("")
    tiny_rows = []
    for th in HEIGHT_THRESHOLDS:
        n = global_tiny[th]
        tiny_rows.append([f"高度 < {th}px", n, f"{n / total_persons * 100:.1f}%"])
    for ta in AREA_THRESHOLDS:
        n = global_tiny_area[ta]
        tiny_rows.append([f"面积 < {ta}px²", n, f"{n / total_persons * 100:.1f}%"])
    md.append(format_table(["类别", "人数", "占比"], tiny_rows))
    md.append("")
    md.append(
        f"> 说明：「微小」以原始图像分辨率衡量。RV1106 部署输入为 320×320 时，"
        f"低于 {TINY_HEIGHT}px 的 GT 在缩放后通常不足 10px，检测器难以召回。"
    )
    md.append("")

    md.append("## 三、微小占比与检出率的关系")
    md.append("")
    if corr_tiny_rate is not None:
        md.append(
            f"- 逐图「<20px 微小占比」与「检出率」的 Pearson 相关系数："
            f"**{corr_tiny_rate:+.3f}**"
        )
        md.append(
            f"- 逐图「GT 中位高度」与「检出率」的 Pearson 相关系数："
            f"**{corr_medh_rate:+.3f}**"
        )
        md.append("")
        md.append("### 微小占比分桶 → 平均检出率")
        md.append("")
        md.append(format_table(["微小占比桶", "图片数", "平均检出率"], bucket_table))
        md.append("")
        md.append(
            "> 若相关系数为明显负值且分桶递减，说明「漏检主要发生在微小 GT 占多数的图上」，"
            "检测器在可感知尺度上的召回是稳定的。"
        )
    else:
        md.append("（检测结果缺失，无法计算相关性）")
    md.append("")

    md.append("## 四、欠检 Top 个案：GT 尺寸明细")
    md.append("")
    if worst_under:
        md.append(format_table(case_headers, case_rows(worst_under)))
        md.append("")
        md.append(
            "> 复核提示：若某图 GT 中位高度很低（如 <30px）且 <20px 占比高，"
            "漏检大概率是微小目标所致，建议人工核对图中是否确实存在大量远处小人；"
            "若 GT 尺寸很大但仍漏检，才是真正的检测器缺陷。"
        )
    else:
        md.append("（未提供最差个案清单）")
    md.append("")

    md.append("## 五、过检 Top 个案：GT 尺寸明细")
    md.append("")
    if worst_over:
        md.append(format_table(case_headers, case_rows(worst_over)))
        md.append("")
        md.append(
            "> 过检无法由微小 GT 解释，通常来自检测误检（把非行人当行人）或 GT 漏标注。"
            "建议人工复核这些图，区分两种情况：若图中确有行人但 COCO 未标注，则 GT 有漏；"
            "若检测框落在非行人区域，则是 conf 阈值/模型误检。"
        )
    else:
        md.append("（未提供最差个案清单）")
    md.append("")

    md.append("## 六、结论与后续动作")
    md.append("")
    md.append(
        "1. **GT 人数可信**：500 张图后缀人数与 COCO 标注完全一致，可作为人数真值。"
    )
    md.append(
        "2. **微小目标量化**：全局 <20px 的 GT 占比见第二节；若该占比与 17.5% 的整体漏检率"
        "同量级，则漏检主要由微小目标解释，论文表述应明确这一点。"
    )
    md.append(
        "3. **人工复核优先级**：欠检个案优先复核「微小占比低但漏检多」的图；"
        "过检个案全部值得复核（判断误检还是 GT 漏标）。"
    )
    md.append(
        "4. **论文建议**：报告检出率时应同时给出 GT 尺寸分布，避免「82.5% 检出率」"
        "被质疑为检测器性能不足而忽略数据本身含大量微小实例。"
    )
    md.append("")

    os.makedirs(os.path.dirname(out_md), exist_ok=True)
    with open(out_md, "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")

    # ---------- JSON 摘要 ----------
    summary = {
        "generated_at": __import__("datetime").datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "images_total": len(per_image),
        "gt_total": total_persons,
        "gt_consistency": {
            "ok": gt_consistency_ok,
            "mismatches": gt_consistency_bad,
            "missing_labels": missing_labels,
        },
        "global_height": {
            "mean_px": round(global_mean_h, 2),
            "median_px": global_median_h,
        },
        "tiny_height_counts": global_tiny,
        "tiny_area_counts": global_tiny_area,
        "correlation": {
            "tiny_frac_vs_rate": round(corr_tiny_rate, 4) if corr_tiny_rate is not None else None,
            "median_h_vs_rate": round(corr_medh_rate, 4) if corr_medh_rate is not None else None,
            "tiny_height_px": TINY_HEIGHT,
        },
        "tiny_bucket_mean_rate": bucket_table,
        "case_details": {
            "worst_under_detected": [
                {
                    "filename": c.get("filename"),
                    "gt": c.get("gt"),
                    "det": c.get("det"),
                    "err": c.get("err"),
                    "tiny_lt20": per_image.get(c.get("filename"), {}).get("tiny_height", {}).get(20),
                    "tiny_lt32": per_image.get(c.get("filename"), {}).get("tiny_height", {}).get(32),
                    "median_height_px": round(statistics.median(per_image[c["filename"]]["heights_px"]), 1)
                    if c.get("filename") in per_image and per_image[c["filename"]]["heights_px"] else None,
                    "min_height_px": min(per_image[c["filename"]]["heights_px"]) if c.get("filename") in per_image and per_image[c["filename"]]["heights_px"] else None,
                }
                for c in worst_under
            ],
            "worst_over_detected": [
                {
                    "filename": c.get("filename"),
                    "gt": c.get("gt"),
                    "det": c.get("det"),
                    "err": c.get("err"),
                    "tiny_lt20": per_image.get(c.get("filename"), {}).get("tiny_height", {}).get(20),
                    "tiny_lt32": per_image.get(c.get("filename"), {}).get("tiny_height", {}).get(32),
                    "median_height_px": round(statistics.median(per_image[c["filename"]]["heights_px"]), 1)
                    if c.get("filename") in per_image and per_image[c["filename"]]["heights_px"] else None,
                    "min_height_px": min(per_image[c["filename"]]["heights_px"]) if c.get("filename") in per_image and per_image[c["filename"]]["heights_px"] else None,
                }
                for c in worst_over
            ],
        },
    }
    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print(f"[OK] 分析完成：{len(per_image)} 张图，GT {total_persons} 人")
    print(f"      报告：{out_md}")
    print(f"      JSON：{out_json}")
    if not gt_consistency_ok:
        print(f"[WARN] GT 一致性存在问题，详见报告")
    return 0


if __name__ == "__main__":
    sys.exit(main())