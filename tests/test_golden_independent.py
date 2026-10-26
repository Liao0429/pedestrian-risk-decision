# -*- coding: utf-8 -*-
"""
黄金数据独立参考断言（Agent7 建议）

问题：generate_golden.py 用项目自身 Python 实现生成期望，若 C++/Python 实现同时错，
      golden 回归测不出来。
方案：本测试用 **numpy 手算**（不调用项目实现）重新计算图拉普拉斯谱特征，
     与 golden JSON 的 expected 比对，作为独立锚点。

运行：pytest tests/test_golden_independent.py -q
"""
import json
import math
import pathlib

import numpy as np

GOLDEN = pathlib.Path(__file__).resolve().parent / "golden" / "graph_laplacian.json"


def _adjacency(points, edge_dist):
    """严格小于 edge_dist 的无向邻接矩阵（与项目语义一致，自环除外）。"""
    n = len(points)
    A = np.zeros((n, n), dtype=float)
    for i in range(n):
        for j in range(i + 1, n):
            d = math.hypot(points[i][0] - points[j][0], points[i][1] - points[j][1])
            if d < edge_dist:
                A[i, j] = A[j, i] = 1.0
    return A


def _spectral_reference(points, edge_dist):
    """numpy 手算谱特征（独立参考实现）。"""
    n = len(points)
    if n == 0:
        return dict(n=0, m=0, density=0.0, r_gcc=0.0, clustering=0.0,
                    fiedler=0.0, spectral_radius=0.0, eig_entropy=0.0)
    A = _adjacency(points, edge_dist)
    deg = A.sum(axis=1)
    m = int(A.sum() / 2)
    L = np.diag(deg) - A
    eig = np.linalg.eigvalsh(L)

    # 连通分量（r_gcc = 最大连通分量 / n）
    visited = [False] * n
    largest = 0
    for s in range(n):
        if visited[s]:
            continue
        stack, comp = [s], 0
        visited[s] = True
        while stack:
            v = stack.pop()
            comp += 1
            for u in range(n):
                if A[v, u] and not visited[u]:
                    visited[u] = True
                    stack.append(u)
        largest = max(largest, comp)

    # 平均聚类系数（局部聚类系数：邻居间边数 / C(k,2)）
    clustering = 0.0
    for i in range(n):
        neigh = [j for j in range(n) if A[i, j]]
        k = len(neigh)
        if k < 2:
            continue
        e = sum(1 for a in range(k) for b in range(a + 1, k) if A[neigh[a], neigh[b]])
        clustering += (2.0 * e) / (k * (k - 1))
    clustering /= n if n else 1.0

    density = (2.0 * m) / (n * (n - 1)) if n >= 2 else 0.0
    fiedler = eig[1] if n >= 2 else 0.0
    spectral_radius = float(eig[-1]) if n >= 1 else 0.0
    eig_entropy = 0.0
    s = float(eig.sum())
    if s > 0:
        p = eig / s
        p = p[p > 0]
        eig_entropy = float(-(p * np.log(p)).sum())

    return dict(n=n, m=m, density=density, r_gcc=(largest / n if n >= 2 else 0.0),
                clustering=clustering, fiedler=float(fiedler),
                spectral_radius=spectral_radius, eig_entropy=eig_entropy)


def test_golden_matches_independent_reference():
    """golden JSON 的期望必须与 numpy 独立参考一致（容差 1e-5）。"""
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    cases = data["cases"]
    assert cases, "golden 应包含用例"
    for case in cases:
        got = _spectral_reference(case["points"], case["edge_dist"])
        exp = case["expected"]
        for key, expected in exp.items():
            assert key in got, f"{case['name']} 缺字段 {key}"
            assert abs(got[key] - expected) < 1e-5, (
                f"{case['name']} 字段 {key} 不一致: golden={expected}, numpy={got[key]:.6f}"
            )
        # 连通分量大小必须 ≤ n 且 ≥1（自洽性）
        assert 0.0 <= got["r_gcc"] <= 1.0 + 1e-9, f"{case['name']} r_gcc 越界"


if __name__ == "__main__":
    test_golden_matches_independent_reference()
    print("✅ 黄金数据独立参考断言通过")