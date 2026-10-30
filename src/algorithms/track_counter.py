# -*- coding: utf-8 -*-
"""轨迹级计数去重 + ROI 进出事件（ByteTrack 接线前置模块，Python 参考实现）。

为什么需要本模块
----------------
板端旧口径把"当帧检测框数"直接当人数（src/luckfox_rtsp_yolov5/src/main.cc:647/733
的 dangerPersons / featN，以及 enhanced_decision.cc:52-138 的贪心最近邻+Kalman 平滑）。
YOLO 对同一行人吐出两个不同 anchor 尺度的框时，只要脚点距离超过 2cm，
GraphLaplacian 的 2cm 去重就兜不住，人数直接 +1 —— 这正是参考作品第 4 个坑
"跟踪器重复计数"在本项目里的同构形态。本模块把计数口径从"框"提升到"轨迹身份"：

    同一条身份（identity）跨帧只计 1 人。

五个必须讲清的语义（也是 C++ 镜像 src/algorithms/track_counter.h 的契约）
--------------------------------------------------------------------------
1. 同一轨迹跨帧只计 1 人：人数 = 本帧出现且已确认的"身份"个数，而不是轨迹条数、
   也不是检测框数。一条轨迹在 N 帧里持续存在，任何一帧都只贡献 1。

2. ID 切换 / 短时丢失的合并策略（re-id merge）：ByteTrack 在遮挡后重现、或检测框
   IoU 掉到门限以下时会分配新 track_id。本模块维护 track_id -> identity_id 映射：
   新 track_id 若脚点落在某条"本帧未出现、且丢失不超过 reid_max_gap 帧"的身份的
   reid_radius 半径内，则并入该身份（取最近者，并列取 identity_id 小者），而不是
   新建身份。合并是**粘性**的，帧间不会来回翻转（避免计数抖动）。reid_radius<=0
   时关闭该合并。该步骤只可能让身份数变少，不会凭空增加。

3. 未确认轨迹（tentative）不计入人数：ByteTrack 的 confirmed 由 min_hits 决定
   （默认连续命中 3 帧）。未确认轨迹只参与身份登记（防止它另起炉灶），不计人数、
   不产生 ROI 进出事件。count_tentative=True 可放开（调试用）。

4. 与 min_danger_n 门控的交互：risk_fsm 的 min_danger_n（默认 3）是 DANGER 的硬
   门槛。旧口径下"同一人两个框"就能把 n 从 1 顶到 2，两个人就能凑出 n>=3 而误升
   DANGER。本模块输出 gate_n = 去重后人数（应喂给 FSM 的 n），以及 gate_ok /
   legacy_gate_ok 两个布尔量，用来量化"旧口径是否会误开门"。

5. 与脚点 2cm 去重的先后顺序：(a) 跨帧身份归并（re-id merge，时间维） ->
   (b) 同帧空间去重（dedup_tol，默认 2cm，空间维） -> (c) GraphLaplacian 内部的
   2cm 去重（在平滑脚点上再做一次，几何兜底）。即"先身份、后空间、最后几何兜底"。
   (b) 是**粘性**合并：同帧两身份脚点距离 <= dedup_tol 时永久并为一个身份
   （置信度高者为规范身份，并列取 ID 小者），而不是每帧重新选一次 —— 每帧重选
   会在置信度抖动时造成计数抖动。(c) 保留在 GraphLaplacian 里不动，与本模块不
   冲突（只会更少不会更多）。

坐标系与单位（板端现实：ROI 是像素，脚点是 IPM 后的米制）
----------------------------------------------------------
- foot（可选）：去重 / re-id 用的脚点，**米制**（IPM 映射后），dedup_tol /
  reid_radius 与之同单位。缺省取 box 底边中点。
- roi_point（可选）：ROI 判定点，**像素**（通常取 box 中心），roi_polygon /
  roi_hysteresis 与之同单位。缺省取 box 中心。
两套点各自成体系、互不换算：既能在米制上做 2cm 物理去重，又能直接复用
config/default_config.json 里像素单位的 roi_zone.polygon。

ROI 滞回 + 最短驻留
-------------------
对多边形做"点到边界的有符号距离" sd（内部为正、外部为负）：
- 当前提交状态 OUT：sd >= +roi_hysteresis 才算"进入候选"（必须深入内部）；
- 当前提交状态 IN ：sd > -roi_hysteresis 才算"仍在内部"（允许贴边抖动）。
滞回带内的点保持上一状态，边界抖动不会产生 ENTER/LEAVE 反复；状态翻转还须连续
满足 min_dwell_frames 帧（最短驻留），缺席帧 / 未确认帧按 outside 计。事件只在
翻转**提交**那一帧产生，并带 since_frame（穿越实际开始帧号，暴露时延）。
身份被合并时沿用更接近 IN 的一侧状态，避免 ID 切换 / 重复框把一次进入拆成两次。

用法::

    from algorithms.track_counter import TrackCounter, TrackObs, FrameObs, TrackCounterConfig

    tc = TrackCounter(TrackCounterConfig(min_danger_n=3))
    for frame_id, tracks, dets in stream:
        obs = [TrackObs(t.track_id, t.box, t.score, t.confirmed, foot=t.ground_foot)
               for t in tracks]
        r = tc.update(FrameObs(frame_id, obs, n_detections=len(dets)))
        print(r.n, r.dangerous_now, [e.kind for e in r.events])
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

__all__ = [
    "TrackObs",
    "FrameObs",
    "TrackCounterConfig",
    "PersonState",
    "RoiEvent",
    "CountStats",
    "CountResult",
    "TrackCounter",
    "polygon_signed_distance",
    "point_in_polygon",
]

# 事件类型（不用 Enum，便于 C++ 侧用 int 对齐）
EVENT_ENTER = 1
EVENT_LEAVE = 0


# ----------------------------------------------------------------------
# 输入结构
# ----------------------------------------------------------------------
@dataclass
class TrackObs:
    """一帧里的一条轨迹观测（由 ByteTrack::states() + trackInfo() 组装）。

    字段
    ----
    track_id : int
        ByteTrack 分配的轨迹 ID（必须 >= 0）。
    box : (x1, y1, x2, y2)
        轨迹框（像素）。
    score : float
        最近一次关联检测的置信度。
    confirmed : bool
        ByteTrack 的 confirmed（未确认轨迹不计人数）。
    misses : int
        连续未关联帧数（0 表示本帧关联上了）；仅做透传。
    foot : (x, y) | None
        米制脚点（IPM 后）；None 时取 box 底边中点。
    roi_point : (x, y) | None
        ROI 判定点（像素）；None 时取 box 中心。
    """

    track_id: int
    box: Tuple[float, float, float, float]
    score: float = 0.0
    confirmed: bool = True
    misses: int = 0
    foot: Optional[Tuple[float, float]] = None
    roi_point: Optional[Tuple[float, float]] = None


@dataclass
class FrameObs:
    """一帧输入。

    frame_id : int
        帧号，必须严格递增（用于 re-id 的丢失时长判断）。
    tracks : list[TrackObs]
        本帧输出轨迹。
    n_detections : int | None
        当帧 person 检测框数（旧口径基线）；None 时取 len(tracks)。
    """

    frame_id: int
    tracks: List[TrackObs] = field(default_factory=list)
    n_detections: Optional[int] = None


# ----------------------------------------------------------------------
# 配置
# ----------------------------------------------------------------------
@dataclass
class TrackCounterConfig:
    """计数去重配置（单位见各字段说明）。

    dedup_tol : float
        同帧脚点去重容差（**米**，与 foot 同单位）。默认 0.02 = 2cm，与
        graph_laplacian 的 DEDUP_TOL_M 对齐。<=0 关闭该分支。
    dup_foot_max : float
        同帧"脚点近到物理上不可能有两个人"的上界（**米**）。默认 0.20：
        两个成年人的脚点在地面上相距 20cm 以内等同于同一人。它是 dedup_tol
        的放宽版（2cm 只覆盖几乎重合的框），用来吃掉"不同 anchor 尺度导致框底
        不一致"的重复框。<=0 关闭该分支。
    dup_iou_min : float
        同帧"框包含式"重复判据的 IoMin 门限（交集 / 较小框面积）。默认 0.75。
        为什么需要它：同一行人被两个不同 anchor 尺度的框检出时，两个框的**脚点**
        可能差到 20cm 以上，脚点分支漏判；但两个框在"身体区域"上高度互相包含
        （IoMin 接近 1）。0.75 的误并边界：等大框并排错开 25% 宽度才会误并
        （40px 框 = 10px≈5cm，正常两人不可能站这么近），再加"中心互含"约束后
        要求两个框中心分别在对方框内；"远小近大"的两人由 dup_height_ratio 挡住。
        注意：ByteTrack 自身已用 IoU 门限把关，同一尺度下偏移超过 0.67 倍框宽的
        两个框会被判成两条轨迹，那种情形本模块**有意不合并**（更可能是真的两个人）。
        <=0 关闭该分支。
    dup_height_ratio : float
        框包含式重复判据的高度比上限（默认 1.5）。防止"远处小人整框落在近处
        大人框内"被误判为同一人（二者高度比通常 >2）。
    reid_radius : float
        ID 切换 / 短时丢失合并半径（**米**）。默认 0.6。<=0 关闭 re-id。
    reid_max_gap : int
        允许的丢失帧数（超过则不再合并）。默认 15。
    identity_ttl : int
        身份记录回收：已 OUT 且连续 identity_ttl 帧未出现则删除记录。默认 300。
    roi_polygon : list[(x, y)]
        ROI 多边形（**像素**），空列表 = 不启用 ROI 事件。
    roi_hysteresis : float
        ROI 滞回带半宽（**像素**）。默认 8.0；<=0 时退化为普通点在多边形判定。
    min_dwell_frames : int
        最短驻留帧数（进入 / 离开都要连续满足）。默认 3，>=1。
    min_danger_n : int
        与 risk_fsm.min_danger_n 对齐的门控门槛。默认 3。
    count_tentative : bool
        是否把未确认轨迹也计入人数（默认 False）。
    coast_frames : int
        "滑行帧数"：轨迹本帧未被关联（ByteTrack 仍在 states() 里输出它外推的
        预测框，TrackObs.misses > 0）时，最多再算它存在几帧。默认 1 ——
        允许单帧漏检不抖人数，但连续 2 帧以上没被关联就不再计入人数
        （否则一个人走出画面后，ByteTrack 会带着预测框滑行 max_lost 帧，
        人数要 1 秒后才回落）。0 = 只算本帧真正关联上的轨迹。
        注意：滑行期间 ROI 状态机不翻转（仍按"在 ROI 内"处理），避免单帧
        漏检把一次进入拆成 LEAVE + ENTER 两次。
    """

    dedup_tol: float = 0.02
    dup_foot_max: float = 0.20
    dup_iou_min: float = 0.75
    dup_height_ratio: float = 1.5
    reid_radius: float = 0.6
    reid_max_gap: int = 15
    identity_ttl: int = 300
    roi_polygon: Sequence[Tuple[float, float]] = field(default_factory=list)
    roi_hysteresis: float = 8.0
    min_dwell_frames: int = 3
    min_danger_n: int = 3
    count_tentative: bool = False
    coast_frames: int = 1


# ----------------------------------------------------------------------
# 输出结构
# ----------------------------------------------------------------------
@dataclass
class PersonState:
    """一个被计入人数的身份（本帧状态）。"""

    identity_id: int
    track_ids: List[int]      # 本帧归并到该身份的所有轨迹 ID（升序）
    box: Tuple[float, float, float, float]
    score: float
    foot: Tuple[float, float]
    roi_point: Tuple[float, float]
    in_roi: bool              # 滞回状态机当前是否 IN
    dwell: int                # 当前状态连续帧数
    confirmed: bool


@dataclass
class RoiEvent:
    """一次 ROI 状态翻转（已过滞回 + 最短驻留）。"""

    frame_id: int
    identity_id: int
    track_id: int
    entering: bool
    since_frame: int          # 穿越实际开始帧号（提交帧号 = frame_id）
    signed_distance: float    # 提交帧对 ROI 边界的有符号距离（内部为正）

    @property
    def kind(self) -> str:
        return "ENTER" if self.entering else "LEAVE"


@dataclass
class CountStats:
    """累计统计（便于报告量化"改进前 vs 改进后"）。"""

    frames: int = 0
    duplicates_suppressed: int = 0     # Σ 被抑制的重复轨迹数（同帧空间合并）
    id_switch_merges: int = 0          # Σ 跨帧 re-id 归并次数
    total_legacy_count: int = 0        # Σ 旧口径（当帧检测框数）
    total_spatial_count: int = 0       # Σ 仅同帧 2cm 空间去重后
    total_dedup_count: int = 0         # Σ 去重后人数
    max_legacy_count: int = 0
    max_dedup_count: int = 0
    frames_legacy_gate_open: int = 0   # 旧口径下 n >= min_danger_n 的帧数
    frames_dedup_gate_open: int = 0    # 去重后 n >= min_danger_n 的帧数


@dataclass
class CountResult:
    """一帧计数结果。"""

    frame_id: int
    n: int                   # 去重后人数（轨迹身份口径）
    n_detections: int        # 旧口径：当帧检测框数
    n_spatial_dedup: int     # 只做同帧 dedup_tol 空间去重（不跨帧）的数量
    n_tracks: int            # 输入轨迹条数
    n_suppressed: int        # 本帧被抑制的重复轨迹条数
    persons: List[PersonState]
    events: List[RoiEvent]
    dangerous_now: int       # 当前滞回状态为 IN 的身份数（OSD Dangerous Person）
    visitors_total: int      # 累计到访身份数（ID 合并后不重复计）
    gate_n: int              # 建议喂给 FSM 的 n（== n）
    gate_ok: bool            # gate_n >= min_danger_n
    legacy_gate_ok: bool     # 旧口径 n_detections >= min_danger_n
    stats: CountStats

    def person_ids(self) -> List[int]:
        return [p.identity_id for p in self.persons]


# ----------------------------------------------------------------------
# 几何：多边形内部判定 + 有符号距离
# ----------------------------------------------------------------------
def point_in_polygon(pt: Tuple[float, float],
                     poly: Sequence[Tuple[float, float]]) -> bool:
    """射线法（PNPOLY），与 src/luckfox_rtsp_yolov5/src/main.cc 的
    pointInPolygonRoi 同语义：边界上的点由射线判据决定（未做特殊处理）。
    """
    if len(poly) < 3:
        return False
    x, y = float(pt[0]), float(pt[1])
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = float(poly[i][0]), float(poly[i][1])
        xj, yj = float(poly[j][0]), float(poly[j][1])
        if (yi > y) != (yj > y):
            x_cross = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < x_cross:
                inside = not inside
        j = i
    return inside


def _point_segment_distance(px: float, py: float, ax: float, ay: float,
                            bx: float, by: float) -> float:
    dx, dy = bx - ax, by - ay
    d2 = dx * dx + dy * dy
    if d2 <= 1e-18:
        return math.sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay))
    t = ((px - ax) * dx + (py - ay) * dy) / d2
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    cx, cy = ax + t * dx, ay + t * dy
    return math.sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy))


def polygon_signed_distance(pt: Tuple[float, float],
                            poly: Sequence[Tuple[float, float]]) -> float:
    """点到多边形边界的最短距离，内部为正、外部为负。

    多边形顶点数 < 3 时返回 -inf（视为永远在外部，ROI 未启用）。
    """
    if len(poly) < 3:
        return float("-inf")
    x, y = float(pt[0]), float(pt[1])
    best = float("inf")
    n = len(poly)
    for i in range(n):
        ax, ay = float(poly[i][0]), float(poly[i][1])
        bx, by = float(poly[(i + 1) % n][0]), float(poly[(i + 1) % n][1])
        d = _point_segment_distance(x, y, ax, ay, bx, by)
        if d < best:
            best = d
    if best == float("inf"):
        best = 0.0
    return best if point_in_polygon((x, y), poly) else -best


# ----------------------------------------------------------------------
# 内部身份记录
# ----------------------------------------------------------------------
class _Identity(object):
    __slots__ = ("identity_id", "last_frame", "last_observed", "last_foot",
                 "last_roi_point", "last_box", "last_score", "last_track_id",
                 "state_in", "dwell", "pending_in", "pending_cnt", "since_frame")

    def __init__(self, identity_id: int):
        self.identity_id = identity_id
        self.last_frame = -1
        self.last_observed = -1     # 最近一次"真正被关联上"（misses==0）的帧号
        self.last_foot = (0.0, 0.0)
        self.last_roi_point = (0.0, 0.0)
        self.last_box = (0.0, 0.0, 0.0, 0.0)
        self.last_score = 0.0
        self.last_track_id = -1
        self.state_in = False       # 已提交的 ROI 状态
        self.dwell = 0              # 当前状态连续帧数
        self.pending_in = False     # 待提交状态
        self.pending_cnt = 0        # 待提交已连续帧数
        self.since_frame = -1       # 待提交序列起始帧


class TrackCounter(object):
    """轨迹级计数去重 + ROI 进出事件（与 C++ 版逐值一致）。"""

    def __init__(self, config: Optional[TrackCounterConfig] = None):
        cfg = config if config is not None else TrackCounterConfig()
        if cfg.min_dwell_frames < 1:
            raise ValueError("min_dwell_frames 必须 >= 1")
        if cfg.reid_max_gap < 0:
            raise ValueError("reid_max_gap 必须 >= 0")
        if cfg.identity_ttl < 0:
            raise ValueError("identity_ttl 必须 >= 0")
        if cfg.min_danger_n < 1:
            raise ValueError("min_danger_n 必须 >= 1")
        if cfg.coast_frames < 0:
            raise ValueError("coast_frames 必须 >= 0")
        self.cfg = cfg
        self._poly = [(float(x), float(y)) for x, y in cfg.roi_polygon]
        self.reset()

    # ------------------------------------------------------------------
    def reset(self) -> None:
        """清空全部状态（必须与 ByteTrack::reset() 同步调用，否则 track_id 会复用）。"""
        self._next_identity = 0
        self._track_to_identity: Dict[int, int] = {}
        self._identities: Dict[int, _Identity] = {}
        self._merge_into: Dict[int, int] = {}
        self._frame_id = 0
        self._visitors_total = 0
        self.stats = CountStats()

    # ------------------------------------------------------------------
    # 查询
    # ------------------------------------------------------------------
    def identity_of(self, track_id: int) -> Optional[int]:
        """轨迹 ID -> 规范身份 ID（未登记返回 None）。"""
        ident = self._track_to_identity.get(int(track_id))
        if ident is None:
            return None
        return self._canonical(ident)

    def merge_count(self) -> int:
        return len(self._merge_into)

    def identity_count(self) -> int:
        return sum(1 for k in self._identities if self._canonical(k) == k)

    # ------------------------------------------------------------------
    # 主入口
    # ------------------------------------------------------------------
    def update(self, frame: FrameObs) -> CountResult:
        """处理一帧，返回去重后人数、目标身份列表与 ROI 进出事件。"""
        frame_id = int(frame.frame_id)
        self._frame_id = frame_id

        # ---- 0. 归一化输入 ----
        obs: List[dict] = []
        for t in frame.tracks:
            if int(t.track_id) < 0:
                continue
            box = tuple(float(v) for v in t.box)
            if len(box) != 4 or not all(math.isfinite(v) for v in box):
                continue
            score = float(t.score)
            if not math.isfinite(score):
                score = 0.0
            foot = t.foot
            if foot is None:
                foot = ((box[0] + box[2]) * 0.5, box[3])
            else:
                foot = (float(foot[0]), float(foot[1]))
            roi_pt = t.roi_point
            if roi_pt is None:
                roi_pt = ((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5)
            else:
                roi_pt = (float(roi_pt[0]), float(roi_pt[1]))
            if not (math.isfinite(foot[0]) and math.isfinite(foot[1])
                    and math.isfinite(roi_pt[0]) and math.isfinite(roi_pt[1])):
                continue
            obs.append({
                "track_id": int(t.track_id), "box": box, "score": score,
                "confirmed": bool(t.confirmed), "misses": int(t.misses),
                "observed": int(t.misses) <= 0,
                "foot": foot, "roi": roi_pt,
            })
        # 同一帧里同一个 track_id 只保留一条（防御上游重复输出）
        by_id: Dict[int, dict] = {}
        for o in obs:
            prev = by_id.get(o["track_id"])
            if prev is None or _better(o, prev):
                by_id[o["track_id"]] = o
        obs = [by_id[k] for k in sorted(by_id)]

        n_tracks = len(obs)
        n_dets = int(frame.n_detections) if frame.n_detections is not None else n_tracks
        if n_dets < 0:
            n_dets = 0
        n_spatial = _spatial_dedup_count(obs, self.cfg.dedup_tol)

        # ---- 1. 身份归并（跨帧 re-id）+ 登记 ----
        claims: Dict[int, dict] = {}           # identity_id -> 本帧代表观测
        canon_tids: Dict[int, List[int]] = {}  # identity_id -> 本帧轨迹 ID 列表
        for o in obs:
            ident = self._track_to_identity.get(o["track_id"])
            if ident is not None:
                ident = self._canonical(ident)
            if ident is None or ident not in self._identities:
                ident = self._reid_match(o, claims, frame_id)
            if ident is None:
                ident = self._new_identity()
            self._track_to_identity[o["track_id"]] = ident
            prev = claims.get(ident)
            if prev is None or _better(o, prev):
                claims[ident] = o
            canon_tids.setdefault(ident, []).append(o["track_id"])

        # ---- 2. 同帧空间去重（粘性身份合并） ----
        # 排序键：置信度高者优先，并列取 identity_id 小者 —— 规范身份 = 先到者
        order = sorted(claims.keys(), key=lambda i: (-claims[i]["score"], i))
        kept: List[int] = []
        n_suppressed = 0
        for ident in order:
            absorbed = False
            for k in kept:
                if _same_person_in_frame(claims[k], claims[ident], self.cfg):
                    self._merge_identity(ident, k)
                    n_suppressed += 1
                    self.stats.duplicates_suppressed += 1
                    absorbed = True
                    break
            if not absorbed:
                kept.append(ident)

        # ---- 3. 更新规范身份的本帧观测 ----
        present_canon: List[int] = []
        for ident in sorted(claims.keys()):
            if self._canonical(ident) != ident:
                continue
            o = claims[ident]
            rec = self._identities[ident]
            rec.last_frame = frame_id
            rec.last_foot = o["foot"]
            rec.last_roi_point = o["roi"]
            rec.last_box = o["box"]
            rec.last_score = o["score"]
            rec.last_track_id = o["track_id"]
            if o["observed"]:
                rec.last_observed = frame_id
            present_canon.append(ident)

        # 合并（吸收）后，把轨迹 ID 列表归到规范身份上
        merged_tids: Dict[int, List[int]] = {}
        for ident in sorted(canon_tids.keys()):
            canon = self._canonical(ident)
            merged_tids.setdefault(canon, []).extend(canon_tids[ident])
        for canon in merged_tids:
            merged_tids[canon] = sorted(merged_tids[canon])

        # ---- 4. ROI 状态机（滞回 + 最短驻留） ----
        events: List[RoiEvent] = []
        roi_enabled = len(self._poly) >= 3
        counted: List[int] = []
        for canon in present_canon:
            o = claims[canon]
            if not (o["confirmed"] or self.cfg.count_tentative):
                continue
            # 本帧真正关联上（observed），或最后一次关联距今不超过 coast_frames
            rec = self._identities[canon]
            if o["observed"] or frame_id - rec.last_observed <= self.cfg.coast_frames:
                counted.append(canon)
        counted_set = set(counted)

        for ident in sorted(self._identities.keys()):
            if self._canonical(ident) != ident:
                continue
            rec = self._identities[ident]
            if rec.last_frame < 0:
                continue
            if roi_enabled and ident in present_canon and ident in counted_set:
                sd = polygon_signed_distance(rec.last_roi_point, self._poly)
            else:
                sd = float("-inf")      # 未确认 / 缺席 -> 按 outside 计
            self._step_roi(rec, sd, frame_id, events)

        # ---- 5. 回收长期不出现的身份记录 ----
        self._prune(frame_id)

        # ---- 6. 统计 + 组装 ----
        persons: List[PersonState] = []
        for canon in counted:
            o = claims[canon]
            rec = self._identities[canon]
            persons.append(PersonState(
                identity_id=canon, track_ids=list(merged_tids.get(canon, [])),
                box=o["box"], score=o["score"], foot=o["foot"], roi_point=o["roi"],
                in_roi=rec.state_in, dwell=rec.dwell, confirmed=o["confirmed"]))

        n = len(persons)
        dangerous_now = sum(
            1 for i in sorted(self._identities)
            if self._canonical(i) == i
            and self._identities[i].last_frame >= 0
            and self._identities[i].state_in)

        st = self.stats
        st.frames += 1
        st.total_legacy_count += n_dets
        st.total_spatial_count += n_spatial
        st.total_dedup_count += n
        if n_dets > st.max_legacy_count:
            st.max_legacy_count = n_dets
        if n > st.max_dedup_count:
            st.max_dedup_count = n
        if n_dets >= self.cfg.min_danger_n:
            st.frames_legacy_gate_open += 1
        if n >= self.cfg.min_danger_n:
            st.frames_dedup_gate_open += 1

        return CountResult(
            frame_id=frame_id, n=n, n_detections=n_dets,
            n_spatial_dedup=n_spatial, n_tracks=n_tracks,
            n_suppressed=n_suppressed, persons=persons, events=events,
            dangerous_now=dangerous_now, visitors_total=self._visitors_total,
            gate_n=n, gate_ok=n >= self.cfg.min_danger_n,
            legacy_gate_ok=n_dets >= self.cfg.min_danger_n, stats=st)

    # ------------------------------------------------------------------
    # 内部
    # ------------------------------------------------------------------
    def _canonical(self, ident: int) -> int:
        guard = 0
        while ident in self._merge_into:
            ident = self._merge_into[ident]
            guard += 1
            if guard > 64:               # 防御：合并链异常时截断
                break
        return ident

    def _new_identity(self) -> int:
        ident = self._next_identity
        self._next_identity += 1
        self._identities[ident] = _Identity(ident)
        return ident

    def _reid_match(self, o: dict, claims: Dict[int, dict],
                    frame_id: int) -> Optional[int]:
        """为没有映射的新 track_id 找一个"刚丢失的"身份（ID 切换合并）。"""
        if self.cfg.reid_radius <= 0.0 or self.cfg.reid_max_gap <= 0:
            return None
        best_id = None
        best_d2 = None
        fx, fy = o["foot"]
        r2 = self.cfg.reid_radius * self.cfg.reid_radius
        for ident in sorted(self._identities.keys()):
            if self._canonical(ident) != ident:
                continue
            if ident in claims:                      # 本帧已被别的轨迹占用
                continue
            rec = self._identities[ident]
            if rec.last_frame < 0:
                continue
            gap = frame_id - rec.last_frame
            if gap <= 0 or gap > self.cfg.reid_max_gap:
                continue
            dx, dy = fx - rec.last_foot[0], fy - rec.last_foot[1]
            d2 = dx * dx + dy * dy
            if d2 > r2:
                continue
            if best_d2 is None or d2 < best_d2 or (d2 == best_d2 and ident < best_id):
                best_id = ident
                best_d2 = d2
        if best_id is not None:
            self.stats.id_switch_merges += 1
        return best_id

    def _merge_identity(self, absorbed: int, canon: int) -> None:
        """把 absorbed 身份并入 canon（粘性）。ROI 状态取更接近 IN 的一侧。"""
        if absorbed == canon:
            return
        a = self._identities[absorbed]
        c = self._identities[canon]
        if a.state_in and not c.state_in:
            c.state_in = True
            c.dwell = a.dwell
            c.pending_in = a.pending_in
            c.pending_cnt = a.pending_cnt
            c.since_frame = a.since_frame
        elif a.state_in and c.state_in and a.dwell > c.dwell:
            c.dwell = a.dwell
        if a.last_frame > c.last_frame:
            c.last_frame = a.last_frame
            c.last_foot = a.last_foot
            c.last_roi_point = a.last_roi_point
        self._merge_into[absorbed] = canon
        # 已登记到被吸收身份的轨迹改指规范身份，保持 identity_of 直观
        for t, i in list(self._track_to_identity.items()):
            if self._canonical(i) == canon:
                self._track_to_identity[t] = canon

    def _step_roi(self, rec: _Identity, sd: float, frame_id: int,
                  events: List[RoiEvent]) -> None:
        """单身份 ROI 状态机（滞回 + 最短驻留）。"""
        cfg = self.cfg
        raw_in = (sd > -cfg.roi_hysteresis) if rec.state_in else (sd >= cfg.roi_hysteresis)
        if raw_in == rec.state_in:
            rec.dwell += 1
            rec.pending_cnt = 0
            return
        if rec.pending_cnt > 0 and rec.pending_in == raw_in:
            rec.pending_cnt += 1
        else:
            rec.pending_in = raw_in
            rec.pending_cnt = 1
            rec.since_frame = frame_id
        if rec.pending_cnt >= cfg.min_dwell_frames:
            rec.state_in = raw_in
            rec.dwell = rec.pending_cnt
            rec.pending_cnt = 0
            if raw_in:
                self._visitors_total += 1
            events.append(RoiEvent(
                frame_id=frame_id, identity_id=rec.identity_id,
                track_id=rec.last_track_id, entering=raw_in,
                since_frame=rec.since_frame, signed_distance=sd))

    def _prune(self, frame_id: int) -> None:
        ttl = self.cfg.identity_ttl
        if ttl <= 0:
            return
        alias_targets = set(self._merge_into.values())
        dead: List[int] = []
        for ident in sorted(self._identities.keys()):
            if self._canonical(ident) != ident:
                continue
            if ident in alias_targets:
                continue
            rec = self._identities[ident]
            if rec.state_in or rec.pending_cnt > 0:
                continue
            if rec.last_frame >= 0 and frame_id - rec.last_frame <= ttl:
                continue
            dead.append(ident)
        for ident in dead:
            del self._identities[ident]
            for t in [t for t, i in self._track_to_identity.items()
                      if self._canonical(i) == ident]:
                del self._track_to_identity[t]
            self._merge_into.pop(ident, None)


# ----------------------------------------------------------------------
# 小工具
# ----------------------------------------------------------------------
def _better(a: dict, b: dict) -> bool:
    """同一身份在本帧的代表观测选择：已确认 > 本帧真关联上 > 置信度高 > track_id 小。

    "本帧真关联上"（observed）优先很关键：一个身份下同时有滑行轨迹和外推轨迹时，
    代表观测必须取真正看到的那条，否则 misses / 位置会用到过期值。
    """
    if a["confirmed"] != b["confirmed"]:
        return a["confirmed"]
    if a["observed"] != b["observed"]:
        return a["observed"]
    if a["score"] != b["score"]:
        return a["score"] > b["score"]
    return a["track_id"] < b["track_id"]


def _same_person_in_frame(a: dict, b: dict,
                          cfg: "TrackCounterConfig") -> bool:
    """同帧两条轨迹是否"同一个人"（重复计数判据），两个独立分支取或：

    分支 1（脚点，米制）：foot 距离 <= dedup_tol（默认 2cm），与 GraphLaplacian
        的 DEDUP_TOL_M 同口径，覆盖"几乎重叠的两个框"。
    分支 2（脚点放宽，米制）：foot 距离 <= dup_foot_max（默认 20cm）—— 两个
        成年人的地面脚点相距 20cm 以内等同同一人，覆盖不同 anchor 尺度导致的
        框底不一致。
    分支 3（框包含，像素）：IoMin >= dup_iou_min 且高度比 <= dup_height_ratio
        且两个框中心互落在对方框内。覆盖"框底差得远但身体区域高度重叠"的重复框，
        同时用高度比排除"远处小人落在近处大人框内"。
    """
    fa, fb = a["foot"], b["foot"]
    dx, dy = fa[0] - fb[0], fa[1] - fb[1]
    d2 = dx * dx + dy * dy
    if cfg.dedup_tol > 0.0 and d2 <= cfg.dedup_tol * cfg.dedup_tol:
        return True
    if cfg.dup_foot_max > 0.0 and d2 <= cfg.dup_foot_max * cfg.dup_foot_max:
        return True
    if cfg.dup_iou_min <= 0.0:
        return False
    ba, bb = a["box"], b["box"]
    ha = ba[3] - ba[1]
    hb = bb[3] - bb[1]
    if ha <= 0.0 or hb <= 0.0:
        return False
    ratio = (ha / hb) if ha >= hb else (hb / ha)
    if ratio > cfg.dup_height_ratio:
        return False
    ix1 = max(ba[0], bb[0])
    iy1 = max(ba[1], bb[1])
    ix2 = min(ba[2], bb[2])
    iy2 = min(ba[3], bb[3])
    inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    area_a = max(0.0, ba[2] - ba[0]) * max(0.0, ba[3] - ba[1])
    area_b = max(0.0, bb[2] - bb[0]) * max(0.0, bb[3] - bb[1])
    min_area = area_a if area_a <= area_b else area_b
    if min_area <= 0.0:
        return False
    if inter / min_area < cfg.dup_iou_min:
        return False
    ca = ((ba[0] + ba[2]) * 0.5, (ba[1] + ba[3]) * 0.5)
    cb = ((bb[0] + bb[2]) * 0.5, (bb[1] + bb[3]) * 0.5)
    in_a = ba[0] <= cb[0] <= ba[2] and ba[1] <= cb[1] <= ba[3]
    in_b = bb[0] <= ca[0] <= bb[2] and bb[1] <= ca[1] <= bb[3]
    return bool(in_a and in_b)


def _spatial_dedup_count(obs: List[dict], tol: float) -> int:
    """只做同帧空间去重（不跨帧）的数量 —— 中间口径，用于消融对比。"""
    if tol <= 0.0 or len(obs) <= 1:
        return len(obs)
    order = sorted(obs, key=lambda o: (-o["score"], o["track_id"]))
    kept: List[Tuple[float, float]] = []
    r2 = tol * tol
    for o in order:
        fx, fy = o["foot"]
        dup = False
        for kx, ky in kept:
            dx, dy = fx - kx, fy - ky
            if dx * dx + dy * dy <= r2:
                dup = True
                break
        if not dup:
            kept.append((fx, fy))
    return len(kept)
