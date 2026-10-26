#ifndef GRAPH_LAPLACIAN_H
#define GRAPH_LAPLACIAN_H

/**
 * graph_laplacian.h - 图拉普拉斯矩阵 L = D - A 与谱特征分析（C++ 接口）
 *
 * 与 Python 端 src/algorithms/graph_laplacian.py 对应，供开发板(Luckfox)
 * C++ 工程集成。提供：
 *   - 基于行人地面坐标(米)构建邻接矩阵 A（距离阈值 edge_dist 内连边）
 *   - 推导图拉普拉斯矩阵 L = D - A
 *   - 计算谱特征：Fiedler 值/代数连通度(第二小特征值)、谱半径、
 *     特征熵，以及网络特征 n/m/density/r_gcc/clustering
 *
 * 依赖：仅标准库(<vector>/<utility>/<cmath>/<algorithm>/<cstddef>)，
 *       不依赖 Eigen/OpenCV，适合板端交叉编译。
 *
 * 注意：特征值计算采用经典 Jacobi 旋转法(对称矩阵)，算法正确性与精度
 *       已在 x86 上验证；移植到 Luckfox 板端(RKNN/ARM)后需在真实工程中
 *       重新编译并做数值验证（见 docs/algorithms.md 的“板端验证”一节）。
 */

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <utility>
#include <vector>

// 谱特征结构体（字段与 Python 端 compute_laplacian_features 返回字典对应）
struct SpectralFeatures {
    int    n = 0;                       // 节点数(行人数)
    int    m = 0;                       // 边数
    double density = 0.0;               // 网络密度 m / [n*(n-1)/2]
    double r_gcc = 0.0;                 // 最大连通分量占比
    double clustering = 0.0;            // 平均局部聚类系数
    double fiedler = 0.0;               // Fiedler 值 = 第二小特征值 lambda_2
    double spectral_radius = 0.0;       // 谱半径 = 最大特征值 lambda_max
    double algebraic_connectivity = 0.0;// 代数连通度(与 fiedler 数学等价)
    double eig_entropy = 0.0;           // 特征熵(归一化特征谱的香农熵，自然对数)
    bool   converged = true;             // 特征分解是否收敛（Jacobi 达到 max_sweeps 未收敛时为 false）
};

class GraphLaplacian {
public:
    explicit GraphLaplacian(double edge_dist = 2.0);

    void setEdgeDist(double d);
    double edgeDist() const;

    void clear();
    void addPoint(float x, float y);                    // 添加一个行人地面坐标(米)
    void setPoints(const std::vector<std::pair<float,float>>& pts);

    SpectralFeatures computeFeatures() const;

    // 返回 0/1 邻接矩阵(n x n)，供调试/可视化
    std::vector<std::vector<char>> adjacencyMatrix() const;

private:
    double m_edgeDist;
    std::vector<std::pair<float,float>> m_pts;

    // 由点集构建邻接矩阵 adj 与拉普拉斯矩阵 L，并输出边数 m
    static void buildLaplacian(const std::vector<std::pair<float,float>>& pts,
                               double edgeDist,
                               std::vector<std::vector<char>>& adj,
                               std::vector<std::vector<double>>& L,
                               int& m);
    // 对称矩阵 Jacobi 特征值分解(仅取特征值，升序返回)。
    // 返回 true 表示收敛；false 表示达到 max_sweeps 仍未收敛（调用方应回退到安全值）。
    static bool jacobiEigenvalues(const std::vector<std::vector<double>>& A,
                                  std::vector<double>& eigvals);
};

// ----------------------------------------------------------------------
// 实现（头文件内联，方便单头文件集成到板端工程）
// ----------------------------------------------------------------------

inline GraphLaplacian::GraphLaplacian(double edge_dist) : m_edgeDist(2.0) {
    setEdgeDist(edge_dist);
}

inline void GraphLaplacian::setEdgeDist(double d) {
    if (std::isfinite(d) && d > 0.0) m_edgeDist = d;
    // 非法输入（NaN/Inf/非正）：保持原值，避免全图无边/异常建边
}
inline double GraphLaplacian::edgeDist() const { return m_edgeDist; }

inline void GraphLaplacian::clear() { m_pts.clear(); }

inline void GraphLaplacian::addPoint(float x, float y) {
    if (std::isfinite(x) && std::isfinite(y)) {
        m_pts.push_back(std::make_pair(x, y));
    }
    // 非法坐标（NaN/Inf）：忽略该点，避免污染距离矩阵
}

inline void GraphLaplacian::setPoints(const std::vector<std::pair<float,float>>& pts) {
    m_pts.clear();
    m_pts.reserve(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (std::isfinite(pts[i].first) && std::isfinite(pts[i].second))
            m_pts.push_back(pts[i]);
    }
}

inline std::vector<std::vector<char>> GraphLaplacian::adjacencyMatrix() const {
    std::vector<std::vector<char>> adj;
    std::vector<std::vector<double>> L;
    int m = 0;
    buildLaplacian(m_pts, m_edgeDist, adj, L, m);
    return adj;
}

inline void GraphLaplacian::buildLaplacian(
        const std::vector<std::pair<float,float>>& pts,
        double edgeDist,
        std::vector<std::vector<char>>& adj,
        std::vector<std::vector<double>>& L,
        int& m) {
    const int n = (int)pts.size();
    adj.assign(n, std::vector<char>(n, 0));
    m = 0;
    // 输入校验：edgeDist 必须为正有限值，否则安全返回空图
    if (!(edgeDist > 0.0) || !std::isfinite(edgeDist)) {
        L.assign(n, std::vector<double>(n, 0.0));
        return;
    }
    if (n < 2) {
        L.assign(n, std::vector<double>(n, 0.0));
        return;
    }
    // 距离阈值内建边（严格小于，与 risk_config.h 默认 edge_dist_m=2.0 一致）
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double dx = (double)pts[i].first - (double)pts[j].first;
            double dy = (double)pts[i].second - (double)pts[j].second;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d < edgeDist) {
                adj[i][j] = adj[j][i] = 1;
                ++m;
            }
        }
    }
    // 度数矩阵 D 与拉普拉斯矩阵 L = D - A
    L.assign(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        int deg = 0;
        for (int j = 0; j < n; ++j) deg += (int)adj[i][j];
        L[i][i] = (double)deg;          // 对角线为度
        for (int j = 0; j < n; ++j)
            if (adj[i][j]) L[i][j] = -1.0;  // 邻接为 -1
    }
}

inline bool GraphLaplacian::jacobiEigenvalues(
        const std::vector<std::vector<double>>& A,
        std::vector<double>& eigvals) {
    const int n = (int)A.size();
    eigvals.clear();
    if (n == 0) return true;

    std::vector<std::vector<double>> a = A;   // 工作矩阵(将被对角化)
    const double tol = 1e-12;
    const int max_sweeps = 100;
    bool converged = false;

    for (int sweep = 0; sweep < max_sweeps; ++sweep) {
        // 找最大非对角元 a[p][q]
        double off = 0.0;
        int p = 0, q = 1;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double av = std::fabs(a[i][j]);
                if (av > off) { off = av; p = i; q = j; }
            }
        }
        if (off < tol) { converged = true; break; }   // 已收敛

        const double app = a[p][p];
        const double aqq = a[q][q];
        const double apq = a[p][q];
        const double theta = (aqq - app) / (2.0 * apq);
        const double t = (theta >= 0.0 ? 1.0 : -1.0)
                      / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;

        // 旋转行/列(k != p, q)
        for (int k = 0; k < n; ++k) {
            if (k == p || k == q) continue;
            const double akp = a[k][p];
            const double akq = a[k][q];
            a[k][p] = c * akp - s * akq;
            a[k][q] = s * akp + c * akq;
            a[p][k] = a[k][p];
            a[q][k] = a[k][q];
        }
        // 更新对角元与 p,q 交叉项
        a[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        a[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        a[p][q] = 0.0;
        a[q][p] = 0.0;
    }

    if (!converged) {
        // 达到 max_sweeps 仍未收敛：不返回不可靠特征值，由调用方回退
        eigvals.clear();
        return false;
    }

    eigvals.resize(n);
    for (int i = 0; i < n; ++i) eigvals[i] = a[i][i];
    std::sort(eigvals.begin(), eigvals.end());
    // 与 Python 端 np.maximum(eigvals, 0) 一致：所有负特征值一律截断为 0
    for (double& e : eigvals)
        if (e < 0.0) e = 0.0;
    return true;
}

inline SpectralFeatures GraphLaplacian::computeFeatures() const {
    SpectralFeatures feat;
    const int n = (int)m_pts.size();
    feat.n = n;

    if (n < 2) {
        feat.m = 0;
        feat.density = 0.0;
        feat.r_gcc = (n > 0) ? 1.0 : 0.0;
        feat.clustering = 0.0;
        feat.fiedler = 0.0;
        feat.spectral_radius = 0.0;
        feat.algebraic_connectivity = 0.0;
        feat.eig_entropy = 0.0;
        feat.converged = true;   // n<2 不涉及特征分解，视为收敛
        return feat;
    }

    std::vector<std::vector<char>> adj;
    std::vector<std::vector<double>> L;
    int m = 0;
    buildLaplacian(m_pts, m_edgeDist, adj, L, m);
    feat.m = m;

    // ---- 网络密度 ----
    const int possible = n * (n - 1) / 2;
    feat.density = (possible > 0) ? (double)m / possible : 0.0;

    // ---- 最大连通分量占比(BFS) ----
    {
        std::vector<bool> visited(n, false);
        int maxComp = 0;
        for (int i = 0; i < n; ++i) {
            if (visited[i]) continue;
            int comp = 0;
            std::vector<int> stack;
            stack.push_back(i);
            visited[i] = true;
            while (!stack.empty()) {
                const int u = stack.back();
                stack.pop_back();
                ++comp;
                for (int v = 0; v < n; ++v) {
                    if (adj[u][v] && !visited[v]) {
                        visited[v] = true;
                        stack.push_back(v);
                    }
                }
            }
            if (comp > maxComp) maxComp = comp;
        }
        feat.r_gcc = (double)maxComp / n;
    }

    // ---- 平均局部聚类系数 ----
    {
        double sumC = 0.0;
        for (int i = 0; i < n; ++i) {
            int k = 0;
            for (int j = 0; j < n; ++j) if (adj[i][j]) ++k;
            if (k >= 2) {
                int links = 0;
                for (int a = 0; a < n; ++a) {
                    if (!adj[i][a]) continue;
                    for (int b = a + 1; b < n; ++b) {
                        if (adj[i][b] && adj[a][b]) ++links;
                    }
                }
                const int maxPairs = k * (k - 1) / 2;
                sumC += (double)links / maxPairs;
            }
        }
        feat.clustering = (n > 0) ? sumC / n : 0.0;
    }

    // ---- 谱特征(Jacobi 特征值) ----
    {
        std::vector<double> eig;
        const bool converged = jacobiEigenvalues(L, eig);   // 已升序
        feat.converged = converged;
        if (!converged) {
            // 特征分解未收敛：谱特征回退到安全值（0），不输出不可靠特征值
            feat.fiedler = 0.0;
            feat.algebraic_connectivity = 0.0;
            feat.spectral_radius = 0.0;
            feat.eig_entropy = 0.0;
        } else {
            const double lambda2  = (n >= 2) ? eig[1] : 0.0;  // 第二小特征值
            const double lambdaMax = eig[n - 1];              // 最大特征值

            feat.fiedler = lambda2;
            feat.algebraic_connectivity = lambda2;
            feat.spectral_radius = lambdaMax;

            // 特征熵：归一化特征谱的香农熵(自然对数)
            double s = 0.0;
            for (double e : eig) s += e;
            if (s > 1e-12) {
                double ent = 0.0;
                for (double e : eig) {
                    if (e <= 0.0) continue;
                    const double p = e / s;
                    ent -= p * std::log(p);
                }
                feat.eig_entropy = ent;
            } else {
                feat.eig_entropy = 0.0;
            }
        }
    }

    return feat;
}

#endif // GRAPH_LAPLACIAN_H