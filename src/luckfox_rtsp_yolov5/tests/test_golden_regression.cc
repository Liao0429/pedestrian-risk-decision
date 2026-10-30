// =============================================================================
// test_golden_regression.cc — C++/Python 双实现一致性黄金回归测试
// -----------------------------------------------------------------------------
// 验证点：
//   1. graph_laplacian 的谱特征与 Python 参考实现（scripts/generate_golden.py
//      生成的黄金数据 tests/golden/graph_laplacian.json）一致；
//   2. ema_smoother 的平滑序列与黄金数据 tests/golden/ema_smoother.json 一致；
//   3. risk_fsm 的等级序列与黄金数据 tests/golden/risk_fsm.json 一致。
//
// 两侧读取同一批 JSON，从机制上防止 C++/Python 双实现漂移。
//
// 编译（主机 x86，无需 OpenCV/RKNN/RTSP）：
//   g++ -std=c++11 -O2 -I../include -I../../algorithms test_golden_regression.cc -o test_golden_regression (单行，勿在注释行尾用反斜杠：会触发 -Wcomment)
//   运行：./test_golden_regression [golden_dir]
// 其中 golden_dir 缺省时依次尝试 ../../tests/golden/ 与 tests/golden/。
//
// 若找不到黄金数据文件，返回非零并打印明确提示（不做任何断言）。
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

#include "ema_smoother.h"
#include "graph_laplacian.h"
#include "minijson.h"
#include "risk_fsm.h"

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

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        double _a = (double)(a);                                             \
        double _b = (double)(b);                                             \
        if (!(std::fabs(_a - _b) <= (tol))) {                                \
            std::printf("FAIL %s:%d  %s≈%s  (%g vs %g, tol %g)\n", __FILE__, \
                        __LINE__, #a, #b, _a, _b, (double)(tol));            \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long long _a = (long long)(a);                                       \
        long long _b = (long long)(b);                                       \
        if (_a != _b) {                                                      \
            std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__,  \
                        __LINE__, #a, #b, _a, _b);                           \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

const double kFeatTol = 1e-4;  // 谱特征：Jacobi vs np.linalg.eigvalsh 数值差异
const double kEmaTol = 1e-6;   // EMA 浮点误差

std::string loadFile(const std::string& path) {
    std::ifstream ifs(path.c_str());
    if (!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// 尝试定位 golden 数据目录（依次尝试候选路径，返回第一个文件齐全的）。
bool resolveGoldenDir(std::vector<std::string> candidates,
                      std::string& outDir) {
    const char* files[] = {"graph_laplacian.json", "ema_smoother.json",
                           "risk_fsm.json"};
    for (size_t i = 0; i < candidates.size(); ++i) {
        bool ok = true;
        for (int j = 0; j < 3; ++j) {
            std::string p = candidates[i] + "/" + files[j];
            if (loadFile(p).empty()) { ok = false; break; }
        }
        if (ok) {
            outDir = candidates[i];
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// graph_laplacian 黄金回归
// ---------------------------------------------------------------------
void testGraphLaplacian(const std::string& dir) {
    const std::string path = dir + "/graph_laplacian.json";
    const std::string text = loadFile(path);
    minijson::Value root = minijson::parse(text);
    const minijson::Array& cases = root["cases"].asArray();

    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const minijson::Value& c = cases[ci];
        const std::string name = c["name"].asString();
        const minijson::Array& pts = c["points"].asArray();
        const double edgeDist = c["edge_dist"].asDouble();
        const minijson::Value& exp = c["expected"];

        GraphLaplacian gl(edgeDist);
        for (size_t i = 0; i < pts.size(); ++i) {
            gl.addPoint(static_cast<float>(pts[i][0].asDouble()),
                        static_cast<float>(pts[i][1].asDouble()));
        }
        SpectralFeatures sf = gl.computeFeatures();

        CHECK_EQ(sf.n, exp["n"].asInt());
        CHECK_EQ(sf.m, exp["m"].asInt());
        CHECK_NEAR(sf.density, exp["density"].asDouble(), kFeatTol);
        CHECK_NEAR(sf.r_gcc, exp["r_gcc"].asDouble(), kFeatTol);
        CHECK_NEAR(sf.clustering, exp["clustering"].asDouble(), kFeatTol);
        CHECK_NEAR(sf.fiedler, exp["fiedler"].asDouble(), kFeatTol);
        CHECK_NEAR(sf.spectral_radius, exp["spectral_radius"].asDouble(),
                   kFeatTol);
        CHECK_NEAR(sf.eig_entropy, exp["eig_entropy"].asDouble(), kFeatTol);
        std::printf("graph_laplacian[%s] n=%d m=%d fiedler=%.6f\n",
                    name.c_str(), sf.n, sf.m, sf.fiedler);
    }
}

// ---------------------------------------------------------------------
// ema_smoother 黄金回归
// ---------------------------------------------------------------------
void testEmaSmoother(const std::string& dir) {
    const std::string path = dir + "/ema_smoother.json";
    const std::string text = loadFile(path);
    minijson::Value root = minijson::parse(text);
    const minijson::Array& cases = root["cases"].asArray();

    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const minijson::Value& c = cases[ci];
        const std::string name = c["name"].asString();
        const minijson::Value& alphaVal = c["alpha"];
        const int dim = c["dim"].asInt();
        const bool useTs = c["use_timestamp"].asBool();
        const double tau = c["tau"].asDouble();
        const minijson::Array& values = c["values"].asArray();
        const minijson::Array& timestamps = c["timestamps"].asArray();
        const minijson::Array& expected = c["expected"].asArray();

        // 构造平滑器（标量 alpha 或每维独立 alpha）
        pedestrian::EmaSmoother sm(0.3, static_cast<std::size_t>(dim),
                                   useTs, tau);
        if (alphaVal.isNumber()) {
            sm.setAlpha(alphaVal.asDouble(), static_cast<std::size_t>(dim));
        } else {
            const minijson::Array& a = alphaVal.asArray();
            std::vector<double> alpha;
            for (size_t i = 0; i < a.size(); ++i) alpha.push_back(a[i].asDouble());
            sm.setAlpha(alpha);
        }

        std::printf("ema_smoother[%s] useTs=%d tau=%.1f dim=%d useTimestamp()=%d\n",
                    name.c_str(), useTs ? 1 : 0, tau, dim,
                    sm.useTimestamp() ? 1 : 0);

        std::vector<std::vector<double>> got;
        const bool isVector = values[0].isArray();
        for (size_t i = 0; i < values.size(); ++i) {
            const int64_t t = timestamps[i].isNull() ? -1
                                                    : (int64_t)timestamps[i].asDouble();
            if (isVector) {
                const minijson::Array& v = values[i].asArray();
                std::vector<double> vec;
                for (size_t j = 0; j < v.size(); ++j) vec.push_back(v[j].asDouble());
                std::vector<double> out = sm.update(vec, t);
                got.push_back(out);
            } else {
                std::vector<double> out(1);
                out[0] = sm.update(values[i].asDouble(), t);
                got.push_back(out);
            }
        }

        for (size_t i = 0; i < got.size(); ++i) {
            if (isVector) {
                const minijson::Array& e = expected[i].asArray();
                CHECK_EQ(got[i].size(), e.size());
                for (size_t j = 0; j < e.size() && j < got[i].size(); ++j)
                    CHECK_NEAR(got[i][j], e[j].asDouble(), kEmaTol);
            } else {
                CHECK_NEAR(got[i][0], expected[i].asDouble(), kEmaTol);
            }
        }
        std::printf("ema_smoother[%s] frames=%zu\n", name.c_str(), got.size());
    }
}

// ---------------------------------------------------------------------
// risk_fsm 黄金回归
// ---------------------------------------------------------------------
void testRiskFsm(const std::string& dir) {
    const std::string path = dir + "/risk_fsm.json";
    const std::string text = loadFile(path);
    minijson::Value root = minijson::parse(text);
    const minijson::Array& cases = root["cases"].asArray();

    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const minijson::Value& c = cases[ci];
        const std::string name = c["name"].asString();
        const minijson::Value& cfgJ = c["config"];
        const minijson::Array& frames = c["frames"].asArray();
        const minijson::Array& timestamps = c["timestamps"].asArray();
        const minijson::Array& expected = c["expected"].asArray();

        pedestrian::RiskFSMConfig cfg;
        cfg.confirm_up = cfgJ["confirm_up"].asInt();
        cfg.confirm_down = cfgJ["confirm_down"].asInt();
        cfg.danger_hold_frames = cfgJ["danger_hold_frames"].asInt();
        cfg.danger_hold_ms = cfgJ["danger_hold_ms"].asInt();

        pedestrian::RiskFSM fsm(cfg);
        for (size_t i = 0; i < frames.size(); ++i) {
            const minijson::Value& f = frames[i];
            pedestrian::FsmFeatures feats;
            feats.n = f["n"].asInt();
            feats.density = f["density"].asDouble();
            feats.r_gcc = f["r_gcc"].asDouble();
            feats.trend = f["trend"].asDouble();
            // JSON null -> -1 哨兵（now_ms<0 禁用毫秒保持，仅帧保持生效）
            const int64_t nowMs = timestamps[i].isNull()
                ? -1 : (int64_t)timestamps[i].asDouble();
            pedestrian::RiskLevel lvl = fsm.update(feats, nowMs);
            const std::string exp = expected[i].asString();
            const std::string gotLvl = pedestrian::RiskFSM::levelName(lvl);
            if (gotLvl != exp) {
                std::printf("FAIL %s frame%zu: 期望 %s 实际 %s\n",
                            name.c_str(), i, exp.c_str(), gotLvl.c_str());
                ++g_failed;
            } else {
                ++g_passed;
            }
        }
        std::printf("risk_fsm[%s] frames=%zu\n", name.c_str(), frames.size());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> candidates;
    if (argc > 1) {
        candidates.push_back(argv[1]);
    } else {
        // 依据运行目录自适应：make test 从 src/luckfox_rtsp_yolov5 运行，
        // 也可从项目根目录直接运行。
        candidates.push_back("../../tests/golden");
        candidates.push_back("tests/golden");
        const char* env = std::getenv("GOLDEN_DIR");
        if (env && *env) candidates.insert(candidates.begin(), env);
    }

    std::string dir;
    if (!resolveGoldenDir(candidates, dir)) {
        std::printf("未找到黄金数据目录（尝试了以下路径，均缺文件）：\n");
        for (size_t i = 0; i < candidates.size(); ++i)
            std::printf("  %s\n", candidates[i].c_str());
        std::printf("请先运行 scripts/generate_golden.py 生成 tests/golden/*.json\n");
        return 2;
    }

    std::printf("golden dir: %s\n", dir.c_str());
    try {
        testGraphLaplacian(dir);
        testEmaSmoother(dir);
        testRiskFsm(dir);
    } catch (const std::exception& e) {
        std::printf("FAIL 解析/运行异常: %s\n", e.what());
        ++g_failed;
    }

    std::printf("\n%d/%d tests passed\n", g_passed, g_passed + g_failed);
    if (g_failed > 0) {
        std::printf("FAILED: %d assertion(s)\n", g_failed);
        return 1;
    }
    return 0;
}