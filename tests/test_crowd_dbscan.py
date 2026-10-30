#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_crowd_dbscan.py - crowd_dbscan 模块单元测试(unittest)

覆盖用例：
  1. 分散点集(彼此 > eps)：0 簇，全部噪声，最大簇占比=0
  2. 两簇点集(左右各一密集组)：簇数=2，每簇人数、簇中心、最大簇占比
  3. 全连通点集(完全图 K4)：簇数=1，每簇人数=4，占比=1.0
  4. 链式密度可达点集：跨中间点连成 1 簇(验证密度可达而非仅两两邻居)
  5. 噪声点：簇外孤立点被正确标记为噪声
  6. 参数可配置：min_samples 与 eps 改变聚类结果(配置通过函数参数传入)
  7. eps 闭区间边界：距离恰好等于 eps 的两点视为邻居(经典 DBSCAN)
  8. 边界情况：空点集、单点、非法形状输入
  9. 与独立参考实现(核心图连通分量 + 边界点归属)随机交叉验证

运行：
  python -m unittest discover -s tests -p "test_*.py" -v
  或
  python tests/test_crowd_dbscan.py
"""

import os
import sys
import unittest

import numpy as np

# 确保可导入 src/algorithms 下的模块(无需安装)
_HERE = os.path.dirname(os.path.abspath(__file__))
_SRC = os.path.join(_HERE, "..", "src", "algorithms")
if _SRC not in sys.path:
    sys.path.insert(0, _SRC)

from crowd_dbscan import NOISE, UNCLASSIFIED, dbscan_crowd_features, dbscan_labels

# ----------------------------------------------------------------------
# 测试用的真实几何点(单位：米)
# ----------------------------------------------------------------------

# 1) 分散：4 点彼此相距 >= 5m，均超过 eps=1.0 -> 0 簇，全部噪声
DISPERSED_PTS = [(0.0, 0.0), (5.0, 0.0), (10.0, 0.0), (15.0, 0.0)]

# 2) 两簇：左簇 4 点密集(边长 1m 的正方形)，右簇 4 点同样密集，
#    两簇间距约 9m >> eps=1.5 -> 2 簇，每簇 4 人
LEFT_CLUSTER = [(0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (1.0, 1.0)]
RIGHT_CLUSTER = [(10.0, 0.0), (10.0, 1.0), (11.0, 0.0), (11.0, 1.0)]
TWO_CLUSTERS_PTS = LEFT_CLUSTER + RIGHT_CLUSTER

# 3) 全连通：边长 1m 的正方形 4 顶点，两两距离 <= 1.414m，eps=1.5 下完全连通
FULLY_CONNECTED_PTS = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]

# 4) 链式：沿直线每隔 1m 一个点。A-C 相距 2m > eps=1.5，但经 B 密度可达 -> 1 簇
CHAIN_PTS = [(0.0, 0.0), (1.0, 0.0), (2.0, 0.0), (3.0, 0.0)]

# 5) 带噪声：左簇 3 点 + 远处孤立点
CLUSTER_WITH_NOISE = [(0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (20.0, 20.0)]

# ----------------------------------------------------------------------
# 独立参考实现：核心图连通分量 + 边界点归属(与 dbscan_labels 不同代码结构，
# 但语义相同)，用于随机交叉验证簇数/噪声数/聚类数。
# ----------------------------------------------------------------------
def reference_dbscan(pts, eps, min_samples=2):
    """参考 DBSCAN：
    1) 标记核心点(邻域点数 >= min_samples)
    2) 在核心点间建边(距离 <= eps)，求连通分量 -> 每个分量一个簇
    3) 每个边界点(非核心)归属到任意核心邻居所在的簇；否则为噪声
    返回 (labels, n_clusters)。
    """
    n = len(pts)
    if n == 0:
        return [], 0
    dist = np.zeros((n, n))
    for i in range(n):
        for j in range(n):
            dist[i, j] = np.hypot(pts[i][0] - pts[j][0], pts[i][1] - pts[j][1])
    core = [int((dist[i] <= eps).sum()) >= min_samples for i in range(n)]

    # 核心图并查集
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for i in range(n):
        if not core[i]:
            continue
        for j in range(i + 1, n):
            if core[j] and dist[i, j] <= eps:
                union(i, j)

    labels = [-1] * n
    cluster_id = 0
    comp_map = {}
    for i in range(n):
        if core[i]:
            r = find(i)
            if r not in comp_map:
                cluster_id += 1
                comp_map[r] = cluster_id
            labels[i] = comp_map[r]

    # 边界点归属
    for i in range(n):
        if core[i]:
            continue
        for j in range(n):
            if core[j] and dist[i, j] <= eps and labels[j] > 0:
                labels[i] = labels[j]
                break
        if labels[i] == -1:
            labels[i] = NOISE

    return labels, cluster_id


class TestDispersed(unittest.TestCase):
    """分散点集：0 簇，全部噪声，最大簇占比=0。"""

    def setUp(self):
        self.feat = dbscan_crowd_features(DISPERSED_PTS, eps=1.0)

    def test_cluster_count(self):
        self.assertEqual(self.feat["n_clusters"], 0)
        self.assertEqual(self.feat["cluster_sizes"], [])
        self.assertEqual(self.feat["cluster_centers"], [])

    def test_largest_cluster(self):
        self.assertEqual(self.feat["largest_cluster_size"], 0)
        self.assertAlmostEqual(self.feat["largest_cluster_ratio"], 0.0)

    def test_noise(self):
        self.assertEqual(self.feat["n_noise"], 4)
        self.assertEqual(self.feat["n_clustered"], 0)
        self.assertTrue(np.all(self.feat["labels"] == NOISE))

    def test_n(self):
        self.assertEqual(self.feat["n"], 4)


class TestTwoClusters(unittest.TestCase):
    """两簇点集：簇数=2，每簇 4 人，最大簇占比=0.5，簇中心正确。"""

    def setUp(self):
        self.feat = dbscan_crowd_features(TWO_CLUSTERS_PTS, eps=1.5)

    def test_cluster_count(self):
        self.assertEqual(self.feat["n_clusters"], 2)
        self.assertEqual(self.feat["cluster_sizes"], [4, 4])

    def test_largest_cluster(self):
        self.assertEqual(self.feat["largest_cluster_size"], 4)
        self.assertAlmostEqual(self.feat["largest_cluster_ratio"], 0.5)

    def test_cluster_centers(self):
        centers = sorted(self.feat["cluster_centers"])
        # 左簇质心(0.5,0.5)，右簇质心(10.5,0.5)
        expected = sorted([(0.5, 0.5), (10.5, 0.5)])
        for got, exp in zip(centers, expected):
            self.assertAlmostEqual(got[0], exp[0], places=6)
            self.assertAlmostEqual(got[1], exp[1], places=6)

    def test_noise(self):
        self.assertEqual(self.feat["n_noise"], 0)
        self.assertEqual(self.feat["n_clustered"], 8)
        self.assertTrue(np.all(self.feat["labels"] > 0))


class TestFullyConnected(unittest.TestCase):
    """全连通点集(完全图 K4)：簇数=1，每簇 4 人，占比=1.0。"""

    def setUp(self):
        self.feat = dbscan_crowd_features(FULLY_CONNECTED_PTS, eps=1.5)

    def test_cluster_count(self):
        self.assertEqual(self.feat["n_clusters"], 1)
        self.assertEqual(self.feat["cluster_sizes"], [4])

    def test_largest_cluster(self):
        self.assertEqual(self.feat["largest_cluster_size"], 4)
        self.assertAlmostEqual(self.feat["largest_cluster_ratio"], 1.0)

    def test_center(self):
        cx, cy = self.feat["cluster_centers"][0]
        self.assertAlmostEqual(cx, 0.5, places=6)
        self.assertAlmostEqual(cy, 0.5, places=6)

    def test_no_noise(self):
        self.assertEqual(self.feat["n_noise"], 0)
        self.assertEqual(self.feat["n_clustered"], 4)


class TestChainDensityReachable(unittest.TestCase):
    """链式密度可达：A-C 相距 2m > eps，但经 B 连成 1 簇。"""

    def setUp(self):
        self.feat = dbscan_crowd_features(CHAIN_PTS, eps=1.5)

    def test_single_cluster(self):
        self.assertEqual(self.feat["n_clusters"], 1)
        self.assertEqual(self.feat["cluster_sizes"], [4])
        self.assertAlmostEqual(self.feat["largest_cluster_ratio"], 1.0)

    def test_no_noise(self):
        self.assertEqual(self.feat["n_noise"], 0)
        self.assertEqual(self.feat["n_clustered"], 4)


class TestNoisePoints(unittest.TestCase):
    """簇外孤立点被标记为噪声。"""

    def setUp(self):
        self.feat = dbscan_crowd_features(CLUSTER_WITH_NOISE, eps=1.5)

    def test_cluster_count(self):
        self.assertEqual(self.feat["n_clusters"], 1)
        self.assertEqual(self.feat["cluster_sizes"], [3])

    def test_noise(self):
        self.assertEqual(self.feat["n_noise"], 1)          # 远处 (20,20) 点
        self.assertEqual(self.feat["n_clustered"], 3)

    def test_labels(self):
        # 前 3 点同簇(标签=1)，最后一点为噪声(-1)
        self.assertEqual(set(self.feat["labels"][:3]), {1})
        self.assertEqual(int(self.feat["labels"][3]), NOISE)


class TestConfigurableParams(unittest.TestCase):
    """DBSCAN 参数通过配置传入：改变 min_samples 与 eps 会改变聚类结果。"""

    def test_min_samples_3_turns_pair_into_noise(self):
        pair = [(0.0, 0.0), (1.0, 0.0)]          # 间距 1m <= eps=1.5
        # min_samples=2 -> 1 簇
        feat2 = dbscan_crowd_features(pair, eps=1.5, min_samples=2)
        self.assertEqual(feat2["n_clusters"], 1)
        # min_samples=3 -> 每点邻域只有 2 点，不是核心 -> 0 簇，全部噪声
        feat3 = dbscan_crowd_features(pair, eps=1.5, min_samples=3)
        self.assertEqual(feat3["n_clusters"], 0)
        self.assertEqual(feat3["n_noise"], 2)

    def test_min_samples_3_with_dense_group_still_clusters(self):
        # 2x2 网格在 eps=1.5 下为完全图 K4，每点邻域=4 >= 3 -> 1 簇
        feat = dbscan_crowd_features(FULLY_CONNECTED_PTS, eps=1.5, min_samples=3)
        self.assertEqual(feat["n_clusters"], 1)
        self.assertEqual(feat["cluster_sizes"], [4])

    def test_eps_too_small_merges_clusters(self):
        # 两簇间距约 9m：eps=1.5 -> 2 簇；eps 过大(如 20m) -> 全部并入 1 簇
        feat_small = dbscan_crowd_features(TWO_CLUSTERS_PTS, eps=1.5)
        self.assertEqual(feat_small["n_clusters"], 2)
        feat_large = dbscan_crowd_features(TWO_CLUSTERS_PTS, eps=20.0)
        self.assertEqual(feat_large["n_clusters"], 1)
        self.assertEqual(feat_large["cluster_sizes"], [8])


class TestEpsInclusiveBoundary(unittest.TestCase):
    """经典 DBSCAN 使用闭区间：距离恰好等于 eps 的两点应视为邻居。"""

    def test_exact_eps_distance_forms_cluster(self):
        pts = [(0.0, 0.0), (1.0, 0.0)]           # 距离恰为 eps=1.0
        feat = dbscan_crowd_features(pts, eps=1.0, min_samples=2)
        self.assertEqual(feat["n_clusters"], 1)
        self.assertEqual(feat["cluster_sizes"], [2])
        self.assertAlmostEqual(feat["largest_cluster_ratio"], 1.0)

    def test_just_beyond_eps_is_noise(self):
        pts = [(0.0, 0.0), (1.0001, 0.0)]        # 距离略大于 eps=1.0
        feat = dbscan_crowd_features(pts, eps=1.0, min_samples=2)
        self.assertEqual(feat["n_clusters"], 0)
        self.assertEqual(feat["n_noise"], 2)


class TestEdgeCases(unittest.TestCase):
    """空点集、单点、非法形状。"""

    def test_empty(self):
        feat = dbscan_crowd_features([], eps=1.0)
        self.assertEqual(feat["n"], 0)
        self.assertEqual(feat["n_clusters"], 0)
        self.assertEqual(feat["cluster_sizes"], [])
        self.assertEqual(feat["largest_cluster_ratio"], 0.0)
        self.assertEqual(feat["n_noise"], 0)
        self.assertEqual(len(feat["labels"]), 0)

    def test_single_point(self):
        feat = dbscan_crowd_features([(3.0, 4.0)], eps=1.0)
        self.assertEqual(feat["n"], 1)
        self.assertEqual(feat["n_clusters"], 0)
        self.assertEqual(feat["n_noise"], 1)

    def test_two_points_within_eps(self):
        feat = dbscan_crowd_features([(0.0, 0.0), (0.8, 0.0)], eps=1.0)
        self.assertEqual(feat["n_clusters"], 1)
        self.assertEqual(feat["cluster_sizes"], [2])

    def test_raises_on_bad_shape(self):
        with self.assertRaises(ValueError):
            dbscan_crowd_features([1.0, 2.0, 3.0], eps=1.0)   # 不是 (n,2)
        with self.assertRaises(ValueError):
            dbscan_labels(np.zeros((3, 3)), eps=1.0)          # 列数不为 2

    def test_dbscan_labels_empty(self):
        labels, n = dbscan_labels([], eps=1.0)
        self.assertEqual(n, 0)
        self.assertEqual(len(labels), 0)


class TestAgainstReference(unittest.TestCase):
    """与独立参考实现交叉验证：簇数、噪声数、聚类点数在随机点集上应一致。"""

    def _assert_match(self, pts, eps, min_samples):
        labels, n_clusters = dbscan_labels(pts, eps, min_samples)
        ref_labels, ref_clusters = reference_dbscan(pts, eps, min_samples)
        self.assertEqual(n_clusters, ref_clusters,
                         msg="簇数不一致: pts=%s eps=%s ms=%d" % (pts, eps, min_samples))
        self.assertEqual(int((labels == NOISE).sum()),
                         int((np.asarray(ref_labels) == NOISE).sum()),
                         msg="噪声数不一致: pts=%s eps=%s ms=%d" % (pts, eps, min_samples))
        self.assertEqual(int((labels > 0).sum()),
                         int((np.asarray(ref_labels) > 0).sum()),
                         msg="聚类点数不一致")

    def test_geometric_sets(self):
        for pts, eps, ms in [
            (DISPERSED_PTS, 1.0, 2),
            (TWO_CLUSTERS_PTS, 1.5, 2),
            (FULLY_CONNECTED_PTS, 1.5, 2),
            (CHAIN_PTS, 1.5, 2),
            (CLUSTER_WITH_NOISE, 1.5, 2),
            (FULLY_CONNECTED_PTS, 1.5, 3),
        ]:
            self._assert_match(pts, eps, ms)

    def test_random_sets(self):
        rng = np.random.default_rng(2024)
        for _ in range(50):
            n = int(rng.integers(0, 15))
            pts = list(rng.uniform(-5.0, 5.0, size=(n, 2)))
            eps = float(rng.uniform(0.5, 2.5))
            ms = int(rng.integers(2, 5))
            self._assert_match(pts, eps, ms)

    def test_random_gaussian_blobs(self):
        """在混合高斯生成的簇状点集上交叉验证(更贴近真实人群分布)。"""
        rng = np.random.default_rng(7)
        for _ in range(20):
            blobs = []
            for _c in range(int(rng.integers(1, 4))):
                cx, cy = rng.uniform(-6, 6, size=2)
                k = int(rng.integers(2, 8))
                blobs.append(rng.normal(loc=[cx, cy], scale=0.5, size=(k, 2)))
            pts = list(np.concatenate(blobs))
            self._assert_match(pts, eps=1.2, min_samples=2)


class TestCppAlgorithmLogic(unittest.TestCase):
    """逐行复刻 crowd_dbscan.h 中的 dbscan 算法(纯 Python)，验证其与
    Python 端 dbscan_labels 输出完全一致(间接验证 C++ 头文件的正确性)。

    说明：本机(git-bash/Windows)未安装 C++ 编译器，故沿用
    test_graph_laplacian.py 对 Jacobi 的验证方式，用 Python 精确复刻
    C++ 头文件算法逻辑做等价性测试；在目标板(Luckfox/RKNN)或 CI 上
    编译 crowd_dbscan.h 时应再跑一遍真实 C++ 用例。
    """

    @staticmethod
    def cpp_style_dbscan(pts, eps, min_samples):
        """与 crowd_dbscan.h 中 CrowdDBSCAN::dbscan 相同的算法(逐行翻译)。"""
        n = len(pts)
        # 与 crowd_dbscan.h 保持一致：噪声标签 -1，内部哨兵 -2
        UNCLASSIFIED, NOISE = -2, -1
        labels = [UNCLASSIFIED] * n
        n_clusters = 0
        if n == 0:
            return labels, 0
        neighbors = []
        for i in range(n):
            nbrs = []
            for j in range(n):
                dx = pts[i][0] - pts[j][0]
                dy = pts[i][1] - pts[j][1]
                if (dx * dx + dy * dy) ** 0.5 <= eps:
                    nbrs.append(j)
            neighbors.append(nbrs)
        for i in range(n):
            if labels[i] != UNCLASSIFIED:
                continue
            if len(neighbors[i]) < min_samples:
                labels[i] = NOISE
                continue
            n_clusters += 1
            cid = n_clusters
            labels[i] = cid
            stack = list(neighbors[i])
            while stack:
                j = stack.pop()
                if labels[j] == cid:
                    continue
                if labels[j] != UNCLASSIFIED and labels[j] != NOISE:
                    continue
                labels[j] = cid
                if len(neighbors[j]) >= min_samples:
                    for nb in neighbors[j]:
                        if labels[nb] == UNCLASSIFIED or labels[nb] == NOISE:
                            stack.append(nb)
        return labels, n_clusters

    def test_matches_python_on_geometric_sets(self):
        for pts, eps, ms in [
            (DISPERSED_PTS, 1.0, 2),
            (TWO_CLUSTERS_PTS, 1.5, 2),
            (FULLY_CONNECTED_PTS, 1.5, 2),
            (CHAIN_PTS, 1.5, 2),
            (CLUSTER_WITH_NOISE, 1.5, 2),
        ]:
            py_labels, py_n = dbscan_labels(pts, eps, ms)
            cpp_labels, cpp_n = self.cpp_style_dbscan(pts, eps, ms)
            self.assertEqual(py_n, cpp_n)
            self.assertTrue(np.array_equal(py_labels, np.asarray(cpp_labels)))

    def test_matches_python_on_random(self):
        rng = np.random.default_rng(99)
        for _ in range(30):
            n = int(rng.integers(0, 12))
            pts = list(rng.uniform(-4, 4, size=(n, 2)))
            eps = float(rng.uniform(0.5, 2.0))
            ms = int(rng.integers(2, 5))
            py_labels, py_n = dbscan_labels(pts, eps, ms)
            cpp_labels, cpp_n = self.cpp_style_dbscan(pts, eps, ms)
            self.assertEqual(py_n, cpp_n)
            self.assertTrue(np.array_equal(py_labels, np.asarray(cpp_labels)))


if __name__ == "__main__":
    unittest.main(verbosity=2)