# -*- coding: utf-8 -*-
"""scripts/evaluate_images500_gt.py 的单元测试。

验证 GT 解析、检出率/MAE/RMSE/Pearson、分桶统计、列联表与最差个案计算。
数据为合成输入，不依赖真实结果文件。
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts"))

from evaluate_images500_gt import (  # noqa: E402
    BUCKETS,
    compute_stats,
    parse_rows,
    render_report,
)

# 合成 4 行：覆盖四个 GT 分桶，含欠检与等级差异
SYNTH_RES = [
    {"filename": "000000000001_2", "n": 2, "level": "SAFE"},     # 1-3 桶
    {"filename": "000000000002_5", "n": 5, "level": "WARNING"},  # 4-9 桶
    {"filename": "000000000003_12", "n": 6, "level": "SAFE"},    # 10-13 桶（欠检）
    {"filename": "000000000004_15", "n": 12, "level": "DANGER"},  # 14-19 桶（欠检）
]


def test_parse_rows():
    rows = parse_rows(SYNTH_RES)
    assert len(rows) == 4
    assert rows[0]["gt"] == 2
    assert rows[0]["det"] == 2
    assert rows[0]["err"] == 0
    assert rows[0]["abs_err"] == 0
    assert rows[2]["gt"] == 12
    assert rows[2]["det"] == 6
    assert rows[2]["err"] == -6
    assert rows[2]["abs_err"] == 6


def test_parse_rows_bad_filename():
    with pytest.raises(ValueError):
        parse_rows([{"filename": "no_suffix", "n": 1, "level": "SAFE"}])


def test_compute_stats_overall():
    s = compute_stats(parse_rows(SYNTH_RES))
    assert s["images_total"] == 4
    assert s["gt_total"] == 34
    assert s["det_total"] == 25
    assert s["overall_detection_rate_pct"] == pytest.approx(73.53, abs=0.01)
    assert s["mae"] == pytest.approx(2.25)
    assert s["rmse"] == pytest.approx(3.354, abs=0.001)
    assert s["pearson_r"] == pytest.approx(0.9034, abs=0.0002)


def test_compute_stats_buckets():
    s = compute_stats(parse_rows(SYNTH_RES))
    bs = s["bucket_stats"]
    assert bs["1-3"]["rate_pct"] == pytest.approx(100.0)
    assert bs["4-9"]["rate_pct"] == pytest.approx(100.0)
    assert bs["10-13"]["rate_pct"] == pytest.approx(50.0)
    assert bs["14-19"]["rate_pct"] == pytest.approx(80.0)
    # 每桶图片数
    for name in ["1-3", "4-9", "10-13", "14-19"]:
        assert bs[name]["images"] == 1


def test_compute_stats_contingency():
    s = compute_stats(parse_rows(SYNTH_RES))
    c = s["contingency_gt_bucket_x_level"]
    assert c["1-3"] == {"SAFE": 1, "WARNING": 0, "DANGER": 0}
    assert c["4-9"] == {"SAFE": 0, "WARNING": 1, "DANGER": 0}
    assert c["10-13"] == {"SAFE": 1, "WARNING": 0, "DANGER": 0}
    assert c["14-19"] == {"SAFE": 0, "WARNING": 0, "DANGER": 1}
    assert s["level_counts"] == {"SAFE": 2, "WARNING": 1, "DANGER": 1}


def test_compute_stats_worst_cases():
    s = compute_stats(parse_rows(SYNTH_RES))
    assert len(s["worst_under_detected"]) == 2
    assert s["worst_under_detected"][0]["filename"] == "000000000003_12"
    assert s["worst_under_detected"][0]["err"] == -6
    assert s["worst_over_detected"] == []


def test_render_report_contains_key_figures():
    s = compute_stats(parse_rows(SYNTH_RES))
    report = render_report(s, "results/x.json", "2026-01-01", "yolov8n.pt", 0.25)
    assert "整体检出率 | 73.53%" in report
    assert "MAE（绝对误差均值） | 2.250" in report
    assert "GT 与检测数 Pearson r | 0.9034" in report
    assert "yolov8n.pt" in report
    assert "欠检 Top 10" in report
    assert "过检 Top 10" in report
    assert "不等同于风险判断准确率" in report


def test_bucket_boundaries():
    # 分桶边界与 images500 README 一致：1-3 / 4-9 / 10-13 / 14-19
    from evaluate_images500_gt import bucket_of

    assert bucket_of(1) == "1-3"
    assert bucket_of(3) == "1-3"
    assert bucket_of(4) == "4-9"
    assert bucket_of(9) == "4-9"
    assert bucket_of(10) == "10-13"
    assert bucket_of(13) == "10-13"
    assert bucket_of(14) == "14-19"
    assert bucket_of(19) == "14-19"
    assert bucket_of(20) == "other"
    assert [name for _, _, name in BUCKETS] == ["1-3", "4-9", "10-13", "14-19"]