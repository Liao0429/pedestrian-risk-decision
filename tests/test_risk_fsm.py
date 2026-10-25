# -*- coding: utf-8 -*-
"""
RiskFSM 单元测试
================
可两种方式运行：
  python tests/test_risk_fsm.py          # 独立运行（无需 pytest）
  python -m pytest tests/test_risk_fsm.py  # 若安装了 pytest

验证点：
1. 短时抖动不触发（单帧高分不升级，连续确认帧才升级）；
2. 持续聚集才升级（连续 confirm_up 帧达标才 SAFE->WARNING->DANGER）；
3. 降级有滞回（滞回带内保持原等级，需低于降级阈值且连续 confirm_down 帧）；
4. DANGER 状态保持（帧数 / 毫秒双重保持）；
5. 综合拥挤度评分公式正确性；
6. 配置合法性校验；
7. Python 配置默认值与 C++ 头文件保持一致。
"""

import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJECT_ROOT, "src")
if SRC not in sys.path:
    sys.path.insert(0, SRC)

from algorithms.risk_fsm import LEVELS, LEVEL_RANK, RiskFSM, RiskFSMConfig


# ---------------------------------------------------------------------------
# 工具
# ---------------------------------------------------------------------------
def make_fsm(**cfg_overrides):
    return RiskFSM(RiskFSMConfig(**cfg_overrides))


HIGH = {"n": 20, "density": 1.0, "r_gcc": 1.0}    # score = 1.0
LOW = {"n": 0, "density": 0.0, "r_gcc": 0.0}       # score = 0.0


def feed(fsm, frames, start_ms=0, step_ms=33):
    """按顺序喂入若干帧特征，返回每次 update 后的等级列表。"""
    levels = []
    now = start_ms
    for feat in frames:
        levels.append(fsm.update(feat, now))
        now += step_ms
    return levels


# ---------------------------------------------------------------------------
# 1. 短时抖动不触发
# ---------------------------------------------------------------------------
def test_short_jitter_does_not_trigger():
    fsm = make_fsm()  # confirm_up=3, confirm_down=5
    # 交替出现单帧高分与低分：不应触发升级
    frames = [HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW]
    levels = feed(fsm, frames)
    assert all(lv == "SAFE" for lv in levels), levels
    assert fsm.level == "SAFE"


# ---------------------------------------------------------------------------
# 2. 持续聚集才升级
# ---------------------------------------------------------------------------
def test_sustained_clustering_upgrades_to_danger():
    fsm = make_fsm()  # confirm_up=3
    # 连续高分：第 3 帧 SAFE->WARNING，第 6 帧 WARNING->DANGER
    levels = feed(fsm, [HIGH] * 6)
    assert levels[0] == "SAFE"
    assert levels[1] == "SAFE"
    assert levels[2] == "WARNING", levels
    assert levels[3] == "WARNING"
    assert levels[4] == "WARNING"
    assert levels[5] == "DANGER", levels
    assert fsm.level == "DANGER"


def test_danger_only_reachable_from_warning():
    fsm = make_fsm(confirm_up=3)
    # 即使单帧 score 极高（=1.0），也不能从 SAFE 直接跳到 DANGER
    levels = feed(fsm, [HIGH] * 2 + [LOW] * 5)
    assert all(lv != "DANGER" for lv in levels), levels
    assert fsm.level == "SAFE"


# ---------------------------------------------------------------------------
# 3. 降级有滞回
# ---------------------------------------------------------------------------
def test_warning_hysteresis_band_keeps_state():
    fsm = make_fsm()  # down_safe=0.28, up_warning=0.40
    # 先升到 WARNING
    feed(fsm, [HIGH] * 3)
    assert fsm.level == "WARNING"

    # 滞回带内分数 0.35（> down_safe 0.28 且 < up_danger 0.65）：
    # 从 SAFE 出发 0.35 不会触发，但从 WARNING 不会掉回 SAFE
    mid = {"n": 10, "density": 0.3, "r_gcc": 0.25}
    # score = 0.4*0.5 + 0.3*0.3 + 0.3*0.25 = 0.365
    levels = feed(fsm, [mid] * 5)
    assert all(lv == "WARNING" for lv in levels), levels
    assert fsm.level == "WARNING"


def test_warning_downgrade_requires_confirm_down():
    fsm = make_fsm()  # confirm_down=5
    feed(fsm, [HIGH] * 3)
    assert fsm.level == "WARNING"

    # 分数跌到降级阈值以下，但只持续 3 帧：仍保持 WARNING
    low3 = feed(fsm, [LOW] * 3)
    assert all(lv == "WARNING" for lv in low3), low3
    assert fsm.level == "WARNING"

    # 已累计 down_count=3，再喂 5 帧：第 2 帧（累计第 5 帧）才回到 SAFE
    levels = feed(fsm, [LOW] * 5)
    assert levels[0] == "WARNING", levels
    assert levels[1] == "SAFE", levels
    assert fsm.level == "SAFE"


def test_danger_to_safe_goes_through_warning():
    fsm = make_fsm(confirm_down=5, danger_hold_frames=2, danger_hold_ms=0)
    # 进入 DANGER
    feed(fsm, [HIGH] * 6)
    assert fsm.level == "DANGER"

    # 分数 0.50（> down_safe 0.28，<= down_warning 0.55）：
    # DANGER 先降到 WARNING，而不会直接 SAFE
    mid = {"n": 12, "density": 0.4, "r_gcc": 0.4}
    # score = 0.4*0.6 + 0.3*0.4 + 0.3*0.4 = 0.48
    levels = feed(fsm, [mid] * 10)
    assert "DANGER" in levels[:2]      # 前 2 帧仍处于保持期
    assert levels[-1] == "WARNING", levels
    assert fsm.level == "WARNING"

    # 此时分数 0.48 仍在滞回带（> down_safe），不会继续掉到 SAFE
    more = feed(fsm, [mid] * 5)
    assert all(lv == "WARNING" for lv in more), more
    assert fsm.level == "WARNING"

    # 跌破 down_safe 并持续 confirm_down=5 帧才回 SAFE
    levels = feed(fsm, [LOW] * 5)
    assert levels[4] == "SAFE", levels
    assert fsm.level == "SAFE"


# ---------------------------------------------------------------------------
# 4. DANGER 状态保持
# ---------------------------------------------------------------------------
def test_danger_hold_frames():
    fsm = make_fsm(confirm_up=3, confirm_down=2, danger_hold_frames=4, danger_hold_ms=0)
    feed(fsm, [HIGH] * 6)  # 第 6 帧进入 DANGER
    assert fsm.level == "DANGER"

    # 进入后立即给低分：保持期内不得降级
    levels = feed(fsm, [LOW] * 5)
    # 进入帧为第 6 帧，保持 4 帧：
    #   第 7~9 帧（diff 1~3）仍保持 DANGER；
    #   第 10 帧（diff 4）保持期结束，开始累计降级（confirm_down=2）；
    #   第 11 帧降为 WARNING。
    assert levels[0] == "DANGER", levels   # frame 7
    assert levels[1] == "DANGER", levels   # frame 8
    assert levels[2] == "DANGER", levels   # frame 9
    assert levels[3] == "DANGER", levels   # frame 10（保持结束，降级计数 1）
    assert levels[4] == "WARNING", levels  # frame 11（降级计数 2 -> WARNING）
    assert fsm.level == "WARNING"


def test_danger_hold_frames_without_timestamp():
    """now_ms=None（无时间戳）时帧保持应生效（回归：修复前 _hold_elapsed 早退致帧保持失效）。"""
    fsm = make_fsm(confirm_up=3, confirm_down=2, danger_hold_frames=4, danger_hold_ms=0)
    levels = [fsm.update(HIGH, None) for _ in range(6)]  # 第 6 帧进入 DANGER
    assert fsm.level == "DANGER"
    # 进入后立即给低分：保持期内不得降级（now_ms 全程 None）
    for _ in range(5):
        levels.append(fsm.update(LOW, None))
    # 进入帧为第 6 帧，保持 4 帧：
    assert levels[6] == "DANGER", levels   # frame 7
    assert levels[7] == "DANGER", levels   # frame 8
    assert levels[8] == "DANGER", levels   # frame 9
    assert levels[9] == "DANGER", levels   # frame 10（保持结束，降级计数 1）
    assert levels[10] == "WARNING", levels  # frame 11（降级计数 2 -> WARNING）
    assert fsm.level == "WARNING"


def test_danger_hold_ms():
    fsm = make_fsm(confirm_up=3, confirm_down=2, danger_hold_frames=0, danger_hold_ms=1000)
    # 用递增时间戳进入 DANGER
    now = 0
    for _ in range(6):
        fsm.update(HIGH, now)
        now += 33
    assert fsm.level == "DANGER"
    entry_ms = now - 33  # 进入 DANGER 的时间戳

    # 保持期内给低分，时间未到 -> 仍 DANGER
    for _ in range(10):
        now += 100
        lv = fsm.update(LOW, now)
        assert lv == "DANGER", (lv, now - entry_ms)

    # 循环最后一步（now=1165，diff=1033）保持期刚结束，down_count 已累计 1，
    # 但 level 仍为 DANGER；再喂 1 帧低分（累计第 2 帧）才降级到 WARNING
    assert fsm.level == "DANGER", fsm.level
    lv = fsm.update(LOW, now + 1)
    assert lv == "WARNING", lv


# ---------------------------------------------------------------------------
# 5. 评分公式
# ---------------------------------------------------------------------------
def test_score_formula():
    fsm = make_fsm()
    fsm.update({"n": 10, "density": 0.5, "r_gcc": 0.25}, 0)
    # score = 0.4*(10/20) + 0.3*0.5 + 0.3*0.25 = 0.2 + 0.15 + 0.075 = 0.425
    assert abs(fsm.score - 0.425) < 1e-9, fsm.score

    # 人数超过 n_ref 时 norm(n) 封顶为 1
    fsm.update({"n": 100, "density": 0.0, "r_gcc": 0.0}, 0)
    assert abs(fsm.score - 0.4) < 1e-9, fsm.score

    # 运动趋势加成
    fsm2 = make_fsm(trend_bias=0.05)
    fsm2.update({"n": 10, "density": 0.5, "r_gcc": 0.25, "trend": 1.0}, 0)
    assert abs(fsm2.score - (0.425 + 0.05)) < 1e-9, fsm2.score

    # 负趋势抑制
    fsm2.update({"n": 10, "density": 0.5, "r_gcc": 0.25, "trend": -1.0}, 0)
    assert abs(fsm2.score - (0.425 - 0.05)) < 1e-9, fsm2.score


def test_score_clamped_to_unit_interval():
    fsm = make_fsm(w1=1.0, w2=1.0, w3=1.0, trend_bias=1.0)
    fsm.update({"n": 100, "density": 1.0, "r_gcc": 1.0, "trend": 1.0}, 0)
    assert 0.0 <= fsm.score <= 1.0
    assert fsm.score == 1.0


# ---------------------------------------------------------------------------
# 6. 配置校验
# ---------------------------------------------------------------------------
def test_config_validation():
    def expect_raise(**kwargs):
        try:
            RiskFSMConfig(**kwargs)
        except ValueError:
            return
        raise AssertionError(f"应抛出 ValueError: {kwargs}")

    expect_raise(up_warning=0.20, down_safe=0.30)   # 滞回带被破坏
    expect_raise(up_danger=0.50, down_warning=0.60)
    expect_raise(confirm_up=0)                     # 确认帧必须 >= 1
    expect_raise(w1=-1.0)                           # 权重不能为负
    expect_raise(w1=0.0, w2=0.0, w3=0.0)           # 权重和必须 > 0
    expect_raise(n_ref=0)                          # n_ref 必须为正
    expect_raise(danger_hold_frames=-1)            # 保持帧数不能为负


def test_reset():
    fsm = make_fsm()
    feed(fsm, [HIGH] * 6)
    assert fsm.level == "DANGER"
    fsm.reset()
    assert fsm.level == "SAFE"
    assert fsm.score == 0.0
    # 重置后抖动仍不触发
    levels = feed(fsm, [HIGH, LOW, HIGH, LOW])
    assert all(lv == "SAFE" for lv in levels), levels


# ---------------------------------------------------------------------------
# 7. C++ 头文件与 Python 默认值一致性
# ---------------------------------------------------------------------------
def test_cpp_header_defaults_consistent():
    hdr = os.path.join(SRC, "algorithms", "risk_fsm.h")
    with open(hdr, "r", encoding="utf-8") as f:
        text = f.read()

    cfg = RiskFSMConfig()
    # 每个默认值应在头文件里以 "名字 = 值" 形式出现
    pairs = [
        ("w1", cfg.w1), ("w2", cfg.w2), ("w3", cfg.w3),
        ("n_ref", cfg.n_ref),
        ("up_warning", cfg.up_warning), ("up_danger", cfg.up_danger),
        ("down_safe", cfg.down_safe), ("down_warning", cfg.down_warning),
        ("confirm_up", cfg.confirm_up), ("confirm_down", cfg.confirm_down),
        ("danger_hold_frames", cfg.danger_hold_frames),
        ("danger_hold_ms", cfg.danger_hold_ms),
        ("trend_bias", cfg.trend_bias),
    ]
    for name, value in pairs:
        literal = f"{name} = {value}"
        assert literal in text, f"头文件缺少默认值: {literal}"

    # 等级名与 Python 端一致
    for lvl in ("SAFE", "WARNING", "DANGER"):
        assert f'"{lvl}"' in text, f"头文件缺少等级名: {lvl}"


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
    print(f"\n{passed}/{len(tests)} tests passed")
    if failed:
        print("failed:", ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())