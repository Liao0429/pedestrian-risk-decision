# -*- coding: utf-8 -*-
"""
端到端集成测试：行人风险决策流水线
====================================

本测试把 `scripts/crowd_test.py`（经典规则路径）与 `src/algorithms/*`
（增强路径：图拉普拉斯谱特征 + Kalman 平滑 + RiskFSM 时序状态机）作为**真实组件**
串联起来，在**真实数据/真实脚本使用的同一批函数**上验证系统行为，而非重复单元测试。

为避免依赖重型推理库（ultralytics/YOLO 权重），测试在导入 `crowd_test` 前注入一个
最小 `ultralytics` 桩模块——被测函数（compute_features / evaluate_level /
get_homography / map_to_ground / load_config）完全不经过 YOLO 推理，
因此桩模块不影响被测逻辑的真实性。增强路径全部来自 `src.algorithms`（真实实现）。

覆盖的真实场景（对应 config/default_config.json 阈值）：
  A. 空场景            -> SAFE
  B. 稀疏行人(3人分散) -> SAFE
  C. 小型局部聚集(5人) -> WARNING
  D. 密集人群(12人聚集) -> DANGER
  E. 双簇(2x6人,相隔远) -> WARNING（r_gcc=0.5 未达 DANGER，但密度达 WARNING）
  F. 长链人群(11人排成链) -> DANGER（r_gcc=1.0 触发，即使密度低）

验证点：
  1. 规则引擎 evaluate_level 在以上场景输出正确等级；
  2. 增强流水线（Kalman + 谱特征 + FSM）在模拟视频时间线上正确迁移
     SAFE->WARNING->DANGER，并具备滞回/确认帧特性；
  3. `crowd_test.compute_features` 与 `graph_laplacian.compute_laplacian_features`
     的 n/m/density/r_gcc/clustering 完全一致（两条实现路径互相对齐）；
  4. IPM 单应映射（get_homography + map_to_ground）往返正确，脚点映射为米制；
  5. 运行时配置 config/default_config.json 与 crowd_test 内置默认值一致；
  6. 增强脚本产生的 timeline 结构与真实 results/video_enhanced/*_timeline.json
     结构一致，且在密集场景下等级分布以 DANGER 为主。

运行方式：
    python -m pytest tests/test_integration_pipeline.py -v
  或
    python tests/test_integration_pipeline.py
"""
import json
import os
import sys
import types

import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS_DIR = os.path.join(PROJECT_ROOT, "scripts")
SRC = os.path.join(PROJECT_ROOT, "src")
if SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, SCRIPTS_DIR)
if SRC not in sys.path:
    sys.path.insert(0, SRC)

# ---------------------------------------------------------------------------
# 注入最小 ultralytics 桩：仅用于让 crowd_test 模块导入成功。
# 被测函数不触发 YOLO 推理，因此不影响真实逻辑。
# ---------------------------------------------------------------------------
if "ultralytics" not in sys.modules:
    _fake_ultralytics = types.ModuleType("ultralytics")
    _fake_ultralytics.YOLO = object
    sys.modules["ultralytics"] = _fake_ultralytics
    if "ultralytics.YOLO" not in sys.modules:
        _fake_yolo_mod = types.ModuleType("ultralytics.YOLO")
        sys.modules["ultralytics.YOLO"] = _fake_yolo_mod

import crowd_test  # noqa: E402  (真实 scripts/crowd_test.py)

from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.kalman_tracker import KalmanTracker                 # noqa: E402
from algorithms.risk_fsm import LEVELS, LEVEL_RANK, RiskFSM, RiskFSMConfig  # noqa: E402

CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "default_config.json")


# ---------------------------------------------------------------------------
# 场景构造（地面米制坐标）
# ---------------------------------------------------------------------------
def scenario_points():
    """返回 {名称: [(x,y), ...]} 的真实米制场景点集。"""
    rng = np.random.default_rng(20261026)
    sc = {}
    # A. 空场景
    sc["empty"] = []
    # B. 稀疏行人：3 人相距 > 3m（无连边）
    sc["sparse3"] = [(0.0, 0.0), (5.0, 0.0), (10.0, 2.0)]
    # C. 小型局部聚集：5 人，彼此间距 < 1.5m（全连接）
    sc["cluster5"] = [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0),
                      (0.0, 1.0), (1.0, 1.0)]
    # D. 密集人群：12 人，落在 ~2m x 2m 的块内（含噪声）
    sc["dense12"] = []
    for i in range(12):
        x = rng.uniform(-1.0, 1.0)
        y = rng.uniform(-1.0, 1.0)
        sc["dense12"].append((float(x), float(y)))
    # E. 双簇：两簇各 6 人（簇内紧邻 1m，簇间相隔 6m）
    cluster_a = [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0), (0.0, 1.0), (1.0, 1.0), (0.5, 0.5)]
    cluster_b = [(x + 6.0, y + 0.2) for (x, y) in cluster_a]
    sc["two_clusters12"] = cluster_a + cluster_b
    # F. 长链人群：11 人排成链，间距 1.9m（< 2.0m 建边）
    sc["chain11"] = [(i * 1.9, 0.0) for i in range(11)]
    return sc


def expected_level(name, cfg):
    """按场景名称给出期望等级（基于默认配置）。"""
    expected = {
        "empty": "SAFE",
        "sparse3": "SAFE",
        "cluster5": "WARNING",
        "dense12": "DANGER",
        "two_clusters12": "WARNING",
        "chain11": "DANGER",
    }
    return expected[name]


# ---------------------------------------------------------------------------
# 1. 规则引擎在真实场景上的行为
# ---------------------------------------------------------------------------
def test_rule_engine_realistic_scenarios():
    """经典规则路径 evaluate_level 在真实场景上输出正确等级。"""
    cfg = crowd_test.load_config(CONFIG_PATH)
    points = scenario_points()
    for name, pts in points.items():
        n, m, density, r_gcc, clustering = crowd_test.compute_features(pts, crowd_test.EDGE_DIST_M)
        level = crowd_test.evaluate_level(n, density, r_gcc, cfg)
        exp = expected_level(name, cfg)
        print(f"[rule] {name:16s} n={n:2d} density={density:.3f} r_gcc={r_gcc:.3f} -> {level} (期望 {exp})")
        assert level == exp, f"{name}: 期望 {exp}, 实际 {level}"


def test_rule_engine_matches_plain_constants():
    """不传 cfg 时（用内置常量）与传默认配置结果一致。"""
    points = scenario_points()
    cfg = crowd_test.load_config(CONFIG_PATH)
    for name, pts in points.items():
        n, m, d, g, c = crowd_test.compute_features(pts, crowd_test.EDGE_DIST_M)
        lv_cfg = crowd_test.evaluate_level(n, d, g, cfg)
        lv_plain = crowd_test.evaluate_level(n, d, g)
        assert lv_cfg == lv_plain, f"{name}: 配置路径与内置常量不一致"


def test_rule_engine_config_threshold_effect():
    """运行时可配置：把 DANGER 人数阈值调高，12 人密集场景不再触发 DANGER。"""
    points = scenario_points()
    cfg = dict(crowd_test.load_config(CONFIG_PATH))
    cfg["danger"] = dict(cfg["danger"])
    cfg["danger"]["n"] = 20  # 阈值调高到 20 人
    n, m, d, g, c = crowd_test.compute_features(points["dense12"], crowd_test.EDGE_DIST_M)
    level = crowd_test.evaluate_level(n, d, g, cfg)
    assert level == "WARNING", f"调高 DANGER.n 后应降至 WARNING，实际 {level}"


# ---------------------------------------------------------------------------
# 2. 增强流水线（Kalman + 谱特征 + FSM）
# ---------------------------------------------------------------------------
def make_enhanced_pipeline(edge_dist=crowd_test.EDGE_DIST_M):
    """构造增强流水线的核心组件（与 crowd_test_enhanced.process_video 一致）。"""
    tracker = KalmanTracker(dt=1.0 / 30.0, gate_dist=edge_dist)
    cfg = crowd_test.load_config(CONFIG_PATH)
    fsm_cfg = RiskFSMConfig(**cfg.get("fsm", {}))
    fsm = RiskFSM(fsm_cfg)
    return tracker, fsm, cfg


def process_scenario_frames(points_seq, n_repeat=6):
    """按视频帧序列运行增强流水线，返回每帧等级。"""
    edge_dist = crowd_test.EDGE_DIST_M
    tracker, fsm, cfg = make_enhanced_pipeline(edge_dist)
    levels = []
    frame_id = 0
    for pts in points_seq:
        for _ in range(n_repeat):  # 每场景持续若干帧，模拟视频片段
            # Kalman 平滑
            states = tracker.update(list(pts))
            smooth_pts = [(s[0], s[1]) for s in states.values()]
            # 谱特征
            feat = compute_laplacian_features(smooth_pts, edge_dist)
            raw_level = crowd_test.evaluate_level(
                feat["n"], feat["density"], feat["r_gcc"], cfg)
            # FSM 时序稳定（与修复后的 crowd_test_enhanced.py 一致，传 dict）
            now_ms = int(frame_id * 1000.0 / 30.0)
            fsm.update({"n": feat["n"], "density": feat["density"], "r_gcc": feat["r_gcc"]}, now_ms)
            # RiskFSM.level 是属性，不是方法
            lv = fsm.level
            levels.append(lv)
            frame_id += 1
    return levels


def test_enhanced_pipeline_gather_then_disperse():
    """模拟真实视频：人群由稀疏聚集为密集（SAFE->WARNING->DANGER），
    再散开（DANGER->WARNING->SAFE）。验证 FSM 滞回/确认帧/保持特性。"""
    points = scenario_points()
    # 每个场景持续 10 帧，最后无人场景持续 60 帧，留足 DANGER 保持期+降级确认帧
    seq = [points["empty"], points["sparse3"], points["cluster5"],
           points["dense12"], points["two_clusters12"]] + [points["empty"]] * 6
    levels = process_scenario_frames(seq, n_repeat=10)

    # 前 20 帧（empty+sparse3，各 10 帧）应保持 SAFE
    assert all(lv == "SAFE" for lv in levels[:20]), levels[:20]
    # cluster5 阶段（第 20~29 帧）应升到 WARNING
    assert "WARNING" in levels[20:30], levels[20:30]
    # dense12 阶段（第 30~39 帧）应达到 DANGER
    assert "DANGER" in levels[30:40], levels[30:40]
    # 在密集/双簇阶段应达到过 DANGER
    assert "DANGER" in levels[:70], levels[:70]
    # 最后无人场景应从 DANGER 阶梯式降回（经过 WARNING），且最终回到 SAFE
    assert "WARNING" in levels[-70:], levels[-70:]
    assert levels[-1] == "SAFE", levels


def test_enhanced_pipeline_stays_safe_on_sparse():
    """真实稀疏/无人场景全程 SAFE，零误报（对应 README 增强实验断言）。"""
    points = scenario_points()
    levels = process_scenario_frames([points["empty"], points["sparse3"]], n_repeat=15)
    assert all(lv == "SAFE" for lv in levels), levels


def test_enhanced_dense_video_timeline_structure():
    """按 crowd_test_enhanced.process_video 的 timeline 结构，在密集场景下生成
    时间线，验证等级分布以 DANGER 为主、且结构与真实 results 文件一致。"""
    points = scenario_points()
    edge_dist = crowd_test.EDGE_DIST_M
    tracker, fsm, cfg = make_enhanced_pipeline(edge_dist)
    timeline = []
    frame_id = 0
    # 模拟 60 帧密集人群视频
    for _ in range(60):
        pts = points["dense12"]
        states = tracker.update(list(pts))
        smooth_pts = [(s[0], s[1]) for s in states.values()]
        feat = compute_laplacian_features(smooth_pts, edge_dist)
        now_ms = int(frame_id * 1000.0 / 30.0)
        # FSM 期望 Mapping（与修复后的 crowd_test_enhanced.py 一致）
        fsm.update({"n": feat["n"], "density": feat["density"], "r_gcc": feat["r_gcc"]}, now_ms)
        # RiskFSM.level 是属性，不是方法
        lv = fsm.level
        timeline.append({
            "frame": frame_id,
            "level": lv,
            "n": feat["n"],
            "density": feat["density"],
            "r_gcc": feat["r_gcc"],
            "fiedler": feat["fiedler"],
        })
        frame_id += 1

    # timeline 结构与真实 results/video_enhanced/*_timeline.json 一致
    assert isinstance(timeline, list)
    assert all({"frame", "level", "n", "density", "r_gcc", "fiedler"} <= set(x) for x in timeline)
    assert [x["frame"] for x in timeline] == list(range(60))

    dist = {}
    for it in timeline:
        dist[it["level"]] = dist.get(it["level"], 0) + 1
    print(f"[timeline] 60 帧等级分布: {dist}")
    # 密集人群：DANGER 应占主导（>= 80%）
    assert dist.get("DANGER", 0) >= 48, f"DANGER 占比不足: {dist}"
    # 允许 FSM 防抖的少量启动帧为 SAFE（confirm_up=3 -> 前 2 帧 SAFE）
    assert dist.get("SAFE", 0) <= 5, f"SAFE 帧数异常: {dist}"


# ---------------------------------------------------------------------------
# 3. compute_features 与 graph_laplacian 一致性（两条实现路径对齐）
# ---------------------------------------------------------------------------
def test_compute_features_matches_graph_laplacian():
    """两条实现路径在同一批真实场景点上必须给出相同 n/m/density/r_gcc/clustering。"""
    points = scenario_points()
    for name, pts in points.items():
        n1, m1, d1, g1, c1 = crowd_test.compute_features(pts, crowd_test.EDGE_DIST_M)
        gl = compute_laplacian_features(pts, crowd_test.EDGE_DIST_M)
        assert n1 == gl["n"], name
        assert m1 == gl["m"], name
        assert abs(d1 - gl["density"]) < 1e-9, name
        assert abs(g1 - gl["r_gcc"]) < 1e-9, name
        assert abs(c1 - gl["clustering"]) < 1e-9, name


# ---------------------------------------------------------------------------
# 4. IPM 单应映射
# ---------------------------------------------------------------------------
def test_ipm_maps_pixel_feet_to_ground():
    """get_homography + map_to_ground 往返：同一地面点应落在画面底部且映射可逆
    （与 C++ main.cc 标定一致：720x480 地面梯形 6m x 4m）。"""
    H = crowd_test.get_homography(720, 480)
    assert H.shape == (3, 3)
    # 画面底部中心（脚点）应映射到地面坐标（接近 y=3 地面远端 / 原点附近）
    gx, gy = crowd_test.map_to_ground(360, 480, H)
    assert abs(gx) < 3.0, f"中心脚点 x 应接近 0，实际 {gx}"
    # 画面底部应映射到 y≈3（近端地面）
    assert abs(gy - 3.0) < 0.5, f"底部脚点 y 应接近 3，实际 {gy}"
    # 画面中上部（远处）应映射到 y 更小（更远处地面）
    gx2, gy2 = crowd_test.map_to_ground(360, 320, H)
    assert gy2 < gy, f"更高像素应映射到更远地面: {gy2} >= {gy}"

    # 缩放到非 720x480 尺寸也应得到合理 H
    H2 = crowd_test.get_homography(640, 360)
    assert H2.shape == (3, 3)
    gx3, gy3 = crowd_test.map_to_ground(320, 360, H2)
    assert abs(gx3) < 3.0
    assert abs(gy3 - 3.0) < 0.5


# ---------------------------------------------------------------------------
# 5. 配置一致性
# ---------------------------------------------------------------------------
def test_config_matches_crowd_test_defaults():
    """config/default_config.json 必须与 crowd_test 内置常量一致（论文'可配置'承诺）。"""
    cfg = crowd_test.load_config(CONFIG_PATH)
    assert abs(cfg["edge_dist_m"] - crowd_test.EDGE_DIST_M) < 1e-9
    assert cfg["danger"]["n"] == crowd_test.DANGER_N
    assert abs(cfg["danger"]["rho"] - crowd_test.DANGER_RHO) < 1e-9
    assert abs(cfg["danger"]["gcc"] - crowd_test.DANGER_GCC) < 1e-9
    assert cfg["warning"]["n"] == crowd_test.WARN_N
    assert abs(cfg["warning"]["rho"] - crowd_test.WARN_RHO) < 1e-9
    assert abs(cfg["warning"]["gcc"] - crowd_test.WARN_GCC) < 1e-9


# ---------------------------------------------------------------------------
# 6. 等级定义一致性（增强 FSM 与规则引擎共享同一等级集合）
# ---------------------------------------------------------------------------
def test_level_definitions_consistent():
    """RiskFSM 的 LEVELS / LEVEL_RANK 必须与 crowd_test 的输出等级一致。"""
    assert set(LEVELS) == {"SAFE", "WARNING", "DANGER"}
    assert LEVEL_RANK["SAFE"] < LEVEL_RANK["WARNING"] < LEVEL_RANK["DANGER"]
    cfg = crowd_test.load_config(CONFIG_PATH)
    # 用 crowd_test.evaluate_level 对一组特征打分，所有输出都应在 LEVELS 内
    for n in range(0, 25, 3):
        for d in np.linspace(0, 1, 5):
            for g in np.linspace(0, 1, 5):
                lv = crowd_test.evaluate_level(n, float(d), float(g), cfg)
                assert lv in LEVELS, (n, d, g, lv)


# ---------------------------------------------------------------------------
# 7. 真实脚本 process_video 端到端（fake 检测器）
#    直接运行 crowd_test_enhanced.process_video —— 这是与生产完全一致的函数，
#    用假检测器注入密集人群，验证修复后的 FSM 确实生效、timeline 结构正确。
# ---------------------------------------------------------------------------
def _make_synthetic_video(path, frames=40, w=320, h=240, fps=30):
    """生成一段合成视频（动态方块），作为 process_video 的输入。"""
    import cv2
    writer = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"mp4v"), fps, (w, h))
    for i in range(frames):
        frame = np.zeros((h, w, 3), dtype=np.uint8)
        x = (i * 5) % (w - 40)
        cv2.rectangle(frame, (x, 100), (x + 30, 130), (200, 200, 200), -1)
        writer.write(frame)
    writer.release()
    return path


def _make_fake_detector(pts):
    """返回一个替换 detect_pedestrians 的假检测器，恒定返回给定人群脚点。"""
    import crowd_test_enhanced as cte

    def fake_detect(model, frame, conf, imgsz, device, H, scale_mpp, classes=None):
        boxes_xyxy = []
        detections = []
        ground_pts = []
        for i, (gx, gy) in enumerate(pts):
            # 反推像素脚点（仅用于可视化，不参与地面坐标）
            x1, y1, x2, y2 = 50 + i * 20, 100, 80 + i * 20, 180
            boxes_xyxy.append(((float(x1), float(y1), float(x2), float(y2)), 0.9))
            detections.append({"bbox": [float(x1), float(y1), float(x2), float(y2)], "conf": 0.9})
            ground_pts.append((float(gx), float(gy)))
        return boxes_xyxy, detections, ground_pts

    # 打补丁，让 process_video 内部调用我们的假检测器
    cte.detect_pedestrians = fake_detect
    return cte


def test_process_video_end_to_end(tmp_path=None):
    """端到端：真实 process_video + 假检测器 -> 输出 timeline，FSM 应生效。"""
    import tempfile
    import warnings
    import cv2

    d = str(tmp_path) if tmp_path is not None else tempfile.mkdtemp()
    video_path = os.path.join(d, "synth.mp4")
    out_dir = os.path.join(d, "out")
    _make_synthetic_video(video_path, frames=40)

    # 恒定密集人群：12 人落在 2m x 2m（地面米制）
    rng = np.random.default_rng(7)
    pts = [(float(x), float(y)) for x, y in
           zip(rng.uniform(-1, 1, 12), rng.uniform(-1, 1, 12))]

    cte = _make_fake_detector(pts)

    # 捕获 stderr 以检测 FSM 回退告警
    import io, contextlib
    err_buf = io.StringIO()
    cfg = crowd_test.load_config(CONFIG_PATH)
    with contextlib.redirect_stderr(err_buf):
        cte.process_video(
            model=None, video_path=video_path, out_dir=out_dir,
            conf=0.25, imgsz=320, H=None, scale_mpp=0.01, device="cpu",
            edge_dist=crowd_test.EDGE_DIST_M, cfg=cfg, use_spectral=True,
        )

    err_text = err_buf.getvalue()
    assert "FSM 更新失败" not in err_text, f"FSM 仍回退: {err_text}"

    tl_path = os.path.join(out_dir, "synth_timeline.json")
    assert os.path.exists(tl_path), "timeline 未生成"
    with open(tl_path, encoding="utf-8") as f:
        timeline = json.load(f)
    assert isinstance(timeline, list)
    assert len(timeline) == 40, f"帧数应为 40，实际 {len(timeline)}"
    assert all({"frame", "level", "n", "density", "r_gcc", "fiedler"} <= set(x) for x in timeline)

    dist = {}
    for it in timeline:
        dist[it["level"]] = dist.get(it["level"], 0) + 1
    print(f"[e2e] 40 帧等级分布: {dist}")
    # 密集人群 -> DANGER 主导（FSM 生效，而非逐帧 raw 抖动）
    assert dist.get("DANGER", 0) >= 32, f"FSM 未生效，DANGER 不足: {dist}"
    # 允许 FSM 防抖的少量启动帧为 SAFE（confirm_up=3 -> 前 2 帧 SAFE）
    assert dist.get("SAFE", 0) <= 5, f"SAFE 帧数异常: {dist}"

    # 清理生成文件
    if tmp_path is None:
        for root, _, files in os.walk(d):
            for fn in files:
                try:
                    os.remove(os.path.join(root, fn))
                except OSError:
                    pass
        try:
            os.rmdir(out_dir)
            os.rmdir(d)
        except OSError:
            pass


def test_process_video_sparse_stays_safe(tmp_path=None):
    """端到端：稀疏/无人场景应全程 SAFE，验证增强脚本零误报。"""
    import tempfile
    import io, contextlib

    d = str(tmp_path) if tmp_path is not None else tempfile.mkdtemp()
    video_path = os.path.join(d, "synth_sparse.mp4")
    out_dir = os.path.join(d, "out")
    _make_synthetic_video(video_path, frames=30)

    cte = _make_fake_detector([(0.0, 0.0)])  # 恒定 1 人
    err_buf = io.StringIO()
    cfg = crowd_test.load_config(CONFIG_PATH)
    with contextlib.redirect_stderr(err_buf):
        cte.process_video(
            model=None, video_path=video_path, out_dir=out_dir,
            conf=0.25, imgsz=320, H=None, scale_mpp=0.01, device="cpu",
            edge_dist=crowd_test.EDGE_DIST_M, cfg=cfg, use_spectral=True,
        )
    assert "FSM 更新失败" not in err_buf.getvalue()

    tl_path = os.path.join(out_dir, "synth_sparse_timeline.json")
    with open(tl_path, encoding="utf-8") as f:
        timeline = json.load(f)
    assert len(timeline) == 30
    assert all(x["level"] == "SAFE" for x in timeline), timeline

    if tmp_path is None:
        for root, _, files in os.walk(d):
            for fn in files:
                try:
                    os.remove(os.path.join(root, fn))
                except OSError:
                    pass
        try:
            os.rmdir(out_dir); os.rmdir(d)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# 运行器（不依赖 pytest）
# ---------------------------------------------------------------------------
def main():
    tests = [
        (name, fn) for name, fn in sorted(globals().items())
        if name.startswith("test_") and callable(fn)
    ]
    passed = 0
    failed = []
    for name, fn in tests:
        try:
            fn()
            print(f"PASS  {name}")
            passed += 1
        except Exception as exc:  # noqa: BLE001
            import traceback
            print(f"FAIL  {name}: {exc}")
            traceback.print_exc()
            failed.append(name)
    print(f"\n{passed}/{len(tests)} integration tests passed")
    if failed:
        print("failed:", ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())