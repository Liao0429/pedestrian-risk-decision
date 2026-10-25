#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_golden.py — 生成 C++/Python 双实现一致性回归的黄金数据（Golden Data）。

背景
----
graph_laplacian / ema_smoother / risk_fsm 存在 C++ 头文件版（板端）
与 Python 版（原型/测试）双实现，接口刻意对齐。为避免两套实现漂移，
本脚本用 **已由 130+ 单测验证的 Python 实现** 作为参考（reference），
生成固定输入/期望输出 JSON 到 tests/golden/：

  - graph_laplacian.json : 点集 + edge_dist -> 谱特征
  - ema_smoother.json    : alpha/dim + 数值序列 + 时间戳 -> 平滑序列
  - risk_fsm.json        : 配置 + 特征帧序列 + 时间戳 -> 等级序列

C++ 主机测试（tests/test_golden_regression.cc）读取同一批 JSON，
运行 C++ 实现并与期望输出比对；Python 侧测试（tests/test_golden_regression.py）
读取同一批 JSON 验证 Python 实现仍与黄金参考一致。从机制上防止双实现漂移。

运行：
  H:/py311_gpu/Scripts/python.exe scripts/generate_golden.py
  （在项目根目录执行；需 numpy）
"""

from __future__ import annotations

import json
import os
import sys

# 让脚本可在项目根目录或任意位置运行
_HERE = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(_HERE)
_SRC = os.path.join(_PROJECT_ROOT, "src", "algorithms")
if _SRC not in sys.path:
    sys.path.insert(0, _SRC)

from ema_smoother import EMASmoother          # noqa: E402
from graph_laplacian import compute_laplacian_features  # noqa: E402
from risk_fsm import RiskFSM, RiskFSMConfig   # noqa: E402

GOLDEN_DIR = os.path.join(_PROJECT_ROOT, "tests", "golden")


def fround(v: float, ndigits: int = 10) -> float:
    """截断浮点精度，避免 JSON 里出现 0.30000000000000004 之类的长尾。"""
    return round(float(v), ndigits)


def gen_graph_laplacian() -> list:
    """生成 graph_laplacian 黄金用例。

    注意：C++ GraphLaplacian 会对 NaN/Inf 坐标做输入过滤（板端健壮性），
    而 Python 原型不做过滤，两实现语义在此 **有意不同**。因此黄金回归只收录
    两实现语义一致的合法输入用例；NaN 过滤路径由各自单测单独覆盖。
    """
    cases = [
        {"name": "empty", "points": [], "edge_dist": 2.0},
        {"name": "single", "points": [[0.0, 0.0]], "edge_dist": 2.0},
        {"name": "dispersed_4",
         "points": [[0.0, 0.0], [5.0, 0.0], [10.0, 0.0], [15.0, 0.0]],
         "edge_dist": 2.0},
        {"name": "path_p5",
         "points": [[i * 1.5, 0.0] for i in range(5)],
         "edge_dist": 2.0},
        {"name": "complete_k4",
         "points": [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]],
         "edge_dist": 2.0},
        {"name": "two_clusters",
         "points": [[0.0, 0.0], [0.5, 0.0], [0.0, 0.5], [10.0, 10.0],
                    [10.5, 10.0], [10.0, 10.5]],
         "edge_dist": 2.0},
    ]
    out = []
    for c in cases:
        pts = c["points"]
        feats = compute_laplacian_features(pts, c["edge_dist"])
        expected = {
            "n": int(feats["n"]),
            "m": int(feats["m"]),
            "density": fround(feats["density"]),
            "r_gcc": fround(feats["r_gcc"]),
            "clustering": fround(feats["clustering"]),
            "fiedler": fround(feats["fiedler"]),
            "spectral_radius": fround(feats["spectral_radius"]),
            "eig_entropy": fround(feats["eig_entropy"]),
        }
        out.append({
            "name": c["name"],
            "points": pts,
            "edge_dist": c["edge_dist"],
            "expected": expected,
        })
    return {"description": "graph_laplacian 双实现黄金回归", "cases": out}


def gen_ema_smoother() -> list:
    """生成 ema_smoother 黄金用例。"""
    cases = []

    # 1) 标量 EMA（alpha=0.3，dim=1）
    values = [0.6, 0.9, 0.2, 0.8, 0.5]
    sm = EMASmoother(alpha=0.3)
    smoothed = [fround(sm.update(v, t=i)) for i, v in enumerate(values)]
    cases.append({
        "name": "scalar_alpha_0.3",
        "alpha": 0.3,
        "dim": 1,
        "use_timestamp": False,
        "tau": 1.0,
        "values": values,
        "timestamps": list(range(len(values))),
        "expected": smoothed,
    })

    # 2) 向量 EMA（dim=3，每维独立 alpha）
    alpha_vec = [0.2, 0.5, 0.8]
    values_vec = [
        [0.5, 0.6, 0.4],
        [0.9, 0.3, 0.7],
        [0.1, 0.8, 0.2],
        [0.7, 0.4, 0.6],
    ]
    smv = EMASmoother(alpha=alpha_vec, dim=3)
    smoothed_vec = [
        [fround(x) for x in smv.update(v, t=i)]
        for i, v in enumerate(values_vec)
    ]
    cases.append({
        "name": "vector_dim3_per_dim_alpha",
        "alpha": alpha_vec,
        "dim": 3,
        "use_timestamp": False,
        "tau": 1.0,
        "values": values_vec,
        "timestamps": list(range(len(values_vec))),
        "expected": smoothed_vec,
    })

    # 3) 时间感知模式（use_timestamp=True, tau=2.0），非等间隔
    smt = EMASmoother(alpha=0.3, dim=1, use_timestamp=True, tau=2.0)
    t_ts = [0, 50, 130, 210, 400]
    v_ts = [1.0, 2.0, 0.5, 1.5, 3.0]
    smoothed_ts = []
    for t, v in zip(t_ts, v_ts):
        smoothed_ts.append(fround(smt.update(v, t=t)))
    cases.append({
        "name": "timestamped_tau_2",
        "alpha": 0.3,
        "dim": 1,
        "use_timestamp": True,
        "tau": 2.0,
        "values": v_ts,
        "timestamps": t_ts,
        "expected": smoothed_ts,
    })

    return {"description": "ema_smoother 双实现黄金回归", "cases": cases}


def gen_risk_fsm() -> list:
    """生成 risk_fsm 黄金用例。

    config 以 dict 形式定义，Python 端用 RiskFSMConfig(**cfg) 运行，
    同一份 dict 原样写入 JSON 供 C++ 端读取 —— 保证两侧使用相同配置。
    """
    cases = []

    def run(cfg: dict, frames, timestamps):
        fsm = RiskFSM(RiskFSMConfig(**cfg))
        return [fsm.update(f, t) for f, t in zip(frames, timestamps)]

    HIGH = {"n": 15, "density": 0.8, "r_gcc": 0.9, "trend": 0.0}  # score=0.81
    LOW = {"n": 0, "density": 0.0, "r_gcc": 0.0, "trend": 0.0}    # score=0.0

    # 1) 阶梯升级（3 帧确认）-> DANGER -> 帧+毫秒双重保持 -> 阶梯降级
    cfg1 = {"confirm_up": 3, "confirm_down": 2,
            "danger_hold_frames": 2, "danger_hold_ms": 200}
    frames1 = [LOW] + [HIGH] * 6 + [LOW] * 11
    t1 = [i * 33 for i in range(len(frames1))]
    cases.append({
        "name": "ladder_up_down_with_dual_hold",
        "config": cfg1,
        "frames": frames1,
        "timestamps": t1,
        "expected": run(cfg1, frames1, t1),
    })

    # 2) now_ms=None 帧保持（no-timestamp 路径）—— 覆盖曾修复的 bug
    #    JSON 里用 null 表示"无时间戳"：Python 端映射为 None，
    #    C++ 端映射为 -1 哨兵（now_ms<0 时禁用毫秒保持，仅帧保持生效）。
    cfg2 = {"confirm_up": 1, "confirm_down": 2,
            "danger_hold_frames": 3, "danger_hold_ms": 1000}
    frames2 = [HIGH, HIGH] + [LOW] * 7
    t2 = [None] * len(frames2)
    cases.append({
        "name": "hold_without_timestamp",
        "config": cfg2,
        "frames": frames2,
        "timestamps": t2,
        "expected": run(cfg2, frames2, t2),
    })

    # 3) 滞回：score 在滞回带内保持原等级
    cfg3 = {"confirm_up": 1, "confirm_down": 1,
            "danger_hold_frames": 0, "danger_hold_ms": 0}
    frames3 = [
        {"n": 10, "density": 0.5, "r_gcc": 0.5, "trend": 0.0},
        {"n": 8, "density": 0.4, "r_gcc": 0.4, "trend": 0.0},
        {"n": 2, "density": 0.1, "r_gcc": 0.1, "trend": 0.0},
    ]
    t3 = [0, 33, 66]
    cases.append({
        "name": "hysteresis_band",
        "config": cfg3,
        "frames": frames3,
        "timestamps": t3,
        "expected": run(cfg3, frames3, t3),
    })

    return {"description": "risk_fsm 双实现黄金回归", "cases": cases}


def main() -> None:
    os.makedirs(GOLDEN_DIR, exist_ok=True)

    datasets = {
        "graph_laplacian.json": gen_graph_laplacian(),
        "ema_smoother.json": gen_ema_smoother(),
        "risk_fsm.json": gen_risk_fsm(),
    }

    for fname, data in datasets.items():
        path = os.path.join(GOLDEN_DIR, fname)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(f"写入 {path}（{len(data['cases'])} 个用例）")


if __name__ == "__main__":
    main()