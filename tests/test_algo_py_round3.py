# -*- coding: utf-8 -*-
"""round3 算法审计（algo-py）新增回归测试。

每个用例对应本轮审计（docs/review/20261030_round3/algo_python.md）发现的一条真实缺陷，
先写失败用例、再修实现，避免"改了代码但没人证明改对了"。

覆盖：
  A. crowd_dbscan 噪声标签约定 —— 必须与板端 crowd_dbscan.h 一致（-1），
     原 Python 用 -2，且原测试里的"Python 复刻 C++"副本同样写 -2，导致分歧被掩盖。
  B. crowd_dbscan 簇输出顺序 —— 簇大小并列时必须是确定顺序（簇编号升序），
     原实现用 np.argsort(...)[::-1]（非稳定排序 + 反转），并列簇中心顺序不确定。
  C. graph_laplacian 脚点去重语义 —— 只与"已保留点"比较，2cm 严格小于；
     证明 C++ 头文件当前实现（与全部历史点比较）与 Python 语义不同（C++ 侧交 T1）。
  D. risk_fsm min_danger_n 门控 —— 人数不足禁止升 DANGER；且"保持路径"并非
     注释所说"立即退出"，仍需 holdElapsed + confirm_down（注释与代码不符）。
  E. ipm_calibrate 必须拒绝"像素点共线"的标定输入 —— 原实现只检查地面点，
     像素点共线时 cv2 返回退化单应，所有脚点映射到同一点，静默失效。
"""
import os
import re
import sys

import numpy as np
import pytest

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")
if SRC_DIR not in sys.path:
    sys.path.insert(0, SRC_DIR)

from algorithms.crowd_dbscan import (  # noqa: E402
    NOISE,
    UNCLASSIFIED,
    dbscan_crowd_features,
    dbscan_labels,
)
from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.ipm_calibrate import calibrate_from_points, pixel_to_ground  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402

CPP_DBSCAN_HEADER = os.path.join(SRC_DIR, "algorithms", "crowd_dbscan.h")


# ---------------------------------------------------------------------------
# A. crowd_dbscan 噪声标签约定
# ---------------------------------------------------------------------------
class TestDbscanNoiseLabel:
    """标签取值是跨语言契约：C++ header 与 Python 必须一致，否则下游按 -1 判噪声会漏。"""

    def test_noise_constant_is_minus_one(self):
        assert NOISE == -1, "crowd_dbscan.h 约定噪声标签为 -1；Python 侧必须一致"
        assert UNCLASSIFIED != NOISE, "内部哨兵不能与对外噪声标签重叠"

    def test_cpp_header_uses_same_convention(self):
        """直接读板端头文件，防止两边再次漂移（头文件是板端唯一真相）。"""
        if not os.path.isfile(CPP_DBSCAN_HEADER):
            pytest.skip("缺少 crowd_dbscan.h")
        text = open(CPP_DBSCAN_HEADER, encoding="utf-8", errors="replace").read()
        m = re.search(r"NOISE\s*=\s*(-?\d+)", text)
        assert m, "未在 crowd_dbscan.h 中找到 NOISE 常量"
        assert int(m.group(1)) == NOISE

    def test_noise_points_labelled_noise(self):
        # (5,5) 远离其余两点 -> 噪声
        labels, n_clusters = dbscan_labels([(0.0, 0.0), (0.0, 0.5), (5.0, 5.0)], 1.0, 2)
        assert n_clusters == 1
        assert labels[2] == NOISE

    def test_crowd_features_noise_consistent_with_labels(self):
        feat = dbscan_crowd_features([(0.0, 0.0), (0.0, 0.5), (5.0, 5.0)], 1.0, 2)
        assert feat["n_noise"] == int((feat["labels"] == -1).sum())


# ---------------------------------------------------------------------------
# B. 并列簇的输出顺序必须确定
# ---------------------------------------------------------------------------
class TestDbscanDeterministicOrder:
    """cluster_sizes / cluster_centers 是可视化与下游决策的输入，顺序必须可复现。"""

    def test_tied_clusters_ordered_by_cluster_id(self):
        # 两簇人数相同(2 vs 2)，簇 1 在 x=0，簇 2 在 x=10
        pts = [(0.0, 0.0), (0.0, 0.5), (10.0, 0.0), (10.0, 0.5)]
        feat = dbscan_crowd_features(pts, 1.0, 2)
        assert feat["cluster_sizes"] == [2, 2]
        assert feat["cluster_centers"][0] == pytest.approx((0.0, 0.25))
        assert feat["cluster_centers"][1] == pytest.approx((10.0, 0.25))

    def test_order_is_reproducible(self):
        pts = [(0.0, 0.0), (0.0, 0.5), (10.0, 0.0), (10.0, 0.5)]
        runs = [dbscan_crowd_features(pts, 1.0, 2)["cluster_centers"] for _ in range(5)]
        assert all(r == runs[0] for r in runs)

    def test_size_order_still_descending(self):
        # 3 人簇 + 2 人簇：大的在前，不受"并列按编号"规则影响
        pts = [(0, 0), (0, 0.5), (0, 1.0), (10, 0), (10, 0.5)]
        feat = dbscan_crowd_features(pts, 1.0, 2)
        assert feat["cluster_sizes"] == [3, 2]


# ---------------------------------------------------------------------------
# C. graph_laplacian 脚点去重语义（2cm）
# ---------------------------------------------------------------------------
class TestFootPointDedup:
    """去重目的是消除"同一行人被检出两个几乎重合脚点"，不是做单链传递聚类。"""

    def test_kept_points_only_no_transitive_collapse(self):
        # 0 / 1.5cm / 3.0cm 三点：第 3 点距"已保留点"(0,0) 为 3cm > 2cm -> 保留
        feat = compute_laplacian_features([(0.0, 0.0), (0.015, 0.0), (0.030, 0.0)], 2.0)
        assert feat["n"] == 2

    def test_points_within_tolerance_collapse_to_one(self):
        feat = compute_laplacian_features(
            [(0.0, 0.0), (0.010, 0.0), (0.019, 0.0)], 2.0
        )
        assert feat["n"] == 1

    def test_tolerance_is_strictly_less_than_2cm(self):
        # 恰好 2.000cm：不满足 "d < 0.02" -> 保留两个点
        feat = compute_laplacian_features([(0.0, 0.0), (0.02, 0.0)], 2.0)
        assert feat["n"] == 2


# ---------------------------------------------------------------------------
# D. risk_fsm min_danger_n 门控（含保持路径的真实行为）
# ---------------------------------------------------------------------------
class TestMinDangerNGate:
    def test_two_people_never_escalate_to_danger(self):
        fsm = RiskFSM(RiskFSMConfig(confirm_up=1))
        # score = 0.4*(2/20)+0.3*1.0+0.3*1.0 = 0.64 < up_danger(0.65)，但 n=2 才是真正门控
        levels = [fsm.update({"n": 2, "density": 1.0, "r_gcc": 1.0}, None) for _ in range(6)]
        assert levels[-1] == "WARNING"
        # 提高 score 越过 up_danger：人数仍不足，禁止 DANGER
        levels = [
            fsm.update({"n": 2, "density": 1.0, "r_gcc": 1.0, "fiedler": 50.0}, None)
            for _ in range(6)
        ]
        assert levels[-1] == "WARNING"
        assert fsm.score > 0.65  # 确认确实是"评分够但人数不够"

    def test_three_people_can_escalate(self):
        fsm = RiskFSM(RiskFSMConfig(confirm_up=1))
        for _ in range(4):
            lv = fsm.update({"n": 3, "density": 1.0, "r_gcc": 1.0, "fiedler": 10.0}, None)
        assert lv == "DANGER"

    def test_hold_path_needs_hold_and_confirm_down(self):
        """钉住真实行为：人数跌破门槛后不是"立即退出"，仍受 hold + confirm_down 约束。

        历史注释（risk_fsm.h / risk_fsm.py）写的是"立即退出、不受 hold 保护"，
        与代码不符；本用例把代码的真实语义固定下来，注释已同步修正。
        配置：confirm_up=1, confirm_down=3, danger_hold_frames=5, danger_hold_ms=0。
        """
        cfg = RiskFSMConfig(
            confirm_up=1, confirm_down=3, danger_hold_frames=5, danger_hold_ms=0
        )
        fsm = RiskFSM(cfg)
        for _ in range(4):
            fsm.update({"n": 5, "density": 1.0, "r_gcc": 1.0, "fiedler": 10.0}, None)
        assert fsm.level == "DANGER"

        # n 掉到 1（< min_danger_n=3）：人数不足并不"立即退出"
        levels = [
            fsm.update({"n": 1, "density": 0.0, "r_gcc": 0.0}, None) for _ in range(9)
        ]
        # 进入 DANGER 在第 2 帧，hold=5 帧 -> 第 7 帧才可降；
        # 之后仍需 confirm_down=3 帧连续满足才真正降级。
        # DANGER 保持 4 帧（hold）-> 1 帧到达 confirm_down -> 再 3 帧确认降级
        assert levels == ["DANGER"] * 4 + ["WARNING"] * 3 + ["SAFE"] * 2


# ---------------------------------------------------------------------------
# E. ipm_calibrate 退化标定必须报错，不能静默返回废矩阵
# ---------------------------------------------------------------------------
class TestIpmCalibrationValidation:
    def test_collinear_pixel_points_rejected(self):
        # 像素点全在一条线上（地面上取 y 方向有差异，使地面点不共线）
        pixel = np.array([[0.0, 0.0], [100.0, 0.0], [200.0, 0.0], [300.0, 0.0]])
        ground = np.array([[0.0, 0.0], [1.0, 0.0], [2.0, 1.0], [3.0, 2.0]])
        with pytest.raises(ValueError):
            calibrate_from_points(pixel, ground)

    def test_collinear_ground_points_rejected(self):
        pixel = np.array([[0.0, 0.0], [100.0, 0.0], [0.0, 100.0], [100.0, 100.0]])
        ground = np.array([[0.0, 0.0], [1.0, 0.0], [2.0, 0.0], [3.0, 0.0]])
        with pytest.raises(ValueError):
            calibrate_from_points(pixel, ground)

    def test_valid_calibration_still_roundtrips(self):
        pixel = np.array([[100.0, 220.0], [300.0, 230.0], [320.0, 400.0], [80.0, 410.0]])
        ground = np.array([[0.0, 0.0], [5.0, 0.0], [5.0, 8.0], [0.0, 8.0]])
        H = calibrate_from_points(pixel, ground)
        back = pixel_to_ground(H, pixel)
        assert np.allclose(back, ground, atol=1e-3)
