# -*- coding: utf-8 -*-
"""track_counter 语义测试 + 端到端"重复计数"量化（ByteTrack -> TrackCounter）。

覆盖（对应 DoD）：
  - 几何：点在多边形 / 有符号距离（ROI 滞回的基础）；
  - 同一轨迹跨帧只计 1 人；未确认（tentative）不计；滑行帧（coast_frames）语义；
  - ID 切换 / 短时丢失的 re-id 合并；同帧重复框的粘性合并；
  - 误并防护：两个真人不合并（并排同尺寸 / 远小近大）；
  - ROI 进出事件：滞回 + 最短驻留 + 一次访问只计一次 + 重复身份不拆成两次；
  - min_danger_n 门控交互（旧口径 vs 去重口径）；
  - 端到端量化：旧口径（检测框数 / states() 条数）vs 去重后人数。

数据均为合成框（纯测试数据，非真实检测器输出）。
"""
import os
import sys

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, ROOT)

from src.algorithms.bytetrack_tracker import ByteTrack  # noqa: E402
from src.algorithms.track_counter import (  # noqa: E402
    CountResult,
    FrameObs,
    TrackCounter,
    TrackCounterConfig,
    TrackObs,
    point_in_polygon,
    polygon_signed_distance,
)

# ----------------------------------------------------------------------
# 工具
# ----------------------------------------------------------------------
ROI_PX = [(400.0, 50.0), (600.0, 50.0), (600.0, 450.0), (400.0, 450.0)]
M_PER_PX = 0.01          # 测试用比例：1 像素 = 1cm（使 2cm/20cm 阈值在像素场景下可解释）
BT_PARAMS = dict(track_thresh=0.5, det_thresh=0.1, match_thresh=0.8,
                 max_lost=30, min_hits=3)


def box(cx, w=40.0, h=200.0, foot_y=300.0):
    return [cx - w / 2.0, foot_y - h, cx + w / 2.0, foot_y]


def cfg(**kw):
    base = dict(roi_polygon=ROI_PX, roi_hysteresis=8.0, min_dwell_frames=3,
                min_danger_n=3, dedup_tol=0.02, dup_foot_max=0.20,
                dup_iou_min=0.75, dup_height_ratio=1.5, reid_radius=0.6,
                reid_max_gap=15, coast_frames=1)
    base.update(kw)
    return TrackCounterConfig(**base)


def mk_obs(track_id, b, score=0.9, confirmed=True, misses=0, scale=M_PER_PX):
    """由像素框构造 TrackObs：脚点/ROI 点按 scale 转到"米"（脚点）与像素（ROI）。"""
    cx = (b[0] + b[2]) * 0.5
    foot = (cx * scale, b[3] * scale)
    return TrackObs(track_id, b, score, confirmed, misses, foot=foot,
                    roi_point=(cx, (b[1] + b[3]) * 0.5))


def run_pipeline(scene, tc_cfg=None, bt_params=None, dets_fn=None):
    """ByteTrack -> TrackCounter 端到端：scene[i] 为第 i 帧检测框列表。

    返回 (tracker, counter, rows)；rows[i] = (dets, bt_states, count_result,
    legacy_roi_count)。
    """
    bt = ByteTrack(**(bt_params or BT_PARAMS))
    tc = TrackCounter(tc_cfg or cfg())
    rows = []
    for i, dets in enumerate(scene):
        states = bt.update(dets)
        info = bt.track_info()
        obs = [mk_obs(s[0], (s[1], s[2], s[3], s[4]), s[5],
                      info[s[0]]["confirmed"], info[s[0]]["misses"])
               for s in states]
        r = tc.update(FrameObs(i + 1, obs, n_detections=len(dets)))
        # 旧口径（main.cc 现行为）：逐检测框判 ROI 中心，重复框重复计
        legacy_roi = 0
        for d in dets:
            cx = (d[0] + d[2]) * 0.5
            cy = (d[1] + d[3]) * 0.5
            if point_in_polygon((cx, cy), ROI_PX):
                legacy_roi += 1
        rows.append((dets, states, r, legacy_roi))
    return bt, tc, rows


# ----------------------------------------------------------------------
# 几何
# ----------------------------------------------------------------------
class TestGeometry:
    def test_point_in_polygon(self):
        assert point_in_polygon((500.0, 200.0), ROI_PX)
        assert not point_in_polygon((300.0, 200.0), ROI_PX)
        assert not point_in_polygon((500.0, 20.0), ROI_PX)
        assert not point_in_polygon((500.0, 200.0), [(0.0, 0.0), (1.0, 1.0)])

    def test_signed_distance_sign_and_magnitude(self):
        # 矩形内距左边界 100px
        assert abs(polygon_signed_distance((500.0, 200.0), ROI_PX) - 100.0) < 1e-9
        # 矩形外距右边界 50px
        assert abs(polygon_signed_distance((650.0, 200.0), ROI_PX) + 50.0) < 1e-9
        assert polygon_signed_distance((0.0, 0.0), []) == float("-inf")


# ----------------------------------------------------------------------
# 配置校验
# ----------------------------------------------------------------------
class TestConfig:
    @pytest.mark.parametrize("kw", [
        {"min_dwell_frames": 0},
        {"reid_max_gap": -1},
        {"identity_ttl": -1},
        {"min_danger_n": 0},
        {"coast_frames": -1},
    ])
    def test_invalid_config_raises(self, kw):
        with pytest.raises(ValueError):
            TrackCounter(cfg(**kw))


# ----------------------------------------------------------------------
# 基本计数语义
# ----------------------------------------------------------------------
class TestBasicCounting:
    def test_same_track_many_frames_counts_once(self):
        tc = TrackCounter(cfg())
        for f in range(30):
            b = box(100 + 4 * f)
            r = tc.update(FrameObs(f + 1, [mk_obs(0, b)], n_detections=1))
            assert r.n == 1, "第 %d 帧应只计 1 人（实测 %d）" % (f + 1, r.n)
        assert tc.identity_count() == 1
        assert tc.identity_of(0) == 0

    def test_two_people_count_two(self):
        tc = TrackCounter(cfg())
        for f in range(10):
            obs = [mk_obs(0, box(100 + 3 * f)), mk_obs(1, box(500 - 3 * f))]
            r = tc.update(FrameObs(f + 1, obs, n_detections=2))
            assert r.n == 2

    def test_tentative_not_counted(self):
        tc = TrackCounter(cfg())
        r1 = tc.update(FrameObs(1, [mk_obs(0, box(100), confirmed=False)],
                                n_detections=1))
        assert r1.n == 0, "未确认轨迹不应计入人数"
        assert r1.person_ids() == []
        # 同一条轨迹随后确认 -> 计入；身份沿用，不新建
        r2 = tc.update(FrameObs(2, [mk_obs(0, box(104), confirmed=True)],
                                n_detections=1))
        assert r2.n == 1 and tc.identity_count() == 1
        # count_tentative=True 时未确认也计
        tc2 = TrackCounter(cfg(count_tentative=True))
        assert tc2.update(FrameObs(1, [mk_obs(0, box(100), confirmed=False)],
                                   n_detections=1)).n == 1

    def test_coast_frames_limits_ghost_count(self):
        """轨迹滑行（misses>0）只允许 coast_frames 帧仍计入，避免出画后人数滞留。"""
        tc = TrackCounter(cfg(coast_frames=1))
        b = box(100)
        assert tc.update(FrameObs(1, [mk_obs(0, b, misses=0)], 1)).n == 1
        assert tc.update(FrameObs(2, [mk_obs(0, b, misses=1)], 1)).n == 1, "滑行 1 帧仍计"
        assert tc.update(FrameObs(3, [mk_obs(0, b, misses=2)], 1)).n == 0, "滑行 2 帧不再计"
        assert tc.update(FrameObs(4, [mk_obs(0, b, misses=0)], 1)).n == 1, "重现后恢复"

    def test_coast_frames_zero_is_strict(self):
        tc = TrackCounter(cfg(coast_frames=0))
        assert tc.update(FrameObs(1, [mk_obs(0, box(100), misses=0)], 1)).n == 1
        assert tc.update(FrameObs(2, [mk_obs(0, box(100), misses=1)], 1)).n == 0


# ----------------------------------------------------------------------
# 重复框 / ID 切换合并
# ----------------------------------------------------------------------
class TestDuplicateMerge:
    def test_concurrent_duplicate_boxes_merge(self):
        """同一人两个不同 anchor 尺度的框（脚点差 6px=6cm）：粘性合并为 1 个身份。"""
        tc = TrackCounter(cfg())
        r1 = tc.update(FrameObs(1, [mk_obs(0, box(150)), mk_obs(1, box(150, 44, 196, 302),
                                                                   score=0.8)], 2))
        assert r1.n == 1, "重复框应合并为 1 人（实测 %d）" % r1.n
        assert r1.n_suppressed == 1
        assert tc.identity_of(0) == tc.identity_of(1)
        for f in range(1, 20):
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(150 + 4 * f)),
                                           mk_obs(1, box(150 + 4 * f, 44, 196, 302),
                                                  score=0.8)], 2))
            assert r.n == 1, "第 %d 帧重复框泄漏（n=%d）" % (f + 1, r.n)
        assert tc.stats.duplicates_suppressed == 1, "粘性合并只应发生一次"

    def test_two_close_people_not_merged(self):
        """两个真人不许误并：并排同尺寸、框中心相距 40px（半个框宽）。"""
        tc = TrackCounter(cfg())
        r = tc.update(FrameObs(1, [mk_obs(0, box(300)), mk_obs(1, box(340))], 2))
        assert r.n == 2, "并排两人被误并（n=%d）" % r.n
        assert tc.identity_count() == 2

    def test_far_small_person_inside_near_big_box_not_merged(self):
        """远小近大：小框整落在大人框内，靠高度比挡住，不许并成 1 人。"""
        big = box(300, 120, 400, 420)        # 高 400
        small = box(300, 40, 120, 260)       # 高 120，高度比 3.3 > 1.5
        tc = TrackCounter(cfg())
        r = tc.update(FrameObs(1, [mk_obs(0, big), mk_obs(1, small)], 2))
        assert r.n == 2, "远小近大被误并（n=%d）" % r.n

    def test_reid_merge_on_id_switch(self):
        """ByteTrack 丢失后重现分配新 ID：re-id 合并成同一身份，人数不涨。"""
        tc = TrackCounter(cfg())
        for f in range(5):
            tc.update(FrameObs(f + 1, [mk_obs(0, box(200 + 4 * f))], 1))
        # 第 6~10 帧丢失（无轨迹），第 11 帧以新 ID 1 在附近重现
        for f in range(5, 10):
            tc.update(FrameObs(f + 1, [], 0))
        r = tc.update(FrameObs(11, [mk_obs(1, box(260))], 1))
        assert r.n == 1, "ID 切换后应仍只算 1 人（实测 %d）" % r.n
        assert tc.identity_of(0) == tc.identity_of(1)
        assert tc.stats.id_switch_merges == 1

    def test_reid_respects_max_gap(self):
        """丢失超过 reid_max_gap 帧后不再合并（避免把两个人粘成一个）。"""
        tc = TrackCounter(cfg(reid_max_gap=5))
        tc.update(FrameObs(1, [mk_obs(0, box(200))], 1))
        for f in range(2, 12):
            tc.update(FrameObs(f, [], 0))
        r = tc.update(FrameObs(12, [mk_obs(1, box(200))], 1))
        assert r.n == 1, "人数应为 1（新身份）"
        assert tc.identity_of(0) != tc.identity_of(1)
        assert tc.stats.id_switch_merges == 0

    def test_reid_radius_disabled(self):
        tc = TrackCounter(cfg(reid_radius=0.0))
        tc.update(FrameObs(1, [mk_obs(0, box(200))], 1))
        tc.update(FrameObs(2, [], 0))
        tc.update(FrameObs(3, [mk_obs(1, box(200))], 1))
        assert tc.identity_of(0) != tc.identity_of(1)

    def test_identity_ttl_reclaims_records(self):
        tc = TrackCounter(cfg(identity_ttl=3, reid_radius=0.0))
        tc.update(FrameObs(1, [mk_obs(0, box(200))], 1))
        for f in range(2, 8):
            tc.update(FrameObs(f, [], 0))
        assert tc.identity_count() == 0, "OUT 且长期不出现的身份记录应被回收"


# ----------------------------------------------------------------------
# ROI 进出事件（滞回 + 最短驻留）
# ----------------------------------------------------------------------
class TestRoiEvents:
    def test_enter_leave_single_event(self):
        """一人从 x=100 走到 x=700 穿过 ROI：恰好 1 次 ENTER + 1 次 LEAVE。"""
        tc = TrackCounter(cfg())
        kinds = []
        for f in range(42):
            cx = 100 + 15 * f
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(cx))], 1))
            kinds.extend(e.kind for e in r.events)
        assert kinds == ["ENTER", "LEAVE"], kinds
        assert tc.stats.frames == 42

    def test_hysteresis_prevents_boundary_flapping(self):
        """已在 ROI 内的人贴边界抖动：不产生重复 ENTER/LEAVE。"""
        tc = TrackCounter(cfg())
        events = []
        for f in range(30):
            if f < 5:
                cx = 500.0                      # 深处内部
            else:
                cx = 600.0 + (12.0 if f % 2 == 0 else -2.0)   # 边界内外抖动
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(cx))], 1))
            events.extend(e.kind for e in r.events)
        assert events == ["ENTER"], "边界抖动产生了重复事件：%r" % (events,)

    def test_min_dwell_delays_commit(self):
        """穿越开始到事件提交之间恰好 min_dwell_frames 帧（事件带 since_frame）。"""
        tc = TrackCounter(cfg(min_dwell_frames=3, roi_hysteresis=8.0))
        entered = None
        for f in range(20):
            cx = 380 + 20 * f
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(cx))], 1))
            if r.events:
                entered = r.events[0]
                break
        assert entered is not None and entered.kind == "ENTER"
        # sd>=8 需要 cx>=408；首帧候选 cx=420 起（frame 3），提交在第 3 个候选帧
        assert entered.frame_id - entered.since_frame == 2, (
            "提交帧与穿越起始帧应相差 min_dwell_frames-1 帧（实测 %d）"
            % (entered.frame_id - entered.since_frame))

    def test_jitter_below_dwell_never_commits(self):
        """进来了 1 帧又出去：不满足最短驻留，不产生任何事件。"""
        tc = TrackCounter(cfg())
        kinds = []
        for f, cx in enumerate([500.0, 300.0, 500.0, 300.0]):
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(cx))], 1))
            kinds.extend(e.kind for e in r.events)
        assert kinds == []

    def test_duplicate_identity_enters_once(self):
        """同一人两个框一起进 ROI：只产生 1 次 ENTER、危险人数只 +1。"""
        tc = TrackCounter(cfg())
        kinds = []
        for f in range(20):
            cx = 420 + 5 * f
            obs = [mk_obs(0, box(cx)), mk_obs(1, box(cx, 44, 196, 302), score=0.8)]
            r = tc.update(FrameObs(f + 1, obs, n_detections=2))
            kinds.extend(e.kind for e in r.events)
            assert r.dangerous_now <= 1, "危险人数被重复框放大：%d" % r.dangerous_now
        assert kinds == ["ENTER"], kinds
        assert tc.stats.duplicates_suppressed == 1

    def test_visitors_total_counts_visits_not_frames(self):
        """进->出->再进：visitors_total 计 2 次（同一身份两次访问），依然不重复计脏。"""
        tc = TrackCounter(cfg())
        visits = []
        seq = [500, 500, 500, 500, 700, 700, 700, 700, 700, 500, 500, 500, 500]
        for f, cx in enumerate(seq):
            r = tc.update(FrameObs(f + 1, [mk_obs(0, box(cx))], 1))
            visits.extend(e.kind for e in r.events)
        assert visits == ["ENTER", "LEAVE", "ENTER"], visits
        assert tc.update(FrameObs(len(seq) + 1, [mk_obs(0, box(500))], 1)).visitors_total == 2

    def test_leave_when_target_disappears(self):
        """人从画面消失：缺席帧按 outside 计，最短驻留后产生 LEAVE。"""
        tc = TrackCounter(cfg())
        for f in range(5):
            tc.update(FrameObs(f + 1, [mk_obs(0, box(500))], 1))
        events = []
        for f in range(5, 12):
            r = tc.update(FrameObs(f + 1, [], 0))
            events.extend(e.kind for e in r.events)
        assert events == ["LEAVE"], events


# ----------------------------------------------------------------------
# min_danger_n 门控 + 端到端量化
# ----------------------------------------------------------------------
class TestGateAndQuantification:
    def test_min_danger_n_gate_uses_dedup_count(self):
        """两人各被 3 个框检出：旧口径 6 人误开门，去重后 2 人不开门。"""
        scene = [[box(120 + 3 * f, 40, 200, 300) + [0.92],
                  box(120 + 3 * f, 44, 196, 302) + [0.80],
                  box(120 + 3 * f, 48, 190, 306) + [0.55],
                  box(500 + 3 * f, 40, 200, 300) + [0.91],
                  box(500 + 3 * f, 44, 196, 302) + [0.79],
                  box(500 + 3 * f, 48, 190, 306) + [0.54]]
                 for f in range(30)]
        tc_cfg = cfg(roi_polygon=[])       # 关 ROI，只看人数口径
        _bt, _tc, rows = run_pipeline(scene, tc_cfg=tc_cfg)
        assert all(len(states) == 6 for _d, states, _r, _l in rows), "每人 3 框应出 3 条轨迹"
        assert all(r.n == 2 for _d, _s, r, _l in rows), (
            "去重后应恒为 2 人（实测 %r）" % [r.n for _d, _s, r, _l in rows][:5])
        assert all(r.legacy_gate_ok for _d, _s, r, _l in rows), "旧口径应开门"
        assert not any(r.gate_ok for _d, _s, r, _l in rows), "去重后不应开门"
        st = _tc.stats
        assert st.frames_legacy_gate_open == 30 and st.frames_dedup_gate_open == 0
        assert st.max_legacy_count == 6 and st.max_dedup_count == 2

    def test_duplicate_counting_quantified(self):
        """量化：单人 3 框场景下 max 人数 3 -> 1，Σ 人数 60 -> 20（帧数 20）。"""
        scene = [[box(150 + 4 * f, 40, 200, 300) + [0.92],
                  box(150 + 4 * f, 44, 196, 302) + [0.80],
                  box(150 + 4 * f, 48, 190, 306) + [0.55]]
                 for f in range(20)]
        _bt, tc, rows = run_pipeline(scene, tc_cfg=cfg(roi_polygon=[]))
        st = tc.stats
        assert st.max_legacy_count == 3, st.max_legacy_count
        assert st.max_dedup_count == 1, st.max_dedup_count
        assert st.total_legacy_count == 60, st.total_legacy_count
        assert st.total_dedup_count == 20, st.total_dedup_count
        # 中间口径（只做同帧 2cm 脚点去重）兜不住 6cm 偏移，说明必须靠身份级判据
        assert st.total_spatial_count == 60, st.total_spatial_count

    def test_id_switch_overcount_quantified(self):
        """量化：丢失 12 帧后 ID 切换，旧口径 2 人 -> 去重后 1 人。"""
        scene = []
        for f in range(30):
            if f < 8:
                scene.append([box(100 + 10 * f) + [0.9]])
            elif f >= 20:
                scene.append([box(350) + [0.9]])
            else:
                scene.append([])
        _bt, tc, rows = run_pipeline(scene, tc_cfg=cfg(roi_polygon=[]))
        late = [r for _d, _s, r, _l in rows[26:]]
        assert all(r.n == 1 for r in late), [r.n for r in late]
        assert max(len(s) for _d, s, _r, _l in rows) == 2, "第 26 帧起旧轨迹仍在滑行"
        assert all(r.n_detections == 1 for r in late)

    def test_roi_dangerous_person_dedup(self):
        """OSD Dangerous Person：重复框在 ROI 内时旧口径 2 -> 去重后 1。"""
        scene = [[box(500) + [0.9], box(500, 44, 196, 302) + [0.8]] for _ in range(12)]
        _bt, _tc, rows = run_pipeline(scene)
        legacy = [l for _d, _s, _r, l in rows]
        dedup = [r.dangerous_now for _d, _s, r, _l in rows]
        assert max(legacy) == 2, legacy[:3]
        assert max(dedup) == 1, "去重后危险人数应恒为 1（实测 %r）" % dedup[:6]
        assert sum(dedup) < sum(legacy)


class TestReportTable:
    """给 docs/review/20261030_round3/track_counting.md 用的量化表（pytest -s 可见）。

    三个场景都是**合成框序列**（纯测试数据），口径：
      legacy   = 旧口径：当帧检测框数（main.cc 的 dangerPersons / 送进 IPM 的点数）；
      spatial  = 只做同帧 2cm 脚点去重（GraphLaplacian::DEDUP_TOL_M 的现有能力）；
      tracked  = 本模块：轨迹身份去重（跨帧 + 同帧空间判据 + re-id 合并）。
    """

    def _table(self, name, scene, tc_cfg=None):
        _bt, tc, rows = run_pipeline(scene, tc_cfg=tc_cfg)
        st = tc.stats
        legacy = [r.n_detections for _d, _s, r, _l in rows]
        spatial = [r.n_spatial_dedup for _d, _s, r, _l in rows]
        tracked = [r.n for _d, _s, r, _l in rows]
        states = [len(s) for _d, s, _r, _l in rows]
        return {
            "name": name, "frames": st.frames,
            "legacy_max": max(legacy), "spatial_max": max(spatial),
            "tracked_max": max(tracked),
            "states_max": max(states), "states_sum": sum(states),
            "legacy_sum": st.total_legacy_count,
            "spatial_sum": st.total_spatial_count,
            "tracked_sum": st.total_dedup_count,
            "gate_legacy": st.frames_legacy_gate_open,
            "gate_tracked": st.frames_dedup_gate_open,
            "dup_suppressed": st.duplicates_suppressed,
            "id_merges": st.id_switch_merges,
        }

    def test_report_table(self):
        one = [[box(150 + 4 * f, 40, 200, 300) + [0.92],
                box(150 + 4 * f, 44, 196, 302) + [0.80],
                box(150 + 4 * f, 48, 190, 306) + [0.55]] for f in range(20)]
        two = [[box(120 + 3 * f, 40, 200, 300) + [0.92],
                box(120 + 3 * f, 44, 196, 302) + [0.80],
                box(120 + 3 * f, 48, 190, 306) + [0.55],
                box(500 + 3 * f, 40, 200, 300) + [0.91],
                box(500 + 3 * f, 44, 196, 302) + [0.79],
                box(500 + 3 * f, 48, 190, 306) + [0.54]] for f in range(30)]
        switch = []
        for f in range(30):
            if f < 8:
                switch.append([box(100 + 10 * f) + [0.9]])
            elif f >= 20:
                switch.append([box(350) + [0.9]])
            else:
                switch.append([])
        roi_dup = [[box(500) + [0.9], box(500, 44, 196, 302) + [0.8]]
                   for _ in range(12)]
        rows = [
            self._table("单人 3 框（20 帧）", one, cfg(roi_polygon=[])),
            self._table("双人各 3 框（30 帧，min_danger_n=3）", two, cfg(roi_polygon=[])),
            self._table("丢失 12 帧后 ID 切换（30 帧）", switch, cfg(roi_polygon=[])),
            self._table("重复框在 ROI 内（12 帧）", roi_dup),
        ]
        print("")
        print("| 场景 | 帧数 | legacy max | spatial max | states max | tracked max | "
              "legacy Σ | spatial Σ | states Σ | tracked Σ | legacy 开门帧 | tracked 开门帧 |")
        print("|---|---|---|---|---|---|---|---|---|---|---|---|")
        for r in rows:
            print("| %s | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d | %d |" % (
                r["name"], r["frames"], r["legacy_max"], r["spatial_max"],
                r["states_max"], r["tracked_max"], r["legacy_sum"],
                r["spatial_sum"], r["states_sum"], r["tracked_sum"],
                r["gate_legacy"], r["gate_tracked"]))
        # 三个核心断言（数值同时出现在报告表格里）
        assert rows[0]["legacy_sum"] == 60 and rows[0]["tracked_sum"] == 20
        assert rows[0]["spatial_sum"] == 60, "2cm 几何去重兜不住 6cm 脚点偏移"
        assert rows[1]["legacy_max"] == 6 and rows[1]["tracked_max"] == 2
        assert rows[1]["gate_legacy"] == 30 and rows[1]["gate_tracked"] == 0
        assert rows[2]["states_max"] == 2, "ID 切换后 states() 有 2 条轨迹（旧滑行+新）"
        assert rows[2]["tracked_max"] == 1, "去重后应回到 1 人"
        assert rows[0]["states_max"] == 3 and rows[0]["states_sum"] == 60


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
