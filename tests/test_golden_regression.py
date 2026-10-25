# -*- coding: utf-8 -*-
"""
test_golden_regression.py — C++/Python 双实现一致性黄金回归测试（Python 侧）

读取 tests/golden/*.json（由 scripts/generate_golden.py 用 Python 参考实现生成），
重新运行当前 Python 实现并与黄金期望比对，防止 Python 实现相对黄金数据漂移。
C++ 侧对应主机测试见 src/luckfox_rtsp_yolov5/tests/test_golden_regression.cc，
两侧读取同一批 JSON，从机制上保证双实现一致。

运行：
  H:/py311_gpu/Scripts/python.exe -m pytest tests/test_golden_regression.py -q
"""

import json
import math
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT_ROOT, "src")
if SRC not in sys.path:
    sys.path.insert(0, SRC)

from algorithms.ema_smoother import EMASmoother                  # noqa: E402
from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig            # noqa: E402

GOLDEN_DIR = os.path.join(PROJECT_ROOT, "tests", "golden")

# 与 C++ 主机测试保持一致的容差
FEAT_TOL = 1e-4   # 谱特征（Jacobi vs np.linalg.eigvalsh 的数值差异）
EMA_TOL = 1e-6    # EMA 浮点误差


def _load(name: str):
    with open(os.path.join(GOLDEN_DIR, name), encoding="utf-8") as f:
        return json.load(f)


def _close(a, b, tol):
    return abs(float(a) - float(b)) <= tol


def test_graph_laplacian_golden():
    data = _load("graph_laplacian.json")
    assert data["cases"], "黄金数据为空"
    for case in data["cases"]:
        pts = [tuple(p) for p in case["points"]]
        got = compute_laplacian_features(pts, case["edge_dist"])
        exp = case["expected"]
        for key in ("n", "m"):
            assert int(got[key]) == exp[key], (
                f"{case['name']}: {key} 期望 {exp[key]} 实际 {got[key]}")
        for key in ("density", "r_gcc", "clustering", "fiedler",
                    "spectral_radius", "eig_entropy"):
            assert _close(got[key], exp[key], FEAT_TOL), (
                f"{case['name']}: {key} 期望 {exp[key]} 实际 {got[key]:.10f}")


def test_ema_smoother_golden():
    data = _load("ema_smoother.json")
    assert data["cases"], "黄金数据为空"
    for case in data["cases"]:
        sm = EMASmoother(alpha=case["alpha"], dim=case["dim"],
                         use_timestamp=case["use_timestamp"], tau=case["tau"])
        got = []
        for v, t in zip(case["values"], case["timestamps"]):
            out = sm.update(v, t=t)
            if hasattr(out, "__len__") and not isinstance(out, (int, float)):
                got.append([float(x) for x in out])
            else:
                got.append(float(out))
        exp = case["expected"]
        assert len(got) == len(exp), (
            f"{case['name']}: 输出长度 {len(got)} != 期望 {len(exp)}")
        for i, (g, e) in enumerate(zip(got, exp)):
            if isinstance(e, (list, tuple)):
                assert len(g) == len(e), (
                    f"{case['name']} frame{i}: 维度 {len(g)} != {len(e)}")
                for gi, ei in zip(g, e):
                    assert _close(gi, ei, EMA_TOL), (
                        f"{case['name']} frame{i}: 期望 {ei} 实际 {gi}")
            else:
                assert _close(g, e, EMA_TOL), (
                    f"{case['name']} frame{i}: 期望 {e} 实际 {g}")


def test_risk_fsm_golden():
    data = _load("risk_fsm.json")
    assert data["cases"], "黄金数据为空"
    for case in data["cases"]:
        fsm = RiskFSM(RiskFSMConfig(**case["config"]))
        got = []
        for feat, t in zip(case["frames"], case["timestamps"]):
            # JSON null -> None（无时间戳），对应 C++ 端 -1 哨兵
            got.append(fsm.update(feat, now_ms=t))
        assert got == case["expected"], (
            f"{case['name']}: 期望 {case['expected']} 实际 {got}")


if __name__ == "__main__":
    import traceback

    failed = 0
    for fn in (test_graph_laplacian_golden, test_ema_smoother_golden,
               test_risk_fsm_golden):
        try:
            fn()
            print(f"PASS {fn.__name__}")
        except Exception:
            failed += 1
            print(f"FAIL {fn.__name__}")
            traceback.print_exc()
    sys.exit(1 if failed else 0)