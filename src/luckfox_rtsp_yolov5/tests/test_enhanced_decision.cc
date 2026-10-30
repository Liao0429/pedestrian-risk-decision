// =============================================================================
// test_enhanced_decision.cc — EnhancedDecision 增强决策模块主机单元测试
// -----------------------------------------------------------------------------
// 验证点：
//   1. 稀疏点集（无聚集）稳定输出 SAFE；
//   2. 密集聚集（连续多帧高分）经 FSM 阶梯升级到 DANGER；
//   3. 确定性：相同输入序列在不同实例上产生完全一致的输出；
//   4. 建边距离阈值正确性（< edge_dist 连边、>= 不连边）；
//   5. Kalman 平滑对抖动脚点的抑制作用（输出轨迹更平滑）；
//   6. reset() 复位内部状态。
//
// 编译（主机 x86，无需 OpenCV/RKNN/RTSP）：
//   g++ -std=c++11 -O2 -I../include -I../../algorithms
//       test_enhanced_decision.cc ../src/enhanced_decision.cc -o enhanced_decision_test
//   ./enhanced_decision_test
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "enhanced_decision.h"
#include "risk_config.h"

using pedestrian::EnhancedConfig;
using pedestrian::EnhancedDecision;
using pedestrian::EnhancedResult;

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        double _a = (double)(a);                                              \
        double _b = (double)(b);                                              \
        if (!(std::fabs(_a - _b) < 1e-9)) {                                  \
            std::printf("FAIL %s:%d  %s == %s  (%g vs %g)\n", __FILE__,       \
                         __LINE__, #a, #b, _a, _b);                            \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

std::vector<std::pair<float, float>> denseCluster() {
    // 10 个行人聚集在 1.5m x 1.5m 区域：密度=1.0，r_gcc=1.0
    std::vector<std::pair<float, float>> pts;
    for (int i = 0; i < 10; ++i) {
        pts.push_back(std::make_pair((float)(i % 4) * 0.5f,
                                    (float)(i / 4) * 0.5f));
    }
    return pts;
}

std::vector<std::pair<float, float>> sparsePts() {
    // 4 个行人彼此相距 > 2m（4m 网格）：无聚集
    std::vector<std::pair<float, float>> pts;
    pts.push_back(std::make_pair(0.0f, 0.0f));
    pts.push_back(std::make_pair(4.0f, 0.0f));
    pts.push_back(std::make_pair(0.0f, 4.0f));
    pts.push_back(std::make_pair(4.0f, 4.0f));
    return pts;
}

void test_sparse_is_safe() {
    EnhancedDecision ed;
    for (int i = 0; i < 8; ++i) {
        EnhancedResult r = ed.processFrame(sparsePts());
        CHECK(r.level == 0);  // SAFE
        CHECK_EQ(r.n, 4);
    }
}

void test_dense_cluster_reaches_danger() {
    EnhancedDecision ed;
    int level = -1;
    for (int i = 0; i < 8; ++i) {
        EnhancedResult r = ed.processFrame(denseCluster());
        level = r.level;
    }
    // 默认 confirm_up=3：第 3 帧 WARNING，第 6 帧 DANGER，第 8 帧保持 DANGER
    CHECK(level == 2);  // DANGER
}

void test_deterministic() {
    // 两个独立实例喂入相同序列，输出必须逐帧一致
    EnhancedDecision ed1, ed2;
    bool same = true;
    for (int i = 0; i < 10; ++i) {
        std::vector<std::pair<float, float>> pts = (i % 2 == 0) ? denseCluster() : sparsePts();
        EnhancedResult r1 = ed1.processFrame(pts);
        EnhancedResult r2 = ed2.processFrame(pts);
        if (r1.level != r2.level || r1.n != r2.n ||
            std::fabs(r1.density - r2.density) > 1e-12 ||
            std::fabs(r1.r_gcc - r2.r_gcc) > 1e-12 ||
            std::fabs(r1.fiedler - r2.fiedler) > 1e-9) {
            same = false;
            break;
        }
    }
    CHECK(same);
}

void test_edge_distance() {
    // 两节点相距 1.0m < 2.0m：应连边，density=1.0
    {
        EnhancedDecision ed;
        std::vector<std::pair<float, float>> pts;
        pts.push_back(std::make_pair(0.0f, 0.0f));
        pts.push_back(std::make_pair(1.0f, 0.0f));
        EnhancedResult r = ed.processFrame(pts);
        CHECK_EQ(r.n, 2);
        CHECK_EQ(r.density, 1.0);
    }
    // 两节点相距 3.0m >= 2.0m：不连边，density=0.0
    {
        EnhancedDecision ed;
        std::vector<std::pair<float, float>> pts;
        pts.push_back(std::make_pair(0.0f, 0.0f));
        pts.push_back(std::make_pair(3.0f, 0.0f));
        EnhancedResult r = ed.processFrame(pts);
        CHECK_EQ(r.n, 2);
        CHECK_EQ(r.density, 0.0);
    }
}

void test_smoothing_reduces_jitter() {
    EnhancedConfig cfg;
    cfg.kalman_process_noise = 1.5;
    cfg.kalman_measurement_noise = 0.5;
    EnhancedDecision ed(cfg);

    // 目标大致静止在 (1.0, 1.0)，观测抖动 ±0.4m；平滑后应显著收敛到均值附近
    std::vector<std::pair<float, float>> seq;
    for (int i = 0; i < 20; ++i) {
        float jx = (i % 2 == 0) ? 1.4f : 0.6f;
        float jy = (i % 3 == 0) ? 1.3f : 0.8f;
        seq.push_back(std::make_pair(jx, jy));
    }
    double sum = 0.0;
    std::vector<std::pair<float, float>> last;
    for (size_t i = 0; i < seq.size(); ++i) {
        EnhancedResult r = ed.processFrame(std::vector<std::pair<float, float>>(1, seq[i]));
        sum += r.smoothPts[0].first;
        last = r.smoothPts;
    }
    double mean = sum / (double)seq.size();
    // 平滑输出应接近真值 1.0（抖动被显著抑制）
    CHECK(std::fabs(mean - 1.0) < 0.2);
    // 末帧平滑位置应比原始观测（0.6/1.4 抖动）更接近真值 1.0
    CHECK(std::fabs(last[0].first - 1.0) < 0.25);
}

// 谱特征降频：每 spectral_interval 帧才重算特征分解，中间帧复用缓存
void test_spectral_decimation() {
    EnhancedConfig cfg;
    cfg.spectral_interval = 4;
    EnhancedDecision ed(cfg);
    CHECK(ed.spectralUpdates() == 0);
    for (int i = 0; i < 9; ++i) ed.processFrame(denseCluster());
    CHECK(ed.spectralUpdates() == 3);   // 第 1/5/9 帧重算（旧行为：每帧 1 次共 9 次）
}

// 降频期间：网络/谱特征复用，但节点数 n 必须逐帧刷新
void test_spectral_cache_reuses_but_n_is_fresh() {
    EnhancedConfig cfg;
    cfg.spectral_interval = 4;
    EnhancedDecision ed(cfg);
    EnhancedResult r1 = ed.processFrame(denseCluster());
    CHECK_EQ(r1.n, 10);
    CHECK_EQ(r1.density, 1.0);
    std::vector<std::pair<float, float>> one;
    one.push_back(std::make_pair(0.0f, 0.0f));
    EnhancedResult r2 = ed.processFrame(one);
    CHECK_EQ(r2.n, 1);
    CHECK_EQ(r2.density, 1.0);   // 特征缓存复用（最多滞后 interval-1 帧）
    EnhancedResult r3 = ed.processFrame(one);
    CHECK_EQ(r3.n, 1);
    CHECK_EQ(r3.density, 1.0);
    ed.processFrame(one);
    EnhancedResult r5 = ed.processFrame(one);
    CHECK_EQ(r5.n, 1);
    CHECK_EQ(r5.density, 0.0);   // 第 5 帧重算 -> 单人无邻居
    CHECK(ed.spectralUpdates() == 2);
}

// interval=1 时行为与旧实现完全一致（每帧都重算）
void test_spectral_interval_one_is_legacy() {
    EnhancedConfig cfg;
    cfg.spectral_interval = 1;
    EnhancedDecision ed(cfg);
    for (int i = 0; i < 6; ++i) ed.processFrame(denseCluster());
    CHECK(ed.spectralUpdates() == 6);
}

void test_reset() {
    EnhancedDecision ed;
    for (int i = 0; i < 8; ++i) ed.processFrame(denseCluster());
    CHECK(ed.processFrame(denseCluster()).level == 2);  // DANGER
    ed.reset();
    CHECK(ed.spectralUpdates() == 0);   // reset 同时清空谱特征缓存计数
    EnhancedResult r = ed.processFrame(denseCluster());
    CHECK(r.level == 0);  // reset 后从 SAFE 重新开始
    CHECK(ed.spectralUpdates() == 1);   // reset 后第一帧必须重算特征
}

}  // namespace

int main() {
    test_sparse_is_safe();
    test_dense_cluster_reaches_danger();
    test_deterministic();
    test_edge_distance();
    test_smoothing_reduces_jitter();
    test_spectral_decimation();
    test_spectral_cache_reuses_but_n_is_fresh();
    test_spectral_interval_one_is_legacy();
    test_reset();

    std::printf("\n%d/%d tests passed\n", g_passed, g_passed + g_failed);
    if (g_failed > 0) {
        std::printf("FAILED: %d assertion(s)\n", g_failed);
        return 1;
    }
    return 0;
}