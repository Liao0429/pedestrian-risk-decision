#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ema_smoother.py - 指数滑动平均（EMA）时序平滑器

对风险特征序列（如行人数 n、网络密度 density、最大连通分量占比 r_gcc、
行人/车速、TTC 等）做指数滑动平均，输出平滑后的特征值，用于 FSM 输入降噪，
抑制检测抖动与 IPM 映射误差带来的单帧跳变。

核心递推（帧级固定 alpha）：
    s_t = alpha * x_t + (1 - alpha) * s_{t-1}      (alpha ∈ (0, 1])

首帧初始化：s_0 = x_0。

支持：
  - 标量与多变量（向量）特征：向量时各分量独立平滑；
  - 每维独立的 alpha（传入数组）；
  - 时间感知模式（use_timestamp=True）：
    按时间戳间隔自适应 alpha_t = 1 - exp(-dt / tau)，
    dt 越小平滑越强，适合非等间隔帧序列；
  - reset() 重置，便于场景切换 / 轨迹切换。

与 C++ 侧 src/algorithms/ema_smoother.h 接口一致。
依赖：numpy。

用法
----
>>> from src.algorithms.ema_smoother import EMASmoother
>>> sm = EMASmoother(alpha=0.3)          # 单变量
>>> y = sm.update(0.6, t=1)              # -> 0.6（首帧原样）
>>> y = sm.update(0.9, t=2)              # -> 0.3*0.9 + 0.7*0.6 = 0.69
>>>
>>> smv = EMASmoother(alpha=0.3, dim=3)  # 3 维风险特征
>>> y = smv.update([0.5, 0.6, 0.4], t=1)
"""

from __future__ import annotations

from typing import Optional, Sequence, Union

import numpy as np

__all__ = ["EMASmoother"]

# 类型别名：标量或一维向量
ValueType = Union[float, int, Sequence[float], np.ndarray]


class EMASmoother:
    """指数滑动平均（EMA）时序平滑器。"""

    def __init__(
        self,
        alpha: Union[float, Sequence[float]] = 0.3,
        dim: Optional[int] = None,
        use_timestamp: bool = False,
        tau: float = 1.0,
    ):
        """构造 EMA 平滑器。

        参数
        ----
        alpha : float 或 array-like
            平滑因子 alpha ∈ (0, 1]。标量时对所有维生效；数组时为每维独立 alpha
            （长度须与特征维度一致）。
        dim : int, optional
            特征维度。为 None 时由首次 update 的值自动推断。
        use_timestamp : bool
            True 时启用时间感知模式：根据相邻时间戳间隔 dt 自适应
            alpha_t = 1 - exp(-dt/tau)；False 时恒用 alpha（帧级 EMA）。
        tau : float
            时间感知模式的时间常数（秒）。dt 与 tau 同量纲；
            tau 越小对高频噪声抑制越强、但响应越滞后。

        校验：0 < alpha <= 1；alpha 数组长度须与 dim 一致；tau > 0。
        """
        if use_timestamp and tau <= 0:
            raise ValueError(f"tau 必须 > 0，实际: {tau}")

        self.alpha_raw = alpha
        self.dim = dim
        self.use_timestamp = bool(use_timestamp)
        self.tau = float(tau)

        # alpha 归一化为数组（后续统一广播）
        self._alpha = self._prepare_alpha(alpha, dim)

        self._s: Optional[np.ndarray] = None   # 平滑状态（长度 dim）
        self._dim: Optional[int] = None        # 实际推断维度
        self._last_t: Optional[float] = None   # 上一时间戳
        self._initialized = False

    # ------------------------------------------------------------------
    # 构造辅助
    # ------------------------------------------------------------------
    @staticmethod
    def _prepare_alpha(
        alpha: Union[float, Sequence[float]],
        dim: Optional[int],
    ) -> np.ndarray:
        arr = np.asarray(alpha, dtype=np.float64)
        if arr.ndim == 0:
            a = float(arr)
            if not (0.0 < a <= 1.0):
                raise ValueError(f"alpha 必须在 (0,1] 内，实际: {a}")
            return np.array([a])
        if arr.ndim != 1:
            raise ValueError(f"alpha 必须是标量或一维数组，实际形状: {arr.shape}")
        if np.any(arr <= 0.0) or np.any(arr > 1.0):
            raise ValueError("alpha 的每个分量必须在 (0,1] 内")
        if dim is not None and len(arr) != dim:
            raise ValueError(
                f"alpha 数组长度 {len(arr)} 与 dim={dim} 不一致"
            )
        return arr.copy()

    # ------------------------------------------------------------------
    # 查询接口
    # ------------------------------------------------------------------
    @property
    def initialized(self) -> bool:
        """是否已接收过至少一帧数据。"""
        return self._initialized

    @property
    def smoothed(self) -> Optional[np.ndarray]:
        """当前平滑值（副本）。未初始化时返回 None。"""
        if not self._initialized:
            return None
        return self._s.copy()

    @property
    def dimension(self) -> Optional[int]:
        """当前特征维度（首次 update 后确定）。"""
        return self._dim

    # ------------------------------------------------------------------
    # 主接口
    # ------------------------------------------------------------------
    def update(
        self,
        value: ValueType,
        t: Optional[Union[float, int]] = None,
    ) -> Union[float, np.ndarray]:
        """用新观测更新平滑值。

        参数
        ----
        value : float 或 array-like
            当前帧的特征值。标量视为 1 维；数组长度须与已确定维度一致。
        t : float/int, optional
            当前帧时间戳（秒或帧号）。use_timestamp=True 时用于计算 dt；
            否则仅记录，不影响平滑因子。

        返回
        ----
        平滑后的值：
          - 输入为标量时返回 float；
          - 输入为数组/序列时返回 np.ndarray（副本，形状与输入一致）。
        """
        scalar_input = np.isscalar(value) or isinstance(value, (int, float))
        v = np.asarray(value, dtype=np.float64)
        if v.ndim == 0:
            v = v.reshape(1)
        if v.ndim != 1:
            raise ValueError(
                f"value 必须是标量或一维数组，实际形状: {v.shape}"
            )

        # 首次调用：推断维度并初始化状态
        if not self._initialized:
            if self._dim is None:
                self._dim = int(v.shape[0])
                if len(self._alpha) == 1 and self._dim > 1:
                    # 标量 alpha 广播到所有维度
                    self._alpha = np.full(self._dim, self._alpha[0])
            elif v.shape[0] != self._dim:
                raise ValueError(
                    f"输入维度 {v.shape[0]} 与已确定维度 {self._dim} 不一致"
                )
            self._s = v.copy()
            self._last_t = t
            self._initialized = True
            result = v.copy()
        else:
            if v.shape[0] != self._dim:
                raise ValueError(
                    f"输入维度 {v.shape[0]} 与已确定维度 {self._dim} 不一致"
                )
            alpha = self._effective_alpha(t)
            self._s = alpha * v + (1.0 - alpha) * self._s
            self._last_t = t
            result = self._s.copy()

        return float(result[0]) if scalar_input else result

    def _effective_alpha(self, t: Optional[float]) -> np.ndarray:
        """计算实际使用的 alpha 向量。

        - 非时间感知模式：恒为固定 alpha；
        - 时间感知模式且有有效时间差：alpha_t = 1 - exp(-dt/tau)（标量广播）。
        """
        if not self.use_timestamp or t is None or self._last_t is None:
            return self._alpha.copy()

        dt = float(t) - float(self._last_t)
        if dt <= 0:
            # 时间戳倒退或相同：退化为固定 alpha，避免负平滑
            return self._alpha.copy()

        a_t = 1.0 - np.exp(-dt / self.tau)
        a_t = min(max(a_t, 1e-9), 1.0)
        return np.full(self._dim, a_t, dtype=np.float64)

    # ------------------------------------------------------------------
    # 维护
    # ------------------------------------------------------------------
    def reset(self) -> None:
        """清空状态，回到未初始化状态。"""
        self._s = None
        self._last_t = None
        self._initialized = False
        # dim 保留：同一平滑器一般复用同一特征维度
        # （如需彻底清空推断维度，可重新构造对象）

    def __repr__(self) -> str:  # pragma: no cover
        return (
            f"EMASmoother(alpha={self.alpha_raw!r}, "
            f"dim={self._dim}, use_timestamp={self.use_timestamp}, "
            f"tau={self.tau}, initialized={self._initialized})"
        )


# ----------------------------------------------------------------------
# 命令行快速演示：python src/algorithms/ema_smoother.py
# ----------------------------------------------------------------------
if __name__ == "__main__":
    rng = np.random.default_rng(0)
    true = 1.0
    noise = rng.normal(0.0, 0.3, 200)
    raw = true + noise

    sm = EMASmoother(alpha=0.3)
    smoothed = np.array([sm.update(x) for x in raw])

    warm = 50
    var_raw = float(np.var(raw[warm:]))
    var_sm = float(np.var(smoothed[warm:]))
    print("=== EMA 平滑演示（alpha=0.3，真值=1.0，高斯噪声 σ=0.3）===")
    print(f"原始噪声方差    : {var_raw:.5f}")
    print(f"平滑后方差      : {var_sm:.5f}")
    print(f"方差抑制比      : {var_sm / var_raw:.3f}  (理论 alpha/(2-alpha)={0.3/1.7:.3f})")

    print("\n=== 多变量演示 ===")
    smv = EMASmoother(alpha=0.3, dim=3)
    feat = np.array([[0.5, 0.6, 0.4], [0.8, 0.5, 0.9], [0.6, 0.7, 0.5]])
    for row in feat:
        print(smv.update(row, t=1))