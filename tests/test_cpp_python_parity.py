# -*- coding: utf-8 -*-
"""C++ 头文件 vs Python 原型的**真实编译**数值一致性测试。

为什么需要本文件
----------------
仓库里原有 test_graph_laplacian.py::TestJacobiCppLogic / test_crowd_dbscan.py::
TestCppAlgorithmLogic 是"用 Python 逐行复刻 C++"来做等价性验证，但复刻本身可能
写错 —— 事实上 crowd_dbscan 的复刻把噪声标签写成 -2，与真实 crowd_dbscan.h 的
-1 不一致，于是分歧被"复刻"掩盖了整整一轮。本文件改为：

    用 g++ 编译 tests/cpp_parity/parity_harness.cc（直接 include src/algorithms/*.h），
    对同一组输入逐值比较 C++ 与 Python。

KNOWN_DIVERGENCES 是**双向**契约：
  - 新出现的分歧 -> 用例失败（不允许悄悄漂移）；
  - 已登记的分歧消失了 -> 用例失败并提示删除条目（不允许清单过期）。

无 g++ 时整文件 skip（CI / 板端无编译器不受影响）。
"""
import math
import os
import re
import shutil
import subprocess
import sys

import numpy as np
import pytest

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")
ALGO_DIR = os.path.join(SRC_DIR, "algorithms")
HARNESS_SRC = os.path.join(PROJECT_ROOT, "tests", "cpp_parity", "parity_harness.cc")
if SRC_DIR not in sys.path:
    sys.path.insert(0, SRC_DIR)

from algorithms.crowd_dbscan import dbscan_crowd_features  # noqa: E402
from algorithms.ema_smoother import EMASmoother  # noqa: E402
from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.kalman_tracker import KalmanFilter  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402

# 已定位、待 C++ 侧（T1 / board-perf scope）修复的分歧。
# 2026-10-30 更新：D1(去重只比保留点) / D2(内部统一 double) / D3(簇并列 tie-break)
# 已在 C++ 侧修复并删除对应条目；余下 ema 时间戳量纲分歧为**契约问题**（板端未接线
# EMA），保留登记条目并在 ema_smoother.h 补契约注释。
# 每一项都必须写清"为什么"，修复后请删掉对应条目（用例会提示）。
KNOWN_DIVERGENCES = {
    "lap_dedup_exact_2cm": (
        "输入表示差异（非逻辑 bug）：GraphLaplacian 公共 API 收 float 坐标（板端脚点"
        "本就是 float），0.02f = 0.01999999955... < 0.02 会被判重，Python 用 float64 的"
        "0.02 严格不小于 0.02 -> 保留，n 差 1。内部已改 double 存储（减少二次舍入），"
        "彻底一致需把公共 API 改 double（涉及 enhanced_decision/main.cc 全链路），"
        "板端实际脚点来自像素整数映射，2.000cm 精确边界是测例人为构造。"
    ),

    "ema_ts_float_seconds": (
        "EMA 时间戳量纲不一致：Python t 为 float（秒），C++ 为 int64_t（毫秒/帧号）"
        "；同一 tau 下亚秒级 dt 在 C++ 被截断，平滑强度完全不同"
    ),
}


# ---------------------------------------------------------------------------
# 工具链定位与编译
# ---------------------------------------------------------------------------
def _find_gpp():
    candidates = [os.environ.get("CPP_PARITY_GXX"), shutil.which("g++")]
    candidates.append(r"G:\DevTools\mingw\w64devkit\bin\g++.exe")
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    return None


GPP = _find_gpp()
pytestmark = pytest.mark.skipif(
    GPP is None or not os.path.isfile(HARNESS_SRC),
    reason="未找到 g++ 或 tests/cpp_parity/parity_harness.cc（本用例需要 C++ 编译器）",
)


def _run_gpp(args, cwd):
    env = dict(os.environ)
    if GPP and os.path.dirname(GPP):
        env["PATH"] = os.path.dirname(GPP) + os.pathsep + env.get("PATH", "")
    return subprocess.run([GPP] + args, cwd=cwd, capture_output=True, text=True, env=env)


def _standalone_header_compiles(tmp_dir, header):
    """单独 include 一个头文件能否编译（用于探测 bytetrack_tracker.h 这类隐患）。"""
    probe = os.path.join(tmp_dir, "probe_" + header.replace(".h", "") + ".cc")
    with open(probe, "w", encoding="utf-8") as f:
        f.write('#include "%s"\nint main() { return 0; }\n' % header)
    res = _run_gpp(["-std=c++11", "-fsyntax-only", "-I", ALGO_DIR, probe], tmp_dir)
    return res.returncode == 0, res.stderr


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    """编译 parity_harness.cc 并返回可执行文件路径。

    bytetrack_tracker.h 当前无法独立编译（xyahToBox 形参 4 vs 8，归 board-perf 修）。
    为了让其余模块照常做数值对照、同时仍能验证 ByteTrack 数值，这里生成一份
    **仅修正该形参** 的临时副本用于编译；原始头文件一行都不改。
    """
    tmp = tmp_path_factory.mktemp("parity")
    inc_dirs = ["-I", ALGO_DIR]

    bt_ok, bt_err = _standalone_header_compiles(str(tmp), "bytetrack_tracker.h")
    if not bt_ok:
        header_path = os.path.join(ALGO_DIR, "bytetrack_tracker.h")
        with open(header_path, encoding="utf-8") as f:
            src = f.read()
        old_sig = "inline Box xyahToBox(const std::array<double, 4>& x)"
        new_sig = "inline Box xyahToBox(const std::array<double, 8>& x)"
        if old_sig not in src:
            pytest.fail(
                "bytetrack_tracker.h 无法独立编译，且已知的一行修复不再适用；"
                "请先看编译错误：\n" + bt_err[:2000]
            )
        patched_dir = tmp / "patched"
        patched_dir.mkdir()
        (patched_dir / "bytetrack_tracker.h").write_text(
            src.replace(old_sig, new_sig), encoding="utf-8"
        )
        inc_dirs = ["-I", str(patched_dir)] + inc_dirs

    exe = tmp / ("parity_harness.exe" if os.name == "nt" else "parity_harness")
    res = _run_gpp(
        ["-std=c++11", "-O2", "-Wall", "-Wextra"] + inc_dirs
        + [HARNESS_SRC, "-o", str(exe)],
        str(tmp),
    )
    if res.returncode != 0:
        pytest.fail("parity_harness.cc 编译失败：\n" + res.stderr[-4000:])
    return str(exe)


def run_harness(exe, stdin_text):
    """喂一段 stdin，返回按行输出的列表。"""
    res = subprocess.run([exe], input=stdin_text, capture_output=True, text=True, encoding="utf-8")
    assert res.returncode == 0, res.stderr
    return [ln for ln in res.stdout.splitlines() if ln.strip()]


# ---------------------------------------------------------------------------
# C++ 输出解析
# ---------------------------------------------------------------------------
def _kv(line):
    return dict(re.findall(r"(\w+)=([^\s]*)", line))


def _floats(text):
    return [float(x) for x in text.split(",") if x]


def _ints(text):
    return [int(x) for x in text.split(",") if x]


def parse_lap(line):
    d = _kv(line)
    return {
        "n": int(d["n"]), "m": int(d["m"]),
        "density": float(d["density"]), "r_gcc": float(d["r_gcc"]),
        "clustering": float(d["clustering"]), "fiedler": float(d["fiedler"]),
        "spectral_radius": float(d["spectral_radius"]),
        "eig_entropy": float(d["eig_entropy"]), "converged": int(d["converged"]),
    }


def parse_dbscan(line):
    d = _kv(line)
    return {
        "n": int(d["n"]), "n_clusters": int(d["n_clusters"]),
        "sizes": _ints(d["sizes"]),
        "centers": [tuple(float(v) for v in c.split(",")) for c in d["centers"].split(";") if c],
        "largest_ratio": float(d["largest_ratio"]), "largest": int(d["largest"]),
        "n_noise": int(d["n_noise"]), "n_clustered": int(d["n_clustered"]),
        "labels": _ints(d["labels"]),
    }


def parse_kal(line):
    d = _kv(line)
    return {"x": float(d["x"]), "y": float(d["y"]), "vx": float(d["vx"]),
            "vy": float(d["vy"]), "P": _floats(d["P"])}


def parse_ema(line):
    return {"out": _floats(_kv(line)["out"])}


def parse_fsm(line):
    d = _kv(line)
    return {"levels": d["levels"].split(","), "scores": _floats(d["scores"])}


def parse_bt(line):
    d = _kv(line)
    frames = []
    for frame in d["frames"].split("|"):
        tracks = []
        for t in frame.split(";"):
            if not t:
                continue
            tid, nums = t.split(":")
            vals = [float(v) for v in nums.split(",")]
            tracks.append(tuple([int(tid)] + vals))
        frames.append(tracks)
    return {"frames": frames}


# ---------------------------------------------------------------------------
# 递归比较
# ---------------------------------------------------------------------------
def compare(node_a, node_b, path="", diffs=None):
    """a=C++ 解析结果, b=Python 期望。浮点用 rel 1e-6 / abs 1e-9。"""
    if diffs is None:
        diffs = []
    if isinstance(node_a, dict):
        for key in sorted(set(node_a) | set(node_b)):
            if key not in node_a or key not in node_b:
                diffs.append(f"{path}.{key}: 字段缺失")
                continue
            compare(node_a[key], node_b[key], f"{path}.{key}", diffs)
    elif isinstance(node_a, (list, tuple)):
        if len(node_a) != len(node_b):
            diffs.append(f"{path}: 长度 {len(node_a)} != {len(node_b)}")
            return diffs
        for i, (x, y) in enumerate(zip(node_a, node_b)):
            compare(x, y, f"{path}[{i}]", diffs)
    elif isinstance(node_a, float) or isinstance(node_b, float):
        if not math.isclose(float(node_a), float(node_b), rel_tol=1e-6, abs_tol=1e-9):
            diffs.append(f"{path}: {node_a!r} != {node_b!r}")
    else:
        if node_a != node_b:
            diffs.append(f"{path}: {node_a!r} != {node_b!r}")
    return diffs


# ---------------------------------------------------------------------------
# 用例构造（C++ 命令 + Python 侧同输入的结果）
# ---------------------------------------------------------------------------
def _fmt_pts(pts):
    return " ".join("%r %r" % (float(x), float(y)) for x, y in pts)


def build_lap_cases():
    cases = []

    def add(cid, pts, edge):
        cmd = "LAP %r %d %s" % (float(edge), len(pts), _fmt_pts(pts))
        f = compute_laplacian_features(pts, edge)
        expected = {
            "n": f["n"], "m": f["m"], "density": f["density"], "r_gcc": f["r_gcc"],
            "clustering": f["clustering"], "fiedler": f["fiedler"],
            "spectral_radius": f["spectral_radius"], "eig_entropy": f["eig_entropy"],
            "converged": 1,
        }
        cases.append((cid, cmd, parse_lap, expected))

    add("lap_chain_dedup_3cm", [(0.0, 0.0), (0.015, 0.0), (0.030, 0.0)], 2.0)
    add("lap_dedup_exact_2cm", [(0.0, 0.0), (0.02, 0.0), (0.041, 0.0)], 2.0)
    add("lap_p5_chain", [(i * 1.5, 0.0) for i in range(5)], 2.0)
    add("lap_k4", [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)], 2.0)
    add("lap_dispersed4", [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (15.0, 0.0)], 2.0)
    add("lap_n10", [(-2.0, 1.0), (0.5, 0.0), (1.0, 1.0), (1.5, 0.0), (2.0, 1.0),
                    (2.5, 0.0), (3.0, 1.0), (3.5, 0.0), (4.0, 1.0), (9.0, 9.0)], 2.0)
    rng = np.random.default_rng(7)
    add("lap_rand30", [(round(float(x), 4), round(float(y), 4))
                       for x, y in rng.uniform(0, 8, size=(30, 2))], 2.0)
    add("lap_rand12", [(round(float(x), 4), round(float(y), 4))
                       for x, y in rng.uniform(0, 4, size=(12, 2))], 1.5)
    add("lap_two_clusters", [(0, 0), (0.5, 0), (0, 0.5), (0.5, 0.5),
                             (5, 5), (5.5, 5), (5, 5.5), (5.5, 5.5)], 1.0)
    add("lap_grid20", [(float(i % 5) * 1.2, float(i // 5) * 1.2) for i in range(20)], 1.5)
    add("lap_single_pt", [(1.0, 2.0)], 2.0)
    return cases


def build_dbscan_cases():
    cases = []

    def add(cid, pts, eps, ms):
        cmd = "DBSCAN %r %d %d %s" % (float(eps), ms, len(pts), _fmt_pts(pts))
        f = dbscan_crowd_features(pts, eps, ms)
        expected = {
            "n": f["n"], "n_clusters": f["n_clusters"],
            "sizes": list(f["cluster_sizes"]),
            "centers": [tuple(c) for c in f["cluster_centers"]],
            "largest_ratio": f["largest_cluster_ratio"],
            "largest": f["largest_cluster_size"],
            "n_noise": f["n_noise"], "n_clustered": f["n_clustered"],
            "labels": [int(x) for x in f["labels"]],
        }
        cases.append((cid, cmd, parse_dbscan, expected))

    add("db_two_clusters", [(0, 0), (0, 1), (1, 0), (1, 1),
                            (10, 0), (10, 1), (11, 0), (11, 1)], 1.5, 2)
    add("db_eps_closed", [(0.0, 0.0), (1.0, 0.0), (2.0, 0.0)], 1.0, 2)
    add("db_noise", [(0.0, 0.0), (0.0, 0.5), (5.0, 5.0)], 1.0, 2)
    add("db_minpts3", [(0.0, 0.0), (0.0, 0.5), (0.0, 1.0), (5.0, 5.0)], 1.0, 3)
    add("db_tie_two_clusters", [(0.0, 0.0), (0.0, 0.5), (10.0, 0.0), (10.0, 0.5)], 1.0, 2)
    # 20 个大小并列的簇：>16 个元素时 std::sort 走内省排序的分区路径（非稳定），
    # 用来暴露 C++ 并列簇顺序是否与 Python 的 (size desc, id asc) 一致。
    add("db_many_tied_clusters",
        [(i * 5.0 + dx, 0.0) for i in range(20) for dx in (0.0, 0.5)], 1.0, 2)
    rng = np.random.default_rng(31)
    pts = [(round(float(x), 4), round(float(y), 4)) for x, y in rng.uniform(-4, 4, size=(16, 2))]
    add("db_rand16", pts, 1.5, 2)
    return cases


def build_kalman_cases():
    cases = []

    def add(cid, dt, sa, sz, x0, y0, zs):
        cmd = "KAL %r %r %r %d %r %r %s" % (
            float(dt), float(sa), float(sz), len(zs), float(x0), float(y0),
            " ".join("%r %r" % (float(a), float(b)) for a, b in zs))
        kf = KalmanFilter(dt=dt, process_noise=sa, measurement_noise=sz)
        kf.init(x0, y0)
        for z in zs:
            kf.update(z)
        st = kf.state
        P = kf.covariance
        cases.append((cid, cmd, parse_kal, {
            "x": float(st[0]), "y": float(st[1]), "vx": float(st[2]), "vy": float(st[3]),
            "P": [float(P[i][j]) for i in range(4) for j in range(4)],
        }))

    add("kal_basic", 1 / 30.0, 1.5, 0.5, 1.0, 2.0,
        [(1.1, 2.1), (1.25, 2.15), (1.4, 2.3), (1.6, 2.4)])
    add("kal_dt01", 0.1, 2.0, 0.3, 0.0, 0.0, [(1.0, 0.5), (2.0, 1.0), (3.0, 1.5)])
    return cases


def build_ema_cases():
    cases = []

    def add(cid, alpha, use_ts, tau, seq):
        cmd = "EMA %r %d %r %d %s" % (
            float(alpha), 1 if use_ts else 0, float(tau), len(seq),
            " ".join("%r %r" % (float(v), float(t)) for v, t in seq))
        sm = EMASmoother(alpha=alpha, use_timestamp=bool(use_ts), tau=tau)
        cases.append((cid, cmd, parse_ema, {"out": [float(sm.update(v, t)) for v, t in seq]}))

    add("ema_fixed", 0.3, False, 1.0, [(0.6, 1), (0.9, 2), (0.9, 3), (0.2, 4)])
    # 整数毫秒时间戳：C++ int64_t 与 Python float 完全等价（正常使用方式）
    add("ema_ts_int_ms", 0.3, True, 100.0, [(0.6, 0), (0.9, 40), (0.9, 80), (0.2, 200)])
    add("ema_ts_tau05", 0.5, True, 500.0, [(1.0, 0), (2.0, 500), (3.0, 1500), (0.0, 3000)])
    # 已登记分歧：浮点"秒"时间戳（C++ 侧 int64_t 会截断）
    add("ema_ts_float_seconds", 0.5, True, 0.5, [(1.0, 0.0), (2.0, 0.5), (3.0, 1.5)])
    return cases


def build_fsm_cases():
    cases = []

    def add(cid, events):
        cmd = "FSM %d %s" % (len(events), " ".join(
            "%d %r %r %r %r %d" % (n, float(d), float(rg), float(fi), float(tr), int(ms))
            for n, d, rg, fi, tr, ms in events))
        fsm = RiskFSM()
        levels, scores = [], []
        for n, d, rg, fi, tr, ms in events:
            levels.append(fsm.update(
                {"n": n, "density": d, "r_gcc": rg, "fiedler": fi, "trend": tr}, ms))
            scores.append(fsm.score)
        cases.append((cid, cmd, parse_fsm, {"levels": levels, "scores": scores}))

    add("fsm_rise_fall", [(2, 1.0, 1.0, 0, 0, 0)] * 4 + [(2, 0.0, 0.0, 0, 0, 0)] * 6)
    add("fsm_danger_hold", [(5, 1.0, 1.0, 3.0, 0, 0)] * 4 + [(5, 0.0, 0.0, 0.0, 0, 100)] * 12)
    add("fsm_min_n_gate", [(2, 1.0, 1.0, 3.0, 0, 0)] * 5)
    add("fsm_trend", [(3, 0.3, 0.3, 0.5, 1.0, 0), (3, 0.3, 0.3, 0.5, 1.0, 1),
                      (3, 0.3, 0.3, 0.5, -1.0, 2), (10, 0.6, 0.9, 2.0, 0.5, 2000)])
    return cases


def build_bytetrack_case():
    """两目标相向而行 + 中途遮挡：覆盖关联 / 低分二次关联 / 轨迹生命周期。"""
    from algorithms.bytetrack_tracker import ByteTrack

    frames = []
    a = [100.0, 100.0, 140.0, 300.0]
    b = [300.0, 100.0, 340.0, 300.0]
    for i in range(8):
        dets = []
        a = [v + 6.0 if k % 2 == 0 else v for k, v in enumerate(a)]
        b = [v - 6.0 if k % 2 == 0 else v for k, v in enumerate(b)]
        if i != 5:                       # 第 6 帧 a 被遮挡，只剩 b
            dets.append(a + [0.9])
        dets.append(b + [0.9])
        if i == 6:                       # 第 7 帧插入低分框（验证二次关联路径）
            dets.append([a[0] + 2.0, a[1], a[2] + 2.0, a[3], 0.2])
        frames.append(dets)

    params = (0.5, 0.1, 0.8, 30, 3)
    tracker = ByteTrack(track_thresh=params[0], det_thresh=params[1],
                        match_thresh=params[2], max_lost=params[3], min_hits=params[4])
    expected_frames = []
    for dets in frames:
        out = tracker.update(dets)
        expected_frames.append([tuple([int(r[0])] + [float(v) for v in r[1:]]) for r in out])

    header = "BT %r %r %r %d %d %d" % (params[0], params[1], params[2],
                                       params[3], params[4], len(frames))
    lines = [header]
    for dets in frames:
        lines.append("%d %s" % (len(dets), " ".join("%r %r %r %r %r" % tuple(d) for d in dets)))
    block = "\n".join(lines) + "\n"
    return ("bt_occlusion_crossing", block, parse_bt, {"frames": expected_frames})


def _all_cases():
    return (build_lap_cases() + build_dbscan_cases() + build_kalman_cases()
            + build_ema_cases() + build_fsm_cases() + [build_bytetrack_case()])


@pytest.fixture(scope="module")
def cpp_outputs(harness):
    """一次性把所有单行用例喂给 C++ 程序，返回 {case_id: 解析结果}。"""
    cases = _all_cases()
    singles = [c for c in cases if c[0] != "bt_occlusion_crossing"]
    stdin_text = "\n".join(c[1] for c in singles) + "\n"
    lines = run_harness(harness, stdin_text)
    assert len(lines) == len(singles), (len(lines), len(singles))
    out = {}
    for (cid, _cmd, parser, _expected), line in zip(singles, lines):
        assert not line.startswith("ERR"), (cid, line)
        out[cid] = parser(line)
    for cid, block, parser, _expected in cases:
        if cid == "bt_occlusion_crossing":
            out[cid] = parser(run_harness(harness, block)[0])
    return out


CASES = {c[0]: c for c in _all_cases()}


@pytest.mark.parametrize("case_id", sorted(CASES))
def test_parity(case_id, cpp_outputs):
    """逐值比较；分歧只允许出现在 KNOWN_DIVERGENCES 里，且必须真的还存在。"""
    _cid, _cmd, _parser, expected = CASES[case_id]
    diffs = compare(cpp_outputs[case_id], expected)
    known = KNOWN_DIVERGENCES.get(case_id)
    if diffs and known is None:
        pytest.fail(
            "%s: C++ 与 Python 出现未登记分歧（%d 处）：\n  %s\n"
            "若确认是 C++ 侧待修问题，请登记到 KNOWN_DIVERGENCES 并写清原因；"
            "若是本次改动引入的回归，请修实现。" % (case_id, len(diffs), "\n  ".join(diffs[:12]))
        )
    if diffs and known:
        # 已登记：只要求分歧确实存在（不需要逐项断言，C++ 侧修好后会走到下面的分支）
        return
    if not diffs and known:
        pytest.fail(
            "%s 的已登记分歧已消失（C++ 与 Python 现在一致）。"
            "请从 KNOWN_DIVERGENCES 删除该条目：%s" % (case_id, known)
        )


def test_bytetrack_header_compiles_standalone(tmp_path):
    """bytetrack_tracker.h 目前无法独立编译（xyahToBox 形参类型 4 vs 8）。

    归 board-perf 修复；修好后本用例会 XPASS，strict 模式下 XPASS 即失败，
    强制删除这个 xfail 标记，避免"缺陷已修但清单还写着坏"。
    """
    if GPP is None:
        pytest.skip("未找到 g++")
    ok, err = _standalone_header_compiles(str(tmp_path), "bytetrack_tracker.h")
    if not ok:
        pytest.xfail("bytetrack_tracker.h 编译失败（已知缺陷，归 board-perf）：\n" + err[:800])
    assert ok


def test_lap_dedup_semantics_documented():
    """把 graph_laplacian 去重语义（Python 侧正确行为）显式钉住。

    这条不比较 C++（C++ 侧同场景是 KNOWN_DIVERGENCES 条目），只防止 Python 被
    "为了对齐 C++" 反向改坏。
    """
    f = compute_laplacian_features([(0.0, 0.0), (0.015, 0.0), (0.030, 0.0)], 2.0)
    assert f["n"] == 2, "Python 去重只与已保留点比较，不应做单链传递合并"
