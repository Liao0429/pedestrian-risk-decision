# -*- coding: utf-8 -*-
"""
风险决策有限状态机（Risk Decision Finite-State Machine, RiskFSM）
=================================================================

背景
----
原 `RiskEngine::evaluate()` 使用固定阈值硬切（`n>=DANGER_N && (density>=... ||
r_gcc>=...)`），单帧结果直接判定，检测抖动 / 脚点抖动会导致等级在
SAFE/WARNING/DANGER 之间逐帧闪烁。

本模块实现一个带**内部状态**的风险决策 FSM，对每帧复杂网络特征
（人数 n / 密度 density / 最大连通分量占比 r_gcc）+ 可选运动趋势 trend 做时序
平滑与分级，解决三个问题：

1. **滞回（hysteresis）**：升级与降级使用不同的阈值带，消除阈值附近抖动。
2. **确认帧（debounce）**：升级需连续 `confirm_up` 帧满足；降级需连续
   `confirm_down` 帧满足。
3. **状态保持（hold）**：进入 DANGER 后至少保持 `danger_hold_frames` 帧 /
   `danger_hold_ms` 毫秒，防止瞬时回落。

分级依据
--------
综合拥挤度评分（归一化到 [0,1]）：

    score = w1 * norm(n) + w2 * density + w3 * r_gcc
              + w4 * fiedler_norm * r_gcc     # 谱特征修正项（可选）

    其中：
        norm(n) = min(n / n_ref, 1.0)   # 人数归一化
        density, r_gcc ∈ [0,1]          # 网络密度 / 最大连通分量占比
        fiedler_norm = fiedler / (fiedler + 1) ∈ [0,1)   # Fiedler 值单调归一化
        trend 为可选运动趋势（∈[-1,1]），>0 表示聚集/人数上升，会小幅抬升 score：
            score += trend_bias * clamp(trend, -1, 1)

    谱特征修正项只对连通结构生效：紧密聚集（fiedler 大）显著加分，
    链状伪聚集（r_gcc=1 但 fiedler 小）加分极小，分散场景（r_gcc≈0）不加分。

阈值采用**状态相关（动态）滞回阈值**：同一 score 在不同状态下对应不同目标等级，
从而形成重叠区间（滞回带）：

    SAFE    --(score >= up_warning)-->     WARNING
    WARNING --(score >= up_danger)-->     DANGER
    WARNING --(score <= down_safe)-->      SAFE
    DANGER  --(score <= down_warning)-->  WARNING

约定：DANGER 只能从 WARNING 阶梯式升级到达（不允许 SAFE 单帧跳 DANGER），
进一步抑制突变噪声。

用法
----
>>> from algorithms.risk_fsm import RiskFSM, RiskFSMConfig
>>> fsm = RiskFSM(RiskFSMConfig(confirm_up=3, confirm_down=5))
>>> level = fsm.update({"n": 12, "density": 0.6, "r_gcc": 0.8}, now_ms=1234)
>>> print(level)
'SAFE'
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Mapping, Optional, Tuple

# 等级常量（顺序即等级序）
LEVELS: Tuple[str, ...] = ("SAFE", "WARNING", "DANGER")
LEVEL_RANK: Dict[str, int] = {"SAFE": 0, "WARNING": 1, "DANGER": 2}


def _clamp(value: float, low: float, high: float) -> float:
    if value < low:
        return low
    if value > high:
        return high
    return value


@dataclass
class RiskFSMConfig:
    """RiskFSM 配置项。

    默认值与 C++ 端 `src/algorithms/risk_fsm.h` 的 `RiskFSMConfig` 保持一致。
    """

    # ---------- 综合拥挤度评分权重（score = w1*norm(n) + w2*density + w3*r_gcc + w4*fn*r_gcc）
    w1: float = 0.4          # 人数权重
    w2: float = 0.3          # 密度权重
    w3: float = 0.3          # 最大连通分量占比权重
    w4: float = 0.1           # 谱特征权重（score += w4 * fiedler/(fiedler+1) * r_gcc，0=禁用）
    n_ref: int = 20          # 人数归一化参考值（norm(n) = min(n/n_ref, 1)）

    # ---------- 动态（状态相关）滞回阈值，基于 score ∈ [0,1]
    up_warning: float = 0.40    # SAFE -> WARNING 升级阈值
    up_danger: float = 0.65    # WARNING -> DANGER 升级阈值
    down_safe: float = 0.28    # WARNING -> SAFE 降级阈值
    down_warning: float = 0.55  # DANGER -> WARNING 降级阈值

    # ---------- 确认帧数（防抖）
    confirm_up: int = 3     # 升级需连续满足新等级的帧数
    confirm_down: int = 5   # 降级需连续满足更低等级的帧数

    # ---------- 状态保持（DANGER 至少保持）
    danger_hold_frames: int = 10   # 进入 DANGER 至少保持的帧数（0=禁用）
    danger_hold_ms: int = 1000     # 进入 DANGER 至少保持的毫秒数（0 或 now_ms=None 时禁用）

    # ---------- 运动趋势偏置（可选）
    trend_bias: float = 0.05  # trend ∈[-1,1] 对 score 的加成幅度

    def __post_init__(self) -> None:
        # 类型/取值校验，尽早暴露配置错误
        if self.w1 < 0 or self.w2 < 0 or self.w3 < 0 or self.w4 < 0:
            raise ValueError("权重 w1/w2/w3/w4 必须为非负值")
        if self.w1 + self.w2 + self.w3 <= 0:
            raise ValueError("权重之和必须大于 0")
        if self.w4 > 1.0:
            raise ValueError("w4 权重不应超过 1.0（谱项为辅助修正）")
        if self.n_ref <= 0:
            raise ValueError("n_ref 必须为正整数")
        if self.confirm_up < 1 or self.confirm_down < 1:
            raise ValueError("confirm_up / confirm_down 必须 >= 1")
        if self.danger_hold_frames < 0 or self.danger_hold_ms < 0:
            raise ValueError("danger_hold_frames / danger_hold_ms 必须 >= 0")
        if not (0.0 <= self.up_warning <= 1.0 and 0.0 <= self.up_danger <= 1.0
                and 0.0 <= self.down_safe <= 1.0 and 0.0 <= self.down_warning <= 1.0):
            raise ValueError("阈值必须在 [0,1] 范围内")
        # 滞回一致性：升级阈值必须严格高于对应降级阈值
        if self.up_warning <= self.down_safe:
            raise ValueError("up_warning 必须大于 down_safe（否则无滞回带）")
        if self.up_danger <= self.down_warning:
            raise ValueError("up_danger 必须大于 down_warning（否则无滞回带）")


class RiskFSM:
    """风险决策有限状态机。

    状态集合：{SAFE, WARNING, DANGER}。
    输入：每帧特征字典 + 当前时间戳(ms)。
    输出：当前风险等级字符串（'SAFE' / 'WARNING' / 'DANGER'）。
    """

    def __init__(self, config: Optional[RiskFSMConfig] = None) -> None:
        if config is None:
            config = RiskFSMConfig()
        elif isinstance(config, Mapping):
            config = RiskFSMConfig(**config)
        elif not isinstance(config, RiskFSMConfig):
            raise TypeError("config 必须是 RiskFSMConfig 或可转换为它的映射")
        self.config: RiskFSMConfig = config
        self.reset()

    # ------------------------------------------------------------------
    # 状态访问
    # ------------------------------------------------------------------
    @property
    def level(self) -> str:
        """当前风险等级。"""
        return self._state

    @property
    def state(self) -> str:
        """当前风险等级（同 level）。"""
        return self._state

    @property
    def score(self) -> float:
        """最近一帧的综合拥挤度评分（更新前为 0）。"""
        return self._score

    # ------------------------------------------------------------------
    # 公共接口
    # ------------------------------------------------------------------
    def reset(self) -> None:
        """重置到初始状态 SAFE，清空所有内部计数器。"""
        self._state: str = "SAFE"
        self._score: float = 0.0
        self._up_count: int = 0      # 连续满足升级条件的帧数
        self._down_count: int = 0    # 连续满足降级条件的帧数
        self._frame: int = 0        # 累计调用 update 的次数
        self._entry_frame: int = 0  # 进入 DANGER 时的帧序号
        self._entry_ms: Optional[int] = None  # 进入 DANGER 时的时间戳
        self._last_ms: Optional[int] = None

    def update(self, features: Mapping[str, Any], now_ms: Optional[int]) -> str:
        """输入一帧特征，更新状态并返回当前风险等级。

        参数
        ----
        features : dict
            键：
              - n       : int     行人数（必须）
              - density : float   网络密度 [0,1]
              - r_gcc   : float   最大连通分量占比 [0,1]
              - clustering : float 平均聚类系数（预留，当前不参与评分）
              - fiedler : float   （可选）Fiedler 特征值（谱连通度），
                                    参与评分项 w4*fiedler/(fiedler+1)*r_gcc
              - trend   : float   （可选）运动趋势 [-1,1]，>0 表示聚集/人数上升
        now_ms : int 或 None
            当前时间戳（毫秒）。为 None 时禁用基于毫秒的 DANGER 保持。

        返回
        ----
        str : 'SAFE' / 'WARNING' / 'DANGER'
        """
        self._frame += 1
        self._last_ms = now_ms
        self._score = self._compute_score(features)
        target = self._raw_target(self._score, self._state)
        return self._transition(target, now_ms)

    # ------------------------------------------------------------------
    # 内部实现
    # ------------------------------------------------------------------
    def _compute_score(self, features: Mapping[str, Any]) -> float:
        cfg = self.config
        n = features.get("n", 0) or 0
        density = features.get("density", 0.0) or 0.0
        r_gcc = features.get("r_gcc", 0.0) or 0.0
        trend = features.get("trend", 0.0) or 0.0

        norm_n = min(max(int(n), 0) / float(cfg.n_ref), 1.0)
        density = _clamp(float(density), 0.0, 1.0)
        r_gcc = _clamp(float(r_gcc), 0.0, 1.0)

        score = cfg.w1 * norm_n + cfg.w2 * density + cfg.w3 * r_gcc

        # 谱特征修正项：Fiedler 值（代数连通度）越大、连通占比越大，风险越高。
        # 归一化采用 fiedler/(fiedler+1)（单调映射到 [0,1)），只对 r_gcc>0 生效：
        # 紧密聚集（fiedler 大）加分明显，链状伪聚集（r_gcc=1 但 fiedler 小）加分极小。
        if cfg.w4 != 0.0 and r_gcc > 0.0:
            fiedler = features.get("fiedler", 0.0) or 0.0
            if fiedler > 0.0:
                fiedler_norm = fiedler / (fiedler + 1.0)
                score += cfg.w4 * fiedler_norm * r_gcc

        if cfg.trend_bias != 0.0 and trend:
            score += cfg.trend_bias * _clamp(float(trend), -1.0, 1.0)
        return _clamp(score, 0.0, 1.0)

    def _raw_target(self, score: float, state: str) -> str:
        """根据当前状态 + score，应用滞回阈值得到"理想目标等级"。"""
        cfg = self.config
        if state == "SAFE":
            return "WARNING" if score >= cfg.up_warning else "SAFE"
        if state == "WARNING":
            if score >= cfg.up_danger:
                return "DANGER"
            if score <= cfg.down_safe:
                return "SAFE"
            return "WARNING"
        # state == DANGER
        if score <= cfg.down_warning:
            return "WARNING"
        return "DANGER"

    def _transition(self, target: str, now_ms: Optional[int]) -> str:
        """根据目标等级驱动状态转移，并施加确认帧 / 状态保持。"""
        cfg = self.config
        current = self._state

        # 目标与当前一致：无转移，清空两侧计数器
        if target == current:
            self._up_count = 0
            self._down_count = 0
            return current

        if LEVEL_RANK[target] > LEVEL_RANK[current]:
            # ---- 升级路径（SAFE->WARNING 或 WARNING->DANGER）
            self._down_count = 0
            self._up_count += 1
            if self._up_count >= cfg.confirm_up:
                self._state = target
                self._entry_ms = now_ms
                self._entry_frame = self._frame
                self._up_count = 0
            return self._state  # 未达确认帧前保持原等级

        # ---- 降级路径（DANGER->WARNING 或 WARNING->SAFE）
        self._up_count = 0
        if current == "DANGER" and not self._hold_elapsed(now_ms):
            # DANGER 仍处于强制保持期：不累计降级计数
            self._down_count = 0
            return current
        self._down_count += 1
        if self._down_count >= cfg.confirm_down:
            self._state = target
            self._down_count = 0
        return self._state

    def _hold_elapsed(self, now_ms: Optional[int]) -> bool:
        """DANGER 强制保持期是否已结束（帧数与毫秒数同时满足才结束）。"""
        cfg = self.config
        if self._state != "DANGER":
            return True  # 不在 DANGER 状态，无需保持（对齐 C++ risk_fsm.h：level_ != RISK_DANGER 时早退）

        frame_ok = True
        if cfg.danger_hold_frames > 0:
            frame_ok = (self._frame - self._entry_frame) >= cfg.danger_hold_frames

        ms_ok = True
        if cfg.danger_hold_ms > 0 and now_ms is not None and self._entry_ms is not None:
            ms_ok = (now_ms - self._entry_ms) >= cfg.danger_hold_ms

        return frame_ok and ms_ok

    # ------------------------------------------------------------------
    def __repr__(self) -> str:
        return (f"RiskFSM(level={self._state!r}, score={self._score:.3f}, "
                f"config={self.config!r})")