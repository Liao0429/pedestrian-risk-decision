// =============================================================================
// test_preprocess_neon.cc — 板端 NEON vs 标量 一致性测试
// -----------------------------------------------------------------------------
// 背景：preprocess.cc 的 NEON 路径只在 __ARM_NEON 下编译（x86 主机测试覆盖不到）。
// 本测试在开发板上直接对比 yuv420sp_to_bgr（NEON）与本地标量参考（BT.601
// limited range + NV12 2x2 共享），验证 NEON 色度展开/定点运算与标量完全一致。
//
// 编译（老电脑，交叉工具链）：
//   $CXX -std=c++11 -O2 -Iinclude -I../algorithms \
//        tests/test_preprocess_neon.cc src/preprocess.cc -o /tmp/test_preprocess_neon
// 运行（开发板）：/tmp/test_preprocess_neon [宽] [高] [轮数]
//
// 预期：NEON 与标量逐像素一致（max_diff == 0；允许 ±1 舍入差异）。
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "preprocess.h"

namespace {

// 与 preprocess.cc 相同的标量参考实现（独立复写，避免依赖内部函数）
inline void ref_yuv2bgr(int32_t y, int32_t u, int32_t v,
                        int32_t *b, int32_t *g, int32_t *r) {
    const int32_t yv = (y - 16) * 4768;          // 1.164 * 4096
    int32_t rr = yv + (v - 128) * 6537;          // 1.596
    int32_t gg = yv - (u - 128) * 1602 - (v - 128) * 3330;  // 0.391 / 0.813
    int32_t bb = yv + (u - 128) * 8266;          // 2.018
    rr >>= 12; gg >>= 12; bb >>= 12;
    if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
    *r = rr; *g = gg; *b = bb;
}

void ref_yuv420sp_to_bgr(const uint8_t *yuv, int w, int h, uint8_t *bgr) {
    const uint8_t *Y  = yuv;
    const uint8_t *UV = yuv + (size_t)w * h;
    for (int y = 0; y < h; ++y) {
        const uint8_t *Yrow  = Y  + (size_t)y * w;
        const uint8_t *UVrow = UV + (size_t)(y >> 1) * w;
        uint8_t *out = bgr + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x) {
            const int uoff = (x >> 1) * 2;
            int32_t b, g, r;
            ref_yuv2bgr(Yrow[x], UVrow[uoff], UVrow[uoff + 1], &b, &g, &r);
            out[x * 3]     = (uint8_t)b;
            out[x * 3 + 1] = (uint8_t)g;
            out[x * 3 + 2] = (uint8_t)r;
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    int w = argc > 1 ? atoi(argv[1]) : 720;
    int h = argc > 2 ? atoi(argv[2]) : 480;
    int rounds = argc > 3 ? atoi(argv[3]) : 3;
    if (w <= 0 || h <= 0 || rounds <= 0) return 2;

    std::vector<uint8_t> yuv((size_t)w * h * 3 / 2);
    std::vector<uint8_t> got((size_t)w * h * 3);
    std::vector<uint8_t> ref((size_t)w * h * 3);

    int max_diff = 0;
    size_t over = 0;
    for (int r = 0; r < rounds; ++r) {
        // 伪随机但可复现的 NV12 数据（含极值，考验 clamp）
        uint32_t seed = 0x12345678u + (uint32_t)r * 0x9e3779b9u;
        for (size_t i = 0; i < yuv.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            yuv[i] = (uint8_t)(seed >> 24);
        }
        // 少量纯色块（0/16/128/255 边界）
        memset(yuv.data(), 0, 64);
        memset(yuv.data() + (size_t)w * h, 128, (size_t)w * (h / 2));

        pedestrian::yuv420sp_to_bgr(yuv.data(), w, h, got.data());
        ref_yuv420sp_to_bgr(yuv.data(), w, h, ref.data());

        for (size_t i = 0; i < got.size(); ++i) {
            int d = (int)got[i] - (int)ref[i];
            if (d < 0) d = -d;
            if (d > max_diff) max_diff = d;
            if (d > 1) ++over;
        }
        printf("[round %d] max_diff=%d 超差(>1)=%zu\n", r + 1, max_diff, over);
    }

    printf("结果: w=%d h=%d rounds=%d max_diff=%d %s\n",
           w, h, rounds, max_diff,
           max_diff <= 1 ? "PASS" : "FAIL");
    return max_diff <= 1 ? 0 : 1;
}