#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_graph_laplacian.py - graph_laplacian 模块单元测试(unittest)

覆盖用例：
  1. 分散点集(无连边)：特征值全 0，m=0，r_gcc=1/n，特征熵=0
  2. 聚集点集(路径图 P5)：连通但稀疏，验证 Fiedler 值/谱半径解析值
  3. 全连接点集(完全图 K4)：验证密度/聚类系数=1，Fiedler 值=n，特征熵=ln(n-1)
  4. 谱特征单调性：分散 -> 聚集 -> 全连接，密度/代数连通度/谱半径单调递增
  5. 邻接矩阵性质：对称、零对角、0/1 值
  6. 与 risk_engine compute_features 兼容性比对

运行：
  python -m unittest discover -s tests -p "test_*.py" -v
  或
  python tests/test_graph_laplacian.py
"""

import math
import os
import sys
import unittest

import numpy as np

# 确保可导入 src/algorithms 下的模块(无需安装)
_HERE = os.path.dirname(os.path.abspath(__file__))
_SRC = os.path.join(_HERE, "..", "src", "algorithms")
if _SRC not in sys.path:
    sys.path.insert(0, _SRC)

from graph_laplacian import build_adjacency, compute_laplacian_features

# ----------------------------------------------------------------------
# 测试用的真实几何点(单位：米)
# ----------------------------------------------------------------------
EDGE_DIST = 2.0

# 1) 分散：4 点彼此相距 5m，均超过 2.0m 阈值 -> 空图
DISPERSED_PTS = [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (15.0, 0.0)]

# 2) 聚集：5 点沿直线间距 1.5m -> 路径图 P5(连通但稀疏，无三角形)
CLUSTERED_PTS = [(i * 1.5, 0.0) for i in range(5)]

# 3) 全连接：边长 1m 的正方形 4 顶点，任意两点 < 2.0m -> 完全图 K4
FULLY_CONNECTED_PTS = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]


def reference_compute_features(pts, edge_dist):
    """复刻 risk_engine.cc / scripts/crowd_test.py 的 compute_features 逻辑，
    用于验证兼容性(输入输出与 C++ 端完全一致)。
    """
    n = len(pts)
    if n < 2:
        density = 0.0
        r_gcc = 1.0 if n > 1 else 0.0
        clustering = 0.0
        return n, 0, density, r_gcc, clustering

    adj = [[False] * n for _ in range(n)]
    m = 0
    for i in range(n):
        xi, yi = pts[i]
        for j in range(i + 1, n):
            xj, yj = pts[j]
            d = ((xi - xj) ** 2 + (yi - yj) ** 2) ** 0.5
            if d < edge_dist:
                adj[i][j] = adj[j][i] = True
                m += 1

    possible = n * (n - 1) // 2
    density = m / float(possible) if possible > 0 else 0.0

    visited = [False] * n
    max_comp = 0
    for i in range(n):
        if visited[i]:
            continue
        comp = 0
        queue = [i]
        visited[i] = True
        while queue:
            u = queue.pop(0)
            comp += 1
            for v in range(n):
                if adj[u][v] and not visited[v]:
                    visited[v] = True
                    queue.append(v)
        if comp > max_comp:
            max_comp = comp
    r_gcc = max_comp / float(n)

    sum_c = 0.0
    for i in range(n):
        neigh = [j for j in range(n) if adj[i][j]]
        k = len(neigh)
        if k >= 2:
            links = 0
            for a in range(k):
                for b in range(a + 1, k):
                    if adj[neigh[a]][neigh[b]]:
                        links += 1
            max_pairs = k * (k - 1) // 2
            sum_c += links / float(max_pairs)
    clustering = sum_c / float(n) if n > 0 else 0.0

    return n, m, density, r_gcc, clustering


class TestDispersed(unittest.TestCase):
    """分散点集：无连边，拉普拉斯为全零矩阵，所有谱特征为 0。"""

    def setUp(self):
        self.feat = compute_laplacian_features(DISPERSED_PTS, EDGE_DIST)

    def test_basic_network_features(self):
        self.assertEqual(self.feat["n"], 4)
        self.assertEqual(self.feat["m"], 0)
        self.assertAlmostEqual(self.feat["density"], 0.0)
        self.assertAlmostEqual(self.feat["r_gcc"], 0.25)  # 每个点自成连通分量
        self.assertAlmostEqual(self.feat["clustering"], 0.0)

    def test_spectral_features_all_zero(self):
        self.assertAlmostEqual(self.feat["fiedler"], 0.0)
        self.assertAlmostEqual(self.feat["algebraic_connectivity"], 0.0)
        self.assertAlmostEqual(self.feat["spectral_radius"], 0.0)
        self.assertAlmostEqual(self.feat["eig_entropy"], 0.0)

    def test_laplacian_is_zero_matrix(self):
        A = build_adjacency(DISPERSED_PTS, EDGE_DIST)
        self.assertTrue(np.all(A == 0.0))
        L = np.diag(A.sum(axis=1)) - A
        self.assertTrue(np.allclose(L, 0.0))
        eig = np.linalg.eigvalsh(L)
        self.assertTrue(np.allclose(eig, 0.0))


class TestClustered(unittest.TestCase):
    """聚集点集(路径图 P5)：连通但稀疏，验证谱特征解析值。"""

    def setUp(self):
        self.feat = compute_laplacian_features(CLUSTERED_PTS, EDGE_DIST)

    def test_basic_network_features(self):
        self.assertEqual(self.feat["n"], 5)
        self.assertEqual(self.feat["m"], 4)                    # 路径图 P5 有 4 条边
        self.assertAlmostEqual(self.feat["density"], 0.4)      # 4 / C(5,2)=4/10
        self.assertAlmostEqual(self.feat["r_gcc"], 1.0)       # 连通
        self.assertAlmostEqual(self.feat["clustering"], 0.0)  # 路径图无三角形

    def test_spectral_features(self):
        # 路径图 P5 的拉普拉斯特征值: lambda_k = 2 - 2cos(k*pi/5), k=0..4
        fiedler_expected = 2.0 - 2.0 * math.cos(math.pi / 5.0)
        radius_expected = 2.0 - 2.0 * math.cos(4.0 * math.pi / 5.0)
        self.assertAlmostEqual(self.feat["fiedler"], fiedler_expected, places=4)
        self.assertAlmostEqual(self.feat["algebraic_connectivity"],
                               fiedler_expected, places=4)
        self.assertAlmostEqual(self.feat["spectral_radius"], radius_expected, places=4)
        self.assertGreater(self.feat["eig_entropy"], 0.0)

    def test_eigenvalue_trace_and_order(self):
        A = build_adjacency(CLUSTERED_PTS, EDGE_DIST)
        L = np.diag(A.sum(axis=1)) - A
        eig = np.linalg.eigvalsh(L)
        self.assertAlmostEqual(float(eig.sum()), 2.0 * self.feat["m"], places=6)
        self.assertGreaterEqual(float(eig[0]), -1e-9)          # 最小特征值>=0
        self.assertTrue(np.all(np.diff(eig) >= -1e-9))          # 升序排列


class TestFullyConnected(unittest.TestCase):
    """全连接点集(完全图 K4)：密度/聚类系数=1，Fiedler 值=n，特征熵=ln(n-1)。"""

    def setUp(self):
        self.feat = compute_laplacian_features(FULLY_CONNECTED_PTS, EDGE_DIST)

    def test_basic_network_features(self):
        self.assertEqual(self.feat["n"], 4)
        self.assertEqual(self.feat["m"], 6)                    # C(4,2)=6
        self.assertAlmostEqual(self.feat["density"], 1.0)
        self.assertAlmostEqual(self.feat["r_gcc"], 1.0)
        self.assertAlmostEqual(self.feat["clustering"], 1.0)

    def test_spectral_features(self):
        # 完全图 K4 的拉普拉斯特征值: [0, 4, 4, 4]
        self.assertAlmostEqual(self.feat["fiedler"], 4.0, places=6)
        self.assertAlmostEqual(self.feat["algebraic_connectivity"], 4.0, places=6)
        self.assertAlmostEqual(self.feat["spectral_radius"], 4.0, places=6)
        self.assertAlmostEqual(self.feat["eig_entropy"], math.log(3.0), places=6)

    def test_eigenvalues(self):
        A = build_adjacency(FULLY_CONNECTED_PTS, EDGE_DIST)
        L = np.diag(A.sum(axis=1)) - A
        eig = np.linalg.eigvalsh(L)
        self.assertTrue(np.allclose(eig, [0.0, 4.0, 4.0, 4.0], atol=1e-9))


class TestMonotonicity(unittest.TestCase):
    """谱特征单调性：分散 -> 聚集 -> 全连接，网络/谱特征应单调递增。"""

    def test_spectral_monotonic(self):
        f_disp = compute_laplacian_features(DISPERSED_PTS, EDGE_DIST)
        f_clu = compute_laplacian_features(CLUSTERED_PTS, EDGE_DIST)
        f_full = compute_laplacian_features(FULLY_CONNECTED_PTS, EDGE_DIST)

        self.assertLess(f_disp["m"], f_clu["m"])
        self.assertLess(f_clu["m"], f_full["m"])

        self.assertLess(f_disp["density"], f_clu["density"])
        self.assertLess(f_clu["density"], f_full["density"])

        self.assertLess(f_disp["r_gcc"], f_clu["r_gcc"])
        self.assertLessEqual(f_clu["r_gcc"], f_full["r_gcc"])

        # Fiedler 值 / 代数连通度单调递增
        self.assertLess(f_disp["fiedler"], f_clu["fiedler"])
        self.assertLess(f_clu["fiedler"], f_full["fiedler"])
        self.assertLess(f_disp["algebraic_connectivity"],
                        f_clu["algebraic_connectivity"])
        self.assertLess(f_clu["algebraic_connectivity"],
                        f_full["algebraic_connectivity"])

        # 谱半径单调递增
        self.assertLess(f_disp["spectral_radius"], f_clu["spectral_radius"])
        self.assertLess(f_clu["spectral_radius"], f_full["spectral_radius"])

        # 特征熵：分散图为 0，聚集/全连接为正
        self.assertAlmostEqual(f_disp["eig_entropy"], 0.0)
        self.assertGreater(f_clu["eig_entropy"], 0.0)
        self.assertGreater(f_full["eig_entropy"], 0.0)

    def test_fully_connected_entropy_monotonic_in_n(self):
        """完全图 K_n 的特征熵 = ln(n-1)，随 n 单调递增。"""
        entropies = []
        for n in range(2, 7):
            pts = [(i * 0.25, 0.0) for i in range(n)]  # 间距 0.25m，保证全连接(K_n)
            feat = compute_laplacian_features(pts, EDGE_DIST)
            self.assertAlmostEqual(feat["density"], 1.0)
            self.assertAlmostEqual(feat["eig_entropy"], math.log(n - 1), places=6)
            entropies.append(feat["eig_entropy"])
        for a, b in zip(entropies, entropies[1:]):
            self.assertLess(a, b)


class TestBuildAdjacency(unittest.TestCase):
    """邻接矩阵构造的正确性。"""

    def test_symmetry_diagonal_binary(self):
        A = build_adjacency(FULLY_CONNECTED_PTS, EDGE_DIST)
        self.assertEqual(A.shape, (4, 4))
        self.assertTrue(np.allclose(A, A.T))                   # 对称
        self.assertTrue(np.all(np.diag(A) == 0.0))             # 无自环
        self.assertTrue(np.all(np.logical_or(A == 0.0, A == 1.0)))  # 0/1

    def test_known_edges(self):
        # A-B 相距 1m(连边)，A-C 相距 ~5.1m(不连)，B-C 相距 ~4.1m(不连)
        pts = [(0.0, 0.0), (1.0, 0.0), (5.0, 1.0)]
        A = build_adjacency(pts, EDGE_DIST)
        self.assertEqual(A[0, 1], 1.0)
        self.assertEqual(A[1, 0], 1.0)
        self.assertEqual(A[0, 2], 0.0)
        self.assertEqual(A[2, 0], 0.0)
        self.assertEqual(A[1, 2], 0.0)

    def test_single_and_empty(self):
        self.assertEqual(build_adjacency([], EDGE_DIST).shape, (0, 0))
        A = build_adjacency([(1.0, 2.0)], EDGE_DIST)
        self.assertEqual(A.shape, (1, 1))
        self.assertTrue(np.all(A == 0.0))

    def test_raises_on_bad_shape(self):
        with self.assertRaises(ValueError):
            build_adjacency([1.0, 2.0, 3.0], EDGE_DIST)  # 不是 (n,2)


class TestRiskEngineCompatibility(unittest.TestCase):
    """compute_laplacian_features 的 n/m/density/r_gcc/clustering 应与
    risk_engine.cc / crowd_test.py 的 compute_features 完全一致。"""

    def test_matches_reference(self):
        for pts in (DISPERSED_PTS, CLUSTERED_PTS, FULLY_CONNECTED_PTS):
            feat = compute_laplacian_features(pts, EDGE_DIST)
            ref = reference_compute_features(pts, EDGE_DIST)
            self.assertEqual(feat["n"], ref[0])
            self.assertEqual(feat["m"], ref[1])
            self.assertAlmostEqual(feat["density"], ref[2], places=12)
            self.assertAlmostEqual(feat["r_gcc"], ref[3], places=12)
            self.assertAlmostEqual(feat["clustering"], ref[4], places=12)

    def test_matches_reference_random(self):
        rng = np.random.default_rng(42)
        for _ in range(30):
            n = int(rng.integers(0, 10))
            pts = list(rng.uniform(-3.0, 3.0, size=(n, 2)))
            feat = compute_laplacian_features(pts, EDGE_DIST)
            ref = reference_compute_features(pts, EDGE_DIST)
            self.assertEqual(feat["n"], ref[0])
            self.assertEqual(feat["m"], ref[1])
            self.assertAlmostEqual(feat["density"], ref[2], places=12)
            self.assertAlmostEqual(feat["r_gcc"], ref[3], places=12)
            self.assertAlmostEqual(feat["clustering"], ref[4], places=12)


class TestJacobiAlgorithmLogic(unittest.TestCase):
    """用 Python 复刻 C++ 头文件的 Jacobi 算法逻辑，验证其与
    numpy.linalg.eigvalsh 一致(间接验证 graph_laplacian.h 的算法正确性)。"""

    @staticmethod
    def jacobi_eigenvalues(A):
        """与 graph_laplacian.h 中 jacobiEigenvalues 相同的算法(纯 Python 复刻)。"""
        n = A.shape[0]
        a = A.astype(np.float64).copy()
        tol = 1e-12
        max_sweeps = 100
        for _ in range(max_sweeps):
            off = 0.0
            p, q = 0, 1
            for i in range(n):
                for j in range(i + 1, n):
                    av = abs(a[i, j])
                    if av > off:
                        off, p, q = av, i, j
            if off < tol:
                break
            app, aqq, apq = a[p, p], a[q, q], a[p, q]
            theta = (aqq - app) / (2.0 * apq)
            t = (1.0 if theta >= 0 else -1.0) / (abs(theta) + math.sqrt(theta * theta + 1.0))
            c = 1.0 / math.sqrt(t * t + 1.0)
            s = t * c
            for k in range(n):
                if k == p or k == q:
                    continue
                akp, akq = a[k, p], a[k, q]
                a[k, p] = c * akp - s * akq
                a[k, q] = s * akp + c * akq
                a[p, k] = a[k, p]
                a[q, k] = a[k, q]
            a[p, p] = c * c * app - 2.0 * s * c * apq + s * s * aqq
            a[q, q] = s * s * app + 2.0 * s * c * apq + c * c * aqq
            a[p, q] = 0.0
            a[q, p] = 0.0
        eig = np.diag(a)
        eig = np.sort(eig)
        # 与 C++ 头文件一致：仅修正微小的数值负值(> -1e-9)
        eig[(eig < 0.0) & (eig > -1e-9)] = 0.0
        return eig

    def test_matches_numpy_random(self):
        rng = np.random.default_rng(7)
        for n in range(2, 8):
            for _ in range(5):
                M = rng.uniform(-1, 1, size=(n, n))
                L = M + M.T  # 对称正半定不保证，但 Jacobi 对任意对称矩阵可用
                jac = self.jacobi_eigenvalues(L)
                np_ = np.linalg.eigvalsh(L)
                self.assertTrue(np.allclose(jac, np_, atol=1e-8),
                                msg="n=%d 时 Jacobi 与 numpy 不一致" % n)

    def test_matches_numpy_laplacian(self):
        """对真实行人图的拉普拉斯矩阵验证。"""
        for pts in (DISPERSED_PTS, CLUSTERED_PTS, FULLY_CONNECTED_PTS):
            A = build_adjacency(pts, EDGE_DIST)
            L = np.diag(A.sum(axis=1)) - A
            jac = self.jacobi_eigenvalues(L)
            np_ = np.linalg.eigvalsh(L)
            self.assertTrue(np.allclose(jac, np_, atol=1e-8),
                            msg="点集 %s 的 Jacobi 结果不一致" % (pts,))


if __name__ == "__main__":
    unittest.main(verbosity=2)