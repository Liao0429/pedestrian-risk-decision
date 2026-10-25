#ifndef CROWD_DBSCAN_H
#define CROWD_DBSCAN_H

/**
 * crowd_dbscan.h - DBSCAN 密度聚类人群分析（C++ 接口）
 *
 * 与 Python 端 src/algorithms/crowd_dbscan.py 对应，供开发板(Luckfox)
 * C++ 工程集成，作为图拉普拉斯谱特征之外的工程化拥挤度特征。
 * 提供：
 *   - 基于行人地面坐标(米)进行 DBSCAN 密度聚类
 *   - 输出簇数、每簇人数(降序)、最大簇人数占比、簇中心列表、
 *     每个行人的簇标签、噪声点/被聚类点数
 *
 * DBSCAN 参数(可通过配置传入)：
 *   - eps         : 聚集半径(米)，两点欧氏距离 dist <= eps 视为邻居(闭区间)
 *   - min_samples : 邻域内最少点数(含自身)才视为核心点，默认 2
 *
 * 依赖：仅标准库(<vector>/<utility>/<cmath>/<cstddef>/<algorithm>)，不依赖
 * Eigen/OpenCV，适合板端交叉编译。
 *
 * 注意：本实现为朴素 O(n^2) 邻域枚举(人群规模 n 通常为几十~几百，
 *       开销可忽略)；如需处理大规模人群可替换为 KD 树等空间索引。
 *       算法正确性已在 x86 上用 Python 端(crowd_dbscan.py)与单元测试验证。
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

// DBSCAN 人群聚类结果结构体(字段与 Python 端 dbscan_crowd_features 返回字典对应)
struct CrowdClusterFeatures {
    int    n = 0;                                   // 行人数
    int    n_clusters = 0;                          // 簇数
    std::vector<int> cluster_sizes;                 // 每簇人数(按簇大小降序)
    int    largest_cluster_size = 0;                // 最大簇人数
    double largest_cluster_ratio = 0.0;         // 最大簇人数占比 = 最大簇人数/n
    std::vector<std::pair<double,double>> cluster_centers; // 簇中心列表(米)
    std::vector<int> labels;                    // 每个行人的簇标签(-1=噪声)
    int    n_noise = 0;                            // 噪声点数
    int    n_clustered = 0;                         // 被聚类点数
};

class CrowdDBSCAN {
public:
    explicit CrowdDBSCAN(double eps = 1.5, int min_samples = 2);

    void setEps(double eps);
    double eps() const;
    void setMinSamples(int ms);
    int minSamples() const;

    void clear();
    void addPoint(float x, float y);                   // 添加一个行人地面坐标(米)
    void setPoints(const std::vector<std::pair<float,float>>& pts);

    CrowdClusterFeatures compute() const;

private:
    double m_eps;
    int    m_minSamples;
    std::vector<std::pair<float,float>> m_pts;

    // 核心 DBSCAN 算法：输出 labels(-1=噪声) 与 nClusters(簇数)
    static void dbscan(const std::vector<std::pair<float,float>>& pts,
                       double eps, int minSamples,
                       std::vector<int>& labels, int& nClusters);
};

// ----------------------------------------------------------------------
// 实现（头文件内联，方便单头文件集成到板端工程）
// ----------------------------------------------------------------------

inline CrowdDBSCAN::CrowdDBSCAN(double eps, int min_samples)
    : m_eps(eps), m_minSamples(min_samples) {}

inline void CrowdDBSCAN::setEps(double eps) { m_eps = eps; }
inline double CrowdDBSCAN::eps() const { return m_eps; }
inline void CrowdDBSCAN::setMinSamples(int ms) { m_minSamples = ms; }
inline int CrowdDBSCAN::minSamples() const { return m_minSamples; }

inline void CrowdDBSCAN::clear() { m_pts.clear(); }

inline void CrowdDBSCAN::addPoint(float x, float y) {
    m_pts.push_back(std::make_pair(x, y));
}

inline void CrowdDBSCAN::setPoints(const std::vector<std::pair<float,float>>& pts) {
    m_pts = pts;
}

inline void CrowdDBSCAN::dbscan(
        const std::vector<std::pair<float,float>>& pts,
        double eps, int minSamples,
        std::vector<int>& labels, int& nClusters) {
    const int n = (int)pts.size();
    const int UNCLASSIFIED = -1;
    const int NOISE = -2;
    labels.assign(n, UNCLASSIFIED);
    nClusters = 0;
    if (n == 0) return;

    // 预计算每个点的 eps 邻域(闭区间，dist <= eps)。点自身距离为 0，恒为邻居。
    std::vector<std::vector<int>> neighbors(n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const double dx = (double)pts[i].first - (double)pts[j].first;
            const double dy = (double)pts[i].second - (double)pts[j].second;
            if (std::sqrt(dx * dx + dy * dy) <= eps)
                neighbors[i].push_back(j);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (labels[i] != UNCLASSIFIED) continue;
        if ((int)neighbors[i].size() < minSamples) {
            labels[i] = NOISE;                       // 噪声点
            continue;
        }
        ++nClusters;                                 // 开始新簇
        const int cid = nClusters;
        labels[i] = cid;
        std::vector<int> stack = neighbors[i];
        while (!stack.empty()) {
            const int j = stack.back();
            stack.pop_back();
            if (labels[j] == cid) continue;                       // 已属本簇
            if (labels[j] != UNCLASSIFIED && labels[j] != NOISE) continue; // 他簇
            labels[j] = cid;
            if ((int)neighbors[j].size() >= minSamples) {         // 核心点 -> 扩张
                for (std::size_t k = 0; k < neighbors[j].size(); ++k) {
                    const int nb = neighbors[j][k];
                    if (labels[nb] == UNCLASSIFIED || labels[nb] == NOISE)
                        stack.push_back(nb);
                }
            }
        }
    }
}

inline CrowdClusterFeatures CrowdDBSCAN::compute() const {
    CrowdClusterFeatures feat;
    const int n = (int)m_pts.size();
    feat.n = n;
    if (n == 0) return feat;

    std::vector<int> labels;
    int nClusters = 0;
    dbscan(m_pts, m_eps, m_minSamples, labels, nClusters);
    feat.labels = labels;
    feat.n_clusters = nClusters;
    if (nClusters == 0) {
        feat.n_noise = n;
        feat.n_clustered = 0;
        return feat;
    }

    // 统计每簇人数与质心(0-based 簇下标)
    std::vector<int>    sizes(nClusters, 0);
    std::vector<double> cx(nClusters, 0.0), cy(nClusters, 0.0);
    for (int i = 0; i < n; ++i) {
        const int c = labels[i];
        if (c > 0) {                                  // 簇编号从 1 开始
            sizes[c - 1]++;
            cx[c - 1] += (double)m_pts[i].first;
            cy[c - 1] += (double)m_pts[i].second;
        }
    }

    feat.n_noise = 0;
    for (int i = 0; i < n; ++i)
        if (labels[i] <= 0) ++feat.n_noise;            // -1/-2 均为未聚类
    feat.n_clustered = n - feat.n_noise;

    std::vector<std::pair<double,double>> centers(nClusters);
    for (int c = 0; c < nClusters; ++c) {
        if (sizes[c] > 0)
            centers[c] = std::make_pair(cx[c] / sizes[c], cy[c] / sizes[c]);
    }

    // 按簇大小降序排序(簇中心同步排序)
    std::vector<int> order(nClusters);
    for (int c = 0; c < nClusters; ++c) order[c] = c;
    std::sort(order.begin(), order.end(),
              [&sizes](int a, int b) { return sizes[a] > sizes[b]; });
    feat.cluster_sizes.resize(nClusters);
    feat.cluster_centers.resize(nClusters);
    for (int c = 0; c < nClusters; ++c) {
        feat.cluster_sizes[c] = sizes[order[c]];
        feat.cluster_centers[c] = centers[order[c]];
    }

    feat.largest_cluster_size = feat.cluster_sizes[0];
    feat.largest_cluster_ratio = (double)feat.largest_cluster_size / n;
    return feat;
}

#endif // CROWD_DBSCAN_H