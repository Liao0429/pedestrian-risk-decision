# -*- coding: utf-8 -*-
"""
对比汇总一致性回归测试（Agent4/交叉A 评审：OR 基线缺陷的长期防护）

验证点：
1. results/rerun/_comparison_summary.json 使用公平 AND 基线（与真实引擎一致），
   不允许再出现 OR 基线的高估数值（旧值：base_sw=5147、DANGER 帧 21969、102 条）。
2. 每条视频的 baseline timeline 帧级等级可用 evaluate_level（AND 规则）逐帧重算复现。
3. 关键聚合值与 README 一致（base_safe=63、enh_safe=31、base_danger=24、enh_danger=40）。

运行：pytest tests/test_comparison_consistency.py -q
"""
import glob
import json
import pathlib
import sys

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from crowd_test import evaluate_level, load_config


def _load_cmp():
    p = PROJECT_ROOT / "results" / "rerun" / "_comparison_summary.json"
    assert p.exists(), f"缺少 {p}"
    return json.loads(p.read_text(encoding="utf-8"))


def _cfg():
    return load_config(str(PROJECT_ROOT / "config" / "default_config.json"))


def test_comparison_uses_fair_and_baseline():
    """聚合值必须是 AND 基线量级（旧 OR 基线已废弃）。"""
    cmp = _load_cmp()
    assert cmp["base_sw"] < 3000, f"base_sw={cmp['base_sw']} 仍像 OR 基线（旧值 5147）"
    assert cmp["base_lv"]["DANGER"] < 5000, f"基线 DANGER 帧 {cmp['base_lv']['DANGER']} 仍像 OR 基线（旧值 21969）"
    assert cmp["base_danger"] == 24, "出现 DANGER 的基线视频数应为 24（AND 基线）"
    assert cmp["enh_danger"] == 40, "出现 DANGER 的增强视频数应为 40"
    assert cmp["base_safe"] == 63, "基线全程 SAFE 视频数应为 63"
    assert cmp["enh_safe"] == 31, "增强全程 SAFE 视频数应为 31"


def test_baseline_timelines_reproducible_with_and_rule():
    """逐帧抽查：baseline timeline 的 level 可用 AND 规则逐帧重算复现（抽样 20 条视频）。

    注意：results/rerun/video_baseline/*/ 被 .gitignore 忽略（仅汇总 JSON 入库），
    仓库克隆/CI 环境没有逐帧数据时本测试自动跳过，避免误报。
    """
    cfg = _cfg()
    files = sorted(glob.glob(str(PROJECT_ROOT / "results" / "rerun" / "video_baseline" / "*" / "*_baseline.json")))
    if len(files) < 100:
        import pytest
        pytest.skip(f"逐帧基线数据未入库（当前 {len(files)} 个），跳过复现检查")
    checked = 0
    for bf in files[::6]:  # 抽样 20 条左右
        b = json.loads(pathlib.Path(bf).read_text(encoding="utf-8"))
        for f in b["timeline"]:
            expected = evaluate_level(f["n"], f["density"], f["r_gcc"], cfg)
            assert expected == f["level"], (
                f"{b['video']} frame {f.get('frame')} 等级不一致: "
                f"n={f['n']} rho={f['density']} gcc={f['r_gcc']} -> {expected} != {f['level']}"
            )
        checked += 1
    assert checked >= 18, f"抽样视频数不足：{checked}"


if __name__ == "__main__":
    test_comparison_uses_fair_and_baseline()
    test_baseline_timelines_reproducible_with_and_rule()
    print("✅ 对比汇总一致性测试通过")