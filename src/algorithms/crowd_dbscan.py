#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
crowd_dbscan.py - DBSCAN 密度聚类人群分析

基于行人地面坐标(米)，使用 DBSCAN(基于密度的聚类)对人群进行分组，
作为图拉普拉斯谱特征(见 graph_laplacian.py)之外的工程化决策特征，
直接刻画"拥挤度 / 局部聚集"语义：

  - n_clusters         簇数
  - cluster_sizes      每簇人数(按簇大小降序)
  - largest_cluster_size   最大簇人数
  - largest_cluster_ratio  最大簇人数占比 = 最大簇人数 / 总行人数
  - cluster_centers    簇中心列表(每簇质心，米)
  - n_noise / n_clustered  噪声点数 / 被聚类点数
  - labels             每个行人的簇标签(-1 表示噪声)

DBSCAN 参数(可通过配置传入)：
  - eps         : 聚集半径(米)。两点欧氏距离 dist <= eps 视为邻居
                   (与经典 DBSCAN 定义一致，闭区间)。
  - min_samples : 邻域内最少点数(含自身)才视为核心点，默认 2。

实现为纯 numpy/标准库手写 DBSCAN(无 sklearn 依赖)，与 graph_laplacian.py
同一风格；板端可由 C++ 头文件 crowd_dbscan.h 直接复刻/替换。
依赖：numpy(仅用于 Python 原型)。
"""

from __future__ import annotations

import numpy as np

__all__ = ["dbscan_labels", "dbscan_crowd_features", "UNCLASSIFIED", "NOISE"]

# 标签约定（必须与板端 crowd_dbscan.h 一致；下游按 -1 判噪声，取值不一致会漏判）：
#   >0 : 簇编号（1..n_clusters）
#   -1 : 噪声点（不属任何簇）
#   -2 : 算法内部哨兵"尚未处理"，对外不会出现
NOISE = -1          # 噪声点(不属任何簇)
UNCLASSIFIED = -2   # 内部哨兵：尚未处理（勿与 NOISE 混淆）


def _region_query(dist, i, eps):
    """返回与点 i 欧氏距离 <= eps 的所有点下标(含 i 自身)。"""
    return set(np.nonzero(dist[i] <= eps)[0].tolist())


def dbscan_labels(pts, eps, min_samples=2):
    """DBSCAN 聚类，返回 (labels, n_clusters)。

    参数
    ----
    pts : array-like, shape (n,2)
        行人地面坐标(米)，形如 [(x0,y0), (x1,y1), ...]。
    eps : float
        聚集半径(米)，距离阈值(闭区间，dist <= eps 视为邻居)。
    min_samples : int, optional
        核心点邻域内最少点数(含自身)，默认 2。

    返回
    ----
    labels : numpy.ndarray, shape (n,), dtype=int64
        labels[i] = 簇编号(1..n_clusters) 或 NOISE(-1)；-2 仅为内部哨兵，不会出现。
    n_clusters : int
        聚类簇数(噪声不计入)。

    算法与经典 DBSCAN 一致：核心点向其 eps 邻域扩张，边界点归入首个
    到达它的簇，噪声点可被后续簇吸收为边界点。
    """
    pts = np.asarray(pts, dtype=np.float64)
    if pts.ndim == 1 and pts.size == 0:
        # 空点集 -> 空标签、0 簇
        return np.empty(0, dtype=np.int64), 0
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError("pts 必须是 (n,2) 的坐标数组，实际形状: %s" % (pts.shape,))
    n = pts.shape[0]
    labels = np.full(n, UNCLASSIFIED, dtype=np.int64)
    n_clusters = 0
    if n == 0:
        return labels, n_clusters

    # 预计算两两欧氏距离矩阵(闭区间，dist[i][i]=0 使 i 恒为自身邻居)
    diff = pts[:, None, :] - pts[None, :, :]
    dist = np.sqrt(np.einsum("ijk,ijk->ij", diff, diff))

    for i in range(n):
        if labels[i] != UNCLASSIFIED:
            continue
        nbrs_i = _region_query(dist, i, eps)
        if len(nbrs_i) < min_samples:
            labels[i] = NOISE
            continue
        n_clusters += 1
        cid = n_clusters
        labels[i] = cid
        stack = list(nbrs_i)
        while stack:
            j = stack.pop()
            if labels[j] == cid:
                continue                      # 已属于本簇
            if labels[j] != UNCLASSIFIED and labels[j] != NOISE:
                continue                      # 属于其他簇，不抢占
            labels[j] = cid
            nbrs_j = _region_query(dist, j, eps)
            if len(nbrs_j) >= min_samples:    # 核心点 -> 继续扩张
                for k in nbrs_j:
                    if labels[k] == UNCLASSIFIED or labels[k] == NOISE:
                        stack.append(k)
    return labels, n_clusters


def dbscan_crowd_features(pts, eps, min_samples=2):
    """计算 DBSCAN 人群聚类特征，返回字典。

    参数
    ----
    pts : array-like, shape (n,2)
        行人地面坐标(米)。
    eps : float
        聚集半径(米)。
    min_samples : int, optional
        核心点邻域内最少点数(含自身)，默认 2。

    返回
    ----
    dict 包含：
        n                       : 行人数
        n_clusters              : 簇数
        cluster_sizes           : 每簇人数(按簇大小降序)
        largest_cluster_size    : 最大簇人数
        largest_cluster_ratio   : 最大簇人数占比 = 最大簇人数 / n
        cluster_centers         : 簇中心列表 [(x,y), ...](每簇质心，米)
        labels                  : 每个行人的簇标签(-1 表示噪声)
        n_noise                 : 噪声点数
        n_clustered             : 被聚类点数
    """
    pts = np.asarray(pts, dtype=np.float64)
    if pts.ndim == 1 and pts.size == 0:
        # 空点集 -> 全零特征
        return {
            "n": 0, "n_clusters": 0, "cluster_sizes": [],
            "largest_cluster_size": 0, "largest_cluster_ratio": 0.0,
            "cluster_centers": [], "labels": np.empty(0, dtype=np.int64),
            "n_noise": 0, "n_clustered": 0,
        }
    if pts.ndim != 2 or pts.shape[1] != 2:
        raise ValueError("pts 必须是 (n,2) 的坐标数组，实际形状: %s" % (pts.shape,))
    n = pts.shape[0]
    labels, n_clusters = dbscan_labels(pts, eps, min_samples)

    if n_clusters == 0:
        return {
            "n": n, "n_clusters": 0, "cluster_sizes": [],
            "largest_cluster_size": 0, "largest_cluster_ratio": 0.0,
            "cluster_centers": [], "labels": labels,
            "n_noise": int((labels == NOISE).sum()), "n_clustered": 0,
        }

    sizes = []
    centers = []
    for cid in range(1, n_clusters + 1):
        mask = labels == cid
        sizes.append(int(mask.sum()))
        centers.append(pts[mask].mean(axis=0))

    # 按簇大小降序排序(簇中心同步排序)。
    # 并列时按簇编号升序：np.argsort 默认非稳定排序，叠加 [::-1] 后并列簇的
    # 先后顺序不可复现，会让 cluster_centers 随实现/数据顺序漂移。
    order = sorted(range(n_clusters), key=lambda c: (-sizes[c], c))
    sizes = [sizes[o] for o in order]
    centers = [tuple(float(v) for v in centers[o]) for o in order]

    n_noise = int((labels == NOISE).sum())
    largest = sizes[0]
    return {
        "n": n,
        "n_clusters": n_clusters,
        "cluster_sizes": sizes,
        "largest_cluster_size": largest,
        "largest_cluster_ratio": largest / n,
        "cluster_centers": centers,
        "labels": labels,
        "n_noise": n_noise,
        "n_clustered": n - n_noise,
    }


# ----------------------------------------------------------------------
# 命令行快速演示：python crowd_dbscan.py
# ----------------------------------------------------------------------
if __name__ == "__main__":
    print("=== 分散(4 点相距 5m，eps=1.0) === 预期 0 簇 ===")
    pts_disp = [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (15.0, 0.0)]
    print(dbscan_crowd_features(pts_disp, eps=1.0))

    print("\n=== 两簇(左右各 4 点密集，eps=1.5) === 预期 2 簇 ===")
    cluster_a = [(0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (1.0, 1.0)]
    cluster_b = [(10.0, 0.0), (10.0, 1.0), (11.0, 0.0), (11.0, 1.0)]
    print(dbscan_crowd_features(cluster_a + cluster_b, eps=1.5))

    print("\n=== 全连通(4 点互为邻居，eps=1.0) === 预期 1 簇 ===")
    pts_full = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    print(dbscan_crowd_features(pts_full, eps=1.0))