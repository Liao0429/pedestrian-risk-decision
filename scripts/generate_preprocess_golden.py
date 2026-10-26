#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_preprocess_golden.py — 生成图像预处理（YUV420sp/NV12 -> BGR 与 RGB letterbox）黄金数据。

背景：P1-6 性能优化（阶段八）新增 C++ 模块 preprocess.{h,cc}，语义应与
"OpenCV 正确 NV12 转换 + INTER_LINEAR bilinear + letterbox" 数学等价（±2 舍入）。
本脚本用 OpenCV 作为权威参考生成期望输出，供 C++ 主机测试对比。

注意：
- 旧 main.cc 使用 cv::COLOR_YUV420sp2BGR（枚举值 == COLOR_YUV2BGR_NV21，NV21 语义），
  对 Rockchip VI 的 NV12 输出存在系统性红蓝互换+色偏。本模块修复为正确 NV12 语义，
  因此参考实现使用 cv2.COLOR_YUV2BGR_NV12，而不是旧代码的 COLOR_YUV420sp2BGR。
- 生成数据：tests/golden/preprocess/case_<name>_{in.bin,bgr.bin,rgb_letterbox.bin,meta.json}

用法：python scripts/generate_preprocess_golden.py [输出目录]
依赖：numpy, opencv-python（参考实现仅用于生成，C++ 测试不依赖 OpenCV）。
"""
import json
import os
import sys

import cv2
import numpy as np

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests", "golden", "preprocess")


def bgr_to_nv12(bgr):
    """BGR 图像 -> NV12（Y 平面 + UV 交错平面，U 在前）。"""
    h, w = bgr.shape[:2]
    i420 = cv2.cvtColor(bgr, cv2.COLOR_BGR2YUV_I420).reshape(-1)
    y = i420[: h * w]
    u = i420[h * w: h * w + (h // 2) * (w // 2)]
    v = i420[h * w + (h // 2) * (w // 2):]
    uv = np.zeros((h // 2) * w, np.uint8)
    uv[0::2] = u
    uv[1::2] = v
    return np.concatenate([y, uv])


def opencv_ref_bgr(nv12, src_w, src_h):
    """正确 NV12 -> BGR 全帧（cv2.COLOR_YUV2BGR_NV12）。"""
    mat = nv12.reshape(src_h + src_h // 2, src_w, 1)
    return cv2.cvtColor(mat, cv2.COLOR_YUV2BGR_NV12)


def opencv_ref_rgb_letterbox(nv12, src_w, src_h, dst_w, dst_h):
    """OpenCV 参考：NV12->BGR -> INTER_LINEAR resize -> letterbox 黑边 -> BGR2RGB。"""
    bgr = opencv_ref_bgr(nv12, src_w, src_h)
    scale = min(dst_w / src_w, dst_h / src_h)
    roi_w = int(src_w * scale)
    roi_h = int(src_h * scale)
    left_pad = (dst_w - roi_w) // 2
    top_pad = (dst_h - roi_h) // 2
    resized = cv2.resize(bgr, (roi_w, roi_h), interpolation=cv2.INTER_LINEAR)
    canvas = np.zeros((dst_h, dst_w, 3), np.uint8)
    canvas[top_pad: top_pad + roi_h, left_pad: left_pad + roi_w] = resized
    return cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB)


def gen_case(name, bgr, src_w, src_h, dst_w=640, dst_h=640):
    nv12 = bgr_to_nv12(bgr)
    bgr_ref = opencv_ref_bgr(nv12, src_w, src_h)
    rgb_ref = opencv_ref_rgb_letterbox(nv12, src_w, src_h, dst_w, dst_h)
    scale = min(dst_w / src_w, dst_h / src_h)
    roi_w = int(src_w * scale)
    roi_h = int(src_h * scale)
    meta = {
        "name": name,
        "src_w": src_w, "src_h": src_h,
        "dst_w": dst_w, "dst_h": dst_h,
        "roi_w": roi_w, "roi_h": roi_h,
        "left_pad": (dst_w - roi_w) // 2,
        "top_pad": (dst_h - roi_h) // 2,
    }
    base = os.path.join(OUT_DIR, f"case_{name}")
    nv12.tofile(base + "_in.bin")
    bgr_ref.tofile(base + "_bgr.bin")
    rgb_ref.tofile(base + "_rgb_letterbox.bin")
    with open(base + "_meta.json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    print(f"[ok] {name}: {src_w}x{src_h} -> {dst_w}x{dst_h}, roi={roi_w}x{roi_h}, "
          f"pads=({meta['left_pad']},{meta['top_pad']})")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = np.random.default_rng(20261027)

    # 1) 纯红/纯绿/纯蓝：验证颜色通道与 NV12 语义（红不再蓝绿互换）
    for ch_name, ch in (("red", 2), ("green", 1), ("blue", 0)):
        img = np.zeros((480, 720, 3), np.uint8)
        img[:, :, ch] = 255
        gen_case(ch_name, img, 720, 480)

    # 2) 灰度渐变：验证 bilinear 插值（平滑信号，差异应 <=1）
    g = np.tile(np.linspace(0, 255, 720, dtype=np.uint8).reshape(1, -1, 1), (480, 1, 3))
    gen_case("gray_gradient", np.ascontiguousarray(g), 720, 480)

    # 3) 随机噪声：验证一般情况与 clamp 行为（源像素 YUV->BGR 会越界，必须先 clamp）
    noise = rng.integers(0, 256, (480, 720, 3), dtype=np.uint8)
    gen_case("random_noise", noise, 720, 480)

    # 4) 平滑纹理：低频自然图像（高斯模糊模拟）
    tex = rng.integers(0, 256, (480, 720, 3)).astype(np.float32)
    # 3x3 盒式模糊迭代 20 次近似高斯（避免依赖 scipy）
    kernel = np.ones((3, 3), np.float32) / 9.0
    for _ in range(20):
        tex = cv2.filter2D(tex, -1, kernel)
    tex = np.clip(tex, 0, 255).astype(np.uint8)
    gen_case("smooth_texture", tex, 720, 480)

    # 5) 小尺寸非对齐：64x48 -> 32x32（验证黑边、非 8 对齐宽度、缩小路径）
    small = rng.integers(0, 256, (48, 64, 3), dtype=np.uint8)
    gen_case("small_64x48", small, 64, 48, dst_w=32, dst_h=32)

    # 6) 小尺寸放大：24x18 -> 32x32（验证放大路径）
    up = rng.integers(0, 256, (18, 24, 3), dtype=np.uint8)
    gen_case("small_24x18", up, 24, 18, dst_w=32, dst_h=32)

    # 7) 非 16 对齐宽：66x50 -> 48x48（验证 NEON 尾部标量路径宽度）
    odd = rng.integers(0, 256, (50, 66, 3), dtype=np.uint8)
    gen_case("odd_66x50", odd, 66, 50, dst_w=48, dst_h=48)

    print(f"\n全部黄金数据已生成到 {OUT_DIR}")


if __name__ == "__main__":
    main()