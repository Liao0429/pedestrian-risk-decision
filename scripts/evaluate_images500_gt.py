# -*- coding: utf-8 -*-
"""images500 数据集 GT 对比评估（纯离线，可复现）。

输入：results/rerun/images500_baseline/results.json（或 --results 指定其他运行结果）。
该文件由 scripts/crowd_test.py 在图片模式下生成，字段语义：
  - filename 后缀 `_<count>` = COCO 官方 ground-truth 行人实例数（GT）
  - n = 检测器输出的行人个数（图节点数）
  - m = 邻接图边数（非人数）
  - level = 算法风险等级（SAFE/WARNING/DANGER）

输出：
  - results/images500_gt_eval/summary.json  机器可读汇总
  - results/images500_gt_eval/report.md   可读报告（含最差个案清单，供人工复核）

用法：
  python scripts/evaluate_images500_gt.py
  python scripts/evaluate_images500_gt.py --results results/images500_baseline/results.json

注意：本脚本只做评估，不重新跑检测，也不修改任何检测结果。
"""
import argparse
import json
import os
import re
import statistics

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# GT 人数分桶（与 dataset/images500/README.md 的密度档一致）
BUCKETS = [(1, 3, "1-3"), (4, 9, "4-9"), (10, 13, "10-13"), (14, 19, "14-19")]

LEVELS = ["SAFE", "WARNING", "DANGER"]

GT_RE = re.compile(r"_(\d+)$")


def bucket_of(gt):
    for lo, hi, name in BUCKETS:
        if lo <= gt <= hi:
            return name
    return "other"


def parse_rows(res):
    """将检测结果条目解析为带 gt/det/err/abs_err/level 的行列表。

    res: crowd_test 输出的 results 列表，每项含 filename/n/level。
    返回: 行 dict 列表；文件名后缀 `_<count>` 即 COCO GT 人数。
    """
    rows = []
    for r in res:
        mt = GT_RE.search(r["filename"])
        if not mt:
            raise ValueError(f"无法从文件名解析 GT 人数: {r['filename']}")
        gt = int(mt.group(1))
        det = int(r["n"])
        rows.append(
            {
                "filename": r["filename"],
                "gt": gt,
                "det": det,
                "err": det - gt,
                "abs_err": abs(det - gt),
                "level": r["level"],
            }
        )
    return rows


def compute_stats(rows):
    """由解析后的行列表计算全部评估统计量（纯函数，可单测）。

    返回 summary dict（不含 generated_at / source_results）。
    """
    det_total = sum(x["det"] for x in rows)
    gt_total = sum(x["gt"] for x in rows)
    overall_rate = det_total / gt_total * 100 if gt_total else 0.0

    # 分桶检出率
    bucket_stats = {}
    for _, _, name in BUCKETS:
        g = sum(x["gt"] for x in rows if bucket_of(x["gt"]) == name)
        d = sum(x["det"] for x in rows if bucket_of(x["gt"]) == name)
        bucket_stats[name] = {
            "images": sum(1 for x in rows if bucket_of(x["gt"]) == name),
            "gt": g,
            "det": d,
            "rate_pct": round(d / g * 100, 2) if g else 0.0,
        }

    # 误差统计
    abs_errs = [x["abs_err"] for x in rows]
    errs = [x["err"] for x in rows]
    mae = statistics.mean(abs_errs)
    rmse = (sum(e * e for e in abs_errs) / len(abs_errs)) ** 0.5

    # GT 人数 与 检测人数 的 Pearson 相关系数
    gts = [x["gt"] for x in rows]
    dets = [x["det"] for x in rows]
    mg, md = statistics.mean(gts), statistics.mean(dets)
    num = sum((a - mg) * (b - md) for a, b in zip(gts, dets))
    den = (sum((a - mg) ** 2 for a in gts) * sum((b - md) ** 2 for b in dets)) ** 0.5
    pearson = num / den if den else 0.0

    # GT 分桶 × 算法等级 列联表（描述性，非准确率）
    level_counts = {lv: 0 for lv in LEVELS}
    contingency = {}
    for x in rows:
        level_counts[x["level"]] += 1
        b = bucket_of(x["gt"])
        contingency.setdefault(b, {lv: 0 for lv in LEVELS})
        contingency[b][x["level"]] += 1

    # 最差个案：欠检（det < gt）与过检（det > gt）各取 10
    under = sorted([x for x in rows if x["err"] < 0], key=lambda x: x["err"])[:10]
    over = sorted([x for x in rows if x["err"] > 0], key=lambda x: -x["err"])[:10]

    summary = {
        "images_total": len(rows),
        "gt_total": gt_total,
        "det_total": det_total,
        "overall_detection_rate_pct": round(overall_rate, 2),
        "mae": round(mae, 3),
        "rmse": round(rmse, 3),
        "pearson_r": round(pearson, 4),
        "level_counts": level_counts,
        "bucket_stats": bucket_stats,
        "contingency_gt_bucket_x_level": contingency,
        "worst_under_detected": [
            {"filename": x["filename"], "gt": x["gt"], "det": x["det"], "err": x["err"]}
            for x in under
        ],
        "worst_over_detected": [
            {"filename": x["filename"], "gt": x["gt"], "det": x["det"], "err": x["err"]}
            for x in over
        ],
    }
    return summary


def render_report(summary, source_results, source_generated_at, weights, conf):
    """由 summary dict 渲染 Markdown 报告文本（纯函数，可单测）。"""
    gt_total = summary["gt_total"]
    det_total = summary["det_total"]
    overall_rate = summary["overall_detection_rate_pct"]
    mae = summary["mae"]
    rmse = summary["rmse"]
    pearson = summary["pearson_r"]
    level_counts = summary["level_counts"]
    bucket_stats = summary["bucket_stats"]
    contingency = summary["contingency_gt_bucket_x_level"]
    under = summary["worst_under_detected"]
    over = summary["worst_over_detected"]

    def bucket_line(b, s):
        return (
            f"| {b} | {s['images']} | {s['gt']} | {s['det']} | "
            f"{s['rate_pct']:.1f}% |"
        )

    def worst_table(items, title):
        lines = [f"### {title}", "", "| 文件 | GT | 检测 | 误差 |", "|---|---|---|---|"]
        for x in items:
            lines.append(
                f"| `{x['filename']}` | {x['gt']} | {x['det']} | {x['err']:+d} |"
            )
        return "\n".join(lines)

    lines = []
    lines.append("# images500 GT 对比评估报告")
    lines.append("")
    lines.append("> 本报告由 `scripts/evaluate_images500_gt.py` 自动生成，纯离线、可复现。")
    lines.append("> 数据源：`{}`（生成于 {}）。".format(source_results, source_generated_at))
    lines.append("")
    lines.append("## 指标定义")
    lines.append("")
    lines.append("- **GT**：COCO 2017 官方 person 实例标注数（来自 Ultralytics coco2017labels.zip，文件名后缀 `_<count>`）。")
    lines.append("- **检测数**：YOLO 检测器在 conf 阈值下输出的行人框数（结果文件 `n` 字段）。")
    lines.append("- **检出率** = Σ检测数 / ΣGT 数（整体与分桶均按此口径，非逐图平均）。")
    lines.append("- 注意：本评估针对**主机侧参考基线**（`{}`，conf={}），并非板端 INT8 部署模型；GT 含微小/遮挡行人实例，检测漏检属预期现象，本报告用于量化误差分布并筛选人工复核样本。".format(
        weights, conf))
    lines.append("")
    lines.append("## 总体指标")
    lines.append("")
    lines.append("| 指标 | 值 |")
    lines.append("|---|---|")
    lines.append(f"| 图片数 | {summary['images_total']} |")
    lines.append(f"| GT 总人数 | {gt_total} |")
    lines.append(f"| 检测总人数 | {det_total} |")
    lines.append(f"| 整体检出率 | {overall_rate:.2f}% |")
    lines.append(f"| MAE（绝对误差均值） | {mae:.3f} |")
    lines.append(f"| RMSE | {rmse:.3f} |")
    lines.append(f"| GT 与检测数 Pearson r | {pearson:.4f} |")
    lines.append(f"| 等级分布 | SAFE {level_counts['SAFE']} / WARNING {level_counts['WARNING']} / DANGER {level_counts['DANGER']} |")
    lines.append("")
    lines.append("## 分桶检出率")
    lines.append("")
    lines.append("| GT 人数桶 | 图片数 | GT 人数 | 检测人数 | 检出率 |")
    lines.append("|---|---|---|---|---|")
    for _, _, name in BUCKETS:
        lines.append(bucket_line(name, bucket_stats[name]))
    lines.append("")
    lines.append("## GT 分桶 × 算法等级 列联表（描述性）")
    lines.append("")
    lines.append("| GT 桶 | SAFE | WARNING | DANGER |")
    lines.append("|---|---|---|---|")
    for _, _, name in BUCKETS:
        c = contingency[name]
        lines.append(f"| {name} | {c['SAFE']} | {c['WARNING']} | {c['DANGER']} |")
    lines.append("")
    lines.append("## 最差个案（供人工复核）")
    lines.append("")
    lines.append(worst_table(under, "欠检 Top 10（GT 多、检测少）"))
    lines.append("")
    lines.append(worst_table(over, "过检 Top 10（检测比 GT 多）"))
    lines.append("")
    lines.append("## 使用说明")
    lines.append("")
    lines.append("1. 重新运行检测后：`python scripts/crowd_test.py --input dataset/images500`，再执行本脚本即可更新报告。")
    lines.append("2. 最差个案清单可作为人工复核 COCO GT 与视觉实际一致性的起点（GT 标注复核任务）。")
    lines.append("3. 若要将检测数误差用于论文，请明确区分：检出率是检测器召回情况的统计，不等同于风险判断准确率。")

    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--results",
        default=os.path.join("results", "rerun", "images500_baseline", "results.json"),
    )
    args = ap.parse_args()

    results_path = os.path.join(PROJECT, args.results)
    with open(results_path, encoding="utf-8") as f:
        data = json.load(f)
    res = data["results"]

    rows = parse_rows(res)
    summary = compute_stats(rows)

    import datetime

    summary["generated_at"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    summary["source_results"] = args.results

    out_dir = os.path.join(PROJECT, "results", "images500_gt_eval")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    report = render_report(
        summary,
        args.results,
        data.get("generated_at", "未知"),
        data.get("weights", "?"),
        data.get("conf", "?"),
    )
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as f:
        f.write(report)

    print(f"已生成: {os.path.join(out_dir, 'summary.json')}")
    print(f"已生成: {os.path.join(out_dir, 'report.md')}")
    print(
        f"整体检出率 {summary['overall_detection_rate_pct']:.2f}%  "
        f"MAE {summary['mae']:.3f}  RMSE {summary['rmse']:.3f}  "
        f"Pearson r {summary['pearson_r']:.4f}"
    )


if __name__ == "__main__":
    main()