# -*- coding: utf-8 -*-
"""KalmanTracker / KalmanFilter 单元测试。

测试使用**合成的带噪声匀速运动轨迹**（纯测试数据，非真实传感器数据）：
在地面坐标系（米）中生成匀速直线轨迹，叠加高斯观测噪声，
验证离散 Kalman 滤波能显著抑制单帧抖动，并正确处理多目标、漏检与 ID 关联。

运行方式（在项目根目录）：
    python -m pytest tests/test_kalman_tracker.py -v
    或
    python tests/test_kalman_tracker.py
"""

import os
import sys

import numpy as np

# 保证可从项目根目录导入 src.algorithms.*
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from src.algorithms.kalman_tracker import KalmanFilter, KalmanTracker


# ----------------------------------------------------------------------
# 工具函数：合成测试数据
# ----------------------------------------------------------------------
def make_noisy_track(
    start=(0.0, 0.0),
    velocity=(1.5, 0.5),
    n_frames=50,
    dt=1.0 / 30.0,
    noise_std=0.5,
    seed=0,
):
    """生成一条匀速直线轨迹并叠加高斯观测噪声。

    返回 (true_positions, noisy_observations)，形状均为 (n, 2)（米）。
    """
    rng = np.random.default_rng(seed)
    true_pos = np.zeros((n_frames, 2))
    true_pos[0] = start
    for k in range(1, n_frames):
        true_pos[k] = true_pos[k - 1] + np.asarray(velocity) * dt
    noisy = true_pos + rng.normal(0.0, noise_std, size=true_pos.shape)
    return true_pos, noisy


def rmse(a, b):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    return float(np.sqrt(np.mean((a - b) ** 2)))


# ----------------------------------------------------------------------
# 单目标滤波器基础行为
# ----------------------------------------------------------------------
class TestKalmanFilter:
    def test_init_and_state(self):
        kf = KalmanFilter(dt=1.0 / 30.0, process_noise=1.5, measurement_noise=0.5)
        assert not kf.is_initialized
        kf.init(1.0, 2.0)
        assert kf.is_initialized
        np.testing.assert_allclose(kf.position, [1.0, 2.0], atol=1e-12)
        np.testing.assert_allclose(kf.velocity, [0.0, 0.0], atol=1e-12)

    def test_predict_constant_velocity(self):
        """无观测时按常速度模型外推。"""
        kf = KalmanFilter(dt=0.1, process_noise=1e-6, measurement_noise=0.5)
        kf.init(0.0, 0.0, vx=2.0, vy=1.0)
        kf.predict()
        np.testing.assert_allclose(kf.state, [0.2, 0.1, 2.0, 1.0], atol=1e-6)

    def test_correct_pulls_toward_measurement(self):
        """有观测时状态应被拉向观测。"""
        kf = KalmanFilter(dt=0.1, process_noise=1e-6, measurement_noise=0.5)
        kf.init(0.0, 0.0)
        # 观测明显偏离当前状态，正确后状态应向观测靠近
        before = kf.state.copy()
        kf.correct((5.0, 5.0))
        after = kf.state
        dist_before = np.hypot(5.0 - before[0], 5.0 - before[1])
        dist_after = np.hypot(5.0 - after[0], 5.0 - after[1])
        assert dist_after < dist_before

    def test_correct_requires_init(self):
        kf = KalmanFilter()
        try:
            kf.correct((0.0, 0.0))
        except RuntimeError:
            return
        raise AssertionError("未初始化时 correct() 应抛出 RuntimeError")

    def test_covariance_symmetric_positive(self):
        kf = KalmanFilter(dt=0.1, process_noise=1.0, measurement_noise=0.5)
        kf.init(0.0, 0.0)
        for _ in range(10):
            kf.predict()
            kf.correct((np.random.uniform(-1, 1), np.random.uniform(-1, 1)))
        P = kf.covariance
        np.testing.assert_allclose(P, P.T, atol=1e-9)
        assert np.all(np.linalg.eigvalsh(P) >= -1e-9)


# ----------------------------------------------------------------------
# 单目标追踪：平滑效果验证（核心验收点）
# ----------------------------------------------------------------------
class TestSingleTargetSmoothing:
    def test_smoothing_reduces_noise(self):
        """匀速轨迹 + 高斯噪声，平滑后位置误差应显著小于观测噪声标准差。"""
        dt = 1.0 / 30.0
        noise_std = 0.5  # 观测噪声标准差（米）
        true_pos, noisy = make_noisy_track(
            start=(0.0, 0.0),
            velocity=(1.5, 0.5),
            n_frames=80,
            dt=dt,
            noise_std=noise_std,
            seed=42,
        )

        tracker = KalmanTracker(
            dt=dt,
            process_noise=1.5,
            measurement_noise=noise_std,
            gate_dist=2.0,
            max_missed=5,
            match_by_id=True,
        )
        smoothed = []
        for k in range(len(noisy)):
            states = tracker.update([(noisy[k, 0], noisy[k, 1], 0)])  # 固定 ID=0
            smoothed.append(states[0][:2])
        smoothed = np.asarray(smoothed)

        # 前几帧收敛期误差较大，跳过前 5 帧再统计
        obs_rmse = rmse(noisy[5:], true_pos[5:])
        sm_rmse = rmse(smoothed[5:], true_pos[5:])
        print(f"\n[单目标] 观测 RMSE={obs_rmse:.4f} m, 平滑后 RMSE={sm_rmse:.4f} m")

        # 平滑误差必须显著小于观测噪声（要求至少下降 50%）
        assert sm_rmse < obs_rmse * 0.5, (
            f"平滑未达到预期效果: obs_rmse={obs_rmse:.4f}, sm_rmse={sm_rmse:.4f}"
        )
        # 且绝对误差应明显小于噪声标准差
        assert sm_rmse < noise_std * 0.5

    def test_velocity_estimation(self):
        """常速度目标的速度估计应接近真实速度。"""
        dt = 1.0 / 30.0
        true_vel = np.array([1.5, 0.5])
        _, noisy = make_noisy_track(
            velocity=true_vel,
            n_frames=80,
            dt=dt,
            noise_std=0.4,
            seed=7,
        )
        tracker = KalmanTracker(dt=dt, process_noise=1.5, measurement_noise=0.4)
        vel_est = []
        for k in range(len(noisy)):
            states = tracker.update([(noisy[k, 0], noisy[k, 1], 0)])
            vel_est.append(states[0][2:])
        vel_est = np.asarray(vel_est)
        # 收敛后平均速度估计
        mean_vel = vel_est[20:].mean(axis=0)
        print(f"\n[速度] 真实={true_vel}, 估计={mean_vel}")
        np.testing.assert_allclose(mean_vel, true_vel, atol=0.3)


# ----------------------------------------------------------------------
# 多目标与漏检
# ----------------------------------------------------------------------
class TestMultiTarget:
    def test_two_targets_nearest_neighbor(self):
        """两条交叉轨迹，无 ID 时通过最近邻正确区分。"""
        dt = 1.0 / 30.0
        # 目标 A 向右，目标 B 向左，在地面坐标中交叉
        true_A, obs_A = make_noisy_track((0.0, 0.0), (1.2, 0.2), 40, dt, 0.3, seed=1)
        true_B, obs_B = make_noisy_track((4.0, 1.0), (-1.0, 0.3), 40, dt, 0.3, seed=2)

        tracker = KalmanTracker(
            dt=dt,
            process_noise=1.5,
            measurement_noise=0.3,
            gate_dist=1.0,
            max_missed=3,
            match_by_id=False,  # 强制最近邻匹配
        )
        track_counts = []
        for k in range(40):
            dets = [
                (obs_A[k, 0], obs_A[k, 1]),
                (obs_B[k, 0], obs_B[k, 1]),
            ]
            states = tracker.update(dets)
            track_counts.append(len(states))

        # 大多数帧应保持 2 条轨迹（交叉瞬间可能短暂合并/分裂，但总体应稳定）
        two_track_frames = sum(1 for c in track_counts if c == 2)
        print(f"\n[多目标] 保持 2 条轨迹的帧数: {two_track_frames}/40")
        assert two_track_frames >= 30, f"多目标区分失败: {two_track_frames}/40"

    def test_missing_detection_extrapolation(self):
        """单帧漏检：轨迹不应中断，位置由预测外推补齐。"""
        dt = 1.0 / 30.0
        true_pos, noisy = make_noisy_track(
            velocity=(1.0, 0.0), n_frames=30, dt=dt, noise_std=0.3, seed=3
        )
        tracker = KalmanTracker(dt=dt, process_noise=1.5, measurement_noise=0.3)
        outputs = []
        for k in range(30):
            if k == 15:  # 第 15 帧整帧漏检
                states = tracker.predict()
            else:
                states = tracker.update([(noisy[k, 0], noisy[k, 1], 0)])
            outputs.append(states)

        # 漏检帧(15)仍应输出 1 条轨迹
        assert 0 in outputs[15], "漏检帧轨迹丢失"
        # 漏检帧的外推位置应接近真实位置（匀速外推误差应小于观测噪声量级）
        extrap_err = np.hypot(
            outputs[15][0][0] - true_pos[15, 0],
            outputs[15][0][1] - true_pos[15, 1],
        )
        print(f"\n[漏检] 第15帧外推位置误差: {extrap_err:.4f} m")
        assert extrap_err < 0.8, f"外推误差过大: {extrap_err:.4f}"

    def test_stale_track_pruned(self):
        """目标消失后，轨迹在超过 max_missed 帧后被清除。"""
        dt = 1.0 / 30.0
        _, noisy = make_noisy_track(n_frames=10, dt=dt, noise_std=0.3, seed=4)
        tracker = KalmanTracker(
            dt=dt, process_noise=1.5, measurement_noise=0.3, max_missed=3
        )
        for k in range(10):
            tracker.update([(noisy[k, 0], noisy[k, 1], 0)])
        assert tracker.track_count() == 1
        # 之后 4 帧无检测（max_missed=3），第 4 帧应被清除
        for _ in range(4):
            tracker.update([])
        assert tracker.track_count() == 0

    def test_id_matching_preserves_identity(self):
        """检测带 ID 时，轨迹 ID 应保持稳定且状态连续。"""
        dt = 1.0 / 30.0
        _, noisy = make_noisy_track(n_frames=20, dt=dt, noise_std=0.3, seed=5)
        tracker = KalmanTracker(dt=dt, process_noise=1.5, measurement_noise=0.3)
        for k in range(20):
            states = tracker.update([(noisy[k, 0], noisy[k, 1], 7)])  # ID=7
            assert set(states.keys()) == {7}, f"第{k}帧轨迹 ID 应为 7: {states.keys()}"


# ----------------------------------------------------------------------
# 参数校验与接口
# ----------------------------------------------------------------------
class TestApi:
    def test_invalid_dt(self):
        try:
            KalmanFilter(dt=-1)
        except ValueError:
            return
        raise AssertionError("dt<=0 应抛出 ValueError")

    def test_input_formats(self):
        """支持 dict / list / ndarray 三种输入格式。"""
        tracker = KalmanTracker(dt=0.1, measurement_noise=0.3)
        obs = [(1.0, 2.0, 0)]
        s1 = tracker.update(obs)
        s2 = tracker.update({0: (1.1, 2.1)})
        s3 = tracker.update(np.array([[1.2, 2.2]]))
        assert set(s1.keys()) == set(s2.keys()) == set(s3.keys()) == {0}

    def test_reset(self):
        tracker = KalmanTracker()
        tracker.update([(0.0, 0.0, 0)])
        assert tracker.track_count() == 1
        tracker.reset()
        assert tracker.track_count() == 0

    def test_track_info(self):
        tracker = KalmanTracker()
        tracker.update([(0.0, 0.0, 0)])
        info = tracker.track_info()
        assert 0 in info
        assert set(info[0].keys()) >= {"position", "velocity", "hits", "misses", "age", "stable"}


# ----------------------------------------------------------------------
# 独立运行入口
# ----------------------------------------------------------------------
if __name__ == "__main__":
    # 以非 pytest 方式运行时，逐项打印关键验收指标
    import traceback

    classes = [
        TestKalmanFilter,
        TestSingleTargetSmoothing,
        TestMultiTarget,
        TestApi,
    ]
    failures = 0
    total = 0
    for cls in classes:
        inst = cls()
        for name in dir(cls):
            if name.startswith("test_"):
                total += 1
                fn = getattr(inst, name)
                try:
                    fn()
                    print(f"[PASS] {cls.__name__}.{name}")
                except Exception:
                    failures += 1
                    print(f"[FAIL] {cls.__name__}.{name}")
                    traceback.print_exc()
    print(f"\n{total} 项测试完成，失败 {failures} 项")
    sys.exit(1 if failures else 0)