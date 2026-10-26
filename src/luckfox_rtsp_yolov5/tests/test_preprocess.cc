// =============================================================================
// test_preprocess.cc — 图像预处理模块主机单元测试
// -----------------------------------------------------------------------------
// 验证点：
//   1. letterbox_prepare 几何参数（roi/pads）与黄金元数据一致；
//   2. yuv420sp_to_bgr（NV12 -> BGR）与 OpenCV 正确 NV12 转换参考一致（<=2 灰度）；
//   3. yuv420sp_to_rgb_letterbox 与 OpenCV 参考流程一致（<=2 灰度），黑边全 0；
//   4. 边界：非法参数返回 -1；重复 prepare 未释放返回 -1；释放后可重用。
//
// 黄金数据由 scripts/generate_preprocess_golden.py 生成（OpenCV 权威参考）。
// 编译（从 src/luckfox_rtsp_yolov5/ 运行）：
//   g++ -std=c++11 -O2 -Iinclude -I../algorithms
//       tests/test_preprocess.cc src/preprocess.cc -o tests/test_preprocess
// 运行：./tests/test_preprocess [黄金数据目录，默认 ../../tests/golden/preprocess]
// =============================================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "minijson.h"
#include "preprocess.h"

using pedestrian::LetterboxParams;
using pedestrian::letterbox_params_free;
using pedestrian::letterbox_prepare;
using pedestrian::yuv420sp_to_bgr;
using pedestrian::yuv420sp_to_rgb_letterbox;

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

std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    std::vector<uint8_t> data;
    if (!ifs.is_open()) {
        std::printf("FAIL 无法打开文件: %s\n", path.c_str());
        ++g_failed;
        return data;
    }
    ifs.seekg(0, std::ios::end);
    std::streamoff len = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    data.resize((size_t)len);
    if (len > 0) ifs.read((char *)data.data(), len);
    return data;
}

struct CaseMeta {
    int src_w = 0, src_h = 0, dst_w = 0, dst_h = 0;
    int roi_w = 0, roi_h = 0, left_pad = 0, top_pad = 0;
};

CaseMeta loadMeta(const std::string &path) {
    CaseMeta m;
    std::vector<uint8_t> raw = readFile(path);
    if (raw.empty()) return m;
    std::string text((const char *)raw.data(), raw.size());
    try {
        minijson::Value root = minijson::parse(text);
        m.src_w = root["src_w"].asInt();
        m.src_h = root["src_h"].asInt();
        m.dst_w = root["dst_w"].asInt();
        m.dst_h = root["dst_h"].asInt();
        m.roi_w = root["roi_w"].asInt();
        m.roi_h = root["roi_h"].asInt();
        m.left_pad = root["left_pad"].asInt();
        m.top_pad = root["top_pad"].asInt();
    } catch (const std::exception &e) {
        std::printf("FAIL meta 解析失败 %s: %s\n", path.c_str(), e.what());
        ++g_failed;
    }
    return m;
}

int compareMax(const std::vector<uint8_t> &got, const std::vector<uint8_t> &ref,
               const char *what, const char *name) {
    if (got.size() != ref.size()) {
        std::printf("FAIL %s: %s 尺寸不一致 (got=%zu ref=%zu)\n",
                    name, what, got.size(), ref.size());
        ++g_failed;
        return -1;
    }
    int max_diff = 0;
    size_t over = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        int d = std::abs((int)got[i] - (int)ref[i]);
        if (d > max_diff) max_diff = d;
        if (d > 2) ++over;
    }
    if (max_diff > 2) {
        std::printf("FAIL %s: %s 最大差异 %d (>2)，超差像素 %zu\n",
                    name, what, max_diff, over);
        ++g_failed;
    } else {
        std::printf("[ok] %s: %s max_diff=%d\n", name, what, max_diff);
        ++g_passed;
    }
    return max_diff;
}

void testCase(const std::string &dir, const std::string &name) {
    const std::string base = dir + "/case_" + name;
    CaseMeta m = loadMeta(base + "_meta.json");
    if (m.src_w <= 0) return;
    const size_t yuv_size = (size_t)m.src_w * m.src_h * 3 / 2;
    const size_t bgr_size = (size_t)m.src_w * m.src_h * 3;
    const size_t rgb_size = (size_t)m.dst_w * m.dst_h * 3;
    std::vector<uint8_t> yuv = readFile(base + "_in.bin");
    std::vector<uint8_t> ref_bgr = readFile(base + "_bgr.bin");
    std::vector<uint8_t> ref_rgb = readFile(base + "_rgb_letterbox.bin");
    CHECK(yuv.size() == yuv_size);
    CHECK(ref_bgr.size() == bgr_size);
    CHECK(ref_rgb.size() == rgb_size);
    if (yuv.size() != yuv_size || ref_bgr.size() != bgr_size ||
        ref_rgb.size() != rgb_size) {
        std::printf("FAIL %s: 黄金文件尺寸不符 (yuv=%zu/%zu bgr=%zu/%zu rgb=%zu/%zu)\n",
                    name.c_str(), yuv.size(), yuv_size,
                    ref_bgr.size(), bgr_size, ref_rgb.size(), rgb_size);
        return;
    }

    // 1) 几何参数
    pedestrian::LetterboxParams p;
    CHECK(letterbox_prepare(m.src_w, m.src_h, m.dst_w, m.dst_h, &p) == 0);
    CHECK_EQ(p.roi_w, m.roi_w);
    CHECK_EQ(p.roi_h, m.roi_h);
    CHECK_EQ(p.left_pad, m.left_pad);
    CHECK_EQ(p.top_pad, m.top_pad);
    CHECK(p.row_y0 != nullptr);
    CHECK(p.row_wy != nullptr);
    CHECK(p.col_x0 != nullptr);
    CHECK(p.col_wx != nullptr);

    // 2) YUV -> BGR 全帧
    std::vector<uint8_t> got_bgr(bgr_size, 0);
    pedestrian::yuv420sp_to_bgr(yuv.data(), m.src_w, m.src_h, got_bgr.data());
    compareMax(got_bgr, ref_bgr, "BGR", name.c_str());

    // 3) YUV -> RGB letterbox
    std::vector<uint8_t> got_rgb(rgb_size, 0);
    pedestrian::yuv420sp_to_rgb_letterbox(yuv.data(), p, got_rgb.data());
    compareMax(got_rgb, ref_rgb, "RGB-letterbox", name.c_str());

    // 4) 黑边全 0（RGB 输出中 ROI 外区域）
    bool pad_ok = true;
    for (int oy = 0; oy < m.dst_h; ++oy) {
        for (int ox = 0; ox < m.dst_w; ++ox) {
            bool in_roi = (ox >= m.left_pad && ox < m.left_pad + m.roi_w &&
                           oy >= m.top_pad && oy < m.top_pad + m.roi_h);
            if (!in_roi) {
                const uint8_t *px = got_rgb.data() + ((size_t)oy * m.dst_w + ox) * 3;
                if (px[0] != 0 || px[1] != 0 || px[2] != 0) {
                    pad_ok = false;
                    break;
                }
            }
        }
        if (!pad_ok) break;
    }
    if (pad_ok) { ++g_passed; } else {
        std::printf("FAIL %s: letterbox 黑边非全 0\n", name.c_str());
        ++g_failed;
    }

    // 5) 释放
    pedestrian::letterbox_params_free(&p);
    CHECK(p.row_y0 == nullptr);
    CHECK(p.col_wx == nullptr);
}

void testEdgeCases() {
    // 非法参数
    pedestrian::LetterboxParams p;
    CHECK(letterbox_prepare(0, 480, 640, 640, &p) == -1);
    CHECK(letterbox_prepare(720, 0, 640, 640, &p) == -1);
    CHECK(letterbox_prepare(720, 480, -1, 640, &p) == -1);
    CHECK(letterbox_prepare(720, 480, 640, 640, nullptr) == -1);

    // 重复 prepare 未释放
    CHECK(letterbox_prepare(720, 480, 640, 640, &p) == 0);
    CHECK(letterbox_prepare(720, 480, 640, 640, &p) == -1);
    letterbox_params_free(&p);

    // 释放后可以再次 prepare
    CHECK(letterbox_prepare(720, 480, 640, 640, &p) == 0);
    CHECK_EQ(p.roi_w, 640);
    CHECK_EQ(p.roi_h, 426);
    CHECK_EQ(p.left_pad, 0);
    CHECK_EQ(p.top_pad, 107);
    letterbox_params_free(&p);

    // 不同尺寸：64x48 -> 32x32（放大+黑边）
    CHECK(letterbox_prepare(64, 48, 32, 32, &p) == 0);
    CHECK_EQ(p.roi_w, 32);
    CHECK_EQ(p.roi_h, 24);
    CHECK_EQ(p.left_pad, 0);
    CHECK_EQ(p.top_pad, 4);
    letterbox_params_free(&p);

    // 24x18 -> 32x32（放大：宽高比 4:3，scale=32/24=4/3，高度留黑边）
    CHECK(letterbox_prepare(24, 18, 32, 32, &p) == 0);
    CHECK_EQ(p.roi_w, 32);
    CHECK_EQ(p.roi_h, 24);
    CHECK_EQ(p.left_pad, 0);
    CHECK_EQ(p.top_pad, 4);
    letterbox_params_free(&p);
}

}  // namespace

int main(int argc, char **argv) {
    const std::string dir =
        argc > 1 ? argv[1] : "../../tests/golden/preprocess";
    std::printf("== test_preprocess: 黄金数据目录 %s ==\n", dir.c_str());

    const char *cases[] = {"red", "green", "blue", "gray_gradient",
                           "random_noise", "smooth_texture",
                           "small_64x48", "small_24x18", "odd_66x50"};
    for (const char *c : cases) testCase(dir, c);

    testEdgeCases();

    std::printf("\n结果: %d 通过, %d 失败\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}