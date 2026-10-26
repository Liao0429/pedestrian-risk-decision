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
#include "ipm_transform.h"
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
    // class_conf_thresholds 缺失 -> 空映射（不区分类别）
    CHECK(cfg.class_conf_thresholds.empty());
    std::remove(kTmpPath);
}

void test_class_conf_thresholds() {
    // 正常读取：字符串键 + 数值
    writeFile(R"({"class_conf_thresholds": {"0": 0.50, "2": 0.60}})");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK_EQ(cfg.class_conf_thresholds.size(), 2u);
    CHECK(cfg.class_conf_thresholds.find(0) != cfg.class_conf_thresholds.end());
    CHECK(cfg.class_conf_thresholds.find(2) != cfg.class_conf_thresholds.end());
    CHECK_EQ(static_cast<double>(cfg.class_conf_thresholds[0]), 0.50);
    CHECK(std::fabs(static_cast<double>(cfg.class_conf_thresholds[2]) - 0.60) < 1e-6);
    std::remove(kTmpPath);

    // 键为 COCO id 整数字符串，允许补齐写法
    writeFile(R"({"class_conf_thresholds": {"07": 0.4}})");
    cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK(cfg.class_conf_thresholds.find(7) != cfg.class_conf_thresholds.end());
    CHECK(std::fabs(static_cast<double>(cfg.class_conf_thresholds[7]) - 0.4) < 1e-6);
    std::remove(kTmpPath);

    // 非法键：非数字
    writeFile(R"({"class_conf_thresholds": {"person": 0.5}})");
    bool threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // 非法键：越界（>79）
    writeFile(R"({"class_conf_thresholds": {"80": 0.5}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // 非法值：>1
    writeFile(R"({"class_conf_thresholds": {"0": 1.5}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // 非法类型：数组
    writeFile(R"({"class_conf_thresholds": [0.5]})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
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

void test_homography_file_field() {
    // 缺失 -> 默认空串（未配置）
    writeFile(R"({"edge_dist_m": 2.5})");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK(cfg.homography_file.empty());
    std::remove(kTmpPath);

    // 正常读取字符串路径
    writeFile(R"({"homography_file": "ipm_homography.txt"})");
    cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK(cfg.homography_file == "ipm_homography.txt");
    std::remove(kTmpPath);

    // 非法类型（数字）-> 抛异常
    writeFile(R"({"homography_file": 123})");
    bool threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);
}

void test_fsm_config_loading() {
    writeFile(R"({
        "fsm": {
            "w1": 0.5, "w2": 0.3, "w3": 0.2,
            "n_ref": 30,
            "up_warning": 0.45, "up_danger": 0.7,
            "down_safe": 0.30, "down_warning": 0.6,
            "confirm_up": 4, "confirm_down": 6,
            "danger_hold_frames": 20, "danger_hold_ms": 2000,
            "trend_bias": 0.1
        }
    })");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    CHECK_EQ(cfg.fsm.w1, 0.5);
    CHECK_EQ(cfg.fsm.w2, 0.3);
    CHECK_EQ(cfg.fsm.w3, 0.2);
    CHECK_EQ(cfg.fsm.n_ref, 30);
    CHECK_EQ(cfg.fsm.up_warning, 0.45);
    CHECK_EQ(cfg.fsm.up_danger, 0.7);
    CHECK_EQ(cfg.fsm.down_safe, 0.30);
    CHECK_EQ(cfg.fsm.down_warning, 0.6);
    CHECK_EQ(cfg.fsm.confirm_up, 4);
    CHECK_EQ(cfg.fsm.confirm_down, 6);
    CHECK_EQ(cfg.fsm.danger_hold_frames, 20);
    CHECK_EQ(cfg.fsm.danger_hold_ms, 2000);
    CHECK_EQ(cfg.fsm.trend_bias, 0.1);
    std::remove(kTmpPath);
}

void test_fsm_missing_uses_default() {
    writeFile(R"({"edge_dist_m": 2.5})");
    pedestrian::RiskConfig cfg = pedestrian::loadRiskConfig(kTmpPath);
    // 默认 FSM 参数与 Python 端 RiskFSMConfig 一致
    CHECK_EQ(cfg.fsm.w1, 0.4);
    CHECK_EQ(cfg.fsm.up_warning, 0.40);
    CHECK_EQ(cfg.fsm.up_danger, 0.65);
    CHECK_EQ(cfg.fsm.confirm_up, 3);
    CHECK_EQ(cfg.fsm.confirm_down, 5);
    std::remove(kTmpPath);
}

void test_fsm_invalid_hysteresis() {
    // up_warning <= down_safe -> 无滞回带，抛异常
    writeFile(R"({"fsm": {"up_warning": 0.3, "down_safe": 0.4}})");
    bool threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // up_danger <= down_warning -> 抛异常
    writeFile(R"({"fsm": {"up_danger": 0.5, "down_warning": 0.55}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);
}

void test_fsm_invalid_weights() {
    // 权重之和为 0
    writeFile(R"({"fsm": {"w1": 0, "w2": 0, "w3": 0}})");
    bool threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // 负权重
    writeFile(R"({"fsm": {"w1": -0.1}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // 阈值为 1.5（越界）
    writeFile(R"({"fsm": {"up_warning": 1.5}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);

    // confirm_up < 1
    writeFile(R"({"fsm": {"confirm_up": 0}})");
    threw = false;
    try { pedestrian::loadRiskConfig(kTmpPath); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    std::remove(kTmpPath);
}

#if HAS_OPENCV
void test_load_homography_file() {
    const char* kHPath = "tmp_ipm_homography_test.txt";
    // 合法 3x3 文本（np.savetxt 风格，空格分隔）-> 加载成功，CV_64F，值正确
    {
        std::ofstream ofs(kHPath);
        ofs << "3.0 0.5 100.0\n0.2 4.0 200.0\n0.001 0.002 1.0\n";
        ofs.close();
    }
    cv::Mat H;
    CHECK(pedestrian::loadHomographyFromFile(kHPath, H));
    CHECK(!H.empty());
    CHECK(H.rows == 3 && H.cols == 3);
    CHECK(H.type() == CV_64F);
    const double* p = reinterpret_cast<const double*>(H.data);
    CHECK_EQ(p[0], 3.0);
    CHECK_EQ(p[4], 4.0);
    CHECK_EQ(p[8], 1.0);
    std::remove(kHPath);

    // 逗号分隔 CSV 也兼容（与 Python .csv 输出一致）
    {
        std::ofstream ofs(kHPath);
        ofs << "1, 0, 5\n0, 1, 6\n0, 0, 1\n";
        ofs.close();
    }
    CHECK(pedestrian::loadHomographyFromFile(kHPath, H));
    CHECK_EQ(p[0], 1.0);
    CHECK_EQ(p[5], 6.0);
    std::remove(kHPath);

    // 文件不存在 -> false
    cv::Mat H2;
    CHECK(!pedestrian::loadHomographyFromFile("no_such_h.txt", H2));

    // 数值不足 9 个 -> false
    {
        std::ofstream ofs(kHPath);
        ofs << "1 2 3\n4 5 6\n";
        ofs.close();
    }
    CHECK(!pedestrian::loadHomographyFromFile(kHPath, H2));
    std::remove(kHPath);

    // 退化矩阵（分母行全 0）-> false
    {
        std::ofstream ofs(kHPath);
        ofs << "1 0 0\n0 1 0\n0 0 0\n";
        ofs.close();
    }
    CHECK(!pedestrian::loadHomographyFromFile(kHPath, H2));
    std::remove(kHPath);

    // loadHomographyOr：文件可加载用文件，否则回退
    cv::Mat fb = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat out = pedestrian::loadHomographyOr("no_such_h.txt", fb);
    CHECK(!out.empty());
    CHECK(out.type() == CV_64F);
}
#else
void test_load_homography_file() {
    std::printf("SKIP test_load_homography_file (OpenCV 不可用)\n");
}
#endif

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
    test_class_conf_thresholds();
    test_invalid_throws();
    test_load_or_fallback();
    test_homography_file_field();
    test_fsm_config_loading();
    test_fsm_missing_uses_default();
    test_fsm_invalid_hysteresis();
    test_fsm_invalid_weights();
    test_load_homography_file();
    test_risk_engine_config();

    std::printf("\n%d/%d tests passed\n", g_passed, g_passed + g_failed);
    if (g_failed > 0) {
        std::printf("FAILED: %d assertion(s)\n", g_failed);
        return 1;
    }
    return 0;
}