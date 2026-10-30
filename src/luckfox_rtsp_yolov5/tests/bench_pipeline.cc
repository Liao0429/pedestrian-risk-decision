// =============================================================================
// bench_pipeline.cc — 主机帧率微基准（不参与 make test，见 make bench）
// -----------------------------------------------------------------------------
// 量化三项改动的"前后对比"（同一台机器、同一编译器、同一时间测）：
//   A) 图拉普拉斯谱特征(Jacobi 特征分解) 每帧算 vs 每 N 帧算（降频）
//   B) 预处理：旧链路等价 vs 新链路（preprocess.cc 单遍零拷贝）
//   C) 每帧 memcpy 成本（1.2MB NPU 输入拷贝 + 1MB 同地址自拷贝）
//
// 口径声明（避免误读）：
//   * host 为 x86-64 标量路径（无 NEON、无 OpenCV SIMD），绝对值不能直接搬到
//     RV1106 单核 Cortex-A7；但"多遍 vs 单遍""每帧 vs 每 N 帧"的相对关系和
//     O(n^3) 的增长趋势是可信的。板端 NEON 实测见 board_perf.md 第 4 节。
//   * 旧链路里的 cvtColor/resize 用 preprocess.cc 的标量等价实现代替 OpenCV，
//     避免主机缺 OpenCV；两者都是"全帧逐像素"量级，遍数一致。
// =============================================================================

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "enhanced_decision.h"
#include "preprocess.h"
#include "../algorithms/graph_laplacian.h"

namespace {

double nowMs() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli> >(
               steady_clock::now().time_since_epoch()).count();
}

// n 个行人在 0.5m 网格上（全部互相在 edge_dist=2m 内 -> 稠密图 = Jacobi 最坏情况）
std::vector<std::pair<float,float> > makeCloud(int n) {
    std::vector<std::pair<float,float> > pts;
    pts.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        pts.push_back(std::make_pair((float)(i % 6) * 0.5f, (float)(i / 6) * 0.5f));
    return pts;
}

std::vector<std::pair<float,float> > makeSpread(int n) {
    // 分散图（几乎无边）：Jacobi 最快情况
    std::vector<std::pair<float,float> > pts;
    pts.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        pts.push_back(std::make_pair((float)(i % 8) * 3.0f, (float)(i / 8) * 3.0f));
    return pts;
}

void benchGraph() {
    std::printf("\n== A1. GraphLaplacian::computeFeatures 单次耗时（稠密图=最坏情况）==\n");
    std::printf("   n |  dense us/call | spread us/call\n");
    const int ns[] = {1, 2, 5, 10, 20, 30, 40};
    for (int k = 0; k < 7; ++k) {
        const int n = ns[k];
        std::vector<std::pair<float,float> > d = makeCloud(n);
        std::vector<std::pair<float,float> > s = makeSpread(n);
        GraphLaplacian gd(2.0), gs(2.0);
        // 预热
        for (int i = 0; i < 5; ++i) {
            gd.setPoints(d); gd.computeFeatures();
            gs.setPoints(s); gs.computeFeatures();
        }
        int iters = (n <= 10) ? 2000 : (n <= 20 ? 1000 : 300);
        double t0 = nowMs();
        for (int i = 0; i < iters; ++i) { gd.setPoints(d); gd.computeFeatures(); }
        double td = (nowMs() - t0) * 1000.0 / iters;
        t0 = nowMs();
        for (int i = 0; i < iters; ++i) { gs.setPoints(s); gs.computeFeatures(); }
        double ts = (nowMs() - t0) * 1000.0 / iters;
        std::printf("%4d | %14.1f | %14.1f\n", n, td, ts);
    }
}

void benchDecision() {
    std::printf("\n== A2. EnhancedDecision::processFrame 平均耗时（稠密节点集，300 帧）==\n");
    std::printf("   persons | interval |  ms/frame |  spectralUpdates | 相对 interval=1\n");
    double base20 = 0.0;
    const int persons[] = {20, 30};
    for (int pi = 0; pi < 2; ++pi) {
        const int np = persons[pi];
        std::vector<std::pair<float,float> > pts = makeCloud(np);
        const int intervals[] = {1, 2, 3, 5};
        for (int k = 0; k < 4; ++k) {
            pedestrian::EnhancedConfig cfg;
            cfg.spectral_interval = intervals[k];
            pedestrian::EnhancedDecision ed(cfg);
            for (int i = 0; i < 30; ++i) ed.processFrame(pts);   // 预热
            const int frames = 300;
            const double t0 = nowMs();
            for (int i = 0; i < frames; ++i) ed.processFrame(pts);
            const double per = (nowMs() - t0) / frames;
            if (np == 20 && k == 0) base20 = per;
            std::printf("   %7d | %8d | %9.4f | %16d | %6.2fx\n", np, intervals[k], per,
                        ed.spectralUpdates(), per > 0 ? base20 / per : 0.0);
        }
    }
    std::printf("   （host 标量口径；板端 A7 约为 host 的 3~10 倍，见 board_perf.md 第 4 节）\n");
}

// ---- 旧链路等价实现：NV12->BGR 全帧 + 双线性 resize + 黑边 + copyTo + 2 次 memcpy ----
void oldPipeline(const uint8_t* nv12, uint8_t* bgr, uint8_t* scaled,
                 uint8_t* letterbox, uint8_t* npu) {
    const int W = 720, H = 480, MW = 640, MH = 640;
    const int roiW = 639, roiH = 426;     // 与 letterbox_prepare 的几何一致
    const int left = 0, top = (MH - roiH) / 2;
    // 1) 等价 cv::cvtColor(YUV420sp2BGR)：全帧逐像素
    pedestrian::yuv420sp_to_bgr(nv12, W, H, bgr);
    // 2) 等价 cv::resize(INTER_LINEAR)：720x480 -> 639x426
    const double sx = (double)W / roiW, sy = (double)H / roiH;
    for (int y = 0; y < roiH; ++y) {
        const double fy = (y + 0.5) * sy - 0.5;
        int y0 = (int)std::floor(fy); if (y0 < 0) y0 = 0; if (y0 > H - 1) y0 = H - 1;
        int y1 = y0 + 1; if (y1 > H - 1) y1 = H - 1;
        const double wy = fy - y0 < 0 ? 0 : (fy - y0 > 1 ? 1 : fy - y0);
        for (int x = 0; x < roiW; ++x) {
            const double fx = (x + 0.5) * sx - 0.5;
            int x0 = (int)std::floor(fx); if (x0 < 0) x0 = 0; if (x0 > W - 1) x0 = W - 1;
            int x1 = x0 + 1; if (x1 > W - 1) x1 = W - 1;
            const double wx = fx - x0 < 0 ? 0 : (fx - x0 > 1 ? 1 : fx - x0);
            for (int c = 0; c < 3; ++c) {
                const double a = bgr[(y0 * W + x0) * 3 + c] * (1 - wx) + bgr[(y0 * W + x1) * 3 + c] * wx;
                const double b = bgr[(y1 * W + x0) * 3 + c] * (1 - wx) + bgr[(y1 * W + x1) * 3 + c] * wx;
                scaled[(y * roiW + x) * 3 + c] = (uint8_t)(a * (1 - wy) + b * wy + 0.5);
            }
        }
    }
    // 3) 等价 letterbox：整幅黑边清零 + copyTo ROI
    memset(letterbox, 0, (size_t)MW * MH * 3);
    for (int y = 0; y < roiH; ++y)
        memcpy(letterbox + ((size_t)(top + y) * MW + left) * 3,
               scaled + (size_t)y * roiW * 3, (size_t)roiW * 3);
    // 4) memcpy 进 NPU 输入缓冲（旧实现每帧一次）
    memcpy(npu, letterbox, (size_t)MW * MH * 3);
}

void benchPreprocess() {
    const int W = 720, H = 480, MW = 640, MH = 640;
    std::vector<uint8_t> nv12((size_t)W * H * 3 / 2), bgr((size_t)W * H * 3),
        scaled((size_t)639 * 426 * 3), letterbox((size_t)MW * MH * 3),
        npuOld((size_t)MW * MH * 3), npuNew((size_t)MW * MH * 3);
    for (size_t i = 0; i < nv12.size(); ++i) nv12[i] = (uint8_t)(i * 37 + 11);
    pedestrian::LetterboxParams p;
    if (pedestrian::letterbox_prepare(W, H, MW, MH, &p) != 0) {
        std::printf("letterbox_prepare failed\n");
        return;
    }
    const int iters = 60;
    for (int i = 0; i < 3; ++i)   // 预热
        oldPipeline(nv12.data(), bgr.data(), scaled.data(), letterbox.data(), npuOld.data());
    double t0 = nowMs();
    for (int i = 0; i < iters; ++i)
        oldPipeline(nv12.data(), bgr.data(), scaled.data(), letterbox.data(), npuOld.data());
    const double oldMs = (nowMs() - t0) / iters;

    for (int i = 0; i < 3; ++i) {
        pedestrian::yuv420sp_to_bgr(nv12.data(), W, H, bgr.data());
        pedestrian::yuv420sp_to_rgb_letterbox(nv12.data(), p, npuNew.data());
    }
    t0 = nowMs();
    for (int i = 0; i < iters; ++i) {
        pedestrian::yuv420sp_to_bgr(nv12.data(), W, H, bgr.data());
        pedestrian::yuv420sp_to_rgb_letterbox(nv12.data(), p, npuNew.data());
    }
    const double newMs = (nowMs() - t0) / iters;

    // 正确性快检：新链路 ROI 内非零、黑边为 0
    int nonzero = 0;
    for (int i = 0; i < MW * MH * 3; ++i) if (npuNew[i]) ++nonzero;

    std::printf("\n== B. 预处理 720x480 NV12 -> 640x640（每帧, host 标量）==\n");
    std::printf("  旧链路等价（cvtColor + resize + 整幅黑边 + copyTo + 1.2MB memcpy）: %7.2f ms\n", oldMs);
    std::printf("  新链路（NV12->BGR + NV12->RGB letterbox 单遍直写 NPU）          : %7.2f ms\n", newMs);
    std::printf("  节省                                                          : %7.2f ms/帧 (%.1fx)\n",
                oldMs - newMs, newMs > 0 ? oldMs / newMs : 0.0);
    std::printf("  (新链路输出非零字节 %d / %d，黑边应为 0 已由黄金测试覆盖)\n",
                nonzero, MW * MH * 3);
    (void)letterbox;
}

void benchMemcpy() {
    const size_t n1 = (size_t)640 * 640 * 3;   // NPU 输入
    const size_t n2 = (size_t)720 * 480 * 3;   // 同地址自拷贝
    std::vector<uint8_t> a(n1), b(n1), c(n2);
    for (size_t i = 0; i < n1; ++i) a[i] = (uint8_t)i;
    for (size_t i = 0; i < n2; ++i) c[i] = (uint8_t)i;
    const int iters = 200;
    double t0 = nowMs();
    for (int i = 0; i < iters; ++i) memcpy(b.data(), a.data(), n1);
    const double m1 = (nowMs() - t0) / iters;
    t0 = nowMs();
    for (int i = 0; i < iters; ++i) memcpy(c.data(), c.data(), n2);
    const double m2 = (nowMs() - t0) / iters;
    std::printf("\n== C. 每帧 memcpy 成本（host；A7 上约为 3~5 倍）==\n");
    std::printf("  1.2MB  NPU 输入拷贝（已由零拷贝消除）: %6.3f ms\n", m1);
    std::printf("  1.0MB  同地址自拷贝（旧 main.cc:645）: %6.3f ms\n", m2);
    std::printf("  合计每帧白扔                        : %6.3f ms\n", m1 + m2);
}

}  // namespace

int main() {
    std::printf("==== board-perf 主机微基准（x86-64 标量口径，非板端实测）====\n");
    benchGraph();
    benchDecision();
    benchPreprocess();
    benchMemcpy();
    std::printf("\n备注：板端 RV1106 为单核 Cortex-A7 @1.2GHz + NEON，绝对耗时约为"
                "host 的 3~10 倍；本基准用于比较【改动前后】与【增长趋势】，"
                "板端闭环数据见 board_perf.md。\n");
    return 0;
}
