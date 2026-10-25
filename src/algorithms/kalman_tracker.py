# -*- coding: utf-8 -*-
"""多目标离散 Kalman 滤波追踪器（常速度模型 CV）。

用于追踪 / 平滑行人在连续帧的地面坐标轨迹，解决单帧漏检与坐标抖动问题。

模型：
    状态       x = [px, py, vx, vy]          （地面坐标，单位：米；速度，单位：m/s）
    观测       z = [px, py]                   （YOLO 脚点经 IPM 逆透视映射得到的地面米坐标）
    运动模型    常速度（CV），固定帧间隔 dt

模块只依赖 numpy，可直接运行。包含两个类：

- ``KalmanFilter``：单目标离散 Kalman 滤波器（与 C++ 版 ``kalman_filter.h`` 接口一致）。
- ``KalmanTracker``：多目标追踪器，负责多目标维护、最近邻/ID 关联、漏检外推与过期轨迹清理。

用法示例::

    from src.algorithms.kalman_tracker import KalmanTracker

    tracker = KalmanTracker(dt=1/30.0, process_noise=1.5, measurement_noise=0.5)
    for frame_dets in detections_sequence:   # 每帧检测结果
        smoothed = tracker.update(frame_dets)
        # smoothed: {track_id: (x, y, vx, vy)}
"""

from __future__ import annotations

import numpy as np

__all__ = ["KalmanFilter", "KalmanTracker"]


class KalmanFilter:
    """单目标常速度(CV)离散 Kalman 滤波器。

    状态向量 [px, py, vx, vy]，观测 [px, py]，固定采样间隔 dt。

    参数
    ----
    dt : float
        两帧之间的固定时间间隔（秒）。默认 1/30（30 FPS）。
    process_noise : float
        过程噪声标准差，即随机加速度的标准差 sigma_a（单位 m/s^2）。
        越大表示越信任观测、越容易跟随机动；越小表示轨迹越平滑。
    measurement_noise : float
        观测噪声标准差 sigma_z（单位 m），对应 IPM 地面坐标的定位噪声量级。
        越大表示越不信任单帧检测、平滑越强。
    """

    def __init__(self, dt=1.0 / 30.0, process_noise=1.5, measurement_noise=0.5):
        self.dt = float(dt)
        if self.dt <= 0:
            raise ValueError("dt 必须为正数")
        self._sig_a = float(process_noise)
        self._sig_z = float(measurement_noise)
        self._initialized = False
        self._x = np.zeros(4, dtype=np.float64)
        self._P = np.zeros((4, 4), dtype=np.float64)
        self._build_matrices()

    # ------------------------------------------------------------------
    # 矩阵构造
    # ------------------------------------------------------------------
    def _build_matrices(self):
        dt = self.dt
        # 状态转移矩阵 F（常速度模型）
        self._F = np.array(
            [
                [1.0, 0.0, dt, 0.0],
                [0.0, 1.0, 0.0, dt],
                [0.0, 0.0, 1.0, 0.0],
                [0.0, 0.0, 0.0, 1.0],
            ],
            dtype=np.float64,
        )
        # 观测矩阵 H
        self._H = np.array(
            [
                [1.0, 0.0, 0.0, 0.0],
                [0.0, 1.0, 0.0, 0.0],
            ],
            dtype=np.float64,
        )
        # 过程噪声 Q = G * sigma_a^2 * G^T，G 为加速度扰动输入矩阵
        # G = [[0.5dt^2, 0], [0, 0.5dt^2], [dt, 0], [0, dt]]
        q = self._sig_a * self._sig_a
        self._Q = q * np.array(
            [
                [0.25 * dt**4, 0.0, 0.5 * dt**3, 0.0],
                [0.0, 0.25 * dt**4, 0.0, 0.5 * dt**3],
                [0.5 * dt**3, 0.0, dt**2, 0.0],
                [0.0, 0.5 * dt**3, 0.0, dt**2],
            ],
            dtype=np.float64,
        )
        # 观测噪声 R（两个方向独立同分布）
        r = self._sig_z * self._sig_z
        self._R = np.diag([r, r])

    # ------------------------------------------------------------------
    # 初始化
    # ------------------------------------------------------------------
    def init(self, x, y, vx=0.0, vy=0.0):
        """用首个观测初始化滤波器，速度缺省为 0。

        位置初始不确定度取 10 倍观测噪声，速度初始不确定度取 10 倍过程噪声。
        """
        self._x = np.array([x, y, vx, vy], dtype=np.float64)
        p_pos = (10.0 * self._sig_z) ** 2
        p_vel = (10.0 * self._sig_a * self.dt) ** 2
        self._P = np.diag([p_pos, p_pos, p_vel, p_vel])
        self._initialized = True
        return self.state

    # ------------------------------------------------------------------
    # 预测 / 更新
    # ------------------------------------------------------------------
    def predict(self):
        """执行一步状态预测（无观测时外推）。"""
        if not self._initialized:
            return self.state
        self._x = self._F @ self._x
        self._P = self._F @ self._P @ self._F.T + self._Q
        return self.state

    def correct(self, z):
        """用观测 z=[px,py] 修正状态（标准 Kalman 更新）。"""
        if not self._initialized:
            raise RuntimeError("滤波器尚未初始化，请先调用 init()")
        z = np.asarray(z, dtype=np.float64).reshape(2)
        # 新息协方差 S = H P H^T + R
        S = self._H @ self._P @ self._H.T + self._R
        # 卡尔曼增益 K = P H^T S^-1
        K = self._P @ self._H.T @ np.linalg.inv(S)
        # 新息
        y = z - self._H @ self._x
        # 状态与协方差更新
        self._x = self._x + K @ y
        I = np.eye(4)
        self._P = (I - K @ self._H) @ self._P
        return self.state

    def update(self, z):
        """便捷接口：一步预测 + 一步更新（带观测时使用）。"""
        self.predict()
        return self.correct(z)

    # ------------------------------------------------------------------
    # 只读访问
    # ------------------------------------------------------------------
    @property
    def state(self):
        """返回 [px, py, vx, vy]。"""
        return self._x.copy()

    @property
    def position(self):
        """返回 [px, py]。"""
        return self._x[:2].copy()

    @property
    def velocity(self):
        """返回 [vx, vy]。"""
        return self._x[2:].copy()

    @property
    def covariance(self):
        """返回 4x4 协方差矩阵 P。"""
        return self._P.copy()

    @property
    def is_initialized(self):
        return self._initialized

    @property
    def process_noise(self):
        return self._sig_a

    @property
    def measurement_noise(self):
        return self._sig_z


class _Track:
    """内部轨迹对象：一个滤波器 + 生命周期状态。"""

    __slots__ = ("id", "kf", "hits", "misses", "age", "last_seen")

    def __init__(self, tid, kf):
        self.id = tid
        self.kf = kf
        self.hits = 0     # 关联成功次数
        self.misses = 0   # 连续未关联帧数
        self.age = 0       # 自创建以来的帧数
        self.last_seen = 0  # 最近一次关联所在帧号


class KalmanTracker:
    """多目标 Kalman 追踪器。

    职责：
    1. 维护多条轨迹，每条轨迹持有一个 :class:`KalmanFilter`；
    2. 每帧先对所有轨迹执行 predict()（漏检时自然外推），再进行数据关联；
    3. 关联支持两种模式：
       - ``match_by_id=True``（默认）：检测自带稳定 ID（如检测框 ID），按 ID 直接匹配；
       - ``match_by_id=False`` 或检测未携带 ID：以预测位置为基准的最近邻匹配 + 距离门限；
    4. 超过 ``max_missed`` 帧未关联的轨迹被清除。

    参数
    ----
    dt : float
        固定帧间隔（秒），默认 1/30。
    process_noise : float
        过程噪声标准差（m/s^2），见 :class:`KalmanFilter`。
    measurement_noise : float
        观测噪声标准差（m），见 :class:`KalmanFilter`。
    gate_dist : float
        最近邻关联的最大门限距离（米）。超过该距离的检测不分配给该轨迹，
        会新建轨迹。默认 2.0 m。
    max_missed : int
        轨迹连续多少帧未关联即被删除（容忍漏检）。默认 5。
    min_hits : int
        轨迹至少连续关联多少帧才被当作“稳定轨迹”输出。
        低于该值仍会输出，但可用于下游过滤。默认 1。
    match_by_id : bool
        是否优先按检测 ID 关联。为 True 且检测带 ID 时按 ID 匹配，
        否则自动退化为最近邻匹配。默认 True。
    """

    def __init__(
        self,
        dt=1.0 / 30.0,
        process_noise=1.5,
        measurement_noise=0.5,
        gate_dist=2.0,
        max_missed=5,
        min_hits=1,
        match_by_id=True,
    ):
        self.dt = float(dt)
        self._sig_a = float(process_noise)
        self._sig_z = float(measurement_noise)
        self.gate_dist = float(gate_dist)
        self.max_missed = int(max_missed)
        self.min_hits = int(min_hits)
        self.match_by_id = bool(match_by_id)

        self._tracks = {}          # track_id -> _Track
        self._next_id = 0          # 自动分配 ID 的计数器
        self._frame = 0            # 当前帧号

    # ------------------------------------------------------------------
    # 主入口
    # ------------------------------------------------------------------
    def update(self, detections):
        """输入一帧检测结果，输出所有活跃轨迹的平滑状态。

        参数
        ----
        detections : 支持以下格式
            - dict: ``{track_id: (x, y)}``
            - list: ``[(x, y)]``、``[(x, y, track_id)]``
            - numpy 数组: shape ``(N, 2)`` 或 ``(N, 3)``

        返回
        ----
        dict : ``{track_id: (x, y, vx, vy)}``
            所有活跃轨迹的平滑地面坐标与速度（米 / 米每秒）。
        """
        self._frame += 1
        dets = self._normalize(detections)
        self._predict_all()
        if dets:
            if self._use_id_matching(dets):
                self._update_by_id(dets)
            else:
                self._update_by_nearest(dets)
        self._prune()
        return self.states()

    def predict(self):
        """无观测帧：对所有轨迹执行一步外推并返回状态。

        用于整帧漏检或两帧之间主动预测。等价于 ``update(None)``。
        """
        self._frame += 1
        self._predict_all()
        self._prune()
        return self.states()

    # ------------------------------------------------------------------
    # 输入规范化
    # ------------------------------------------------------------------
    @staticmethod
    def _normalize(detections):
        """统一转换为 [(x, y, id_or_None), ...]。"""
        if detections is None:
            return []
        if isinstance(detections, dict):
            out = []
            for tid, xy in detections.items():
                out.append((float(xy[0]), float(xy[1]), int(tid)))
            return out
        out = []
        for det in detections:
            det = tuple(float(v) for v in det)
            if len(det) == 3:
                out.append((det[0], det[1], int(det[2])))
            elif len(det) == 2:
                out.append((det[0], det[1], None))
            else:
                raise ValueError(
                    "每个检测必须是 (x, y) 或 (x, y, track_id)，收到: %r" % (det,)
                )
        return out

    def _use_id_matching(self, dets):
        return self.match_by_id and all(d[2] is not None for d in dets)

    # ------------------------------------------------------------------
    # 预测 / 关联 / 清理
    # ------------------------------------------------------------------
    def _predict_all(self):
        for trk in self._tracks.values():
            trk.kf.predict()
            trk.age += 1
            trk.misses += 1

    def _update_by_id(self, dets):
        """按检测 ID 直接匹配。"""
        for x, y, tid in dets:
            trk = self._tracks.get(tid)
            if trk is not None:
                trk.kf.correct((x, y))
                trk.hits += 1
                trk.misses = 0
                trk.last_seen = self._frame
            else:
                self._create_track(x, y, tid)

    def _update_by_nearest(self, dets):
        """最近邻 + 距离门限的数据关联。

        构造“检测 x 轨迹”的距离代价矩阵，贪心从小到大分配，
        超过 ``gate_dist`` 的配对被拒绝。
        """
        n_det = len(dets)
        trk_ids = list(self._tracks.keys())
        n_trk = len(trk_ids)
        if n_trk == 0:
            for x, y, _ in dets:
                self._create_track(x, y)
            return

        # 代价矩阵：检测到各轨迹预测位置的距离（米）
        costs = np.full((n_det, n_trk), np.inf, dtype=np.float64)
        for i, (x, y, _) in enumerate(dets):
            for j, tid in enumerate(trk_ids):
                px, py = self._tracks[tid].kf.position
                costs[i, j] = np.hypot(x - px, y - py)

        assigned_det = set()
        assigned_trk = set()
        flat_order = np.argsort(costs, axis=None)
        for flat in flat_order:
            i, j = np.unravel_index(flat, costs.shape)
            if i in assigned_det or j in assigned_trk:
                continue
            if costs[i, j] > self.gate_dist:
                break  # 升序排列，剩余配对必然更远
            tid = trk_ids[j]
            trk = self._tracks[tid]
            trk.kf.correct((dets[i][0], dets[i][1]))
            trk.hits += 1
            trk.misses = 0
            trk.last_seen = self._frame
            assigned_det.add(i)
            assigned_trk.add(j)

        # 未匹配的检测新建轨迹
        for i in range(n_det):
            if i not in assigned_det:
                self._create_track(dets[i][0], dets[i][1])

    def _create_track(self, x, y, tid=None):
        if tid is None:
            tid = self._next_id
            self._next_id += 1
        kf = KalmanFilter(
            dt=self.dt,
            process_noise=self._sig_a,
            measurement_noise=self._sig_z,
        )
        kf.init(x, y)
        trk = _Track(tid, kf)
        trk.hits = 1
        trk.misses = 0
        trk.last_seen = self._frame
        self._tracks[tid] = trk
        return tid

    def _prune(self):
        stale = [
            tid
            for tid, trk in self._tracks.items()
            if trk.misses > self.max_missed
        ]
        for tid in stale:
            del self._tracks[tid]

    # ------------------------------------------------------------------
    # 输出
    # ------------------------------------------------------------------
    def states(self):
        """返回 ``{track_id: (x, y, vx, vy)}``。"""
        return {tid: tuple(trk.kf.state) for tid, trk in self._tracks.items()}

    def positions(self):
        """返回 ``{track_id: (x, y)}``。"""
        return {tid: tuple(trk.kf.position) for tid, trk in self._tracks.items()}

    def velocities(self):
        """返回 ``{track_id: (vx, vy)}``。"""
        return {tid: tuple(trk.kf.velocity) for tid, trk in self._tracks.items()}

    def track_info(self):
        """返回每条轨迹的生命周期信息（便于可视化/调试）。"""
        info = {}
        for tid, trk in self._tracks.items():
            info[tid] = {
                "position": tuple(trk.kf.position),
                "velocity": tuple(trk.kf.velocity),
                "hits": trk.hits,
                "misses": trk.misses,
                "age": trk.age,
                "last_seen": trk.last_seen,
                "stable": trk.hits >= self.min_hits,
            }
        return info

    def track_count(self):
        return len(self._tracks)

    def reset(self):
        """清空所有轨迹与计数。"""
        self._tracks.clear()
        self._next_id = 0
        self._frame = 0