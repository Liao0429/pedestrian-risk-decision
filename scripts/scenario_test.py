#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
scenario_test.py - 同人数、不同空间分布的风险判别实验（可复现证据）

研究问题（多 Agent 评审核心质疑）：风险分级是否只是"人数+固定规则"的映射，
能否区分"分散人群"与"局部聚集人群"？

方法：
- 构造多组"人数相同、空间分布不同"的合成地面坐标场景（单位：米）。
- 用与板端完全一致的规则链计算：
    1) 图网络特征（n/density/r_gcc/clustering，graph_laplacian.py）
    2) 规则分级 rule_level（与 crowd_test.evaluate_level / C++ RiskEngine 一致）
    3) RiskFSM 时序输出（连续帧重复该场景，验证 FSM 稳定后的等级）
- 输出 results/scenarios/scenario_test.json + 控制台表格。

局限（如实说明）：
- 合成场景用于验证"空间分布→网络特征→风险等级"的判别逻辑本身，
  不代表真实相机画面下的端到端性能（真实性能见 results/rerun/）。
- 规则引擎对"链状连通"场景（1.5m 间距串成一排）会因 r_gcc=1 判为高聚集，
  这是规则阈值（gcc≥0.8 即达 DANGER）的已知特性，见输出中的 chain 场景。

运行：python scripts/scenario_test.py
测试：pytest tests/test_scenario_discrimination.py -q
"""
import json
import math
import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402
from crowd_test import evaluate_level, load_config  # noqa: E402

EDGE_DIST = 2.0  # 与 config/default_config.json edge_dist_m 一致


def grid_points(n, spacing):
    """n 个点排成紧密网格（spacing 为相邻点间距，米）。"""
    side = math.ceil(math.sqrt(n))
    pts = []
    for i in range(n):
        pts.append(((i % side) * spacing, (i // side) * spacing))
    return pts


def ring_points(n, radius):
    """n 个点均匀分布在圆周上（radius 米）。"""
    return [(radius * math.cos(2 * math.pi * i / n),
             radius * math.sin(2 * math.pi * i / n)) for i in range(n)]


def pair_points(n_pairs, gap_between_pairs):
    """n_pairs 个相距 0.5m 的二人组，组间相距 gap_between_pairs 米。"""
    pts = []
    for i in range(n_pairs):
        base_x = i * gap_between_pairs
        pts.append((base_x, 0.0))
        pts.append((base_x + 0.5, 0.0))
    return pts


def chain_points(n, spacing):
    """n 个点沿直线等距排列（链状，spacing 米）。"""
    return [(i * spacing, 0.0) for i in range(n)]


SCENARIOS = [
    ("n8_dispersed",       "8 人完全分散（间距3m）", grid_points(8, 3.0)),
    ("n8_pairs",           "8 人=4个二人组（组间3m）", pair_points(4, 3.0)),
    ("n8_cluster",         "8 人紧密网格（0.8m）", grid_points(8, 0.8)),
    ("n10_dispersed",      "10 人完全分散（间距3m）", grid_points(10, 3.0)),
    ("n10_two_clusters",   "10 人=两个5人簇（簇距4m）", grid_points(5, 0.8) + [(x + 6.0, y) for x, y in grid_points(5, 0.8)]),
    ("n10_cluster",        "10 人紧密网格（0.8m）", grid_points(10, 0.8)),
    ("n10_chain",          "10 人链状（1.5m 间距）", chain_points(10, 1.5)),
    ("n12_dispersed",      "12 人完全分散（间距3m）", grid_points(12, 3.0)),
    ("n12_cluster",        "12 人紧密网格（0.8m）", grid_points(12, 0.8)),
    ("n12_two_clusters",   "12 人=两个6人簇（簇距5m）", grid_points(6, 0.8) + [(x + 7.0, y) for x, y in grid_points(6, 0.8)]),
]


def run_scenario(name, desc, pts):
    cfg = load_config(str(PROJECT_ROOT / "config" / "default_config.json"))
    feat = compute_laplacian_features(pts, EDGE_DIST)
    rule = evaluate_level(feat["n"], feat["density"], feat["r_gcc"], cfg)

    # FSM：同一场景重复 20 帧（模拟静止人群），取最终稳定等级
    fsm = RiskFSM(RiskFSMConfig(**(cfg.get("fsm", {}) if cfg else {})))
    fsm_levels = []
    for t in range(20):
        lvl = fsm.update({"n": feat["n"], "density": feat["density"],
                          "r_gcc": feat["r_gcc"], "clustering": feat["clustering"]}, now_ms=t * 100)
        fsm_levels.append(lvl)
    fsm_final = fsm_levels[-1]

    return {
        "name": name,
        "desc": desc,
        "n": feat["n"],
        "m": feat["m"],
        "density": round(feat["density"], 4),
        "r_gcc": round(feat["r_gcc"], 4),
        "clustering": round(feat["clustering"], 4),
        "fiedler": round(feat["fiedler"], 4),
        "rule_level": rule,
        "fsm_final_level": fsm_final,
        "note": "",
    }


def main():
    results = []
    print(f"{'场景':<18}{'n':>3}{'rho':>8}{'gcc':>7}{'clu':>7}{'fiedler':>9}  规则等级    FSM等级")
    for name, desc, pts in SCENARIOS:
        r = run_scenario(name, desc, pts)
        results.append(r)
        print(f"{name:<18}{r['n']:>3}{r['density']:>8.3f}{r['r_gcc']:>7.3f}"
              f"{r['clustering']:>7.3f}{r['fiedler']:>9.3f}   {r['rule_level']:<9} {r['fsm_final_level']}")

    # 判别有效性结论（程序化判断，非人工挑选）
    same_n_pairs = [
        ("n8", [r for r in results if r["name"].startswith("n8")]),
        ("n10", [r for r in results if r["name"].startswith("n10")]),
        ("n12", [r for r in results if r["name"].startswith("n12")]),
    ]
    conclusions = []
    for prefix, group in same_n_pairs:
        levels = {r["rule_level"] for r in group}
        if len(levels) >= 2:
            conclusions.append(f"{prefix}：同人数不同分布产生不同等级（{sorted(levels)}），判别有效")
        else:
            conclusions.append(f"{prefix}：同人数不同分布等级相同（{sorted(levels)}），需结合其他特征")

    # 链状连通弱点：n10_chain 与 n10_cluster 等级相同说明 gcc 阈值对链状敏感
    chain = next(r for r in results if r["name"] == "n10_chain")
    cluster = next(r for r in results if r["name"] == "n10_cluster")
    if chain["rule_level"] == cluster["rule_level"] == "DANGER":
        conclusions.append(
            "注意：n10_chain（1.5m 链状）与 n10_cluster 均为 DANGER——规则引擎的 gcc≥0.8 把链状连通也判为高聚集，"
            "这是当前阈值设计的已知局限（可改用 clustering/谱特征区分链与团）")

    out = {"edge_dist_m": EDGE_DIST,
           "method": "合成场景 -> 图网络特征 -> 规则分级 + RiskFSM（连续20帧）",
           "scenarios": results,
           "conclusions": conclusions}
    os.makedirs(str(PROJECT_ROOT / "results" / "scenarios"), exist_ok=True)
    out_path = PROJECT_ROOT / "results" / "scenarios" / "scenario_test.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print("\n结论：")
    for c in conclusions:
        print(" -", c)
    print(f"\n已保存 {out_path}")


if __name__ == "__main__":
    main()