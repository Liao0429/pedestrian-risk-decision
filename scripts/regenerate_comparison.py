#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
regenerate_comparison.py - 重新生成算法前后对比汇总（公平基线版本）

背景（多 Agent 评审 P0）：旧版 scripts/rerun_video_baseline.py 的逐帧基线误用
OR 规则（n/rho/gcc 任一达标即 DANGER），导致基线 DANGER 帧被系统性高估
（21969 帧 / 102 条视频）。已修正为 AND 规则（与 crowd_test.evaluate_level /
C++ RiskEngine::evaluate 一致）并全量重跑 111 条视频。

本脚本读取：
- results/rerun/video_baseline/*/*_baseline.json（新基线）
- results/rerun/video_enhanced/*/*_timeline.json（增强时间线）

输出：
- results/rerun/_comparison_summary.json（与旧格式一致，pairs 逐视频对比）

运行：python scripts/regenerate_comparison.py
"""
import glob
import json
import os
from collections import Counter
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
os.chdir(PROJECT)

BASELINE_FILES = sorted(glob.glob("results/rerun/video_baseline/*/*_baseline.json"))
ENHANCED_FILES = sorted(glob.glob("results/rerun/video_enhanced/*/*_timeline.json"))


def load_enhanced(name):
    path = f"results/rerun/video_enhanced/{name}/{name}_timeline.json"
    if not os.path.exists(path):
        return None
    tl = json.load(open(path, encoding="utf-8"))
    levels = [f["level"] for f in tl]
    return {
        "name": name,
        "frames": len(levels),
        "switches": sum(1 for i in range(1, len(levels)) if levels[i] != levels[i - 1]),
        "dist": dict(Counter(levels)),
    }


def main():
    pairs = []
    total_base_sw = total_enh_sw = 0
    base_frames = enh_frames = 0
    base_lv = Counter()
    enh_lv = Counter()
    base_danger_vids = enh_danger_vids = 0
    base_safe_vids = enh_safe_vids = 0

    for bf in BASELINE_FILES:
        b = json.load(open(bf, encoding="utf-8"))
        name = b["video"]
        e = load_enhanced(name)
        if e is None:
            print(f"[skip] {name}: 无增强时间线", flush=True)
            continue
        bd = b["level_distribution"]
        pairs.append([name, {
            "frames": b["frames"],
            "switches": b["switches"],
            "level_distribution": bd,
        }, e])

        total_base_sw += b["switches"]
        total_enh_sw += e["switches"]
        base_frames += b["frames"]
        enh_frames += e["frames"]
        base_lv.update(bd)
        enh_lv.update(e["dist"])
        if bd.get("DANGER", 0) > 0:
            base_danger_vids += 1
        if e["dist"].get("DANGER", 0) > 0:
            enh_danger_vids += 1
        # “全程 SAFE” = 所有帧等级均为 SAFE（与 README “31 条未触发报警”口径一致）
        if set(bd.keys()) == {"SAFE"}:
            base_safe_vids += 1
        if set(e["dist"].keys()) == {"SAFE"}:
            enh_safe_vids += 1

    summary = {
        "n": len(pairs),
        "base_frames": base_frames,
        "enh_frames": enh_frames,
        "base_sw": total_base_sw,
        "enh_sw": total_enh_sw,
        "base_lv": dict(base_lv),
        "enh_lv": dict(enh_lv),
        "base_danger": base_danger_vids,
        "enh_danger": enh_danger_vids,
        "base_safe": base_safe_vids,
        "enh_safe": enh_safe_vids,
        "pairs": pairs,
        "_note": "基线已修正为 AND 规则（与真实引擎一致），旧 OR 基线数据不再使用。"
                 "增强后 DANGER 帧/视频数上升是 FSM 评分更敏感（含密度权重）与 DANGER 保持机制所致，"
                 "论文不得再表述为‘增强后 DANGER 下降’。",
    }
    with open("results/rerun/_comparison_summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: summary[k] for k in
                      ("n", "base_sw", "enh_sw", "base_lv", "enh_lv",
                       "base_danger", "enh_danger", "base_safe", "enh_safe")},
                     ensure_ascii=False, indent=2))
    print("已写入 results/rerun/_comparison_summary.json")


if __name__ == "__main__":  # pragma: no cover
    main()