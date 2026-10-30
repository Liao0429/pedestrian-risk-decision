#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""111 条真实视频时间线聚合重算（论文数字最终真源）。

数据来源
--------
results/rerun/video_enhanced/*/*_timeline.json（111 条真实 Mixkit 视频，逐帧
{"frame","level","n","density","r_gcc","fiedler"}）。本脚本**只做逐帧聚合**，
不重新推理、不修改任何逐帧判定，因此结果可复现、可审计。

为什么单独成脚本
----------------
原聚合逻辑内嵌在 scripts/plot_data_analysis.py（画图脚本）里，写
results/rerun/_aggregate.json。画图脚本依赖 matplotlib，跑不动就出不了数字，
且"数字真源"与"画图"耦合。本脚本保持**纯标准库**，输出字段与旧 _aggregate.json
完全对齐（total_videos/total_frames/levels/switches/videos_with_danger/
fiedler_by_level/per_video），可独立复核论文数字。

用法
----
    python scripts/aggregate_timelines.py                    # 写 _aggregate_recomputed.json
    python scripts/aggregate_timelines.py --compare          # 再与旧 _aggregate.json 逐字段比对
    python scripts/aggregate_timelines.py --check            # 仅比对，不一致则退出码 1

统计口径（与旧实现一致，勿随意更改，否则论文数字会漂移）
    mean/median/std : statistics.mean / median / **pstdev**(总体标准差)
    median 偶数个样本取中间两数平均（statistics.median 语义）
    fiedler/std/mean 四舍五入到 4 位小数；median 在 switches 处不四舍五入
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import statistics as st
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEVELS = ("SAFE", "WARNING", "DANGER")


def load_timelines(pattern: str):
    """读取全部 timeline，返回 [(video_name, frames), ...]（按视频名排序）。"""
    paths = sorted(glob.glob(pattern))
    out = []
    for path in paths:
        name = os.path.basename(os.path.dirname(path))
        with open(path, encoding="utf-8") as f:
            out.append((name, json.load(f)))
    return out


def aggregate(timelines) -> dict:
    """逐帧聚合，字段与 results/rerun/_aggregate.json 完全对齐。"""
    agg = {
        "total_videos": len(timelines),
        "total_frames": 0,
        "levels": {k: 0 for k in LEVELS},
        "switches": [],
        "videos_with_danger": 0,
        "fiedler_by_level": {k: [] for k in LEVELS},
        "per_video": {},
    }
    for name, data in timelines:
        levels = [f["level"] for f in data]
        dist = {k: levels.count(k) for k in LEVELS}
        sw = sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1])

        agg["total_frames"] += len(levels)
        for k in LEVELS:
            agg["levels"][k] += dist[k]
        agg["switches"].append(sw)
        if dist["DANGER"] > 0:
            agg["videos_with_danger"] += 1
        for f in data:
            agg["fiedler_by_level"][f["level"]].append(float(f.get("fiedler", 0.0)))
        agg["per_video"][name] = {"frames": len(levels), "dist": dist, "switches": sw}

    for k in LEVELS:
        arr = agg["fiedler_by_level"][k]
        pos = [x for x in arr if x > 0]
        agg["fiedler_by_level"][k] = {
            "count": len(arr),
            "mean": round(float(st.mean(arr)), 4) if arr else 0.0,
            "median": round(float(st.median(arr)), 4) if arr else 0.0,
            "std": round(float(st.pstdev(arr)), 4) if arr else 0.0,
            "positive_count": len(pos),
            "positive_mean": round(float(st.mean(pos)), 4) if pos else 0.0,
        }

    sw_list = agg["switches"]
    agg["switches"] = {
        "list": sw_list,
        "total": int(sum(sw_list)),
        "mean": round(float(st.mean(sw_list)), 4) if sw_list else 0.0,
        "median": float(st.median(sw_list)) if sw_list else 0.0,
        "min": int(min(sw_list)) if sw_list else 0,
        "max": int(max(sw_list)) if sw_list else 0,
    }
    return agg


def diff_aggregates(old: dict, new: dict):
    """逐字段比较两份聚合结果，返回 [(字段路径, 旧值, 新值), ...]。"""
    out = []

    def walk(path, a, b):
        if isinstance(a, dict) and isinstance(b, dict):
            for k in sorted(set(a) | set(b)):
                if k.startswith("_"):
                    continue  # _comment 等 provenance/说明字段不参与数值比对
                walk(f"{path}.{k}" if path else k, a.get(k, "<缺失>"), b.get(k, "<缺失>"))
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                out.append((path, f"len={len(a)}", f"len={len(b)}"))
                return
            for i, (x, y) in enumerate(zip(a, b)):
                walk(f"{path}[{i}]", x, y)
        else:
            if a != b:
                out.append((path, a, b))

    walk("", old, new)
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="111 条真实视频时间线聚合重算（论文数字真源）")
    ap.add_argument("--timeline-dir", default=os.path.join("results", "rerun", "video_enhanced"),
                    help="timeline 根目录（默认 results/rerun/video_enhanced）")
    ap.add_argument("--out", default=os.path.join("results", "rerun", "_aggregate_recomputed.json"),
                    help="输出 JSON 路径")
    ap.add_argument("--baseline", default=os.path.join("results", "rerun", "_aggregate.json"),
                    help="用于比对的历史聚合结果")
    ap.add_argument("--compare", action="store_true", help="与历史聚合逐字段比对并打印")
    ap.add_argument("--check", action="store_true", help="仅比对；有差异则返回退出码 1")
    args = ap.parse_args(argv)

    os.chdir(PROJECT)
    pattern = os.path.join(args.timeline_dir, "*", "*_timeline.json")
    timelines = load_timelines(pattern)
    if not timelines:
        print(f"[aggregate] 未找到 timeline: {pattern}", file=sys.stderr)
        return 2

    agg = aggregate(timelines)
    print(f"[aggregate] {agg['total_videos']} 条视频 / {agg['total_frames']} 帧")
    print(f"[aggregate] 等级分布: {agg['levels']}")
    print(f"[aggregate] 切换: 总 {agg['switches']['total']} / 均值 {agg['switches']['mean']}"
          f" / 中位 {agg['switches']['median']} / 最大 {agg['switches']['max']}")
    print(f"[aggregate] 出现 DANGER 的视频: {agg['videos_with_danger']} 条")

    if args.compare or args.check:
        if not os.path.isfile(args.baseline):
            print(f"[aggregate] 基线不存在，跳过比对: {args.baseline}", file=sys.stderr)
            return 1 if args.check else 0
        with open(args.baseline, encoding="utf-8") as f:
            old = json.load(f)
        diffs = diff_aggregates(old, agg)
        if diffs:
            print(f"[aggregate] 与 {args.baseline} 有 {len(diffs)} 处差异：")
            for path, a, b in diffs[:40]:
                print(f"  - {path}: 旧={a} 新={b}")
        else:
            print(f"[aggregate] 与 {args.baseline} 逐字段一致（重算可复现，数字可信）")
        if args.check and diffs:
            return 1

    if not args.check:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(agg, f, ensure_ascii=False, indent=2)
        print(f"[aggregate] 已写入 {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
