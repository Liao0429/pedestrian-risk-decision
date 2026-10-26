#include "preprocess.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace pedestrian {

namespace {

// ---------------------------------------------------------------------------
// 颜色转换：BT.601 limited range（与 cv::COLOR_YUV2BGR_NV12 一致）
//   R = 1.164*(Y-16) + 1.596*(V-128)
//   G = 1.164*(Y-16) - 0.391*(U-128) - 0.813*(V-128)
//   B = 1.164*(Y-16) + 2.018*(U-128)
// 系数用 12 位定点（误差 < 0.5/像素，黄金测试容差 2）。
// ---------------------------------------------------------------------------
static const int32_t kC0 = 4768;   //  1.164 * 4096
static const int32_t kC1 = 6537;   //  1.596 * 4096
static const int32_t kC2 = 1602;   //  0.391 * 4096
static const int32_t kC3 = 3330;   //  0.813 * 4096
static const int32_t kC4 = 8266;   //  2.018 * 4096
static const int     kColorShift = 12;
static const int32_t kWeightOne  = 32768;  // 插值权重定点（15 位）

// 单个源像素 YUV->BGR（标量；输出已 clamp 到 [0,255]）
inline void yuv2bgr_scalar(int32_t y, int32_t u, int32_t v,
                           int32_t *b, int32_t *g, int32_t *r) {
    const int32_t yv = (y - 16) * kC0;
    int32_t rr = yv + (v - 128) * kC1;
    int32_t gg = yv - (u - 128) * kC2 - (v - 128) * kC3;
    int32_t bb = yv + (u - 128) * kC4;
    rr >>= kColorShift;
    gg >>= kColorShift;
    bb >>= kColorShift;
    if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
    *r = rr; *g = gg; *b = bb;
}

// ---------------------------------------------------------------------------
// letterbox 核心（标量定点版）：对 ROI 内每个输出像素，取 4 个源像素的
// Y/U/V（NV12 块共享），各自转 BGR 并 clamp，再做 bilinear 插值（与 OpenCV
// "uint8 中间缓冲 + INTER_LINEAR" 数学等价），最后 BGR->RGB 写入。
// ---------------------------------------------------------------------------
void yuv420sp_to_rgb_letterbox_scalar(const uint8_t *yuv,
                                      const LetterboxParams &p,
                                      uint8_t *rgb_out) {
    const int src_w = p.src_w, src_h = p.src_h;
    const int dst_w = p.dst_w, dst_h = p.dst_h;
    const int roi_w = p.roi_w, roi_h = p.roi_h;
    const int left_pad = p.left_pad, top_pad = p.top_pad;
    const uint8_t *Y  = yuv;
    const uint8_t *UV = yuv + (size_t)src_w * src_h;
    const size_t row_bytes = (size_t)dst_w * 3;

    // 顶部/底部黑边（letterbox 填充 0，RGB 黑色仍为 0）
    for (int oy = 0; oy < top_pad; ++oy)
        memset(rgb_out + (size_t)oy * row_bytes, 0, row_bytes);
    const int bottom_start = top_pad + roi_h;
    for (int oy = bottom_start; oy < dst_h; ++oy)
        memset(rgb_out + (size_t)oy * row_bytes, 0, row_bytes);

    for (int ry = 0; ry < roi_h; ++ry) {
        const int oy = top_pad + ry;
        uint8_t *row = rgb_out + (size_t)oy * row_bytes;
        // 左右黑边
        for (int ox = 0; ox < left_pad; ++ox) {
            row[ox * 3] = 0; row[ox * 3 + 1] = 0; row[ox * 3 + 2] = 0;
        }
        const int right_start = left_pad + roi_w;
        for (int ox = right_start; ox < dst_w; ++ox) {
            row[ox * 3] = 0; row[ox * 3 + 1] = 0; row[ox * 3 + 2] = 0;
        }

        const int32_t y0 = p.row_y0[ry], y1 = p.row_y1[ry];
        const int32_t wy = p.row_wy[ry];
        const int32_t wy0 = kWeightOne - wy;
        const uint8_t *Y0 = Y + (size_t)y0 * src_w;
        const uint8_t *Y1 = Y + (size_t)y1 * src_w;
        // NV12：UV 平面每 2 行共享一行（块采样）
        const int uv_row0 = y0 >> 1, uv_row1 = y1 >> 1;
        const uint8_t *UV0 = UV + (size_t)uv_row0 * src_w;
        const uint8_t *UV1 = UV + (size_t)uv_row1 * src_w;

        for (int rx = 0; rx < roi_w; ++rx) {
            const int ox = left_pad + rx;
            const int32_t x0 = p.col_x0[rx], x1 = p.col_x1[rx];
            const int32_t wx = p.col_wx[rx];
            const int32_t wx0 = kWeightOne - wx;
            const int uoff0 = (x0 >> 1) * 2;   // NV12 交错：偶数偏移为 U，+1 为 V
            const int uoff1 = (x1 >> 1) * 2;

            const int32_t y00 = Y0[x0], y01 = Y0[x1], y10 = Y1[x0], y11 = Y1[x1];
            const int32_t u00 = UV0[uoff0],     v00 = UV0[uoff0 + 1];
            const int32_t u01 = UV0[uoff1],     v01 = UV0[uoff1 + 1];
            const int32_t u10 = UV1[uoff0],     v10 = UV1[uoff0 + 1];
            const int32_t u11 = UV1[uoff1],     v11 = UV1[uoff1 + 1];

            int32_t b00, g00, r00, b01, g01, r01, b10, g10, r10, b11, g11, r11;
            yuv2bgr_scalar(y00, u00, v00, &b00, &g00, &r00);
            yuv2bgr_scalar(y01, u01, v01, &b01, &g01, &r01);
            yuv2bgr_scalar(y10, u10, v10, &b10, &g10, &r10);
            yuv2bgr_scalar(y11, u11, v11, &b11, &g11, &r11);

            const int32_t w00 = (wx0 * wy0) >> 15;
            const int32_t w01 = (wx  * wy0) >> 15;
            const int32_t w10 = (wx0 * wy ) >> 15;
            const int32_t w11 = (wx  * wy ) >> 15;

            int32_t b = (b00 * w00 + b01 * w01 + b10 * w10 + b11 * w11) >> 15;
            int32_t g = (g00 * w00 + g01 * w01 + g10 * w10 + g11 * w11) >> 15;
            int32_t r = (r00 * w00 + r01 * w01 + r10 * w10 + r11 * w11) >> 15;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            // BGR -> RGB（RKNN 输入约定 RGB）
            row[ox * 3]     = (uint8_t)r;
            row[ox * 3 + 1] = (uint8_t)g;
            row[ox * 3 + 2] = (uint8_t)b;
        }
    }
}

// YUV420sp -> BGR 全帧（标量参考）
void yuv420sp_to_bgr_scalar(const uint8_t *yuv, int w, int h, uint8_t *bgr_out) {
    const uint8_t *Y  = yuv;
    const uint8_t *UV = yuv + (size_t)w * h;
    for (int y = 0; y < h; ++y) {
        const uint8_t *Yrow  = Y  + (size_t)y * w;
        const uint8_t *UVrow = UV + (size_t)(y >> 1) * w;
        uint8_t *out = bgr_out + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x) {
            const int uoff = (x >> 1) * 2;
            int32_t b, g, r;
            yuv2bgr_scalar(Yrow[x], UVrow[uoff], UVrow[uoff + 1], &b, &g, &r);
            out[x * 3]     = (uint8_t)b;
            out[x * 3 + 1] = (uint8_t)g;
            out[x * 3 + 2] = (uint8_t)r;
        }
    }
}

#if defined(__ARM_NEON)
// ---------------------------------------------------------------------------
// NEON 加速：YUV420sp -> BGR（每 8 像素一批，vtrn 展开 NV12 共享色度，int32 定点乘加）
// 输出 B/G/R 各 8 字节，vst3 交织写入 24 字节（BGR 顺序，与标量一致）。
// ---------------------------------------------------------------------------
static inline uint8x8_t neon_pack_u8x8(int32x4_t lo, int32x4_t hi) {
    const int32x4_t zero = vdupq_n_s32(0);
    const int32x4_t maxv = vdupq_n_s32(255);
    lo = vminq_s32(vmaxq_s32(lo, zero), maxv);
    hi = vminq_s32(vmaxq_s32(hi, zero), maxv);
    int16x4_t l16 = vmovn_s32(lo);
    int16x4_t h16 = vmovn_s32(hi);
    // vmovn_s16 返回 int8x8_t，重解释为 uint8x8_t
    return vreinterpret_u8_s8(vmovn_s16(vcombine_s16(l16, h16)));
}

static inline void yuv2bgr_neon8(const uint8_t *yp, const uint8_t *uvp,
                                 uint8_t *out) {
    uint8x8_t y8 = vld1_u8(yp);
    // NV12 水平 2x 抽样：8 个像素只对应 4 个 U/V（列 0,0,1,1,2,2,3,3）。
    // vld1_u8 读 8 字节交错 UV（U0 V0 U1 V1 U2 V2 U3 V3），vtrn 展开为
    // val[0]=[U0,U0,U1,U1,U2,U2,U3,U3]，val[1]=[V0,V0,V1,V1,V2,V2,V3,V3]。
    uint8x8_t uv = vld1_u8(uvp);
    uint8x8x2_t uv_trn = vtrn_u8(uv, uv);
    uint8x8_t u8 = uv_trn.val[0];
    uint8x8_t v8 = uv_trn.val[1];

    int16x8_t y16 = vreinterpretq_s16_u16(
        vsubq_u16(vmovl_u8(y8), vdupq_n_u16(16)));
    int16x8_t u16 = vreinterpretq_s16_u16(
        vsubq_u16(vmovl_u8(u8), vdupq_n_u16(128)));
    int16x8_t v16 = vreinterpretq_s16_u16(
        vsubq_u16(vmovl_u8(v8), vdupq_n_u16(128)));

    // 低 4 像素
    int32x4_t yv_lo = vmull_n_s16(vget_low_s16(y16), (int16_t)kC0);
    int16x4_t u_lo  = vget_low_s16(u16);
    int16x4_t v_lo  = vget_low_s16(v16);
    int32x4_t r_lo  = vmlal_n_s16(yv_lo, v_lo, (int16_t)kC1);
    int32x4_t g_lo  = vmlsl_n_s16(vmlsl_n_s16(yv_lo, u_lo, (int16_t)kC2),
                                  v_lo, (int16_t)kC3);
    int32x4_t b_lo  = vmlal_n_s16(yv_lo, u_lo, (int16_t)kC4);
    // 高 4 像素
    int32x4_t yv_hi = vmull_n_s16(vget_high_s16(y16), (int16_t)kC0);
    int16x4_t u_hi  = vget_high_s16(u16);
    int16x4_t v_hi  = vget_high_s16(v16);
    int32x4_t r_hi  = vmlal_n_s16(yv_hi, v_hi, (int16_t)kC1);
    int32x4_t g_hi  = vmlsl_n_s16(vmlsl_n_s16(yv_hi, u_hi, (int16_t)kC2),
                                  v_hi, (int16_t)kC3);
    int32x4_t b_hi  = vmlal_n_s16(yv_hi, u_hi, (int16_t)kC4);

    uint8x8_t r8 = neon_pack_u8x8(vshrq_n_s32(r_lo, kColorShift),
                                 vshrq_n_s32(r_hi, kColorShift));
    uint8x8_t g8 = neon_pack_u8x8(vshrq_n_s32(g_lo, kColorShift),
                                 vshrq_n_s32(g_hi, kColorShift));
    uint8x8_t b8 = neon_pack_u8x8(vshrq_n_s32(b_lo, kColorShift),
                                 vshrq_n_s32(b_hi, kColorShift));
    // BGR 顺序写入（与标量路径 yuv420sp_to_bgr_scalar 一致，供画框/VENC 显示）
    uint8x8x3_t bgr;
    bgr.val[0] = b8;
    bgr.val[1] = g8;
    bgr.val[2] = r8;
    vst3_u8(out, bgr);
}

void yuv420sp_to_bgr_neon(const uint8_t *yuv, int w, int h, uint8_t *bgr_out) {
    const uint8_t *Y  = yuv;
    const uint8_t *UV = yuv + (size_t)w * h;
    const int pixels_per_row = w;
    for (int y = 0; y < h; ++y) {
        const uint8_t *Yrow  = Y  + (size_t)y * w;
        const uint8_t *UVrow = UV + (size_t)(y >> 1) * w;
        uint8_t *out = bgr_out + (size_t)y * w * 3;
        int x = 0;
        // NEON 主循环：8 像素一批
        for (; x + 8 <= pixels_per_row; x += 8) {
            yuv2bgr_neon8(Yrow + x, UVrow + x, out + x * 3);
        }
        // 尾部标量
        for (; x < pixels_per_row; ++x) {
            const int uoff = (x >> 1) * 2;
            int32_t b, g, r;
            yuv2bgr_scalar(Yrow[x], UVrow[uoff], UVrow[uoff + 1], &b, &g, &r);
            out[x * 3]     = (uint8_t)b;
            out[x * 3 + 1] = (uint8_t)g;
            out[x * 3 + 2] = (uint8_t)r;
        }
    }
}

// YUV420sp -> RGB 全帧（NEON，RKNN letterbox 中间缓冲用；输出 RGB 顺序）
void yuv420sp_to_rgb_neon(const uint8_t *yuv, int w, int h, uint8_t *rgb_out) {
    const uint8_t *Y  = yuv;
    const uint8_t *UV = yuv + (size_t)w * h;
    const int pixels_per_row = w;
    for (int y = 0; y < h; ++y) {
        const uint8_t *Yrow  = Y  + (size_t)y * w;
        const uint8_t *UVrow = UV + (size_t)(y >> 1) * w;
        uint8_t *out = rgb_out + (size_t)y * w * 3;
        int x = 0;
        for (; x + 8 <= pixels_per_row; x += 8) {
            // 复用 yuv2bgr_neon8 的定点核心：其内部输出为 BGR，这里需要 RGB。
            // 直接调用再交换首尾通道（r<->b），避免复制整段 NEON 定点代码。
            uint8_t tmp[24];
            yuv2bgr_neon8(Yrow + x, UVrow + x, tmp);
            for (int i = 0; i < 8; ++i) {
                out[x * 3 + i * 3]     = tmp[i * 3 + 2];  // R = tmp[B]
                out[x * 3 + i * 3 + 1] = tmp[i * 3 + 1];  // G
                out[x * 3 + i * 3 + 2] = tmp[i * 3];      // B = tmp[R]
            }
        }
        for (; x < pixels_per_row; ++x) {
            const int uoff = (x >> 1) * 2;
            int32_t b, g, r;
            yuv2bgr_scalar(Yrow[x], UVrow[uoff], UVrow[uoff + 1], &b, &g, &r);
            out[x * 3]     = (uint8_t)r;
            out[x * 3 + 1] = (uint8_t)g;
            out[x * 3 + 2] = (uint8_t)b;
        }
    }
}

// letterbox 全帧 RGB 暂存（NEON 路径用；主循环单线程，静态缓冲避免每帧 malloc）。
// 上限 1280x720（实际 720x480=1.0MB，余量充足）。
static uint8_t s_rgb_scratch[1280 * 720 * 3];

// YUV420sp -> RGB letterbox（NEON 加速版）：
//   1) NV12 全帧 NEON 转 RGB（每源像素仅转换一次，~7ms/720x480）；
//   2) 双线性缩放 NEON 化：先垂直混合整行（vrow），再水平重采样（8 像素/组）。
// 数学上与标量 letterbox 等价（±1 舍入，黄金测试容差 2）；黑边逻辑与标量版相同。
void yuv420sp_to_rgb_letterbox_neon(const uint8_t *yuv,
                                  const LetterboxParams &p,
                                  uint8_t *rgb_out) {
    const int src_w = p.src_w, src_h = p.src_h;
    const int dst_w = p.dst_w, dst_h = p.dst_h;
    const int roi_w = p.roi_w, roi_h = p.roi_h;
    const int left_pad = p.left_pad, top_pad = p.top_pad;
    const size_t row_bytes = (size_t)dst_w * 3;

    // 尺寸超出暂存上限时回退标量（正常生产尺寸不会触发）
    if (src_w <= 0 || src_h <= 0 ||
        (size_t)src_w * src_h * 3 > sizeof(s_rgb_scratch) ||
        src_w > 1280) {
        yuv420sp_to_rgb_letterbox_scalar(yuv, p, rgb_out);
        return;
    }

    // 1) 全帧 NV12 -> RGB（NEON）
    yuv420sp_to_rgb_neon(yuv, src_w, src_h, s_rgb_scratch);

    // 2) 黑边
    for (int oy = 0; oy < top_pad; ++oy)
        memset(rgb_out + (size_t)oy * row_bytes, 0, row_bytes);
    const int bottom_start = top_pad + roi_h;
    for (int oy = bottom_start; oy < dst_h; ++oy)
        memset(rgb_out + (size_t)oy * row_bytes, 0, row_bytes);

    // 垂直混合行缓冲（src_w*3 字节）
    uint8_t vrow[1280 * 3];

    for (int ry = 0; ry < roi_h; ++ry) {
        const int oy = top_pad + ry;
        uint8_t *row = rgb_out + (size_t)oy * row_bytes;
        for (int ox = 0; ox < left_pad; ++ox) {
            row[ox * 3] = 0; row[ox * 3 + 1] = 0; row[ox * 3 + 2] = 0;
        }
        const int right_start = left_pad + roi_w;
        for (int ox = right_start; ox < dst_w; ++ox) {
            row[ox * 3] = 0; row[ox * 3 + 1] = 0; row[ox * 3 + 2] = 0;
        }

        // 3a) 垂直混合整行：vrow[c] = (row0[c]*wy0 + row1[c]*wy) >> 15
        const int32_t y0 = p.row_y0[ry], y1 = p.row_y1[ry];
        const int32_t wy = p.row_wy[ry];
        const int32_t wy0 = kWeightOne - wy;
        const uint8_t *row0 = s_rgb_scratch + (size_t)y0 * src_w * 3;
        const uint8_t *row1 = s_rgb_scratch + (size_t)y1 * src_w * 3;

        int cx = 0;
        for (; cx + 8 <= src_w; cx += 8) {
            const uint8x8x3_t a = vld3_u8(row0 + cx * 3);
            const uint8x8x3_t b = vld3_u8(row1 + cx * 3);
            uint8x8x3_t o;
            for (int ch = 0; ch < 3; ++ch) {
                const uint16x8_t a16 = vmovl_u8(a.val[ch]);
                const uint16x8_t b16 = vmovl_u8(b.val[ch]);
                uint32x4_t lo = vaddq_u32(
                                    vmull_n_u16(vget_low_u16(a16), (uint16_t)wy0),
                                    vmull_n_u16(vget_low_u16(b16), (uint16_t)wy));
                                uint32x4_t hi = vaddq_u32(
                                    vmull_n_u16(vget_high_u16(a16), (uint16_t)wy0),
                                    vmull_n_u16(vget_high_u16(b16), (uint16_t)wy));
                                // 四舍五入（+0.5）替代截断，避免两遍处理累积舍入误差
                                lo = vshrq_n_u32(vaddq_u32(lo, vdupq_n_u32(16384)), 15);
                                hi = vshrq_n_u32(vaddq_u32(hi, vdupq_n_u32(16384)), 15);
                                o.val[ch] = vmovn_u16(vcombine_u16(vmovn_u32(lo), vmovn_u32(hi)));
            }
            vst3_u8(vrow + cx * 3, o);
        }
        for (; cx < src_w; ++cx) {
                    for (int ch = 0; ch < 3; ++ch) {
                        int32_t v = (int32_t)row0[cx * 3 + ch] * wy0 +
                                    (int32_t)row1[cx * 3 + ch] * wy;
                        v = (v + 16384) >> 15;   // 四舍五入
                        if (v < 0) v = 0; else if (v > 255) v = 255;
                        vrow[cx * 3 + ch] = (uint8_t)v;
                    }
                }

        // 3b) 水平重采样（8 像素/组；组内 x0 连续时用 NEON 连续块 vld3）
        uint8_t *dst = row + left_pad * 3;
        int rx = 0;
        for (; rx + 8 <= roi_w; rx += 8) {
            const int32_t base = p.col_x0[rx];
            const bool group_ok = (p.col_x0[rx + 7] == base + 7) &&
                                  (p.col_x1[rx + 7] == base + 8);
            if (!group_ok) {
                            // 组内坐标不连续（非常规几何），回退标量（vrow 已垂直混合，
                            // 只需水平 2 样本插值）
                            for (int j = 0; j < 8; ++j) {
                                const int32_t x0 = p.col_x0[rx + j], x1 = p.col_x1[rx + j];
                                const int32_t wx = p.col_wx[rx + j];
                                const int32_t wx0 = kWeightOne - wx;
                                const uint8_t *p00 = vrow + x0 * 3;
                                const uint8_t *p01 = vrow + x1 * 3;
                                uint8_t *o = dst + (rx + j) * 3;
                                for (int ch = 0; ch < 3; ++ch) {
                                    int32_t v = (int32_t)p00[ch] * wx0 + p01[ch] * wx;
                                    v >>= 15;
                                    if (v < 0) v = 0; else if (v > 255) v = 255;
                                    o[ch] = (uint8_t)v;
                                }
                            }
                            continue;
                        }
            const uint8x8x3_t lo = vld3_u8(vrow + base * 3);
            const uint8x8x3_t hi = vld3_u8(vrow + (base + 1) * 3);
            const uint32x4_t w_lo = vld1q_u32((const uint32_t *)&p.col_wx[rx]);
            const uint32x4_t w_hi = vld1q_u32((const uint32_t *)&p.col_wx[rx + 4]);
            const uint32x4_t w0_lo = vsubq_u32(vdupq_n_u32((uint32_t)kWeightOne), w_lo);
            const uint32x4_t w0_hi = vsubq_u32(vdupq_n_u32((uint32_t)kWeightOne), w_hi);
            uint8x8x3_t o;
            for (int ch = 0; ch < 3; ++ch) {
                const uint16x8_t lo16 = vmovl_u8(lo.val[ch]);
                const uint16x8_t hi16 = vmovl_u8(hi.val[ch]);
                uint32x4_t out_lo = vaddq_u32(
                    vmulq_u32(vmovl_u16(vget_low_u16(lo16)), w0_lo),
                    vmulq_u32(vmovl_u16(vget_low_u16(hi16)), w_lo));
                uint32x4_t out_hi = vaddq_u32(
                    vmulq_u32(vmovl_u16(vget_high_u16(lo16)), w0_hi),
                    vmulq_u32(vmovl_u16(vget_high_u16(hi16)), w_hi));
                out_lo = vshrq_n_u32(vaddq_u32(out_lo, vdupq_n_u32(16384)), 15);
                                out_hi = vshrq_n_u32(vaddq_u32(out_hi, vdupq_n_u32(16384)), 15);
                o.val[ch] = vmovn_u16(vcombine_u16(vmovn_u32(out_lo), vmovn_u32(out_hi)));
            }
            vst3_u8(dst + rx * 3, o);
        }
        // 水平尾部（标量；vrow 已垂直混合，只需水平 2 样本插值）
                for (; rx < roi_w; ++rx) {
                    const int32_t x0 = p.col_x0[rx], x1 = p.col_x1[rx];
                    const int32_t wx = p.col_wx[rx];
                    const int32_t wx0 = kWeightOne - wx;
                    const uint8_t *p00 = vrow + x0 * 3;
                    const uint8_t *p01 = vrow + x1 * 3;
                    uint8_t *o = dst + rx * 3;
                    for (int ch = 0; ch < 3; ++ch) {
                        int32_t v = (int32_t)p00[ch] * wx0 + p01[ch] * wx;
                        v >>= 15;
                        if (v < 0) v = 0; else if (v > 255) v = 255;
                        o[ch] = (uint8_t)v;
                    }
                }
    }
}
#endif // __ARM_NEON

}  // namespace

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
int letterbox_prepare(int src_w, int src_h, int dst_w, int dst_h,
                      LetterboxParams *p) {
    if (!p || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return -1;
    if (p->row_y0 || p->row_y1 || p->row_wy || p->col_x0 || p->col_x1 || p->col_wx)
        return -1;  // 未释放或重复调用

    // 与旧 main.cc letterbox() 相同的几何：保持宽高比缩放到模型输入
    const double scale_x = (double)dst_w / src_w;
    const double scale_y = (double)dst_h / src_h;
    const double scale = scale_x < scale_y ? scale_x : scale_y;
    const int roi_w = (int)((double)src_w * scale);
    const int roi_h = (int)((double)src_h * scale);
    const int left_pad = (dst_w - roi_w) / 2;
    const int top_pad  = (dst_h - roi_h) / 2;

    int32_t *row_y0 = (int32_t *)calloc((size_t)roi_h, sizeof(int32_t));
    int32_t *row_y1 = (int32_t *)calloc((size_t)roi_h, sizeof(int32_t));
    int32_t *row_wy = (int32_t *)calloc((size_t)roi_h, sizeof(int32_t));
    int32_t *col_x0 = (int32_t *)calloc((size_t)roi_w, sizeof(int32_t));
    int32_t *col_x1 = (int32_t *)calloc((size_t)roi_w, sizeof(int32_t));
    int32_t *col_wx = (int32_t *)calloc((size_t)roi_w, sizeof(int32_t));
    if (!row_y0 || !row_y1 || !row_wy || !col_x0 || !col_x1 || !col_wx) {
        free(row_y0); free(row_y1); free(row_wy);
        free(col_x0); free(col_x1); free(col_wx);
        return -1;
    }

    // OpenCV INTER_LINEAR 中心对齐映射：src = (dst + 0.5) * scale - 0.5
    for (int ry = 0; ry < roi_h; ++ry) {
        const double src_y = (ry + 0.5) * ((double)src_h / roi_h) - 0.5;
        int32_t y0 = (int32_t)floor(src_y);
        if (y0 < 0) y0 = 0;
        int32_t y1 = y0 + 1;
        if (y1 >= src_h) y1 = src_h - 1;
        double wy = src_y - y0;
        if (wy < 0) wy = 0; else if (wy > 1) wy = 1;
        row_y0[ry] = y0;
        row_y1[ry] = y1;
        row_wy[ry] = (int32_t)(wy * kWeightOne + 0.5);
    }
    for (int rx = 0; rx < roi_w; ++rx) {
        const double src_x = (rx + 0.5) * ((double)src_w / roi_w) - 0.5;
        int32_t x0 = (int32_t)floor(src_x);
        if (x0 < 0) x0 = 0;
        int32_t x1 = x0 + 1;
        if (x1 >= src_w) x1 = src_w - 1;
        double wx = src_x - x0;
        if (wx < 0) wx = 0; else if (wx > 1) wx = 1;
        col_x0[rx] = x0;
        col_x1[rx] = x1;
        col_wx[rx] = (int32_t)(wx * kWeightOne + 0.5);
    }

    p->src_w = src_w;
    p->src_h = src_h;
    p->dst_w = dst_w;
    p->dst_h = dst_h;
    p->roi_w = roi_w;
    p->roi_h = roi_h;
    p->left_pad = left_pad;
    p->top_pad  = top_pad;
    p->row_y0 = row_y0;
    p->row_y1 = row_y1;
    p->row_wy = row_wy;
    p->col_x0 = col_x0;
    p->col_x1 = col_x1;
    p->col_wx = col_wx;
    return 0;
}

void letterbox_params_free(LetterboxParams *p) {
    if (!p) return;
    free(p->row_y0); free(p->row_y1); free(p->row_wy);
    free(p->col_x0); free(p->col_x1); free(p->col_wx);
    p->row_y0 = p->row_y1 = p->row_wy = nullptr;
    p->col_x0 = p->col_x1 = p->col_wx = nullptr;
}

void yuv420sp_to_rgb_letterbox(const uint8_t *yuv, const LetterboxParams &p,
                               uint8_t *rgb_out) {
#if defined(__ARM_NEON)
    yuv420sp_to_rgb_letterbox_neon(yuv, p, rgb_out);
#else
    yuv420sp_to_rgb_letterbox_scalar(yuv, p, rgb_out);
#endif
}

void yuv420sp_to_bgr(const uint8_t *yuv, int w, int h, uint8_t *bgr_out) {
#if defined(__ARM_NEON)
    yuv420sp_to_bgr_neon(yuv, w, h, bgr_out);
#else
    yuv420sp_to_bgr_scalar(yuv, w, h, bgr_out);
#endif
}

}  // namespace pedestrian