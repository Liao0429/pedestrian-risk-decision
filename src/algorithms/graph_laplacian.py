#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
graph_laplacian.py - 图拉普拉斯矩阵 L = D - A 与谱特征分析

基于行人地面坐标(米)构建邻接矩阵 A，推导图拉普拉斯矩阵 L = D - A，
并计算谱特征：
  - Fiedler 值 / 代数连通度 (第二小特征值 lambda_2)
  - 谱半径 (最大特征值)
  - 特征熵 (eig_entropy，归一化特征谱的香农熵)
  - 以及人群聚集结构指标 (n, m, density, r_gcc, clustering)

与 src/luckfox_rtsp_yolov5/src/risk_engine.cc 及 scripts/crowd_test.py 中的
compute_features 保持兼容：本模块返回的 n/m/density/r_gcc/clustering 五项
与 risk_engine 采用完全相同的建边规则与计算公式，供风险决策引擎直接替换/增强。

依赖：numpy（仅用于 Python 原型；板端 C++ 见 graph_laplacian.h）。
"""

from __future__ import annotations

import math

import numpy as np

__all__ = ["build_adjacency", "compute_laplacian_features"]


def build_adjacency(pts, edge_dist):
    """根据地面坐标点集构建无向无权邻接矩阵 A。

    参数
    ----
    pts : array-like, shape (n,2)
        行人地面坐标(米)，形如 [(x0,y0), (x1,y1), ...]。
    edge_dist : float
        建边距离阈值(米)。两点欧氏距离 d < edge_dist 则连边(与 risk_engine
        的 EDGE_DIST_M 一致，严格小于)。

    返回
    ----
    A : numpy.ndarray, shape (n,n), dtype=float64
        0/1 邻接矩阵。A[i][j] = A[j][i] = 1 表示节点 i 与 j 相连。
        对角元恒为 0(无自环)。n<=1 时返回全零矩阵。
    """
    pts = np.asarray(pts, dtype=np.float64)
    if pts.ndim == 1 and pts.size == 0:
        # 空点集 -> 返回 0x0 邻接矩阵
        return np.zeros((0, 0), dtype=np.float64)
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError("pts 必须是 (n,2) 的坐标数组，实际形状: %s" % (pts.shape,))
    n = pts.shape[0]
    A = np.zeros((n, n), dtype=np.float64)
    if n < 2:
        return A
    diff = pts[:, None, :] - pts[None, :, :]
    dist = np.sqrt(np.einsum("ijk,ijk->ij", diff, diff))
    A = (dist < edge_dist).astype(np.float64)
    np.fill_diagonal(A, 0.0)
    return A


def _degree(A):
    """返回度数向量 D[i] = sum_j A[i][j]。"""
    return A.sum(axis=1)


def _largest_component_ratio(A):
    """最大连通分量占比 r_gcc = 最大连通分量节点数 / n (BFS)。

    与 risk_engine.cc 中 BFS 逻辑一致。
    """
    n = A.shape[0]
    if n <= 0:
        return 0.0
    visited = np.zeros(n, dtype=bool)
    max_comp = 0
    for i in range(n):
        if visited[i]:
            continue
        comp = 0
        stack = [i]
        visited[i] = True
        while stack:
            u = stack.pop()
            comp += 1
            # 取 u 的邻居中未访问者
            nbrs = np.nonzero(A[u])[0]
            for v in nbrs:
                if not visited[v]:
                    visited[v] = True
                    stack.append(v)
        if comp > max_comp:
            max_comp = comp
    return float(max_comp) / n


def _avg_clustering(A):
    """平均局部聚类系数 clustering = (1/n) * sum_i C_i。

    对节点 i (度 k_i>=2)：C_i = (A^3)_{ii} / [k_i*(k_i-1)]，
    其中 (A^3)_{ii}/2 为经过节点 i 的三角形数。
    与 risk_engine.cc 的邻接枚举实现等价(可用小图验证)。
    """
    n = A.shape[0]
    if n <= 0:
        return 0.0
    deg = _degree(A)
    tri = np.einsum("ij,jk,ki->i", A, A, A)  # (A^3)_{ii}
    denom = deg * (deg - 1.0)
    c = np.zeros(n, dtype=np.float64)
    mask = denom > 0
    c[mask] = tri[mask] / denom[mask]
    return float(c.sum()) / n


def compute_laplacian_features(pts, edge_dist):
    """计算图拉普拉斯谱特征，返回字典。

    参数
    ----
    pts : array-like, shape (n,2)
        行人地面坐标(米)。
    edge_dist : float
        建边距离阈值(米)。

    返回
    ----
    dict 包含：
        n                    : 节点数(行人数)
        m                    : 边数
        density              : 网络密度 m / [n*(n-1)/2]
        r_gcc                : 最大连通分量占比
        clustering          : 平均局部聚类系数
        fiedler              : Fiedler 值 = 第二小特征值 lambda_2
        algebraic_connectivity : 代数连通度 = 第二小特征值 lambda_2
                               (与 fiedler 数学上等价，二者为同一值)
        spectral_radius     : 谱半径 = 最大特征值 lambda_max
        eig_entropy         : 特征熵(归一化特征谱的香农熵，自然对数单位)
    """
    pts = np.asarray(pts, dtype=np.float64)
    if pts.ndim == 1 and pts.size == 0:
        # 空点集：返回全零特征
        return {
            "n": 0, "m": 0, "density": 0.0, "r_gcc": 0.0,
            "clustering": 0.0, "fiedler": 0.0,
            "spectral_radius": 0.0, "algebraic_connectivity": 0.0,
            "eig_entropy": 0.0,
        }
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError("pts 必须是 (n,2) 的坐标数组，实际形状: %s" % (pts.shape,))
    n = pts.shape[0]

    A = build_adjacency(pts, edge_dist)
    m = int(A.sum()) // 2

    # ---- 与 risk_engine 一致的网络特征 ----
    if n > 1:
        density = (2.0 * m) / (n * (n - 1))
    else:
        density = 0.0
    r_gcc = _largest_component_ratio(A) if n > 1 else 0.0
    clustering = _avg_clustering(A)

    # ---- 图拉普拉斯谱特征 ----
    if n == 0:
        return {
            "n": 0, "m": 0, "density": 0.0, "r_gcc": 0.0,
            "clustering": 0.0, "fiedler": 0.0,
            "spectral_radius": 0.0, "algebraic_connectivity": 0.0,
            "eig_entropy": 0.0,
        }

    deg = _degree(A)
    L = np.diag(deg) - A

    # 对称矩阵特征值，升序返回
    eigvals = np.linalg.eigvalsh(L)
    eigvals = np.maximum(eigvals, 0.0)  # 数值裁剪，消除微小负值
    eigvals.sort()

    fiedler = float(eigvals[1]) if n >= 2 else 0.0
    spectral_radius = float(eigvals[-1])

    s = float(eigvals.sum())
    if s > 1e-12:
        p = eigvals / s
        p = p[p > 0]
        eig_entropy = float(-(p * np.log(p)).sum())
    else:
        eig_entropy = 0.0

    return {
        "n": n,
        "m": m,
        "density": density,
        "r_gcc": r_gcc,
        "clustering": clustering,
        "fiedler": fiedler,
        "spectral_radius": spectral_radius,
        "algebraic_connectivity": fiedler,
        "eig_entropy": eig_entropy,
    }


# ----------------------------------------------------------------------
# 命令行快速演示：python graph_laplacian.py
# ----------------------------------------------------------------------
if __name__ == "__main__":
    print("=== 分散(4 点相距 5m，无连边) ===")
    pts_disp = [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (15.0, 0.0)]
    print(compute_laplacian_features(pts_disp, edge_dist=2.0))

    print("\n=== 聚集(路径图 P5，间距 1.5m) ===")
    pts_clu = [(i * 1.5, 0.0) for i in range(5)]
    print(compute_laplacian_features(pts_clu, edge_dist=2.0))

    print("\n=== 全连接(边长 1m 的正方形 K4) ===")
    pts_full = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    print(compute_laplacian_features(pts_full, edge_dist=2.0))