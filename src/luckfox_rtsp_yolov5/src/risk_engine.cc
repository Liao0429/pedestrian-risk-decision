#include "risk_engine.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <queue>

#define EDGE_DIST_M  2.0f   // 建边距离阈值(米)，论文取 2.0m
#define DANGER_N     10     // 危险：行人数阈值
#define DANGER_RHO   0.55f  // 危险：网络密度阈值
#define DANGER_GCC   0.80f  // 危险：最大连通分量占比阈值
#define WARN_N       5      // 预警：行人数阈值
#define WARN_RHO     0.28f  // 预警：网络密度阈值
#define WARN_GCC     0.55f  // 预警：最大连通分量占比阈值

RiskEngine::RiskEngine() : m_level(CROWD_SAFE), m_levelName("SAFE") {
    std::memset(&m_feat, 0, sizeof(m_feat));
}

RiskEngine::~RiskEngine() {}

void RiskEngine::setHomography(const cv::Mat &H) {
    m_H = H.clone();
}

void RiskEngine::clear() {
    m_pts.clear();
    std::memset(&m_feat, 0, sizeof(m_feat));
    m_level = CROWD_SAFE;
    m_levelName = "SAFE";
}

void RiskEngine::addPedestrian(int footX, int footY) {
    float x = (float)footX;
    float y = (float)footY;
    mapToGround(x, y);
    m_pts.push_back(std::make_pair(x, y));
}

void RiskEngine::mapToGround(float &x, float &y) const {
    if (m_H.empty()) return;
    double wx = m_H.at<double>(0,0)*x + m_H.at<double>(0,1)*y + m_H.at<double>(0,2);
    double wy = m_H.at<double>(1,0)*x + m_H.at<double>(1,1)*y + m_H.at<double>(1,2);
    double w  = m_H.at<double>(2,0)*x + m_H.at<double>(2,1)*y + m_H.at<double>(2,2);
    if (std::fabs(w) > 1e-6) {
        x = (float)(wx / w);
        y = (float)(wy / w);
    }
}

void RiskEngine::computeFeatures() {
    m_feat.n = (int)m_pts.size();
    m_feat.m = 0;
    if (m_feat.n < 2) { m_feat.density = 0; m_feat.r_gcc = (m_feat.n>0)?1.0:0; m_feat.clustering = 0; return; }

    // 距离矩阵 -> 邻接矩阵
    int n = m_feat.n;
    std::vector<std::vector<bool>> adj(n, std::vector<bool>(n, false));
    std::vector<std::vector<float>> dist(n, std::vector<float>(n, 0.0f));
    for (int i = 0; i < n; i++) {
        for (int j = i+1; j < n; j++) {
            float dx = m_pts[i].first - m_pts[j].first;
            float dy = m_pts[i].second - m_pts[j].second;
            float d = std::sqrt(dx*dx + dy*dy);
            dist[i][j] = dist[j][i] = d;
            if (d < EDGE_DIST_M) { adj[i][j] = adj[j][i] = true; m_feat.m++; }
        }
    }

    // 网络密度: rho = m / [n*(n-1)/2]
    int possible = n * (n-1) / 2;
    m_feat.density = (possible > 0) ? (double)m_feat.m / possible : 0.0;

    // 最大连通分量占比(BFS)
    std::vector<bool> visited(n, false);
    int maxComp = 0;
    for (int i = 0; i < n; i++) {
        if (visited[i]) continue;
        int comp = 0;
        std::queue<int> q;
        q.push(i); visited[i] = true;
        while (!q.empty()) {
            int u = q.front(); q.pop(); comp++;
            for (int v = 0; v < n; v++)
                if (adj[u][v] && !visited[v]) { visited[v]=true; q.push(v); }
        }
        if (comp > maxComp) maxComp = comp;
    }
    m_feat.r_gcc = (double)maxComp / n;

    // 平均聚类系数
    double sumC = 0.0;
    for (int i = 0; i < n; i++) {
        std::vector<int> neigh;
        for (int j = 0; j < n; j++) if (adj[i][j]) neigh.push_back(j);
        int k = (int)neigh.size();
        if (k >= 2) {
            int links = 0;
            for (int a = 0; a < k; a++)
                for (int b = a+1; b < k; b++)
                    if (adj[neigh[a]][neigh[b]]) links++;
            int maxPairs = k*(k-1)/2;
            sumC += (double)links / maxPairs;
        }
    }
    m_feat.clustering = (n > 0) ? sumC / n : 0.0;
}

CrowdLevel RiskEngine::evaluate() {
    // 规则判断(参数可配置，论文 4.4 节)
    if (m_feat.n >= DANGER_N && (m_feat.density >= DANGER_RHO || m_feat.r_gcc >= DANGER_GCC))
        m_level = CROWD_DANGER;
    else if (m_feat.n >= WARN_N && (m_feat.density >= WARN_RHO || m_feat.r_gcc >= WARN_GCC))
        m_level = CROWD_WARNING;
    else
        m_level = CROWD_SAFE;
    m_levelName = levelName(m_level);
    return m_level;
}

const char* RiskEngine::levelName(CrowdLevel lvl) {
    switch (lvl) {
        case CROWD_SAFE:    return "SAFE";
        case CROWD_WARNING: return "WARNING";
        case CROWD_DANGER:  return "DANGER";
        default:            return "SAFE";
    }
}
