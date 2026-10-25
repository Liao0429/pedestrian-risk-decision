#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ipm_calibrate.py - IPM 真机标定工具（像素坐标 ↔ 地面米制坐标）

基于画面中 4 个地面点的像素坐标与其真实地面米制坐标，用
cv2.getPerspectiveTransform 求解固定单应矩阵 H（3x3），实现
"像素坐标 -> 地面米制坐标"的逆透视映射（IPM）。

功能：
  - calibrate_from_points(pixel_pts, ground_pts) -> H   # 4 点直接求解
  - calibrate_from_points_ransac(...)                   # >=4 点 RANSAC 鲁棒求解（可选）
  - pixel_to_ground(H, pts) / ground_to_pixel(H, pts)   # 正 / 逆映射
  - save_homography(H, path) / load_homography(path)      # 支持 .npy / .txt
  - 命令行交互标定：给定 4 组像素点与地面点，输出 H 并保存

依赖：numpy、opencv-python（cv2）。
板端 C++ 集成参考 docs/ALGORITHM_UPGRADE.md。

用法示例
--------
>>> import numpy as np
>>> from src.algorithms.ipm_calibrate import calibrate_from_points
>>> pixel = np.array([[100, 220], [300, 230], [320, 400], [80, 410]], dtype=np.float32)
>>> ground = np.array([[0, 0], [5, 0], [5, 8], [0, 8]], dtype=np.float32)  # 米
>>> H = calibrate_from_points(pixel, ground)
>>> pixel_to_ground(H, pixel)   # 应回到 ground 附近
"""

from __future__ import annotations

import argparse
import os
from typing import Optional, Sequence, Union

import numpy as np

try:
    import cv2
except ImportError as exc:  # pragma: no cover
    raise ImportError(
        "ipm_calibrate 需要 opencv-python。请先安装："
        "python -m pip install opencv-python"
    ) from exc

__all__ = [
    "calibrate_from_points",
    "calibrate_from_points_ransac",
    "pixel_to_ground",
    "ground_to_pixel",
    "save_homography",
    "load_homography",
]

# 文本文件扩展名（走 np.savetxt / np.loadtxt 的可读格式）
_TXT_EXTS = (".txt", ".csv", ".dat")
# 二进制扩展名（走 np.save / np.load）
_NPY_EXTS = (".npy",)


# ----------------------------------------------------------------------
# 内部工具
# ----------------------------------------------------------------------
def _as_pts(
    pts: Union[Sequence[Sequence[float]], np.ndarray],
    name: str,
    expect: Optional[int] = None,
) -> np.ndarray:
    """转换为 (N,2) 的 float64 ndarray，并做基本校验。"""
    arr = np.asarray(pts, dtype=np.float64)
    if arr.ndim != 2 or arr.shape[1] != 2:
        raise ValueError(
            f"{name} 必须是 (N,2) 的点数组，实际形状: {arr.shape}"
        )
    if arr.shape[0] < 4:
        raise ValueError(f"{name} 至少需要 4 个点，实际: {arr.shape[0]}")
    if expect is not None and arr.shape[0] != expect:
        raise ValueError(
            f"{name} 应为 {expect} 个点，实际 {arr.shape[0]} 个。"
            f"cv2.getPerspectiveTransform 需要恰好 4 组点对。"
        )
    return arr


def _points_to_homogeneous(pts: np.ndarray) -> np.ndarray:
    """(N,2) -> (N,3) 齐次坐标（最后一维为 1）。"""
    pts = np.asarray(pts, dtype=np.float64)
    return np.hstack([pts, np.ones((pts.shape[0], 1))])


# ----------------------------------------------------------------------
# 标定（求解 H）
# ----------------------------------------------------------------------
def calibrate_from_points(
    pixel_pts: Union[Sequence[Sequence[float]], np.ndarray],
    ground_pts: Union[Sequence[Sequence[float]], np.ndarray],
) -> np.ndarray:
    """由 4 组 (像素点, 地面点) 对应关系求解单应矩阵 H。

    参数
    ----
    pixel_pts : array-like, shape (4,2)
        画面中地面点的像素坐标 (u, v)，建议取框底中心/脚点等地面投影点。
    ground_pts : array-like, shape (4,2)
        与像素点一一对应的真实地面米制坐标 (x, y)。

    返回
    ----
    H : numpy.ndarray, shape (3,3), dtype=float64
        满足 `ground ~ H * pixel`（齐次坐标下）的 3x3 单应矩阵。

    注意
    ----
    cv2.getPerspectiveTransform 要求恰好 4 组点对。若手上有 >=4 组点且希望
    用 RANSAC 抑制个别点误差，请使用 calibrate_from_points_ransac。
    """
    px = _as_pts(pixel_pts, "pixel_pts", expect=4)
    gd = _as_pts(ground_pts, "ground_pts", expect=4)

    # 确保非退化：4 个地面点不能共线（3 点共线也会导致奇异）
    if _is_collinear(gd):
        raise ValueError("ground_pts 中 4 个点近似共线，无法唯一求解单应。")

    H = cv2.getPerspectiveTransform(
        px.astype(np.float32), gd.astype(np.float32)
    )
    return np.asarray(H, dtype=np.float64)


def calibrate_from_points_ransac(
    pixel_pts: Union[Sequence[Sequence[float]], np.ndarray],
    ground_pts: Union[Sequence[Sequence[float]], np.ndarray],
    ransac_thresh: float = 1.0,
    confidence: float = 0.995,
) -> np.ndarray:
    """由 >=4 组对应点用 RANSAC 鲁棒求解单应矩阵 H（可选增强）。

    适合标定现场标了多个点、个别点可能标错的情形。像素残差以"像素"为单位
    （与 ransac_thresh 同量纲）。

    参数
    ----
    pixel_pts : array-like, shape (N,2), N>=4
    ground_pts : array-like, shape (N,2), N>=4
    ransac_thresh : float
        内点判定阈值（像素，默认 1.0）。
    confidence : float
        RANSAC 置信度（默认 0.995）。

    返回
    ----
    H : numpy.ndarray, shape (3,3), dtype=float64
    """
    px = _as_pts(pixel_pts, "pixel_pts")
    gd = _as_pts(ground_pts, "ground_pts")

    H, _mask = cv2.findHomography(
        px.astype(np.float32),
        gd.astype(np.float32),
        method=cv2.RANSAC,
        ransacReprojThreshold=ransac_thresh,
        maxIters=2000,
        confidence=confidence,
    )
    if H is None:
        raise ValueError("RANSAC 未能求到有效单应矩阵，请检查输入点。")
    return np.asarray(H, dtype=np.float64)


def _is_collinear(pts: np.ndarray, tol: float = 1e-9) -> bool:
    """判断 (N,2) 点集是否（近似）共线。"""
    pts = np.asarray(pts, dtype=np.float64)
    if pts.shape[0] < 3:
        return False
    # 以第一点为基准，其余点相对它的向量应线性相关
    d = pts[1:] - pts[0:1]
    # 二维向量叉积的绝对值 <= tol * 尺度，视为共线
    cross = d[:, 0] * d[1:2, 1] - d[:, 1] * d[1:2, 0]
    return bool(np.all(np.abs(cross) <= tol * max(1.0, np.max(np.abs(d)))))


# ----------------------------------------------------------------------
# 正 / 逆映射
# ----------------------------------------------------------------------
def pixel_to_ground(
    H: np.ndarray, pts: Union[Sequence[Sequence[float]], np.ndarray]
) -> np.ndarray:
    """将像素坐标 (u, v) 映射到地面米制坐标 (x, y)。

    等价于对齐次坐标执行 `p_ground ~ H * [u, v, 1]ᵀ` 后除以第三分量。

    参数
    ----
    H : array-like, shape (3,3)
        标定得到的单应矩阵。
    pts : array-like, shape (N,2)
        像素坐标点。

    返回
    ----
    numpy.ndarray, shape (N,2), dtype=float32
        地面米制坐标。返回值保持与 cv2.perspectiveTransform 一致的 float32。
    """
    H = np.asarray(H, dtype=np.float64)
    pts = np.asarray(pts, dtype=np.float32)
    if pts.ndim == 1:
        pts = pts.reshape(1, 2)
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError(f"pts 必须是 (N,2) 的点数组，实际形状: {pts.shape}")
    out = cv2.perspectiveTransform(pts.reshape(-1, 1, 2), H.astype(np.float64))
    return out.reshape(-1, 2)


def ground_to_pixel(
    H: np.ndarray, pts: Union[Sequence[Sequence[float]], np.ndarray]
) -> np.ndarray:
    """将地面米制坐标 (x, y) 映射回像素坐标 (u, v)（单应逆变换）。

    参数
    ----
    H : array-like, shape (3,3)
    pts : array-like, shape (N,2)
        地面米制坐标点。

    返回
    ----
    numpy.ndarray, shape (N,2), dtype=float32
        像素坐标。
    """
    H = np.asarray(H, dtype=np.float64)
    Hinv = cv2.invert(H.astype(np.float64))[1]
    pts = np.asarray(pts, dtype=np.float32)
    if pts.ndim == 1:
        pts = pts.reshape(1, 2)
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError(f"pts 必须是 (N,2) 的点数组，实际形状: {pts.shape}")
    out = cv2.perspectiveTransform(pts.reshape(-1, 1, 2), Hinv)
    return out.reshape(-1, 2)


# ----------------------------------------------------------------------
# 保存 / 加载 H
# ----------------------------------------------------------------------
def save_homography(H: np.ndarray, path: str) -> str:
    """保存单应矩阵 H 到文件。

    支持两种格式（由扩展名决定）：
      - .npy        : 二进制 numpy 格式（推荐，精度无损）
      - .txt/.csv   : 可读文本，3 行、每行 3 个数值

    参数
    ----
    H : array-like, shape (3,3)
    path : str
        保存路径。

    返回
    ----
    str : 实际写入的绝对路径（自动创建父目录）。
    """
    H = np.asarray(H, dtype=np.float64)
    if H.shape != (3, 3):
        raise ValueError(f"H 必须是 3x3 矩阵，实际形状: {H.shape}")
    path = os.path.abspath(path)
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)

    ext = os.path.splitext(path)[1].lower()
    if ext in _NPY_EXTS:
        np.save(path, H)
    elif ext in _TXT_EXTS:
        np.savetxt(path, H, fmt="%.12g")
    else:
        # 未知扩展名默认写文本，便于人工查看
        np.savetxt(path, H, fmt="%.12g")
    return path


def load_homography(path: str) -> np.ndarray:
    """从文件加载单应矩阵 H。

    支持 .npy（二进制）与 .txt/.csv（文本，3 行 x 3 列）。

    参数
    ----
    path : str

    返回
    ----
    numpy.ndarray, shape (3,3), dtype=float64
    """
    path = os.path.abspath(path)
    if not os.path.exists(path):
        raise FileNotFoundError(f"单应文件不存在: {path}")

    ext = os.path.splitext(path)[1].lower()
    if ext in _NPY_EXTS:
        H = np.load(path)
    elif ext in _TXT_EXTS:
        H = np.loadtxt(path)
    else:
        H = np.loadtxt(path)
    H = np.asarray(H, dtype=np.float64)
    if H.shape != (3, 3):
        raise ValueError(
            f"文件中的矩阵形状应为 (3,3)，实际 {H.shape}: {path}"
        )
    return H


# ----------------------------------------------------------------------
# 命令行标定
# ----------------------------------------------------------------------
def _parse_pairs(text: str) -> np.ndarray:
    """解析形如 '100,200 300,220 320,400 80,410' 的坐标对字符串。"""
    items = [p.strip() for p in text.split()]
    pts = []
    for it in items:
        parts = it.split(",")
        if len(parts) != 2:
            raise ValueError(f"无法解析坐标对: {it!r}")
        pts.append((float(parts[0]), float(parts[1])))
    return np.asarray(pts, dtype=np.float64)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="IPM 真机标定：由 4 组像素点与地面点求解单应矩阵 H 并保存。"
    )
    parser.add_argument(
        "--pixel",
        required=True,
        help="4 组像素坐标，形如 '100,200 300,220 320,400 80,410'",
    )
    parser.add_argument(
        "--ground",
        required=True,
        help="4 组地面米制坐标，形如 '0,0 5,0 5,8 0,8'",
    )
    parser.add_argument(
        "--save",
        default="ipm_homography.npy",
        help="保存路径（.npy 或 .txt），默认 ipm_homography.npy",
    )
    parser.add_argument(
        "--print-only",
        action="store_true",
        help="只打印 H，不写文件",
    )
    args = parser.parse_args(argv)

    pixel_pts = _parse_pairs(args.pixel)
    ground_pts = _parse_pairs(args.ground)
    if len(pixel_pts) != 4 or len(ground_pts) != 4:
        parser.error("--pixel 与 --ground 必须各含恰好 4 个点")

    H = calibrate_from_points(pixel_pts, ground_pts)
    print("标定结果 H (3x3):")
    print(np.array2string(H, precision=6, suppress_small=True))

    if not args.print_only:
        path = save_homography(H, args.save)
        print(f"已保存单应矩阵 -> {path}")

    # 自检：把标定像素点映射回地面，验证误差
    back = pixel_to_ground(H, pixel_pts)
    err = np.linalg.norm(back - ground_pts, axis=1)
    print("\n自检（像素点映射回地面，米）：")
    for i, (g, b, e) in enumerate(zip(ground_pts, back, err)):
        print(f"  点{i}: 地面={g} 映射={b}  误差={e:.3e} m")
    print(f"  最大误差 = {err.max():.3e} m")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())