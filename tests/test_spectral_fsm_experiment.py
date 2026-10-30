# -*- coding: utf-8 -*-
"""谱特征接入 FSM 的实验裁决回归测试。

验证内容：
1. SpectralRiskFSM 谱修正的数学行为（M1 加分明、M2 抑制高 r_gcc 低 fiedler）。
2. M0 离线重算与已有 timeline 的一致性（≥99%，ms33 模式）。
3. 实验产物 JSON 完整性（scenarios / video_summary / m2_vs_m0_analysis）。
4. 场景判别的关键结论（n10_chain 不被 FSM 判 DANGER；紧密团保持 DANGER）。
"""
import json
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "src"))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))

from algorithms.risk_fsm import RiskFSMConfig  # noqa: E402
from crowd_test import load_config  # noqa: E402
from spectral_fsm_experiment import (  # noqa: E402
    SpectralRiskFSM, fsm_final_level, replay, load_timelines,
    run_scenarios, SCENARIOS, fiedler_norm,
)

EXP_JSON = PROJECT_ROOT / "results" / "rerun" / "_spectral_fsm_experiment.json"

# 111 条视频的逐帧 timeline 是 GPU 全量重跑的本地产物（.gitignore 排除），CI 不收集。
# 依赖它的用例在数据缺失时跳过；其余用例保持纯离线可复现。
TIMELINES = load_timelines()


@pytest.fixture(scope="module")
def cfg():
    return load_config(str(PROJECT_ROOT / "config" / "default_config.json"))


# ---------- 1. 谱修正数学行为 ----------
def test_fiedler_norm_mapping():
    assert fiedler_norm(0.0) is None
    assert fiedler_norm(-1.0) is None
    assert fiedler_norm(0.1) < fiedler_norm(4.3)
    assert 0.0 < fiedler_norm(4.3) < 1.0


def test_m1_positive_feedback():
    cfg_d = {"w1": 0.0, "w2": 0.0, "w3": 0.3, "n_ref": 20,
             "up_warning": 0.4, "up_danger": 0.65,
             "down_safe": 0.28, "down_warning": 0.55,
             "confirm_up": 3, "confirm_down": 5,
             "danger_hold_frames": 10, "danger_hold_ms": 1000,
             "trend_bias": 0.0}
    fsm = SpectralRiskFSM(RiskFSMConfig(**cfg_d), mode="M1", w4=0.1)
    # 纯谱项场景：n=0, density=0, r_gcc=0，score 只由 M1 提供
    lv = fsm.update({"n": 0, "density": 0.0, "r_gcc": 0.0, "fiedler": 4.0}, now_ms=0)
    # fiedler=4 -> fn=0.8 -> score=0.08，不足以升级
    assert fsm.score == pytest.approx(0.08, abs=1e-6)
    assert lv == "SAFE"


def test_m2_suppresses_high_gcc_low_fiedler():
    cfg_d = {"w1": 0.0, "w2": 0.0, "w3": 0.3, "n_ref": 20,
             "up_warning": 0.4, "up_danger": 0.65,
             "down_safe": 0.28, "down_warning": 0.55,
             "confirm_up": 3, "confirm_down": 5,
             "danger_hold_frames": 10, "danger_hold_ms": 1000,
             "trend_bias": 0.0}
    # 链状：r_gcc=1, fiedler=0.1 -> fn=0.0909 -> 惩罚=0.1*0.909=0.0909
    fsm = SpectralRiskFSM(RiskFSMConfig(**cfg_d), mode="M2", w4=0.1)
    fsm.update({"n": 0, "density": 0.0, "r_gcc": 1.0, "fiedler": 0.1}, now_ms=0)
    assert fsm.score == pytest.approx(0.3 - 0.090909, abs=1e-4)
    # 紧密团：r_gcc=1, fiedler=4.3 -> fn=0.811 -> 惩罚=0.0189
    fsm2 = SpectralRiskFSM(RiskFSMConfig(**cfg_d), mode="M2", w4=0.1)
    fsm2.update({"n": 0, "density": 0.0, "r_gcc": 1.0, "fiedler": 4.3}, now_ms=0)
    assert fsm2.score == pytest.approx(0.3 - 0.01887, abs=1e-4)
    # 紧密团惩罚显著小于链状惩罚
    assert (0.3 - fsm.score) > (0.3 - fsm2.score)


def test_m2_neutral_when_spectral_unavailable():
    cfg_d = {"w1": 0.0, "w2": 0.0, "w3": 0.3, "n_ref": 20,
             "up_warning": 0.4, "up_danger": 0.65,
             "down_safe": 0.28, "down_warning": 0.55,
             "confirm_up": 3, "confirm_down": 5,
             "danger_hold_frames": 10, "danger_hold_ms": 1000,
             "trend_bias": 0.0}
    fsm = SpectralRiskFSM(RiskFSMConfig(**cfg_d), mode="M2", w4=0.1)
    fsm.update({"n": 0, "density": 0.0, "r_gcc": 1.0, "fiedler": 0.0}, now_ms=0)
    # fiedler<=0 视为不可用（中性 0.5）：惩罚=0.1*0.5=0.05
    assert fsm.score == pytest.approx(0.3 - 0.05, abs=1e-6)


# ---------- 2. M0 重算一致性 ----------
@pytest.mark.skipif(len(TIMELINES) == 0,
                    reason="缺少本地 results/rerun/video_enhanced 的 111 条 timeline 数据（.gitignore 排除，CI 不收集）")
def test_m0_replay_consistency():
    """离线重算 vs 在线 timeline 的一致性契约。

    数据前提（重要，2026-10-30 algo-py 复核）
    -----------------------------------------
    这 111 条 timeline 是**旧板端构建**的产物，早于 min_danger_n 门控（P0-7,
    commit 66b4ef0）接入。旧数据里存在 "stored=DANGER 但 n < min_danger_n" 的帧
    （实测 1203/45021 = 2.67%），当前配置下不可能再出现，因此"全程逐帧 ≥99%"
    对这份陈旧数据并不成立（实测 98.3186%）—— 旧断言是过期的，不是算法回归。

    现在锁定的三条契约（实测 111/111 视频全部满足）
    -----------------------------------------------
    1. 每条视频在"首个 stored DANGER 且 n < min_danger_n"的帧之前**逐帧完全一致**
       （实测 0/38751 不一致）；
    2. 上述前缀覆盖 ≥85% 的帧（实测 86.07%），保证契约不是空跑；
    3. 剔除门控前历史帧后整体一致率 ≥99%（实测 99.4705%）；若数据已重算、
       不再含这类帧，则该条退化为全程一致率 ≥99%。
    """
    cfg = load_config(str(PROJECT_ROOT / "config" / "default_config.json"))
    timelines = TIMELINES
    assert len(timelines) >= 100, f"timeline 数量异常: {len(timelines)}"
    try:
        import cv2  # noqa: F401
    except ImportError:
        pytest.skip("缺少 cv2，无法读取视频帧率")

    min_danger_n = cfg.get("fsm", {}).get("min_danger_n", 3)
    total_frames = total_same = 0
    prefix_frames = prefix_bad = 0          # 门控前历史帧之前的"纯净前缀"
    gate_frames = gate_same = 0             # 剔除门控前历史帧后的比较集
    for name, tl in timelines:
        cap = cv2.VideoCapture(str(PROJECT_ROOT / "dataset" / "videos50" / f"{name}.mp4"))
        fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
        cap.release()
        # 用生产配置（w4=0.1）+ 真实帧率重算，验证离线重算与真实推理一致
        levels, _, _ = replay(tl, cfg, "M0", 0.0, now_ms_mode="fps", m0_spectral=True, fps=fps)
        stored = [x["level"] for x in tl]
        same_flags = [a == b for a, b in zip(levels, stored)]
        total_same += sum(same_flags)
        total_frames += len(tl)

        # 首个"与当前 min_danger_n 门控矛盾"的历史帧（旧构建未做该门控）
        first_pregate = next(
            (i for i, x in enumerate(tl)
             if x["level"] == "DANGER" and int(x.get("n", 0) or 0) < min_danger_n),
            len(tl),
        )
        prefix_frames += first_pregate
        prefix_bad += sum(1 for i in range(first_pregate) if not same_flags[i])
        for i, x in enumerate(tl):
            if x["level"] == "DANGER" and int(x.get("n", 0) or 0) < min_danger_n:
                continue
            gate_frames += 1
            gate_same += int(same_flags[i])

    assert prefix_bad == 0, (
        f"门控前纯净前缀出现不一致: {prefix_bad}/{prefix_frames} 帧；"
        f"说明当前算法与在线推理确有差异（不只是旧数据缺 min_danger_n 门控）"
    )
    coverage = prefix_frames / total_frames
    assert coverage >= 0.85, f"纯净前缀覆盖率过低: {coverage:.4%}（契约空跑风险）"
    rate = gate_same / gate_frames if gate_frames else 1.0
    assert rate >= 0.99, f"剔除门控前历史帧后 M0 重算一致性低于 99%: {rate:.4%}"


# ---------- 3. 实验产物完整性 ----------
def test_experiment_json_exists_and_complete():
    assert EXP_JSON.is_file(), "请先运行 python scripts/spectral_fsm_experiment.py"
    data = json.loads(EXP_JSON.read_text(encoding="utf-8"))
    assert "scenarios" in data and len(data["scenarios"]) >= 10
    assert "video_summary" in data and "M0" in data["video_summary"]
    assert "m2_vs_m0_analysis" in data
    ana = data["m2_vs_m0_analysis"]
    assert "fiedler_stats" in ana and "M2被降级(原D)帧" in ana["fiedler_stats"]
    assert "video_diff" in ana


def test_scenario_key_findings(cfg):
    rows = run_scenarios(cfg)
    by_name = {r["name"]: r for r in rows}
    # 链状场景：FSM 原配置（M0）不应判 DANGER（规则引擎会误判，FSM 已抑制）
    assert by_name["n10_chain"]["levels"]["M0"] != "DANGER"
    # 紧密团：任何模式都保持 DANGER
    for k, v in by_name["n10_cluster"]["levels"].items():
        assert v == "DANGER", f"紧密团在 {k} 下不是 DANGER: {v}"
    # 分散场景保持 SAFE
    assert by_name["n10_dispersed"]["levels"]["M0"] == "SAFE"
    assert by_name["n12_dispersed"]["levels"]["M0"] == "SAFE"


def test_video_summary_global_behavior(cfg):
    """M1 正反馈不应被采纳（过度敏感）：DANGER 视频数应显著高于 M0；
    M2 抑制改变行为但被裁决不采纳——本测试只锁定行为方向防回归。"""
    data = json.loads(EXP_JSON.read_text(encoding="utf-8"))
    vs = data["video_summary"]
    assert vs["M1_0.1"]["DANGER_videos"] > vs["M0"]["DANGER_videos"] + 10
    assert vs["M2_0.2"]["DANGER_videos"] < vs["M0"]["DANGER_videos"]
