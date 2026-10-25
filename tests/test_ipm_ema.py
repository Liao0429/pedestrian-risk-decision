# -*- coding: utf-8 -*-
"""IPM 标定工具 + EMA 时序平滑器 单元测试。

测试数据均为**构造的合成数据**（非真实传感器数据）：
  1. IPM 标定：用一个人为给定的透视单应矩阵 H_gt 生成地面矩形四角对应的
     像素坐标（再取整模拟像素量化），验证 calibrate_from_points 求得的 H
     能把像素点映射回给定地面点（误差 < 1e-3 米）。
  2. EMA：对"真值 + 高斯白噪声"的合成特征序列做指数滑动平均，验证平滑后
     方差显著降低；并覆盖多变量、时间感知模式与 reset 行为。

运行方式（在项目根目录）：
    python -m pytest tests/test_ipm_ema.py -v
    或
    python tests/test_ipm_ema.py
"""

import os
import sys

import numpy as np

# 保证可从项目根目录导入 src.algorithms.*
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from src.algorithms.ema_smoother import EMASmoother
from src.algorithms.ipm_calibrate import (
    calibrate_from_points,
    calibrate_from_points_ransac,
    ground_to_pixel,
    load_homography,
    pixel_to_ground,
    save_homography,
)

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src"))


# ----------------------------------------------------------------------
# 合成数据工具
# ----------------------------------------------------------------------
def make_synthetic_correspondences(seed=0):
    """生成一组合成的 (像素点, 地面点) 标定对。

    地面取一个 5m x 8m 矩形四角（米制坐标），再用一个带透视的单应 H_gt
    投影到像素平面并取整（模拟像素量化），得到 4 组对应点。

    返回 (pixel_pts, ground_pts, H_gt)。
    """
    rng = np.random.default_rng(seed)
    ground_pts = np.array(
        [[0.0, 0.0], [5.0, 0.0], [5.0, 8.0], [0.0, 8.0]], dtype=np.float64
    )
    # 人为给定一个"真实"透视单应（含非零第三行 -> 透视形变）
    H_gt = np.array(
        [
            [3.0 + 0.1 * rng.standard_normal(), 0.5, 100.0],
            [0.2, 4.0, 200.0],
            [0.001, 0.002, 1.0],
        ],
        dtype=np.float64,
    )
    # 像素 = H_gt * [x, y, 1]^T，齐次归一化后取整
    homo = np.hstack([ground_pts, np.ones((4, 1))])
    proj = (H_gt @ homo.T).T
    pixel = proj[:, :2] / proj[:, 2:3]
    pixel = np.round(pixel).astype(np.float64)  # 模拟整数像素
    return pixel, ground_pts, H_gt


def make_noisy_series(true=1.0, noise_std=0.3, n=200, seed=42):
    """生成 "真值 + 高斯白噪声" 的一维合成序列。"""
    rng = np.random.default_rng(seed)
    raw = true + rng.normal(0.0, noise_std, n)
    return raw


# ----------------------------------------------------------------------
# IPM 标定
# ----------------------------------------------------------------------
class TestIpmCalibrate:
    def test_calibrate_maps_back_within_1e_3(self):
        """核心验收：标定 H 把像素点映射回给定地面点，误差 < 1e-3 米。"""
        pixel, ground, _ = make_synthetic_correspondences(seed=0)
        H = calibrate_from_points(pixel, ground)
        back = pixel_to_ground(H, pixel)
        err = np.linalg.norm(back.astype(np.float64) - ground, axis=1)
        assert err.max() < 1e-3, f"最大映射误差 {err.max():.3e} 米，超出 1e-3"
        # 打印验收指标
        print(f"\n[IPM] 4 点映射最大误差 = {err.max():.3e} m")

    def test_h_is_3x3_float(self):
        pixel, ground, _ = make_synthetic_correspondences(seed=1)
        H = calibrate_from_points(pixel, ground)
        assert H.shape == (3, 3)
        assert H.dtype == np.float64

    def test_ground_to_pixel_round_trip(self):
        """地面 -> 像素 -> 地面 往返误差应 < 1e-3 米。"""
        pixel, ground, _ = make_synthetic_correspondences(seed=2)
        H = calibrate_from_points(pixel, ground)
        px_again = ground_to_pixel(H, ground)
        ground_again = pixel_to_ground(H, px_again)
        err = np.linalg.norm(ground_again.astype(np.float64) - ground, axis=1)
        assert err.max() < 1e-3, f"往返最大误差 {err.max():.3e} m"

    def test_inverse_matches_direct_ground(self):
        """ground_to_pixel 结果应与生成时的像素一致（<=1 像素量化）。"""
        pixel, ground, _ = make_synthetic_correspondences(seed=3)
        H = calibrate_from_points(pixel, ground)
        px = ground_to_pixel(H, ground)
        err_px = np.linalg.norm(px.astype(np.float64) - pixel, axis=1)
        assert err_px.max() <= 1.0, f"像素往返误差 {err_px.max():.3f} px"

    def test_ransac_with_extra_points(self):
        """RANSAC 支持 >=4 点：加入一个带小噪声的点仍能稳定求解。"""
        pixel, ground, _ = make_synthetic_correspondences(seed=4)
        # 增加一个额外地面点（地面坐标系内，非共线）
        extra_ground = np.array([[2.5, 4.0]])
        # 用已标定 H 反推其像素坐标（模拟现场多标一个点）
        H0 = calibrate_from_points(pixel, ground)
        extra_pixel = ground_to_pixel(H0, extra_ground)
        pixel_all = np.vstack([pixel, extra_pixel]).astype(np.float64)
        ground_all = np.vstack([ground, extra_ground])

        H = calibrate_from_points_ransac(pixel_all, ground_all)
        back = pixel_to_ground(H, pixel)
        err = np.linalg.norm(back.astype(np.float64) - ground, axis=1)
        assert err.max() < 1e-3, f"RANSAC 标定最大误差 {err.max():.3e} m"

    def test_requires_exactly_4_points(self):
        pixel, ground, _ = make_synthetic_correspondences(seed=5)
        with_pixel = np.vstack([pixel, [50.0, 60.0]])
        with_ground = np.vstack([ground, [2.0, 3.0]])
        try:
            calibrate_from_points(with_pixel, with_ground)
        except ValueError:
            return
        raise AssertionError(">4 点输入应抛出 ValueError（getPerspectiveTransform 需恰 4 点）")

    def test_rejects_collinear_ground(self):
        """共线地面点应被拒绝，避免退化。"""
        pixel = np.array([[100, 200], [200, 200], [300, 200], [400, 200]])
        ground = np.array([[0, 0], [1, 0], [2, 0], [3, 0]])  # 全部共线
        try:
            calibrate_from_points(pixel, ground)
        except ValueError:
            return
        raise AssertionError("共线地面点应抛出 ValueError")

    def test_save_load_npy(self, tmp_path=None):
        """保存 / 加载 .npy 后 H 应一致。"""
        import tempfile

        pixel, ground, _ = make_synthetic_correspondences(seed=6)
        H = calibrate_from_points(pixel, ground)
        d = tmp_path if tmp_path is not None else tempfile.mkdtemp()
        path = os.path.join(str(d), "H.npy")
        save_homography(H, path)
        H2 = load_homography(path)
        np.testing.assert_allclose(H2, H, atol=1e-12)
        os.remove(path)

    def test_save_load_txt(self):
        """保存 / 加载 .txt 后 H 应一致（文本精度 %.12g）。"""
        import tempfile

        pixel, ground, _ = make_synthetic_correspondences(seed=7)
        H = calibrate_from_points(pixel, ground)
        d = tempfile.mkdtemp()
        path = os.path.join(d, "H.txt")
        save_homography(H, path)
        H2 = load_homography(path)
        np.testing.assert_allclose(H2, H, atol=1e-9)
        os.remove(path)

    def test_cli(self):
        """命令行入口应能运行并输出 H。"""
        from src.algorithms.ipm_calibrate import main

        rc = main(
            [
                "--pixel", "100,220 300,230 320,400 80,410",
                "--ground", "0,0 5,0 5,8 0,8",
                "--print-only",
            ]
        )
        assert rc == 0


# ----------------------------------------------------------------------
# EMA 平滑器
# ----------------------------------------------------------------------
class TestEma:
    def test_first_update_returns_input(self):
        """首帧应原样返回（无历史）。"""
        sm = EMASmoother(alpha=0.3)
        assert sm.update(0.6) == 0.6
        assert sm.initialized

    def test_scalar_recurrence(self):
        """s_t = alpha*x + (1-alpha)*s_{t-1}。"""
        sm = EMASmoother(alpha=0.3)
        sm.update(0.6)          # 首帧 -> 0.6
        y = sm.update(0.9)      # 0.3*0.9 + 0.7*0.6 = 0.69
        assert abs(y - 0.69) < 1e-12

    def test_smoothing_reduces_variance(self):
        """核心验收：EMA 平滑后噪声方差应显著降低（< 原始 50%）。"""
        raw = make_noisy_series(true=1.0, noise_std=0.3, n=300, seed=42)
        sm = EMASmoother(alpha=0.3)
        smoothed = np.array([sm.update(x) for x in raw])

        warm = 50  # 跳过收敛期
        var_raw = float(np.var(raw[warm:]))
        var_sm = float(np.var(smoothed[warm:]))
        print(f"\n[EMA] 原始方差={var_raw:.5f}, 平滑后方差={var_sm:.5f}")
        # 理论抑制比 alpha/(2-alpha)=0.3/1.7≈0.176，放宽到 0.5 仍可显著下降
        assert var_sm < var_raw * 0.5, (
            f"平滑未达预期: var_raw={var_raw:.5f}, var_sm={var_sm:.5f}"
        )

    def test_smoothing_tracks_true_value(self):
        """平滑后的均值应接近真值（无偏性）。"""
        raw = make_noisy_series(true=1.0, noise_std=0.3, n=300, seed=7)
        sm = EMASmoother(alpha=0.3)
        smoothed = np.array([sm.update(x) for x in raw])
        assert abs(smoothed[100:].mean() - 1.0) < 0.05

    def test_multivariate_independent(self):
        """多变量：各分量独立平滑，首帧向量原样返回。"""
        sm = EMASmoother(alpha=0.3, dim=3)
        v0 = sm.update([0.5, 0.6, 0.4])
        np.testing.assert_allclose(v0, [0.5, 0.6, 0.4], atol=1e-12)
        v1 = sm.update([0.9, 0.2, 0.8])
        expect = 0.3 * np.array([0.9, 0.2, 0.8]) + 0.7 * np.array([0.5, 0.6, 0.4])
        np.testing.assert_allclose(v1, expect, atol=1e-12)
        assert v1.shape == (3,)

    def test_dim_inferred_from_first_call(self):
        """dim=None 时由首次输入自动推断。"""
        sm = EMASmoother(alpha=0.3)
        sm.update([0.1, 0.2, 0.3, 0.4])
        assert sm.dimension == 4
        assert sm.update([0.5, 0.6, 0.7, 0.8]).shape == (4,)

    def test_dim_mismatch_raises(self):
        sm = EMASmoother(alpha=0.3, dim=2)
        sm.update([0.1, 0.2])
        try:
            sm.update([0.1, 0.2, 0.3])
        except ValueError:
            return
        raise AssertionError("维度不一致应抛出 ValueError")

    def test_per_dim_alpha(self):
        """每维独立 alpha：第一维 alpha=0.9（快跟随），第二维 alpha=0.1（慢）。"""
        sm = EMASmoother(alpha=[0.9, 0.1], dim=2)
        sm.update([1.0, 1.0])
        y = sm.update([0.0, 0.0])
        # 第一维接近 0（0.9 快速衰减），第二维接近 0.9（慢衰减）
        assert abs(y[0] - 0.1) < 1e-9
        assert abs(y[1] - 0.9) < 1e-9

    def test_timestamp_mode(self):
        """时间感知模式：固定 dt 的 alpha_t 应接近固定 alpha 的效果。"""
        dt = 1.0 / 30.0
        tau = -dt / np.log(1 - 0.3)   # 使 alpha_t≈0.3
        sm = EMASmoother(alpha=0.3, use_timestamp=True, tau=float(tau))
        raw = make_noisy_series(true=1.0, noise_std=0.3, n=300, seed=99)
        smoothed = []
        for i, x in enumerate(raw):
            smoothed.append(sm.update(x, t=i * dt))
        smoothed = np.array(smoothed)
        warm = 50
        var_sm = float(np.var(smoothed[warm:]))
        var_raw = float(np.var(raw[warm:]))
        assert var_sm < var_raw * 0.5, (
            f"时间感知模式方差未显著下降: {var_sm:.5f} vs {var_raw:.5f}"
        )

    def test_reset(self):
        sm = EMASmoother(alpha=0.3)
        sm.update(0.6)
        assert sm.initialized
        sm.reset()
        assert not sm.initialized
        assert sm.update(0.9) == 0.9  # 重置后首帧原样

    def test_alpha_validation(self):
        for bad in (0.0, 1.5, -0.1):
            try:
                EMASmoother(alpha=bad)
            except ValueError:
                continue
            raise AssertionError(f"非法 alpha={bad} 应抛出 ValueError")

    def test_cpp_header_defaults_consistent(self):
        """C++ 头文件默认值与 Python 默认值保持一致。"""
        hdr = os.path.join(SRC, "algorithms", "ema_smoother.h")
        with open(hdr, "r", encoding="utf-8") as f:
            text = f.read()
        # Python 默认 alpha=0.3, dim=1, use_timestamp=False, tau=1.0
        for literal in ("alpha = 0.3", "dim = 1", "use_timestamp = false",
                        "tau = 1.0"):
            assert literal in text, f"头文件缺少默认值: {literal}"


# ----------------------------------------------------------------------
# 运行器（不依赖 pytest）
# ----------------------------------------------------------------------
def main():
    tests = [
        (name, fn) for name, fn in sorted(globals().items())
        if name.startswith("test_") and callable(fn)
    ]
    # 收集类中的测试方法
    for cls in (TestIpmCalibrate, TestEma):
        inst = cls()
        for name in dir(cls):
            if name.startswith("test_"):
                tests.append((f"{cls.__name__}.{name}", getattr(inst, name)))

    failures = 0
    total = 0
    for name, fn in tests:
        total += 1
        try:
            fn()
            print(f"[PASS] {name}")
        except Exception as exc:
            failures += 1
            print(f"[FAIL] {name}: {exc}")
            import traceback
            traceback.print_exc()
    print(f"\n{total} 项测试完成，失败 {failures} 项")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()