# -*- coding: utf-8 -*-
"""
场景判别回归测试（Agent 评审核心问题：同人数不同分布能否产生不同风险等级）

验证点：
1. 10 人完全分散 -> 规则等级 SAFE；10 人紧密网格 -> DANGER（同人数、不同等级）。
2. 12 人完全分散 -> SAFE；12 人紧密网格 -> DANGER。
3. 8 人完全分散 -> SAFE；8 人紧密网格 -> WARNING（规则引擎 n<10 不上 DANGER）。
4. n10_chain 链状（1.5m）在规则引擎下因 r_gcc=1 判 DANGER（已知局限），
   FSM（含 density 权重）判 WARNING —— 记录该差异，防止未来回归改变语义。

运行：pytest tests/test_scenario_discrimination.py -q
"""
import pathlib
import sys

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from crowd_test import evaluate_level, load_config
from scenario_test import (SCENARIOS, EDGE_DIST, grid_points, chain_points,
                           run_scenario)
from algorithms.graph_laplacian import compute_laplacian_features


def _cfg():
    return load_config(str(PROJECT_ROOT / "config" / "default_config.json"))


def _rule_for(pts):
    cfg = _cfg()
    feat = compute_laplacian_features(pts, EDGE_DIST)
    return evaluate_level(feat["n"], feat["density"], feat["r_gcc"], cfg)


def test_dispersed_vs_cluster_n10():
    """10 人分散 SAFE vs 10 人紧密 DANGER。"""
    assert _rule_for(grid_points(10, 3.0)) == "SAFE"
    assert _rule_for(grid_points(10, 0.8)) == "DANGER"


def test_dispersed_vs_cluster_n12():
    """12 人分散 SAFE vs 12 人紧密 DANGER。"""
    assert _rule_for(grid_points(12, 3.0)) == "SAFE"
    assert _rule_for(grid_points(12, 0.8)) == "DANGER"


def test_small_cluster_n8_is_warning_not_danger():
    """8 人紧密网格：规则引擎 n<10 应为 WARNING（不越级 DANGER）。"""
    assert _rule_for(grid_points(8, 0.8)) == "WARNING"


def test_chain_is_danger_by_gcc_rule():
    """10 人 1.5m 链状：r_gcc=1 使规则引擎判 DANGER（已知阈值局限，文档记录）。"""
    feat = compute_laplacian_features(chain_points(10, 1.5), EDGE_DIST)
    assert feat["r_gcc"] == 1.0
    assert _rule_for(chain_points(10, 1.5)) == "DANGER"


def test_scenario_results_are_discriminative():
    """全量场景 JSON：同人数不同分布必须产生至少两个不同规则等级。"""
    results = [run_scenario(n, d, pts) for n, d, pts in SCENARIOS]
    by_n = {}
    for r in results:
        by_n.setdefault(r["n"], set()).add(r["rule_level"])
    for n, levels in by_n.items():
        assert len(levels) >= 2, f"n={n} 所有场景等级相同 {levels}，判别失效"


if __name__ == "__main__":
    test_dispersed_vs_cluster_n10()
    test_dispersed_vs_cluster_n12()
    test_small_cluster_n8_is_warning_not_danger()
    test_chain_is_danger_by_gcc_rule()
    test_scenario_results_are_discriminative()
    print("✅ 场景判别测试通过")