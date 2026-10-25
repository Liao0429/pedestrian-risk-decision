// =============================================================================
// test_risk_config.cc — RiskConfig 运行时配置读取主机单元测试
// -----------------------------------------------------------------------------
// 验证点：
//   1. 从 JSON 文件正确读取 edge_dist_m / danger / warning；
//   2. 缺失字段回退到默认值；
//   3. 非法配置（warning.n > danger.n）抛异常；
//   4. 文件不存在抛异常；loadRiskConfigOr 回退到 fallback；
//   5. RiskEngine::setConfig 使用配置驱动 evaluate() 结果。
//
// 编译：
//   g++ -std=c++11 -O2 -I../include -I../../algorithms
//       test_risk_config.cc ../src/risk_engine.cc -o risk_config_test
//       $(pkg-config --cflags --libs opencv4)
// =============================================================================

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "risk_config.h"

#if !defined(__has_include)
#define __has_include(x) 0
#endif

// 若可用 OpenCV（主机测试），则一并验证 RiskEngine::setConfig 生效。
#if __has_include(<opencv2/core/core.hpp>)
#define HAS_OPENCV 1
#include "risk_engine.h"
#else
#define HAS_OPENCV 0
#endif

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

const char* kTmpPath = "tmp_risk_config_test.json";

void writeFile(const std::string& content) {
    std::ofstream ofs(kTmpPath);
    ofs << content;
    ofs.close();
}

void test_load_valid() {
    writeFile(R"({
        "edge_dist_m": 3.0,
        "danger": {"n": 15, "rho": 0.6, "gcc": 0.9},
        "warning": {"n": 8, "rho": 0.3, "gcc": 0.6}
    })");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK_EQ(cfg.edge_dist_m, 3.0);
    CHECK_EQ(cfg.danger.n, 15.0);
    CHECK_EQ(cfg.danger.rho, 0.6);
    CHECK_EQ(cfg.danger.gcc, 0.9);
    CHECK_EQ(cfg.warning.n, 8.0);
    CHECK_EQ(cfg.warning.rho, 0.3);
    CHECK_EQ(cfg.warning.gcc, 0.6);
    std::remove(kTmpPath);
}

void test_missing_fields_use_defaults() {
    writeFile(R"({"edge_dist_m": 2.5})");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK_EQ(cfg.edge_dist_m, 2.5);
    // danger / warning 缺失 -> 默认值
    CHECK_EQ(cfg.danger.n, 10.0);
    CHECK_EQ(cfg.warning.n, 5.0);
    CHECK_EQ(cfg.warning.rho, 0.28);
    std::remove(kTmpPath);
}

void test_invalid_throws() {
    // 语义颠倒：warning.n > danger.n
    writeFile(R"({"danger":{"n":5},"warning":{"n":10}})");
    bool threw = false;
    try {
        pedestrian::loadRiskConfig(kTmpPath);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::remove(kTmpPath);

    // 非 JSON
    writeFile("not json");
    threw = false;
    try {
        pedestrian::loadRiskConfig(kTmpPath);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::remove(kTmpPath);

    // 文件不存在
    threw = false;
    try {
        pedestrian::loadRiskConfig("no_such_file.json");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void test_load_or_fallback() {
    pedestrian::RiskConfig fb;
    fb.setDanger(99.0, 0.99, 0.99);
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfigOr("no_such_file.json", fb);
    CHECK_EQ(cfg.danger.n, 99.0);  // 文件不存在 -> 返回 fallback
}

#if HAS_OPENCV
void test_risk_engine_config() {
    // 用配置把 DANGER 阈值调到很低，验证 evaluate() 使用运行时配置而非宏
    pedestrian::RiskConfig cfg;
    cfg.setDanger(1.0, 0.0, 0.0);   // 只要 n>=1 即 DANGER
    cfg.setWarning(1.0, 0.0, 0.0);

    cv::Mat H = cv::Mat::eye(3, 3, CV_64F);
    RiskEngine eng;
    eng.setHomography(H);
    eng.setConfig(cfg);
    // 两个相距 1m 的行人（地面坐标经单位 H 直接透传像素坐标）
    eng.addPedestrian(0, 0);
    eng.addPedestrian(1, 0);
    eng.computeFeatures();
    CHECK(eng.evaluate() == CROWD_DANGER);

    // 默认配置下，2 人不足 DANGER（n=10）也不足 WARNING（n=5）-> SAFE
    RiskEngine eng2;
    eng2.setHomography(H);
    eng2.addPedestrian(0, 0);
    eng2.addPedestrian(1, 0);
    eng2.computeFeatures();
    CHECK(eng2.evaluate() == CROWD_SAFE);
}
#else
void test_risk_engine_config() {
    std::printf("SKIP test_risk_engine_config (OpenCV 不可用)\n");
}
#endif

}  // namespace

int main() {
    test_load_valid();
    test_missing_fields_use_defaults();
    test_invalid_throws();
    test_load_or_fallback();
    test_risk_engine_config();

    std::printf("\n%d/%d tests passed\n", g_passed, g_passed + g_failed);
    if (g_failed > 0) {
        std::printf("FAILED: %d assertion(s)\n", g_failed);
        return 1;
    }
    return 0;
}