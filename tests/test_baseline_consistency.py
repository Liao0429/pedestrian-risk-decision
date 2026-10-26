# -*- coding: utf-8 -*-
"""
视频基线一致性回归测试（Agent4/6 评审发现的 OR 基线缺陷修复验证）

背景：旧版 scripts/rerun_video_baseline.py 的 rule_level 用 OR 规则
（n/rho/gcc 任一达标即 DANGER），而真实引擎 crowd_test.evaluate_level 与
C++ RiskEngine::evaluate 用 AND-OR 规则（n 达标 且 (rho 或 gcc) 达标）。
OR 基线会系统性高估基线的 DANGER/切换次数，造成“增强后大幅下降”的假象。

本测试保证：
1. rerun_video_baseline.rule_level 与 crowd_test.evaluate_level 在边界组合下完全一致。
2. 在 OR/AND 分歧点上（n 不达标但 rho 达标）行为正确（SAFE/WARNING 而非 DANGER）。

运行：pytest tests/test_baseline_consistency.py -q
"""
import pathlib
import sys

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from crowd_test import evaluate_level, load_config
from rerun_video_baseline import rule_level

CONFIG_PATH = PROJECT_ROOT / "config" / "default_config.json"


def _load_cfg():
    return load_config(str(CONFIG_PATH))


def test_baseline_rule_level_matches_evaluate_level():
    """基线 rule_level 必须与真实引擎 evaluate_level 在所有边界组合一致。"""
    cfg = _load_cfg()
    for n in (0, 4, 5, 6, 9, 10, 11, 20):
        for rho in (0.0, 0.27, 0.28, 0.29, 0.54, 0.55, 0.56, 1.0):
            for gcc in (0.0, 0.54, 0.55, 0.56, 0.79, 0.80, 0.81, 1.0):
                assert rule_level(n, rho, gcc, cfg) == evaluate_level(n, rho, gcc, cfg), (
                    f"不一致: n={n} rho={rho} gcc={gcc} "
                    f"rule_level={rule_level(n, rho, gcc, cfg)} "
                    f"evaluate_level={evaluate_level(n, rho, gcc, cfg)}"
                )


def test_or_baseline_regression_points():
    """OR 旧逻辑会在这些点上误报 DANGER，修复后必须与引擎一致。"""
    cfg = _load_cfg()
    # n 不达标但 rho/gcc 达标：OR 会判 DANGER，正确应 SAFE（n 未达阈值）
    assert rule_level(4, 1.0, 1.0, cfg) == "SAFE"
    assert evaluate_level(4, 1.0, 1.0, cfg) == "SAFE"
    # n 达标但 rho/gcc 均不达标：AND 应 SAFE
    assert rule_level(10, 0.0, 0.0, cfg) == "SAFE"
    assert evaluate_level(10, 0.0, 0.0, cfg) == "SAFE"
    # n 达标且 rho 达标：DANGER
    assert rule_level(10, 0.55, 0.0, cfg) == "DANGER"
    assert evaluate_level(10, 0.55, 0.0, cfg) == "DANGER"


if __name__ == "__main__":
    test_baseline_rule_level_matches_evaluate_level()
    test_or_baseline_regression_points()
    print("✅ 视频基线一致性测试通过")