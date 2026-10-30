# -*- coding: utf-8 -*-
"""硬件侧运行配置一致性测试
==========================

覆盖 ``config/default_config.json`` 在板端硬件链路中使用到的字段：

1. ``conf_threshold``：JSON 声明值 0.35（板端运行时消费，2026-10-30 起接入
   ``risk_config.h``；离线原型脚本默认 0.25）；边界 0/1 合法；越界、非数值、
   布尔值抛异常。
2. ``target_classes``：默认 ``[0]``（person）；可切换 ``[2, 5]``（车辆）；空数组/越界 id 抛异常。
3. ``class_conf_thresholds``：按 COCO 类别差异化阈值，键必须是 0~79，值必须在 [0, 1]。
4. ``fsm.min_danger_n``：默认 3；小于 2 抛异常；并验证 Python 端 RiskFSM 的人数门控行为。
5. ``homography_file``：默认空串；可加载外部 3x3 单应文本；非法类型抛异常；
   并验证 ``ipm_calibrate`` 的 3x3 文本读写与错误处理。

说明
----
C++ 端 ``src/luckfox_rtsp_yolov5/include/risk_config.h`` 是 header-only，
pytest 无法直接链接；本文件中的 :func:`load_risk_config` 按该头文件的
``loadRiskConfig()`` 语义实现同一套校验（默认值、范围、异常），用于把
JSON -> 运行时配置的契约固定下来。C++ 侧对应断言见
``src/luckfox_rtsp_yolov5/tests/test_risk_config.cc``。

运行：
    python -m pytest tests/test_hardware_config_consistency.py -v
"""

import json
import math
import os
import sys

import numpy as np
import pytest

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")
if SRC_DIR not in sys.path:
    sys.path.insert(0, SRC_DIR)

from algorithms.ipm_calibrate import load_homography, save_homography  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402

CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "default_config.json")

# conf_threshold 的"声明默认值"。语义（2026-10-30 裁决）：
#   - 板端运行时消费该键（risk_config.h 接入后生效），真源是 config/default_config.json；
#   - 离线原型脚本 scripts/crowd_test.py 的 --conf 默认值保持 0.25（原型阈值语义不同）；
#   - 历史沿革：0.60 -> (e1feef8 按键可调特性) -> a920704 revert -> fdece0d 定为 0.35。
#     本文件旧的 0.60 断言是上述 revert 后的过期断言（algo-py round3 修正）。
DEFAULT_CONF_THRESHOLD = 0.35
DEFAULT_TARGET_CLASSES = [0]
DEFAULT_MIN_DANGER_N = 3


# ---------------------------------------------------------------------------
# 被测契约：risk_config.h loadRiskConfig() 的 Python 镜像
# ---------------------------------------------------------------------------
def _is_number(value):
    """JSON 数值判定：排除 bool（Python 中 bool 是 int 的子类）。"""
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def load_risk_config(raw=None, path=CONFIG_PATH):
    """解析并校验硬件侧风险配置，非法值抛 ``ValueError``。

    参数
    ----
    raw : dict, optional
        已解析的配置对象；为 None 时读取 ``path``。
    path : str
        JSON 配置路径，默认 ``config/default_config.json``。

    返回
    ----
    dict : 归一化后的配置（缺失字段填入默认值）。
    """
    if raw is None:
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)

    if not isinstance(raw, dict):
        raise ValueError("配置根节点必须是对象")

    cfg = {}

    # 1) 全局检测置信度阈值 [0, 1]，默认 0.60
    if "conf_threshold" in raw:
        value = raw["conf_threshold"]
        if not _is_number(value) or not (0.0 <= float(value) <= 1.0):
            raise ValueError("conf_threshold 必须是 [0,1] 范围内的数值")
        cfg["conf_threshold"] = float(value)
    else:
        cfg["conf_threshold"] = DEFAULT_CONF_THRESHOLD

    # 2) 检测目标类别集合（COCO id 0~79），默认 [0]=person
    if "target_classes" in raw:
        ids = raw["target_classes"]
        if not isinstance(ids, list) or not ids:
            raise ValueError("target_classes 必须是非空数组")
        classes = []
        for item in ids:
            if not _is_number(item) or float(item) < 0.0 or float(item) > 79.0:
                raise ValueError("target_classes 元素必须是 0~79 的 COCO 类别 id")
            classes.append(int(item))
        cfg["target_classes"] = classes
    elif "target_class" in raw:  # 兼容旧的单类别配置
        value = raw["target_class"]
        if not _is_number(value) or float(value) < 0.0 or float(value) > 79.0:
            raise ValueError("target_class 必须是 0~79 的 COCO 类别 id")
        cfg["target_classes"] = [int(value)]
    else:
        cfg["target_classes"] = list(DEFAULT_TARGET_CLASSES)

    # 3) 按类别差异化置信度阈值：{"0": 0.50, "2": 0.60}，默认空 = 不区分
    cfg["class_conf_thresholds"] = {}
    if "class_conf_thresholds" in raw:
        table = raw["class_conf_thresholds"]
        if not isinstance(table, dict):
            raise ValueError("class_conf_thresholds 必须是对象")
        for key, value in table.items():
            try:
                cls_id = int(key)
            except (TypeError, ValueError):
                raise ValueError(
                    "class_conf_thresholds 的键必须是 COCO id（0~79）: %r" % (key,)
                )
            if str(cls_id) != key.strip() and key.strip() != ("0" + str(cls_id)):
                # 仅接受规范十进制写法（允许 "07"），拒绝 "7.0" / " 7x"
                raise ValueError(
                    "class_conf_thresholds 的键必须是 COCO id（0~79）: %r" % (key,)
                )
            if cls_id < 0 or cls_id > 79:
                raise ValueError(
                    "class_conf_thresholds 的键必须是 0~79 整数（COCO id）: %r" % (key,)
                )
            if not _is_number(value) or not (0.0 <= float(value) <= 1.0):
                raise ValueError(
                    "class_conf_thresholds 的值必须在 [0,1]: %r" % (key,)
                )
            cfg["class_conf_thresholds"][cls_id] = float(value)

    # 4) FSM 最小 DANGER 人数门槛，默认 3（<2 无意义）
    min_danger_n = DEFAULT_MIN_DANGER_N
    fsm = raw.get("fsm", {})
    if not isinstance(fsm, dict):
        raise ValueError("fsm 必须是对象")
    if "min_danger_n" in fsm:
        value = fsm["min_danger_n"]
        if not _is_number(value) or float(value) < 2.0:
            raise ValueError("fsm.min_danger_n 必须 >= 2")
        min_danger_n = int(value)
    cfg["min_danger_n"] = min_danger_n

    # 5) 外部 IPM 单应标定文件路径，默认空串（用硬编码单应）
    if "homography_file" in raw:
        value = raw["homography_file"]
        if not isinstance(value, str):
            raise ValueError("homography_file 必须是字符串路径")
        cfg["homography_file"] = value
    else:
        cfg["homography_file"] = ""

    return cfg


def write_json(path, obj):
    path.write_text(json.dumps(obj), encoding="utf-8")
    return str(path)


# ---------------------------------------------------------------------------
# 1. conf_threshold
# ---------------------------------------------------------------------------
class TestConfThreshold:
    def test_default_config_declares_035(self):
        """配置未显式声明 conf_threshold 时，取声明默认 0.35（与 JSON 真源一致）。"""
        cfg = load_risk_config({"edge_dist_m": 2.0})
        assert cfg["conf_threshold"] == pytest.approx(0.35)

    def test_default_config_file_loads(self):
        """磁盘配置的 conf_threshold 必须等于声明默认值 0.35（板端真源不得漂移）。"""
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        cfg = load_risk_config(raw)
        assert cfg["conf_threshold"] == pytest.approx(0.35)

    @pytest.mark.parametrize("value", [0, 0.0, 1, 1.0, 0.60, 0.25])
    def test_boundaries_and_common_values_accepted(self, value):
        cfg = load_risk_config({"conf_threshold": value})
        assert cfg["conf_threshold"] == float(value)

    @pytest.mark.parametrize(
        "value", [-0.01, -1, 1.01, 1.5, float("nan"), float("inf")]
    )
    def test_out_of_range_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"conf_threshold": value})

    @pytest.mark.parametrize("value", ["0.5", None, True, False, [0.5], {"v": 0.5}])
    def test_non_numeric_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"conf_threshold": value})


# ---------------------------------------------------------------------------
# 2. target_classes
# ---------------------------------------------------------------------------
class TestTargetClasses:
    def test_default_is_person_only(self):
        cfg = load_risk_config({"edge_dist_m": 2.0})
        assert cfg["target_classes"] == [0]

    def test_default_config_file_is_person_only(self):
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        assert load_risk_config(raw)["target_classes"] == [0]

    def test_switch_to_vehicles(self, tmp_path):
        path = write_json(tmp_path / "vehicles.json", {"target_classes": [2, 5]})
        cfg = load_risk_config(path=path)
        assert cfg["target_classes"] == [2, 5]
        assert 0 not in cfg["target_classes"]

    def test_legacy_single_class(self):
        assert load_risk_config({"target_class": 2})["target_classes"] == [2]

    @pytest.mark.parametrize("value", [[], "0", 0, [None], [[]]])
    def test_invalid_container_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"target_classes": value})

    @pytest.mark.parametrize("value", [-1, 80, 100, 2.5e2])
    def test_out_of_range_id_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"target_classes": [value]})

    @pytest.mark.parametrize("value", ["0", None, True])
    def test_non_numeric_id_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"target_classes": [value]})


# ---------------------------------------------------------------------------
# 3. class_conf_thresholds
# ---------------------------------------------------------------------------
class TestClassConfThresholds:
    def test_default_is_empty(self):
        assert load_risk_config({"edge_dist_m": 2.0})["class_conf_thresholds"] == {}

    def test_per_class_values(self):
        cfg = load_risk_config(
            {"class_conf_thresholds": {"0": 0.50, "2": 0.60, "5": 0.70}}
        )
        table = cfg["class_conf_thresholds"]
        assert table[0] == pytest.approx(0.50)
        assert table[2] == pytest.approx(0.60)
        assert table[5] == pytest.approx(0.70)
        # 差异化：不同类别阈值可不同，缺失类别不额外过滤
        assert table[0] != table[2]
        assert 3 not in table

    def test_padded_key_accepted(self):
        cfg = load_risk_config({"class_conf_thresholds": {"07": 0.4}})
        assert cfg["class_conf_thresholds"] == {7: pytest.approx(0.4)}

    @pytest.mark.parametrize("key", ["person", "7.0", "-1", "80", "7x"])
    def test_invalid_key_rejected(self, key):
        with pytest.raises(ValueError):
            load_risk_config({"class_conf_thresholds": {key: 0.5}})

    @pytest.mark.parametrize("value", [-0.1, 1.5, "0.5", None, True])
    def test_invalid_value_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"class_conf_thresholds": {"0": value}})

    def test_non_object_rejected(self):
        with pytest.raises(ValueError):
            load_risk_config({"class_conf_thresholds": [0.5]})


# ---------------------------------------------------------------------------
# 4. min_danger_n
# ---------------------------------------------------------------------------
class TestMinDangerN:
    def test_default_is_three(self):
        assert load_risk_config({"edge_dist_m": 2.0})["min_danger_n"] == 3

    def test_default_config_file_is_three(self):
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        assert load_risk_config(raw)["min_danger_n"] == 3

    @pytest.mark.parametrize("value", [2, 3, 5, 20])
    def test_valid_values(self, value):
        assert load_risk_config({"fsm": {"min_danger_n": value}})["min_danger_n"] == value

    @pytest.mark.parametrize("value", [1, 0, -1, 1.9])
    def test_below_two_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"fsm": {"min_danger_n": value}})

    @pytest.mark.parametrize("value", ["3", None, True])
    def test_non_numeric_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"fsm": {"min_danger_n": value}})

    def test_python_fsm_default_and_validation(self):
        assert RiskFSMConfig().min_danger_n == 3
        with pytest.raises(ValueError):
            RiskFSMConfig(min_danger_n=1)

    def test_python_fsm_gates_danger_by_person_count(self):
        high = {"n": 2, "density": 1.0, "r_gcc": 1.0}

        small = RiskFSM(RiskFSMConfig(min_danger_n=3, confirm_up=1))
        levels = [small.update(high, None) for _ in range(5)]
        assert levels[-1] == "WARNING"  # 2 人：永远不升 DANGER

        enough = RiskFSM(RiskFSMConfig(min_danger_n=3, confirm_up=1))
        levels = [enough.update({"n": 3, "density": 1.0, "r_gcc": 1.0}, None)
                  for _ in range(5)]
        assert levels[-1] == "DANGER"  # 3 人：允许升级


# ---------------------------------------------------------------------------
# 5. homography_file / IPM 单应加载
# ---------------------------------------------------------------------------
class TestHomographyFile:
    def test_default_is_empty(self):
        assert load_risk_config({"edge_dist_m": 2.0})["homography_file"] == ""

    def test_default_config_file_is_empty(self):
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            raw = json.load(f)
        assert load_risk_config(raw)["homography_file"] == ""

    def test_path_accepted(self):
        cfg = load_risk_config({"homography_file": "/userdata/ipm/H.txt"})
        assert cfg["homography_file"] == "/userdata/ipm/H.txt"

    @pytest.mark.parametrize("value", [123, None, [], True])
    def test_non_string_rejected(self, value):
        with pytest.raises(ValueError):
            load_risk_config({"homography_file": value})

    def test_txt_roundtrip(self, tmp_path):
        H = np.array([[3.0, 0.5, 100.0],
                      [0.2, 4.0, 200.0],
                      [0.001, 0.002, 1.0]], dtype=np.float64)
        path = save_homography(H, str(tmp_path / "H.txt"))
        loaded = load_homography(path)
        assert loaded.shape == (3, 3)
        assert loaded.dtype == np.float64
        assert np.allclose(loaded, H)

    def test_csv_extension_roundtrip(self, tmp_path):
        H = np.array([[1.0, 0.0, 5.0],
                      [0.0, 1.0, 6.0],
                      [0.0, 0.0, 1.0]], dtype=np.float64)
        path = save_homography(H, str(tmp_path / "H.csv"))
        loaded = load_homography(path)
        assert loaded.shape == (3, 3)
        assert np.allclose(loaded, H)

    def test_config_path_used_for_ipm(self, tmp_path):
        H = np.array([[2.0, 0.0, 10.0],
                      [0.0, 2.0, 20.0],
                      [0.0, 0.0, 1.0]], dtype=np.float64)
        h_path = save_homography(H, str(tmp_path / "calib.txt"))
        cfg = load_risk_config({"homography_file": h_path})
        assert cfg["homography_file"] == h_path
        assert np.allclose(load_homography(cfg["homography_file"]), H)

    def test_missing_file_rejected(self, tmp_path):
        with pytest.raises(FileNotFoundError):
            load_homography(str(tmp_path / "nope.txt"))

    def test_non_3x3_rejected(self, tmp_path):
        path = tmp_path / "bad.txt"
        path.write_text("1 0\n0 1\n", encoding="utf-8")
        with pytest.raises(ValueError):
            load_homography(str(path))

    def test_non_numeric_rejected(self, tmp_path):
        path = tmp_path / "bad.txt"
        path.write_text("a b c\nd e f\ng h i\n", encoding="utf-8")
        with pytest.raises(ValueError):
            load_homography(str(path))

    def test_nan_is_not_silently_replaced(self, tmp_path):
        """load_homography 不做数值清理：NaN 原样返回，调用方需自行判断。"""
        path = tmp_path / "bad.txt"
        path.write_text("1 0 0\n0 1 0\n0 0 nan\n", encoding="utf-8")
        H = load_homography(str(path))
        assert not math.isfinite(H[2, 2])


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))