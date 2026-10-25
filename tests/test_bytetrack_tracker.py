# -*- coding: utf-8 -*-
"""ByteTrack 多目标跟踪器单元测试。

测试使用**合成的多目标检测框轨迹**（纯测试数据，非真实传感器 / 检测器输出）：
- 目标 A、B 在图像平面匀速运动，框宽高固定；
- 在第 20~24 帧目标 A 被遮挡，仅产生**低置信度**检测框（模拟部分可见）；
- 验证：全程保持 2 条轨迹、遮挡前后目标 A 的 ID 不漂移，
  且低分框二次关联确实在遮挡期间维持了轨迹更新。

运行方式（在项目根目录）：
    python -m pytest tests/test_bytetrack_tracker.py -v
    或
    python tests/test_bytetrack_tracker.py
"""

import os
import sys

import numpy as np

# 保证可从项目根目录导入 src.algorithms.*
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from src.algorithms.bytetrack_tracker import (
    ByteTrack,
    associate_detections_to_tracks,
    compute_iou_matrix,
    iou,
    linear_sum_assignment,
)


# ----------------------------------------------------------------------
# 工具函数：合成测试数据
# ----------------------------------------------------------------------
def make_box(cx, cy, w=40.0, h=80.0):
    """以中心 (cx, cy)、宽 w、高 h 构造 xyxy 检测框。"""
    return [cx - w / 2.0, cy - h / 2.0, cx + w / 2.0, cy + h / 2.0]


def make_occlusion_scene(n_frames=60, occl_start=20, occl_end=24):
    """生成双行人合成的检测序列。

    目标 A 从 (100, 100) 以 3 px/帧 匀速右移；
    目标 B 从 (200, 200) 以 4 px/帧 匀速右移。
    在 ``[occl_start, occl_end]`` 帧，A 被遮挡，检测器对 A 的置信度大幅下降
    （conf=0.25，模拟部分可见 / 不确定），但框位置仍大致正确；
    其余帧 A 为高分框（conf=0.9），B 恒为高分（0.85）。

    返回
    ----
    list[list[(x1, y1, x2, y2, conf)]]：每帧的检测框列表。
    """
    scene = []
    for f in range(n_frames):
        dets = []
        cx_a = 100.0 + 3.0 * f
        cy_a = 100.0
        cx_b = 200.0 + 4.0 * f
        cy_b = 200.0
        if occl_start <= f <= occl_end:
            # A 被遮挡：仅低分，但框位置大致正确
            dets.append(tuple(make_box(cx_a, cy_a)) + (0.25,))
        else:
            dets.append(tuple(make_box(cx_a, cy_a)) + (0.90,))
        dets.append(tuple(make_box(cx_b, cy_b)) + (0.85,))
        scene.append(dets)
    return scene


def run_scene(scene, **kwargs):
    """把合成场景送入 ByteTrack，返回每帧的轨迹列表与轨迹信息。"""
    defaults = dict(
        track_thresh=0.5,
        det_thresh=0.1,
        match_thresh=0.8,
        max_lost=30,
        min_hits=3,
    )
    defaults.update(kwargs)
    tracker = ByteTrack(**defaults)
    per_frame = []
    per_frame_info = []
    for f, dets in enumerate(scene):
        per_frame.append(tracker.update(dets, frame_id=f))
        per_frame_info.append(tracker.track_info())
    return per_frame, per_frame_info, tracker


def track_centroid(tracks, tid):
    """取某轨迹 ID 在当前帧的框中心 x 坐标；不存在返回 None。"""
    for t in tracks:
        if t[0] == tid:
            return (t[1] + t[3]) / 2.0
    return None


# ----------------------------------------------------------------------
# IoU 工具
# ----------------------------------------------------------------------
class TestIoU:
    def test_iou_overlap(self):
        assert abs(iou([0, 0, 10, 10], [5, 5, 15, 15]) - 25.0 / 175.0) < 1e-9

    def test_iou_no_overlap(self):
        assert iou([0, 0, 10, 10], [20, 20, 30, 30]) == 0.0

    def test_iou_identical(self):
        assert abs(iou([0, 0, 10, 10], [0, 0, 10, 10]) - 1.0) < 1e-9

    def test_iou_matrix(self):
        a = np.array([[0, 0, 10, 10], [20, 20, 30, 30]])
        b = np.array([[5, 5, 15, 15]])
        m = compute_iou_matrix(a, b)
        assert m.shape == (2, 1)
        assert abs(m[0, 0] - 25.0 / 175.0) < 1e-9
        assert m[1, 0] == 0.0


# ----------------------------------------------------------------------
# 匈牙利算法与关联
# ----------------------------------------------------------------------
class TestHungarian:
    def test_matches_bruteforce(self):
        """与暴力枚举最优解对比（小规模随机代价矩阵）。"""
        rng = np.random.default_rng(0)
        for n in range(1, 7):
            for _ in range(20):
                cost = rng.uniform(0, 1, size=(n, n))
                assign = linear_sum_assignment(cost)
                my_cost = sum(cost[i, j] for i, j in assign)
                import itertools

                best = min(
                    sum(cost[i, p[i]] for i in range(n))
                    for p in itertools.permutations(range(n))
                )
                assert abs(my_cost - best) < 1e-8

    def test_identity_cost(self):
        """对角代价为 0 时应选对角线。"""
        cost = np.array([[0.0, 5.0], [5.0, 0.0]])
        assign = linear_sum_assignment(cost)
        assert sorted(assign) == [(0, 0), (1, 1)]


class TestAssociation:
    def test_associate_basic(self):
        dets = np.array([[0, 0, 10, 10], [50, 50, 60, 60]], dtype=np.float64)
        trks = np.array([[1, 1, 11, 11], [49, 49, 59, 59]], dtype=np.float64)
        matches, u_det, u_trk = associate_detections_to_tracks(dets, trks, thresh=0.8)
        assert len(matches) == 2
        assert u_det == [] and u_trk == []

    def test_associate_unmatched(self):
        """所有配对 IoU 过低时应全部判为未匹配。"""
        dets = np.array([[0, 0, 10, 10]], dtype=np.float64)
        trks = np.array([[100, 100, 110, 110]], dtype=np.float64)
        matches, u_det, u_trk = associate_detections_to_tracks(dets, trks, thresh=0.8)
        assert matches == []
        assert u_det == [0] and u_trk == [0]

    def test_associate_empty(self):
        matches, u_det, u_trk = associate_detections_to_tracks(
            np.empty((0, 4)), np.array([[0, 0, 10, 10]], dtype=np.float64), thresh=0.8
        )
        assert matches == [] and u_det == [] and u_trk == [0]


# ----------------------------------------------------------------------
# ByteTrack 核心行为
# ----------------------------------------------------------------------
class TestByteTrack:
    def test_single_target_id_stable(self):
        """单目标匀速运动：只产生 1 条轨迹且 ID 稳定。"""
        scene = [[tuple(make_box(100 + 3 * f, 100)) + (0.9,)] for f in range(40)]
        per_frame, _, tracker = run_scene(scene)
        assert tracker.track_count() == 1
        first_id = per_frame[0][0][0]
        for f, tracks in enumerate(per_frame):
            assert [t[0] for t in tracks] == [first_id], f"第 {f} 帧 ID 漂移"

    def test_two_targets_no_id_drift_on_occlusion(self):
        """核心验收：遮挡期间与遮挡后 ID 保持稳定、不漂移。

        - 全程每帧应恰好输出 2 条轨迹；
        - 目标 A（左侧）在遮挡前（第 19 帧）与遮挡后（第 25 帧）应保持同一 ID。
        """
        scene = make_occlusion_scene()
        per_frame, _, tracker = run_scene(scene)

        # 每帧恰好 2 条轨迹（遮挡帧依赖低分二次关联维持）
        counts = [len(t) for t in per_frame]
        assert all(c == 2 for c in counts), f"轨迹数不稳定: {counts}"

        # 目标 A 的 ID：第 19 帧存在、第 25 帧仍存在，且 x 中心靠左
        ids_19 = {t[0] for t in per_frame[19]}
        ids_25 = {t[0] for t in per_frame[25]}
        a_ids = [
            tid
            for tid in ids_19
            if tid in ids_25 and track_centroid(per_frame[19], tid) < 200
        ]
        assert len(a_ids) == 1, f"目标 A 的 ID 漂移: {a_ids}"
        a_id = a_ids[0]

        # 遮挡后若干帧内 A 的 ID 持续存在
        for f in range(26, 50):
            assert a_id in {t[0] for t in per_frame[f]}, f"第 {f} 帧 A 的 ID 丢失"

    def test_low_score_second_association_updates_track(self):
        """低分框二次关联在遮挡期间确实更新了轨迹。

        遮挡帧（20~24）A 只有低分检测，若二次关联生效，A 轨迹的 hits 应持续
        增长、score 反映低分置信度、misses 保持为 0。
        """
        scene = make_occlusion_scene()
        per_frame, infos, _ = run_scene(scene)

        # 第 19 帧 A 的 ID
        ids_19 = {t[0] for t in per_frame[19]}
        a_id = next(
            tid for tid in ids_19 if track_centroid(per_frame[19], tid) < 200
        )
        info_19 = infos[19][a_id]
        info_25 = infos[25][a_id]

        # 遮挡期间（20~24）A 应持续被关联：misses 在第 25 帧仍为 0
        assert info_25["misses"] == 0, "遮挡期间 A 未被低分二次关联更新"
        # hits 应增加（遮挡 5 帧 + 正常帧）
        assert info_25["hits"] > info_19["hits"], "遮挡期间 hits 未增长"
        # 第 24 帧 A 的 score 应为低分 0.25
        score_24 = None
        for t in per_frame[24]:
            if t[0] == a_id:
                score_24 = t[5]
        assert abs(score_24 - 0.25) < 1e-9, f"A 遮挡帧 score 应为 0.25，得到 {score_24}"

    def test_track_removed_after_max_lost(self):
        """目标完全消失超过 max_lost 帧后，轨迹被删除。"""
        scene = [[tuple(make_box(100 + 3 * f, 100)) + (0.9,)] for f in range(10)]
        _, _, tracker = run_scene(scene, max_lost=5)
        assert tracker.track_count() == 1
        # 之后 6 帧无任何检测（max_lost=5），第 6 帧应被清除
        for _ in range(6):
            tracker.update([], frame_id=100 + _)
        assert tracker.track_count() == 0

    def test_new_object_creates_new_track(self):
        """新目标出现时创建新轨迹，ID 递增分配。"""
        tracker = ByteTrack()
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=2)
        tracker.update(
            [tuple(make_box(100, 100)) + (0.9,), tuple(make_box(300, 100)) + (0.8,)],
            frame_id=3,
        )
        ids = {t[0] for t in tracker.states()}
        assert len(ids) == 2

    # ---------------- 接口与参数校验 ----------------
    def test_output_format(self):
        """输出为 list[track_id, x1, y1, x2, y2, conf]，按 ID 升序。"""
        tracker = ByteTrack()
        tracks = tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        assert isinstance(tracks, list)
        t = tracks[0]
        assert len(t) == 6
        assert isinstance(t[0], int)
        assert all(isinstance(v, float) for v in t[1:])
        assert t[5] == 0.9
        # 多轨迹按 ID 升序
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=2)
        tracks = tracker.update(
            [
                tuple(make_box(100, 100)) + (0.9,),
                tuple(make_box(300, 100)) + (0.8,),
            ],
            frame_id=3,
        )
        ids = [t[0] for t in tracks]
        assert ids == sorted(ids)

    def test_frame_id_explicit(self):
        """显式 frame_id 应被记录。"""
        tracker = ByteTrack()
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=42)
        info = tracker.track_info()
        tid = next(iter(info))
        assert info[tid]["last_seen"] == 42
        assert info[tid]["start_frame"] == 42

    def test_track_info_fields(self):
        tracker = ByteTrack()
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        info = tracker.track_info()
        assert set(info[next(iter(info))].keys()) >= {
            "bbox",
            "score",
            "hits",
            "misses",
            "age",
            "last_seen",
            "start_frame",
            "stable",
            "confirmed",
        }

    def test_reset(self):
        tracker = ByteTrack()
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        assert tracker.track_count() == 1
        tracker.reset()
        assert tracker.track_count() == 0
        # 重置后 ID 重新从 0 分配
        tracks = tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        assert tracks[0][0] == 0

    def test_parameter_validation(self):
        def expect_value_error(**kwargs):
            try:
                ByteTrack(**kwargs)
            except ValueError:
                return
            raise AssertionError(f"参数 {kwargs} 应抛出 ValueError")

        expect_value_error(track_thresh=0.0)
        expect_value_error(det_thresh=0.6, track_thresh=0.5)  # det >= track
        expect_value_error(match_thresh=0.0)
        expect_value_error(max_lost=0)
        expect_value_error(min_hits=0)

    def test_empty_detections_predicts(self):
        """整帧无检测时，已有轨迹应继续外推输出。"""
        tracker = ByteTrack(max_lost=10)
        tracker.update([tuple(make_box(100, 100)) + (0.9,)], frame_id=1)
        for f in range(2, 6):
            tracks = tracker.update([], frame_id=f)
            assert tracker.track_count() == 1, f"第 {f} 帧轨迹丢失"
            assert len(tracks) == 1


# ----------------------------------------------------------------------
# 独立运行入口
# ----------------------------------------------------------------------
if __name__ == "__main__":
    import traceback

    classes = [
        TestIoU,
        TestHungarian,
        TestAssociation,
        TestByteTrack,
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