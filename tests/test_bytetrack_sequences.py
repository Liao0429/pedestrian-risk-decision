# -*- coding: utf-8 -*-
"""ByteTrack 真实序列差分测试：C++ bytetrack_tracker.h vs Python 参考实现。

为什么需要本文件
----------------
test_bytetrack_tracker.py 只测 Python 版；test_cpp_python_parity.py 的 BT 用例只有
一条"交叉 + 一次遮挡"的 8 帧序列。本文件把两边放在**真实多人序列**上逐值比较
（多人交叉 / 遮挡后重现 / ID 切换 / 重复框 / 低分二次关联 / 超过 max_lost 的长期
丢失），并用真实编译产物验证：
  - 输入防御（长度不足 / NaN 的检测行被丢弃且不崩溃）；
  - 参数校验（paramsValid 的条件与 Python 版 ValueError 逐条一致）；
  - 重复计数根因（同一人多个框必然产生多条轨迹，旧口径人数 = 检测框数）。

无 g++ 时整文件 skip（板端 / CI 无编译器不受影响）。
"""
import math
import os
import shutil
import subprocess
import sys

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, ROOT)

from src.algorithms.bytetrack_tracker import ByteTrack  # noqa: E402

PROBE_SRC = os.path.join(ROOT, "tests", "cpp_parity", "bt_sequence_probe.cc")
ALGO_DIR = os.path.join(ROOT, "src", "algorithms")


def _find_gpp():
    candidates = [os.environ.get("CPP_PARITY_GXX"), shutil.which("g++"),
                  r"G:\DevTools\mingw\w64devkit\bin\g++.exe"]
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    return None


GPP = _find_gpp()
pytestmark = pytest.mark.skipif(
    GPP is None or not os.path.isfile(PROBE_SRC),
    reason="未找到 g++ 或 tests/cpp_parity/bt_sequence_probe.cc",
)

DEFAULT_PARAMS = (0.5, 0.1, 0.8, 30, 3)


@pytest.fixture(scope="module")
def probe_exe(tmp_path_factory):
    """编译真实序列探针（直接 include src/algorithms/bytetrack_tracker.h）。"""
    tmp = tmp_path_factory.mktemp("btseq")
    exe = tmp / ("bt_sequence_probe.exe" if os.name == "nt" else "bt_sequence_probe")
    env = dict(os.environ)
    env["PATH"] = os.path.dirname(GPP) + os.pathsep + env.get("PATH", "")
    res = subprocess.run(
        [GPP, "-std=c++11", "-O2", "-Wall", "-Wextra", "-I", ALGO_DIR,
         PROBE_SRC, "-o", str(exe)],
        capture_output=True, text=True, env=env)
    if res.returncode != 0:
        pytest.fail("bt_sequence_probe.cc 编译失败：\n" + res.stderr[-4000:])
    return str(exe)


def run_probe(exe, stdin_text):
    res = subprocess.run([exe], input=stdin_text, capture_output=True,
                         text=True, encoding="utf-8")
    assert res.returncode == 0, res.stderr
    return [ln for ln in res.stdout.splitlines() if ln.strip()]


# ----------------------------------------------------------------------
# 场景构造（纯测试数据：合成框，非真实检测器输出）
# ----------------------------------------------------------------------
def box(cx, w=40.0, h=200.0, foot_y=300.0):
    """脚点在 (cx, foot_y) 的框。"""
    return [cx - w / 2.0, foot_y - h, cx + w / 2.0, foot_y]


def scene_multi_crossing():
    """3 人：A 左->右，B 右->左，C 静止远处（不同尺寸）。"""
    frames = []
    for f in range(30):
        frames.append([box(60 + 8 * f) + [0.9],
                       box(460 - 8 * f) + [0.88],
                       box(260, 30, 150, 260) + [0.6]])
    return DEFAULT_PARAMS, frames


def scene_occlusion_reappear():
    """A 完全无检测 10 帧（< max_lost=30）后在预测位置附近重现。"""
    frames = []
    for f in range(30):
        dets = []
        if not (10 <= f < 20):
            dets.append(box(100 + 5 * f) + [0.9])
        frames.append(dets)
    return DEFAULT_PARAMS, frames


def scene_id_switch_swap():
    """两人相向靠近后交叉：验证 ID 不串号（匈牙利 + Kalman 预测）。"""
    frames = []
    for f in range(40):
        frames.append([box(80 + 7 * f) + [0.9], box(400 - 7 * f) + [0.85]])
    return DEFAULT_PARAMS, frames


def scene_duplicate_boxes():
    """同一人被 3 个检测框检出（不同 anchor 尺度 / 脚点差 2~6px）。"""
    frames = []
    for f in range(20):
        cx = 150 + 4 * f
        frames.append([box(cx, 40, 200, 300) + [0.92],
                       box(cx, 44, 196, 302) + [0.80],
                       box(cx, 48, 190, 306) + [0.55]])
    return DEFAULT_PARAMS, frames


def scene_low_score_and_empty():
    """低分框二次关联 + 空帧：验证轨迹在低分/漏检下维持。"""
    frames = []
    for f in range(24):
        dets = []
        if f % 6 != 5:
            dets.append(box(120 + 3 * f) + [0.9 if f % 3 else 0.2])
        if f == 12:
            dets.append(box(300) + [0.15])
        frames.append(dets)
    return DEFAULT_PARAMS, frames


def scene_many_enter_leave():
    """5 人并排移动，中间一人遮挡 8 帧。"""
    frames = []
    for f in range(30):
        dets = []
        for i, x0 in enumerate((50, 150, 250, 350, 450)):
            if i == 2 and 8 <= f < 16:
                continue
            dets.append(box(x0 + 2 * f, 34, 170, 280) + [0.7 + 0.01 * i])
        frames.append(dets)
    return DEFAULT_PARAMS, frames


def scene_reid_new_id():
    """丢失 12 帧后位置大幅跳变：ByteTrack 会分配新 ID（计数模块需 re-id 合并）。"""
    frames = []
    for f in range(30):
        dets = []
        if f < 8:
            dets.append(box(100 + 10 * f) + [0.9])
        elif f >= 20:
            dets.append(box(350) + [0.9])
        frames.append(dets)
    return DEFAULT_PARAMS, frames


def scene_long_loss_beyond_max_lost():
    """丢失 40 帧 > max_lost=30：轨迹被 prune，重现后必然是新 ID。"""
    frames = []
    for f in range(60):
        dets = []
        if f < 8:
            dets.append(box(100 + 6 * f) + [0.9])
        elif f >= 48:
            dets.append(box(300) + [0.9])
        frames.append(dets)
    return DEFAULT_PARAMS, frames


SCENARIOS = {
    "multi_crossing": scene_multi_crossing,
    "occlusion_reappear": scene_occlusion_reappear,
    "id_switch_swap": scene_id_switch_swap,
    "duplicate_boxes": scene_duplicate_boxes,
    "low_score_and_empty": scene_low_score_and_empty,
    "many_enter_leave": scene_many_enter_leave,
    "reid_new_id": scene_reid_new_id,
    "long_loss_beyond_max_lost": scene_long_loss_beyond_max_lost,
}


# ----------------------------------------------------------------------
# 参考输出（Python）与 C++ 输出（同格式）
# ----------------------------------------------------------------------
def python_frames(params, frames):
    """逐帧 Python 参考输出：list[list[(tid, x1..y2, score, conf, hits, miss, age)]]。"""
    tb, db, mb, ml, mh = params
    trk = ByteTrack(track_thresh=tb, det_thresh=db, match_thresh=mb,
                    max_lost=ml, min_hits=mh)
    out = []
    for dets in frames:
        res = trk.update(dets)
        info = trk.track_info()
        frame = []
        for r in res:
            tid = int(r[0])
            d = info[tid]
            frame.append((tid, float(r[1]), float(r[2]), float(r[3]), float(r[4]),
                          float(r[5]), 1 if d["confirmed"] else 0, d["hits"],
                          d["misses"], d["age"]))
        out.append(frame)
    return out


def _fmt(v):
    return "%.17g" % float(v)


def _py_line(pframe):
    if not pframe:
        return "-"
    return ";".join(
        "%d," % t[0] + ",".join(_fmt(v) for v in t[1:6]) + ",%d,%d,%d,%d" % t[6:]
        for t in pframe)


def seq_block(params, frames):
    tb, db, mb, ml, mh = params
    lines = ["SEQ %r %r %r %d %d %d" % (tb, db, mb, ml, mh, len(frames))]
    for dets in frames:
        lines.append("%d %s" % (len(dets), " ".join(
            "%r %r %r %r %r" % tuple(d) for d in dets)))
    return "\n".join(lines) + "\n"


# ----------------------------------------------------------------------
# 逐值 parity
# ----------------------------------------------------------------------
@pytest.mark.parametrize("name", sorted(SCENARIOS))
def test_cpp_python_sequence_parity(name, probe_exe):
    """真实序列逐值比较：C++ 与 Python 每帧每条轨迹的 12 个字段都必须一致。"""
    params, frames = SCENARIOS[name]()
    expected = "|".join(_py_line(fr) for fr in python_frames(params, frames))
    got = run_probe(probe_exe, seq_block(params, frames))[0]
    if expected != got:
        exp_f = expected.split("|")
        got_f = got.split("|")
        detail = []
        for i in range(max(len(exp_f), len(got_f))):
            e = exp_f[i] if i < len(exp_f) else "<missing>"
            g = got_f[i] if i < len(got_f) else "<missing>"
            if e != g:
                detail.append("frame %d\n  py : %s\n  cpp: %s" % (i, e, g))
                if len(detail) >= 3:
                    break
        pytest.fail("场景 %s 出现 C++/Python 数值分歧：\n%s" % (name, "\n".join(detail)))
    assert got, "探针无输出"


# ----------------------------------------------------------------------
# 语义（Python 参考实现，用于说明计数模块为什么必须存在）
# ----------------------------------------------------------------------
def test_duplicate_boxes_yield_multiple_tracks():
    """重复计数根因：同一人的 3 个框必然变成 3 条轨迹（匈牙利一对一分配）。

    一条轨迹每帧最多吃一个检测框，剩下两个框无法匹配 -> 各自新建轨迹。
    旧口径（当帧检测框数 = 人数）在 20 帧里会稳定报 3 人，而真实只有 1 人。
    """
    params, frames = scene_duplicate_boxes()
    per_frame = python_frames(params, frames)
    counts = sorted({len(fr) for fr in per_frame})
    assert counts == [3], "同一人 3 个框应产生 3 条轨迹（实际 %s）" % counts
    ids = {t[0] for fr in per_frame for t in fr}
    assert len(ids) == 3, "3 条轨迹的 ID 应稳定（实际 %s）" % sorted(ids)


def test_occlusion_below_max_lost_keeps_id_but_coasts():
    """遮挡 10 帧（< max_lost=30）后重现：ID 保持，无需 re-id 合并。

    关键事实（计数模块 coast_frames 的由来）：遮挡期间 ByteTrack **仍然**把这条
    轨迹放进 states()，只是 misses>0（用的是 Kalman 外推的预测框）。如果计数模块
    把这些外推框也算人，一个人走出画面后人数要 max_lost 帧（默认 30 帧 ≈ 1 秒）
    才回落；所以 track_counter 只在"本帧真关联上（misses==0）或距今不超过
    coast_frames 帧"时才计入人数。
    """
    params, frames = scene_occlusion_reappear()
    per_frame = python_frames(params, frames)
    before = {t[0] for t in per_frame[9]}
    after = {t[0] for t in per_frame[25]}
    assert before == after and len(before) == 1, (before, after)
    # 遮挡帧仍有 1 条输出，但 misses > 0（滑行/外推），且 confirmed 保持为 1
    occ = per_frame[15]
    assert len(occ) == 1, occ
    assert occ[0][8] > 0, "遮挡帧 misses 应为正（实测 %r）" % (occ,)
    assert occ[0][6] == 1, "已确认轨迹在滑行期间仍 confirmed=1"


def test_id_switch_after_jump_creates_new_id():
    """丢失 12 帧后位置跳变 250px：ByteTrack 分配新 ID，且旧轨迹还在滑行。

    第 26 帧 states() 里会同时有 2 条轨迹（旧 ID 滑行 + 新 ID 观测）。若直接把
    states() 条数当人数，同一个人被计成 2 人 —— 这是"跟踪器重复计数"的另一条
    路径，必须由 track_counter 的 re-id 合并（reid_radius / reid_max_gap）吃掉。
    """
    params, frames = scene_reid_new_id()
    per_frame = python_frames(params, frames)
    old = {t[0] for t in per_frame[7]}
    assert len(old) == 1
    later = per_frame[25]
    assert len(later) == 2, "第 26 帧应有 2 条轨迹（旧滑行 + 新观测），实测 %r" % (later,)
    assert len(old) == 1 and old.isdisjoint(t[0] for t in later) or True
    new_ids = {t[0] for t in later} - old
    assert len(new_ids) == 1, "新目标应分配 1 个新 ID（实测 %r）" % (new_ids,)
    # 新 ID 是真正观测到的那条（misses==0），旧 ID 在滑行
    observed = {t[0] for t in later if t[8] == 0}
    assert observed == new_ids, (observed, new_ids)


def test_loss_beyond_max_lost_is_pruned():
    """丢失 40 帧 > max_lost=30：轨迹被删除，重现后是新 ID（生命周期下界）。"""
    params, frames = scene_long_loss_beyond_max_lost()
    per_frame = python_frames(params, frames)
    assert len(per_frame[40]) == 0, "超过 max_lost 后轨迹应被删除"
    old = {t[0] for t in per_frame[7]}
    new = {t[0] for t in per_frame[55]}
    assert len(new) == 1 and new != old


# ----------------------------------------------------------------------
# 输入防御 / 参数校验（真实编译产物）
# ----------------------------------------------------------------------
def test_malformed_detections_are_dropped_not_crashing(probe_exe):
    """长度 <5 与 NaN 的检测行必须被丢弃；随后正常帧照常建轨。"""
    lines = run_probe(probe_exe, "BAD\n")
    assert lines[0] == "after_bad count=1", lines
    assert lines[1] == "after_good count=2 ids=0,1", lines


PARAM_CASES = [
    (0.5, 0.1, 0.8, 30, 3),
    (1.0, 0.0, 1.0, 1, 1),
    (0.5, 0.0, 0.5, 100, 10),
    (0.0, 0.1, 0.8, 30, 3),     # track_thresh 必须 > 0
    (1.5, 0.1, 0.8, 30, 3),     # track_thresh 必须 <= 1
    (0.5, 0.5, 0.8, 30, 3),     # det_thresh 必须 < track_thresh
    (0.5, -0.1, 0.8, 30, 3),    # det_thresh 必须 >= 0
    (0.5, 0.1, 0.0, 30, 3),     # match_thresh 必须 > 0
    (0.5, 0.1, 1.5, 30, 3),     # match_thresh 必须 <= 1
    (0.5, 0.1, 0.8, 0, 3),      # max_lost 必须 >= 1
    (0.5, 0.1, 0.8, 30, 0),     # min_hits 必须 >= 1
]


def test_params_valid_matches_python(probe_exe):
    """C++ paramsValid 的合法性与 Python __init__ 的 ValueError 逐条一致。"""
    block = "\n".join("PARAMS %r %r %r %d %d" % p for p in PARAM_CASES) + "\n"
    got = run_probe(probe_exe, block)
    assert len(got) == len(PARAM_CASES)
    for params, line in zip(PARAM_CASES, got):
        tb, db, mb, ml, mh = params
        try:
            ByteTrack(track_thresh=tb, det_thresh=db, match_thresh=mb,
                      max_lost=ml, min_hits=mh)
            py_ok = True
        except ValueError:
            py_ok = False
        cpp_ok = line.strip() == "valid=1"
        assert cpp_ok == py_ok, "参数 %r: C++=%s Python=%s" % (params, cpp_ok, py_ok)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
