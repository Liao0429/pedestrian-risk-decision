# -*- coding: utf-8 -*-
"""ByteTrack 多目标跟踪器（低分框二次关联，边缘端轻量化）。

用于在连续帧中维持行人的检测框轨迹，解决遮挡 / 漏检导致的目标 ID 漂移问题。
原理（ByteTrack, ECCV 2022）：

    1. 每个目标一个线性匀速 Kalman 滤波器（状态 [cx, cy, s, r, vcx, vcy, vs, vr]），
       对检测框做预测，得到预测框；
    2. 检测框与预测框之间按 IoU 代价（1 - IoU）做线性分配（匈牙利算法）关联；
    3. **低分框二次关联**：第一轮用高分检测（conf >= track_thresh）匹配全部轨迹，
       剩下的未匹配轨迹再与低分检测（det_thresh <= conf < track_thresh）匹配，
       从而在遮挡 / 目标部分可见时仍能维持轨迹，避免 ID 漂移与轨迹断裂；
    4. 未匹配的高分检测新建轨迹；超过 ``max_lost`` 帧未关联的轨迹被删除。

模块只依赖 numpy（不含 scipy），匈牙利算法为纯 numpy 实现，可直接运行。

用法示例::

    from src.algorithms.bytetrack_tracker import ByteTrack

    tracker = ByteTrack(track_thresh=0.5, det_thresh=0.1, match_thresh=0.8,
                        max_lost=30, min_hits=3)
    for frame_dets in detections_sequence:      # 每帧检测结果
        tracks = tracker.update(frame_dets)     # list[[id, x1, y1, x2, y2, conf], ...]
"""

from __future__ import annotations

import numpy as np

__all__ = [
    "ByteTrack",
    "iou",
    "compute_iou_matrix",
    "linear_sum_assignment",
    "associate_detections_to_tracks",
]


# ----------------------------------------------------------------------
# 几何工具：IoU
# ----------------------------------------------------------------------
def iou(bbox_a, bbox_b):
    """计算两个 xyxy 框的交并比 IoU。

    参数
    ----
    bbox_a, bbox_b : 序列，长度为 4，格式 ``(x1, y1, x2, y2)``。

    返回
    ----
    float : IoU ∈ [0, 1]。框退化（面积<=0）时返回 0。
    """
    x1 = max(float(bbox_a[0]), float(bbox_b[0]))
    y1 = max(float(bbox_a[1]), float(bbox_b[1]))
    x2 = min(float(bbox_a[2]), float(bbox_b[2]))
    y2 = min(float(bbox_a[3]), float(bbox_b[3]))
    inter_w = max(0.0, x2 - x1)
    inter_h = max(0.0, y2 - y1)
    inter = inter_w * inter_h
    area_a = max(0.0, float(bbox_a[2]) - float(bbox_a[0])) * max(
        0.0, float(bbox_a[3]) - float(bbox_a[1])
    )
    area_b = max(0.0, float(bbox_b[2]) - float(bbox_b[0])) * max(
        0.0, float(bbox_b[3]) - float(bbox_b[1])
    )
    union = area_a + area_b - inter
    if union <= 0.0:
        return 0.0
    return inter / union


def compute_iou_matrix(boxes_a, boxes_b):
    """批量计算两组成对 IoU 矩阵。

    参数
    ----
    boxes_a : ndarray，shape (N, 4)，行格式 ``(x1, y1, x2, y2)``。
    boxes_b : ndarray，shape (M, 4)。

    返回
    ----
    ndarray : shape (N, M)，``out[i, j] = IoU(boxes_a[i], boxes_b[j])``。
    """
    boxes_a = np.asarray(boxes_a, dtype=np.float64)
    boxes_b = np.asarray(boxes_b, dtype=np.float64)

    xx1 = np.maximum(boxes_a[:, None, 0], boxes_b[None, :, 0])
    yy1 = np.maximum(boxes_a[:, None, 1], boxes_b[None, :, 1])
    xx2 = np.minimum(boxes_a[:, None, 2], boxes_b[None, :, 2])
    yy2 = np.minimum(boxes_a[:, None, 3], boxes_b[None, :, 3])

    inter = np.maximum(0.0, xx2 - xx1) * np.maximum(0.0, yy2 - yy1)
    area_a = (boxes_a[:, 2] - boxes_a[:, 0]) * (boxes_a[:, 3] - boxes_a[:, 1])
    area_b = (boxes_b[:, 2] - boxes_b[:, 0]) * (boxes_b[:, 3] - boxes_b[:, 1])
    union = area_a[:, None] + area_b[None, :] - inter
    union = np.maximum(union, 1e-12)
    return inter / union


# ----------------------------------------------------------------------
# 匈牙利算法（最小代价完美匹配，纯 numpy）
# ----------------------------------------------------------------------
def linear_sum_assignment(cost):
    """求解方阵最小代价完全匹配（匈牙利算法 / 交替路径增广）。

    参数
    ----
    cost : ndarray，shape (n, n)，代价矩阵（必须为方阵）。

    返回
    ----
    list : ``[(row, col), ...]``，共 n 个配对，总代价最小。
    """
    cost = np.asarray(cost, dtype=np.float64)
    n = cost.shape[0]
    if n == 0:
        return []

    # 顶标 u（行）/ v（列）；p[col] = 匹配到该列的行；way 记录增广路径
    u = np.zeros(n, dtype=np.float64)
    v = np.zeros(n, dtype=np.float64)
    p = np.full(n, -1, dtype=np.int64)
    way = np.zeros(n, dtype=np.int64)

    for i in range(n):
        p[0] = i
        j0 = 0
        minv = np.full(n, np.inf, dtype=np.float64)
        used = np.zeros(n, dtype=bool)
        while True:
            used[j0] = True
            i0 = p[j0]
            delta = np.inf
            j1 = 0
            for j in range(n):
                if not used[j]:
                    cur = cost[i0, j] - u[i0] - v[j]
                    if cur < minv[j]:
                        minv[j] = cur
                        way[j] = j0
                    if minv[j] < delta:
                        delta = minv[j]
                        j1 = j
            for j in range(n):
                if used[j]:
                    u[p[j]] += delta
                    v[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == -1:
                break
        # 沿增广路径回溯更新匹配
        while True:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
            if j0 == 0:
                break

    return [(int(p[j]), j) for j in range(n)]


# ----------------------------------------------------------------------
# 关联：检测框 vs 预测框，按 IoU 代价 + 阈值门限
# ----------------------------------------------------------------------
def associate_detections_to_tracks(det_boxes, trk_boxes, thresh):
    """将检测框与预测框做 IoU 关联。

    代价矩阵为 ``cost = 1 - IoU``，用匈牙利算法求最小代价匹配；
    只有代价不超过 ``thresh``（即 IoU >= 1 - thresh）的配对才被接受。

    参数
    ----
    det_boxes : ndarray，shape (N, 4)，检测框。
    trk_boxes : ndarray，shape (M, 4)，预测框。
    thresh : float，匹配代价阈值（IoU 门限的补），如 0.8 表示要求 IoU >= 0.2。

    返回
    ----
    matches : list[(det_idx, trk_idx)]，接受的匹配对。
    unmatched_dets : list[int]，未匹配的检测索引。
    unmatched_trks : list[int]，未匹配的轨迹索引。
    """
    det_boxes = np.asarray(det_boxes, dtype=np.float64)
    trk_boxes = np.asarray(trk_boxes, dtype=np.float64)
    n_det, n_trk = det_boxes.shape[0], trk_boxes.shape[0]

    if n_det == 0:
        return [], [], list(range(n_trk))
    if n_trk == 0:
        return [], list(range(n_det)), []

    ious = compute_iou_matrix(det_boxes, trk_boxes)
    cost = 1.0 - ious

    # 补成方阵后做匈牙利匹配；填充列/行用大代价，保证只在真实配对间择优
    size = max(n_det, n_trk)
    cost_pad = np.full((size, size), 1e5, dtype=np.float64)
    cost_pad[:n_det, :n_trk] = cost
    assignment = linear_sum_assignment(cost_pad)

    matches = []
    matched_dets = set()
    matched_trks = set()
    for i, j in assignment:
        if i < n_det and j < n_trk and cost_pad[i, j] <= thresh:
            matches.append((i, j))
            matched_dets.add(i)
            matched_trks.add(j)

    unmatched_dets = [i for i in range(n_det) if i not in matched_dets]
    unmatched_trks = [j for j in range(n_trk) if j not in matched_trks]
    return matches, unmatched_dets, unmatched_trks


# ----------------------------------------------------------------------
# 单目标框 Kalman 滤波器（线性匀速模型）
# ----------------------------------------------------------------------
def _xyxy_to_xyah(bbox):
    """[x1,y1,x2,y2] -> [cx, cy, s(面积), r(宽高比)]。"""
    x1, y1, x2, y2 = (float(v) for v in bbox)
    w = max(x2 - x1, 1e-6)
    h = max(y2 - y1, 1e-6)
    cx = x1 + 0.5 * w
    cy = y1 + 0.5 * h
    return np.array([cx, cy, w * h, w / h], dtype=np.float64)


def _xyah_to_xyxy(mean):
    """状态 [cx, cy, s, r, ...] -> [x1, y1, x2, y2]。"""
    cx, cy, s, r = mean[:4]
    if s <= 0.0 or r <= 0.0:
        return np.array([cx, cy, cx, cy], dtype=np.float64)
    w = np.sqrt(s * r)
    h = s / w
    return np.array(
        [cx - 0.5 * w, cy - 0.5 * h, cx + 0.5 * w, cy + 0.5 * h],
        dtype=np.float64,
    )


class _KalmanBoxFilter:
    """单目标框 Kalman 滤波器（线性匀速，状态 8 维）。

    状态向量 ``[cx, cy, s, r, vcx, vcy, vs, vr]``：
    - ``cx, cy``：检测框中心坐标（像素）；
    - ``s``：检测框面积（宽 x 高）；
    - ``r``：宽高比 w/h（在帧间视为近似不变，仅位置与面积按匀速演化）；
    - ``vcx, vcy, vs, vr``：对应分量的速度。

    运动模型 F = [[I4, dt·I4], [0, I4]]，观测矩阵 H = [I4 | 0]。
    噪声标准差与当前目标尺度（用宽高比 r 作尺度代理，与原始 ByteTrack 一致）
    成正比，从而兼顾近大远小的检测精度差异。

    参数
    ----
    std_weight_position : float
        位置噪声相对尺度系数（默认 1/20，与 ByteTrack 原版一致）。
    std_weight_velocity : float
        速度噪声相对尺度系数（默认 1/160）。
    dt : float
        固定帧间隔（默认 1.0，即逐帧演化）。
    """

    def __init__(
        self,
        std_weight_position=1.0 / 20.0,
        std_weight_velocity=1.0 / 160.0,
        dt=1.0,
    ):
        self._std_pos = float(std_weight_position)
        self._std_vel = float(std_weight_velocity)
        self._dt = float(dt)

        # 状态转移矩阵 F（8x8）
        self._F = np.eye(8, dtype=np.float64)
        for i in range(4):
            self._F[i, i + 4] = self._dt
        # 观测矩阵 H（4x8）
        self._H = np.zeros((4, 8), dtype=np.float64)
        for i in range(4):
            self._H[i, i] = 1.0
        # 状态与协方差
        self._mean = np.zeros(8, dtype=np.float64)
        self._cov = np.eye(8, dtype=np.float64)
        self._initialized = False

    # ---------------- 初始化 / 预测 / 更新 ----------------
    def initiate(self, z):
        """用首个观测 ``[cx, cy, s, r]`` 初始化状态与协方差。"""
        z = np.asarray(z, dtype=np.float64).reshape(4)
        self._mean = np.zeros(8, dtype=np.float64)
        self._mean[:4] = z
        h = max(self._mean[3], 1e-3)  # 宽高比作尺度代理
        std_pos = [self._std_pos * h, self._std_pos * h, 1e-2, self._std_pos * h]
        std_vel = [self._std_vel * h, self._std_vel * h, 1e-5, self._std_vel * h]
        self._cov = np.diag(np.square(np.array(std_pos + std_vel)))
        self._initialized = True
        return self._mean, self._cov

    def predict(self):
        """一步状态预测（漏检时自然外推）。"""
        if not self._initialized:
            return
        self._mean = self._F @ self._mean
        h = max(self._mean[3], 1e-3)
        std_pos = [self._std_pos * h, self._std_pos * h, 1e-2, self._std_pos * h]
        std_vel = [self._std_vel * h, self._std_vel * h, 1e-5, self._std_vel * h]
        q = np.diag(np.square(np.array(std_pos + std_vel)))
        self._cov = self._F @ self._cov @ self._F.T + q

    def update(self, z):
        """用观测 ``[cx, cy, s, r]`` 做标准 Kalman 更新。"""
        if not self._initialized:
            raise RuntimeError("滤波器尚未初始化，请先调用 initiate()")
        z = np.asarray(z, dtype=np.float64).reshape(4)
        h = max(self._mean[3], 1e-3)
        std = [self._std_pos * h, self._std_pos * h, 1e-1, self._std_pos * h]
        r = np.diag(np.square(np.array(std)))

        # 新息协方差 S = H P H^T + R（4x4）
        S = self._H @ self._cov @ self._H.T + r
        # 卡尔曼增益 K = P H^T S^-1（8x4）
        K = self._cov @ self._H.T @ np.linalg.inv(S)
        y = z - self._H @ self._mean
        self._mean = self._mean + K @ y
        I = np.eye(8)
        self._cov = (I - K @ self._H) @ self._cov
        return self._mean, self._cov

    # ---------------- 只读 ----------------
    @property
    def mean(self):
        return self._mean.copy()

    @property
    def covariance(self):
        return self._cov.copy()

    @property
    def is_initialized(self):
        return self._initialized


class _Track:
    """内部轨迹对象：一个框 Kalman 滤波器 + 生命周期状态。"""

    __slots__ = (
        "id",
        "kf",
        "score",
        "hits",
        "misses",
        "age",
        "last_seen",
        "confirmed",
        "start_frame",
    )

    def __init__(self, tid, kf, bbox, score, frame_id, confirmed=False):
        self.id = tid
        self.kf = kf
        z = _xyxy_to_xyah(bbox)
        self.kf.initiate(z)
        self.score = float(score)      # 最近一次关联检测的置信度
        self.hits = 1                  # 关联成功次数
        self.misses = 0                # 连续未关联帧数
        self.age = 1                   # 自创建以来的帧数
        self.last_seen = int(frame_id)  # 最近一次关联所在帧号
        self.confirmed = bool(confirmed)  # 是否已确认（稳定轨迹）
        self.start_frame = int(frame_id)

    def predict(self):
        self.kf.predict()
        self.age += 1
        self.misses += 1

    def update(self, bbox, score, frame_id):
        z = _xyxy_to_xyah(bbox)
        self.kf.update(z)
        self.score = float(score)
        self.hits += 1
        self.misses = 0
        self.last_seen = int(frame_id)

    def confirm(self, min_hits):
        """当关联次数达到 min_hits 时标记为已确认。"""
        if self.hits >= min_hits:
            self.confirmed = True

    def get_box(self):
        """返回当前状态对应的 ``[x1, y1, x2, y2]``。"""
        return _xyah_to_xyxy(self.kf.mean)


class ByteTrack:
    """ByteTrack 多目标跟踪器。

    职责：
    1. 维护多条轨迹，每条轨迹持有一个 :class:`_KalmanBoxFilter`；
    2. 每帧先对所有轨迹执行 predict()，再分两轮关联：
       - 第一轮：高分检测（``conf >= track_thresh``）匹配全部轨迹；
       - 第二轮（低分二次关联）：剩余未匹配的**已确认**轨迹再与低分检测
         （``det_thresh <= conf < track_thresh``）匹配，缓解遮挡断链；
    3. 未匹配的高分检测创建新轨迹（新轨迹需连续关联 ``min_hits`` 帧后确认）；
    4. 超过 ``max_lost`` 帧未关联的轨迹被删除。

    参数
    ----
    track_thresh : float
        高分检测阈值。置信度不低于该值的检测参与第一轮关联。默认 0.5。
    det_thresh : float
        检测置信度下界。低于该值的检测被丢弃，不参与关联。默认 0.1。
    match_thresh : float
        IoU 代价阈值（= 1 - IoU 门限）。默认 0.8，即要求 IoU >= 0.2。越大越宽松。
    max_lost : int
        轨迹连续多少帧未关联即被删除（容忍遮挡时长）。默认 30。
    min_hits : int
        新轨迹连续关联多少帧后被标记为“已确认”（稳定轨迹）。
        未确认轨迹不参与低分二次关联。默认 3。
    frame_rate : int
        视频帧率（参考信息，用于换算时间）。默认 30。
    dt : float
        固定帧间隔（秒）。为 None 时取 1.0（逐帧演化，与 ByteTrack 原版一致）。
    output_confirmed_only : bool
        为 True 时只输出已确认（稳定）轨迹；为 False 时输出所有活跃轨迹，
        未确认轨迹可通过 ``track_info()`` 的 ``stable`` 字段过滤。默认 False。
    """

    def __init__(
        self,
        track_thresh=0.5,
        det_thresh=0.1,
        match_thresh=0.8,
        max_lost=30,
        min_hits=3,
        frame_rate=30,
        dt=None,
        output_confirmed_only=False,
    ):
        if not 0.0 < track_thresh <= 1.0:
            raise ValueError("track_thresh 必须在 (0, 1] 区间")
        if not 0.0 <= det_thresh < track_thresh:
            raise ValueError("det_thresh 必须满足 0 <= det_thresh < track_thresh")
        if not 0.0 < match_thresh <= 1.0:
            raise ValueError("match_thresh 必须在 (0, 1] 区间")
        if max_lost < 1:
            raise ValueError("max_lost 必须为正整数")
        if min_hits < 1:
            raise ValueError("min_hits 必须为正整数")

        self.track_thresh = float(track_thresh)
        self.det_thresh = float(det_thresh)
        self.match_thresh = float(match_thresh)
        self.max_lost = int(max_lost)
        self.min_hits = int(min_hits)
        self.frame_rate = int(frame_rate)
        self._dt = 1.0 if dt is None else float(dt)
        self._output_confirmed_only = bool(output_confirmed_only)

        self._tracks = {}          # track_id -> _Track
        self._next_id = 0          # 自动分配 ID 的计数器
        self._frame_id = 0         # 当前帧号

    # ------------------------------------------------------------------
    # 主入口
    # ------------------------------------------------------------------
    def update(self, detections, frame_id=None):
        """输入一帧检测结果，输出所有活跃轨迹。

        参数
        ----
        detections : list[(x1, y1, x2, y2, conf)]
            当前帧的检测框列表。元素可为 list / tuple / ndarray。
        frame_id : int | None
            显式指定帧号；为 None 时使用内部自增计数。

        返回
        ----
        list : ``[[track_id, x1, y1, x2, y2, conf], ...]``，按 track_id 升序。
        """
        if frame_id is None:
            self._frame_id += 1
            frame_id = self._frame_id
        else:
            self._frame_id = int(frame_id)

        detections = [tuple(float(v) for v in det) for det in detections]

        # 1. 预测所有轨迹（漏检帧自然外推）
        for trk in self._tracks.values():
            trk.predict()

        # 2. 划分高低分检测
        high_dets = [d for d in detections if d[4] >= self.track_thresh]
        low_dets = [d for d in detections if self.det_thresh <= d[4] < self.track_thresh]

        trk_ids = list(self._tracks.keys())
        n_trk = len(trk_ids)

        # 3. 第一轮关联：高分检测 vs 全部轨迹
        if high_dets and n_trk > 0:
            high_boxes = np.array([d[:4] for d in high_dets], dtype=np.float64)
            trk_boxes = np.array(
                [trk.get_box() for trk in self._tracks.values()], dtype=np.float64
            )
            matches1, unmatched_high, unmatched_trk = associate_detections_to_tracks(
                high_boxes, trk_boxes, thresh=1.0 - self.match_thresh
            )
            for det_idx, trk_idx in matches1:
                trk = self._tracks[trk_ids[trk_idx]]
                trk.update(high_dets[det_idx][:4], high_dets[det_idx][4], frame_id)
                trk.confirm(self.min_hits)
        else:
            unmatched_high = list(range(len(high_dets)))
            unmatched_trk = list(range(n_trk))

        # 4. 第二轮关联（低分二次关联）：低分检测 vs 未匹配的已确认轨迹
        unmatched_confirmed = [
            trk_ids[i]
            for i in unmatched_trk
            if self._tracks[trk_ids[i]].confirmed
        ]
        if low_dets and unmatched_confirmed:
            low_boxes = np.array([d[:4] for d in low_dets], dtype=np.float64)
            conf_boxes = np.array(
                [self._tracks[tid].get_box() for tid in unmatched_confirmed],
                dtype=np.float64,
            )
            matches2, _, _ = associate_detections_to_tracks(
                low_boxes, conf_boxes, thresh=1.0 - self.match_thresh
            )
            for det_idx, trk_pos in matches2:
                trk = self._tracks[unmatched_confirmed[trk_pos]]
                trk.update(low_dets[det_idx][:4], low_dets[det_idx][4], frame_id)
                trk.confirm(self.min_hits)

        # 5. 未匹配的高分检测 -> 新建轨迹
        for det_idx in unmatched_high:
            det = high_dets[det_idx]
            self._create_track(
                det[:4], det[4], frame_id, first_frame=(frame_id <= 1)
            )

        # 6. 删除过期轨迹
        self._prune()

        return self.states()

    # ------------------------------------------------------------------
    # 内部：轨迹管理
    # ------------------------------------------------------------------
    def _create_track(self, bbox, score, frame_id, first_frame=False):
        kf = _KalmanBoxFilter(dt=self._dt)
        trk = _Track(
            self._next_id,
            kf,
            bbox,
            score,
            frame_id,
            confirmed=first_frame or self.min_hits <= 1,
        )
        self._next_id += 1
        self._tracks[trk.id] = trk
        return trk.id

    def _prune(self):
        """删除超过 max_lost 帧未关联的轨迹（含未确认的幽灵轨迹）。"""
        stale = [
            tid for tid, trk in self._tracks.items() if trk.misses > self.max_lost
        ]
        for tid in stale:
            del self._tracks[tid]

    # ------------------------------------------------------------------
    # 输出
    # ------------------------------------------------------------------
    def states(self):
        """返回 ``[[track_id, x1, y1, x2, y2, conf], ...]``，按 track_id 升序。"""
        out = []
        for tid, trk in self._tracks.items():
            if self._output_confirmed_only and not trk.confirmed:
                continue
            box = trk.get_box()
            out.append(
                [
                    int(tid),
                    float(box[0]),
                    float(box[1]),
                    float(box[2]),
                    float(box[3]),
                    float(trk.score),
                ]
            )
        out.sort(key=lambda t: t[0])
        return out

    def track_info(self):
        """返回每条轨迹的生命周期信息（便于可视化 / 调试）。

        返回 ``{track_id: {bbox, score, hits, misses, age, last_seen, start_frame,
        stable, confirmed}}``，其中 ``stable == confirmed``。
        """
        info = {}
        for tid, trk in self._tracks.items():
            box = trk.get_box()
            info[tid] = {
                "bbox": tuple(float(v) for v in box),
                "score": trk.score,
                "hits": trk.hits,
                "misses": trk.misses,
                "age": trk.age,
                "last_seen": trk.last_seen,
                "start_frame": trk.start_frame,
                "stable": trk.confirmed,
                "confirmed": trk.confirmed,
            }
        return info

    def track_count(self):
        return len(self._tracks)

    def reset(self):
        """清空所有轨迹与计数。"""
        self._tracks.clear()
        self._next_id = 0
        self._frame_id = 0