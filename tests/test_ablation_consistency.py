# -*- coding: utf-8 -*-
"""
消融/实验一致性回归测试（Agent7 建议）

验证点：
1. ablation_study.rule_level 与 crowd_test.evaluate_level 在相同输入下结果一致
   （消融基线使用 AND 逻辑，与真实规则引擎一致，不再 OR 失真）。
2. 配置加载后 cfg["fsm"] 存在；脚本不再读取已废弃的 "temporal" 键。
3. crowd_test_enhanced / ablation_study / 集成测试均使用 "fsm" 配置键。

运行：pytest tests/test_ablation_consistency.py -q
"""
import os
import pathlib
import sys

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from crowd_test import evaluate_level, load_config
from ablation_study import rule_level

CONFIG_PATH = PROJECT_ROOT / "config" / "default_config.json"


def _load_cfg():
    return load_config(str(CONFIG_PATH)) if os.path.exists(CONFIG_PATH) else None


def test_rule_level_matches_evaluate_level():
    """消融基线(rule_level)必须与真实规则引擎(evaluate_level)一致。"""
    cfg = _load_cfg()
    # 遍历典型边界组合（n/rho/gcc 在阈值两侧）
    for n in (0, 4, 5, 6, 9, 10, 11, 20):
        for rho in (0.0, 0.27, 0.28, 0.29, 0.54, 0.55, 0.56, 1.0):
            for gcc in (0.0, 0.54, 0.55, 0.56, 0.79, 0.80, 0.81, 1.0):
                assert rule_level(n, rho, gcc, cfg) == evaluate_level(n, rho, gcc, cfg), (
                    f"不一致: n={n} rho={rho} gcc={gcc} "
                    f"rule_level={rule_level(n, rho, gcc, cfg)} "
                    f"evaluate_level={evaluate_level(n, rho, gcc, cfg)}"
                )


def test_config_uses_fsm_key():
    """配置加载后应含 fsm 键，且不含废弃的 temporal 键。"""
    cfg = _load_cfg()
    assert cfg is not None
    assert "fsm" in cfg, "config/default_config.json 应包含 fsm 配置块"
    assert "temporal" not in cfg, "不应再使用 temporal 键（脚本已迁移到 fsm）"


def test_scripts_do_not_read_temporal_key():
    """脚本/测试不得再引用已废弃的 temporal 配置键。"""
    files = [
        PROJECT_ROOT / "scripts" / "crowd_test_enhanced.py",
        PROJECT_ROOT / "scripts" / "ablation_study.py",
        PROJECT_ROOT / "tests" / "test_integration_pipeline.py",
    ]
    for fp in files:
        if fp.exists():
            text = fp.read_text(encoding="utf-8")
            assert '"temporal"' not in text and "temporal" not in text, f"{fp.name} 仍引用 temporal 键"


if __name__ == "__main__":
    test_rule_level_matches_evaluate_level()
    test_config_uses_fsm_key()
    test_scripts_do_not_read_temporal_key()
    print("✅ 消融一致性测试通过")