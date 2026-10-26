#ifndef PEDESTRIAN_PREPROCESS_H
#define PEDESTRIAN_PREPROCESS_H
//
// preprocess.h — 边缘端图像预处理（YUV420sp/NV12 -> RGB letterbox）
// ---------------------------------------------------------------------------
// 背景（P1-6 性能优化，阶段八）：
//   旧流程每帧执行 cv::cvtColor(YUV420sp2BGR) + 恒等 cv::resize +
//   letterbox(resize+BGR2RGB+memcpy)，实测 ~136ms（45ms + 91ms），占帧间隔 85%。
//   本模块把预处理合并为两条路径：
//     1) yuv420sp_to_bgr        ：YUV420sp(NV12) -> BGR 全帧（供画框/VENC 编码显示）
//     2) yuv420sp_to_rgb_letterbox：从 YUV 直接采样生成 RGB letterbox（供 RKNN）
//   两者均为单次遍历、定点整数运算（无浮点、无 OpenCV 中间缓冲）。
//
// 语义与 OpenCV 流程数学等价（主机黄金测试验证，允许 ±2 灰度级舍入）：
//   - 每个源像素先按 BT.601 limited range 转 BGR 并 clamp 到 [0,255]（与
//     cv::cvtColor 的 uint8 中间缓冲一致），再做 bilinear 插值。
//   - 坐标映射与 cv::resize INTER_LINEAR 一致（OpenCV 中心对齐：
//     src = (dst + 0.5) * scale - 0.5）。
//   - 颜色转换使用正确 NV12 语义（U 在前）。注意：旧代码使用的
//     cv::COLOR_YUV420sp2BGR 是 OpenCV 的 NV21 变体（枚举值 == COLOR_YUV2BGR_NV21），
//     对 Rockchip VI 的 NV12 输出会产生系统性红蓝互换+色偏，本模块一并修复。
//
// 性能设计：
//   - letterbox 缩放参数与行列映射表一次性预计算（letterbox_prepare），
//     每帧仅执行一次定点插值。
//   - 定点权重 15 位（0..32768），颜色转换系数 12 位小数，全部 int32 运算。
//   - 提供 NEON 加速路径（__ARM_NEON 条件编译），标量路径作为可移植参考。
//
// 本模块不依赖 OpenCV / RKNN，可独立主机单元测试。
// ---------------------------------------------------------------------------

#include <stdint.h>

namespace pedestrian {

// letterbox 预计算参数（映射表由 letterbox_prepare 分配，用后 letterbox_params_free 释放）
struct LetterboxParams {
    int src_w = 0;              // 源图像宽（VI 输出，如 720）
    int src_h = 0;              // 源图像高（如 480）
    int dst_w = 0;              // 模型输入宽（如 640）
    int dst_h = 0;              // 模型输入高（如 640）
    int roi_w = 0;              // letterbox 缩放后内容宽
    int roi_h = 0;              // letterbox 缩放后内容高
    int left_pad = 0;           // 左侧黑边宽
    int top_pad = 0;            // 顶部黑边高

    // 行映射表（长度 roi_h）：输出行 ry -> 源行 y0/y1 与垂直权重 wy（0..32768）
    int32_t *row_y0 = nullptr;
    int32_t *row_y1 = nullptr;
    int32_t *row_wy = nullptr;
    // 列映射表（长度 roi_w）：输出列 rx -> 源列 x0/x1 与水平权重 wx（0..32768）
    int32_t *col_x0 = nullptr;
    int32_t *col_x1 = nullptr;
    int32_t *col_wx = nullptr;
};

// 预计算 letterbox 参数与映射表。成功返回 0；失败返回 -1（内存不足/参数非法）。
int letterbox_prepare(int src_w, int src_h, int dst_w, int dst_h, LetterboxParams *p);

// 释放 letterbox_prepare 分配的映射表（安全置空指针）。
void letterbox_params_free(LetterboxParams *p);

// YUV420sp (NV12) -> RGB letterbox。
//   yuv     : NV12 数据指针（Y 平面 src_w*src_h 字节 + UV 交错平面 src_w*(src_h/2) 字节）
//   p       : letterbox_prepare 输出的参数
//   rgb_out : 输出缓冲，dst_w*dst_h*3 字节（RGB 顺序，直接可送入 RKNN NHWC 输入）
void yuv420sp_to_rgb_letterbox(const uint8_t *yuv, const LetterboxParams &p, uint8_t *rgb_out);

// YUV420sp (NV12) -> BGR 全帧（供显示/编码；与旧 cv::COLOR_YUV420sp2BGR 不同，
// 这里使用正确 NV12 语义，颜色不再红蓝互换）。
//   yuv     : NV12 数据指针
//   w/h     : 源图像宽高
//   bgr_out : 输出缓冲，w*h*3 字节（BGR 顺序）
void yuv420sp_to_bgr(const uint8_t *yuv, int w, int h, uint8_t *bgr_out);

} // namespace pedestrian

#endif // PEDESTRIAN_PREPROCESS_H