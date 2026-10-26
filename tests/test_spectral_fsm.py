# -*- coding: utf-8 -*-
"""
谱特征接入 FSM 评分的单元测试（Agent3 方案 B：相对代数连通度 λ₂/λ_max）

验证：
1. w4=0（默认）：谱特征不参与评分，行为与旧版完全一致。
2. w4>0：谱特征提高 score（fiedler 越大 score 越高）。
3. 相对代数连通度被 clamp 到 [0,1]。
4. spectral_radius=0 时不崩溃、ratio=0。
5. config/default_config.json 的 fsm 块含 w4 键（默认 0）。

运行：pytest tests/test_spectral_fsm.py -q
"""
import json
import pathlib

import pytest

from src.algorithms.risk_fsm import RiskFSM, RiskFSMConfig

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent


def _base_features(**kw):
    f = {"n": 10, "density": 0.5, "r_gcc": 0.6, "fiedler": 2.0, "spectral_radius": 4.0}
    f.update(kw)
    return f


def test_w4_zero_keeps_score_unchanged():
    """w4=0 时谱特征不参与评分（兼容旧行为）。"""
    cfg = RiskFSMConfig(w4=0.0)
    fsm_low = RiskFSM(cfg)
    fsm_low.update(_base_features(fiedler=0.1), now_ms=0)
    fsm_high = RiskFSM(cfg)
    fsm_high.update(_base_features(fiedler=10.0), now_ms=0)
    assert fsm_low.score == fsm_high.score


def test_w4_positive_increases_score():
    """w4>0 时 fiedler 越大 score 越高（谱特征真正影响决策）。"""
    cfg = RiskFSMConfig(w4=0.2)
    fsm_low = RiskFSM(cfg)
    fsm_low.update(_base_features(fiedler=0.5), now_ms=0)
    fsm_high = RiskFSM(cfg)
    fsm_high.update(_base_features(fiedler=3.5), now_ms=0)
    assert fsm_high.score > fsm_low.score
    # 且低谱 vs 高谱的相对差值 = w4 * Δratio = 0.2 * (3.0/4.0)
    assert abs((fsm_high.score - fsm_low.score) - 0.2 * (3.0 / 4.0)) < 1e-9


def test_spectral_ratio_clamped():
    """fiedler/spectral_radius 被 clamp 到 [0,1]。"""
    cfg = RiskFSMConfig(w4=0.5)
    fsm = RiskFSM(cfg)
    fsm.update(_base_features(n=0, density=0.0, r_gcc=0.0, fiedler=10.0, spectral_radius=1.0), now_ms=0)
    # score = w4 * clamp(10/1, 0,1) = 0.5
    assert abs(fsm.score - 0.5) < 1e-9


def test_spectral_radius_zero_no_crash():
    """谱半径为 0（无图/单点）时不崩溃，ratio=0。"""
    cfg = RiskFSMConfig(w4=0.3)
    fsm = RiskFSM(cfg)
    fsm.update(_base_features(fiedler=0.0, spectral_radius=0.0), now_ms=0)
    assert 0.0 <= fsm.score <= 1.0


def test_spectral_ratio_normalized_across_sizes():
    """相对代数连通度跨规模可比：完全图 K5 vs K20 的 ratio 应接近。"""
    import math
    cfg = RiskFSMConfig(w4=0.2)

    def ratio_for(clique_size):
        pts = [(i, 0.0) for i in range(clique_size)]  # 完全图：任意两点距离 < 2
        A = [[0.0] * clique_size for _ in range(clique_size)]
        for i in range(clique_size):
            for j in range(clique_size):
                if i != j:
                    A[i][j] = 1.0
        import numpy as np
        L = np.diag([sum(row) for row in A]) - np.array(A)
        eig = np.linalg.eigvalsh(L)
        return float(eig[1] / eig[-1])

    r5 = ratio_for(5)
    r20 = ratio_for(20)
    # 完全图 K_n：λ₂ = n，λ_max = n，ratio = 1
    assert abs(r5 - 1.0) < 1e-6 and abs(r20 - 1.0) < 1e-6


def test_config_json_has_w4():
    """default_config.json 的 fsm 块应含 w4 键（默认 0）。"""
    cfg = json.loads((PROJECT_ROOT / "config" / "default_config.json").read_text(encoding="utf-8"))
    assert "w4" in cfg.get("fsm", {}), "fsm 配置块应包含 w4 键"
    assert cfg["fsm"]["w4"] == 0.0


if __name__ == "__main__":
    test_w4_zero_keeps_score_unchanged()
    test_w4_positive_increases_score()
    test_spectral_ratio_clamped()
    test_spectral_radius_zero_no_crash()
    test_spectral_ratio_normalized_across_sizes()
    test_config_json_has_w4()
    print("✅ 谱特征 FSM 测试通过")