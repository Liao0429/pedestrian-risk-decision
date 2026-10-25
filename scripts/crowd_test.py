#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
crowd_test.py - 行人检测 + 复杂网络拥挤度/风险等级测试脚本

功能：
  1. 用 ultralytics YOLOv8 检测行人(person, COCO 类别 0)
  2. 以每检测框底部中心作为行人脚点
  3. 逆透视变换(IPM)把像素脚点映射到地面米制坐标
     - 默认使用与 C++ 端 risk_engine 一致的固定单应矩阵 H(720x480 标定)，
       并按输入图像实际尺寸等比缩放
     - 也可用 --scale 指定近似比例尺(米/像素)替代 IPM
     - 也可用 --homography 指定自定义 3x3 单应矩阵文件
  4. 距离阈值 2.0m 内建边，构造行人空间网络
  5. 计算网络特征：n(节点数), m(边数), density, r_gcc, clustering
  6. 按规则输出风险等级：DANGER / WARNING / SAFE
  7. 绘制检测框 + 顶部色条(CROWD:等级+特征)，保存标注图与 JSON 统计

运行方法：
  # 1. 安装依赖(本机无 GPU 可用 CPU)
  pip install ultralytics opencv-python-headless numpy

  # 2. 处理单张图片
  python crowd_test.py --input /path/to/img.jpg --output ./output

  # 3. 处理目录(递归扫描所有图片)
  python crowd_test.py --input /path/to/images_dir --output ./output --conf 0.30

  # 4. 自定义权重 / 近似比例尺 / 自定义单应矩阵
  python crowd_test.py --input img.jpg --weights my.pt --scale 0.01
  python crowd_test.py --input img.jpg --homography H.txt

  # 输出：
  #   <output>/vis/<原名>_crowd.jpg    标注可视化图
  #   <output>/results.json           所有图片的统计汇总

算法(与 src/luckfox_rtsp_yolov5/src/risk_engine.cc 一致)：
  - 检测：YOLOv8(默认 yolov8n.pt 自动下载)，只保留 person(COCO 0)
  - 脚点：每检测框底部中心 ( (x1+x2)/2, y2 )
  - 坐标映射：像素 -> 地面米(固定单应矩阵 H，注释见 get_homography)
  - 建边：地面欧氏距离 < 2.0m
  - density = m / (n*(n-1)/2)
  - r_gcc  = 最大连通分量节点数 / n   (BFS)
  - clustering = 平均局部聚类系数
  - 规则：
      n>=10 且 (density>=0.55 或 r_gcc>=0.80) => DANGER
      n>=5  且 (density>=0.28 或 r_gcc>=0.55) => WARNING
      否则 => SAFE
"""

import argparse
import json
import os
import sys
import time

import numpy as np

try:
    import cv2
except ImportError as e:  # pragma: no cover
    sys.exit("[crowd_test] 缺少 opencv，请先安装: pip install opencv-python-headless\n"
             "原始错误: %s" % e)

try:
    from ultralytics import YOLO
except ImportError as e:  # pragma: no cover
    sys.exit("[crowd_test] 缺少 ultralytics，请先安装: pip install ultralytics\n"
             "原始错误: %s" % e)


# ----------------------------------------------------------------------
# 配置常量(与 C++ risk_engine.cc 保持一致)
# ----------------------------------------------------------------------
EDGE_DIST_M = 2.0    # 建边距离阈值(米)
DANGER_N = 10        # 危险：行人数阈值
DANGER_RHO = 0.55    # 危险：网络密度阈值
DANGER_GCC = 0.80    # 危险：最大连通分量占比阈值
WARN_N = 5           # 预警：行人数阈值
WARN_RHO = 0.28      # 预警：网络密度阈值
WARN_GCC = 0.55      # 预警：最大连通分量占比阈值

# 等级对应的 BGR 颜色(OpenCV 为 BGR)
LEVEL_COLOR = {
    "SAFE":    (0, 255, 0),     # 绿
    "WARNING": (0, 255, 255),   # 黄
    "DANGER":  (0, 0, 255),     # 红
}


def get_homography(img_w, img_h):
    """返回将像素坐标映射到地面米制坐标的 3x3 单应矩阵 H。

    与 C++ 端 main.cc 中标定一致：
      源四边形取 720x480 画面底部地面梯形(近大远小)：
        (0,480),(720,480),(560,300),(160,300)
      目标四边形为地面 6m x 4m 区域(米)：
        (-3,3),(3,3),(3,-1),(-3,-1)
    若输入图像尺寸不是 720x480，则按比例缩放源四边形以近似适配。
    """
    ref_w, ref_h = 720, 480
    src = np.array([[0, 480], [720, 480], [560, 300], [160, 300]],
                    dtype=np.float64)
    dst = np.array([[-3, 3], [3, 3], [3, -1], [-3, -1]], dtype=np.float64)

    if (img_w, img_h) != (ref_w, ref_h):
        sx = img_w / float(ref_w)
        sy = img_h / float(ref_h)
        src = src.copy()
        src[:, 0] *= sx
        src[:, 1] *= sy

    H = cv2.getPerspectiveTransform(src, dst)
    return H


def load_homography(path):
    """从文件加载 3x3 单应矩阵。
    支持：
      - .npy : numpy 二进制数组
      - .txt/.csv : 三行三列数字(空格或逗号分隔)
    """
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        raise FileNotFoundError("单应矩阵文件不存在: %s" % path)

    if path.lower().endswith(".npy"):
        H = np.load(path)
    else:
        rows = []
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                line = line.replace(",", " ")
                vals = [float(t) for t in line.split()]
                if vals:
                    rows.append(vals)
        H = np.array(rows, dtype=np.float64)

    if H.shape != (3, 3):
        raise ValueError("单应矩阵必须为 3x3，实际形状: %s" % (H.shape,))
    return H.astype(np.float64)


def map_to_ground(x, y, H):
    """把图像像素坐标 (x, y) 通过单应矩阵映射到地面米制坐标。"""
    q = H.dot(np.array([x, y, 1.0]))
    w = q[2]
    if abs(w) > 1e-6:
        return float(q[0] / w), float(q[1] / w)
    return float(x), float(y)


def compute_features(pts, edge_dist=EDGE_DIST_M):
    """输入: 地面米制坐标列表 [(x,y), ...]
    返回: (n, m, density, r_gcc, clustering)
    算法与 C++ RiskEngine::computeFeatures() 一致。
    """
    n = len(pts)
    if n < 2:
        density = 0.0
        r_gcc = 1.0 if n > 0 else 0.0
        clustering = 0.0
        return n, 0, density, r_gcc, clustering

    # 距离矩阵 -> 邻接矩阵
    adj = [[False] * n for _ in range(n)]
    m = 0
    for i in range(n):
        xi, yi = pts[i]
        for j in range(i + 1, n):
            xj, yj = pts[j]
            d = ((xi - xj) ** 2 + (yi - yj) ** 2) ** 0.5
            if d < edge_dist:
                adj[i][j] = adj[j][i] = True
                m += 1

    # 网络密度
    possible = n * (n - 1) // 2
    density = m / float(possible) if possible > 0 else 0.0

    # 最大连通分量占比(BFS)
    visited = [False] * n
    max_comp = 0
    for i in range(n):
        if visited[i]:
            continue
        comp = 0
        queue = [i]
        visited[i] = True
        while queue:
            u = queue.pop(0)
            comp += 1
            for v in range(n):
                if adj[u][v] and not visited[v]:
                    visited[v] = True
                    queue.append(v)
        if comp > max_comp:
            max_comp = comp
    r_gcc = max_comp / float(n)

    # 平均聚类系数
    sum_c = 0.0
    for i in range(n):
        neigh = [j for j in range(n) if adj[i][j]]
        k = len(neigh)
        if k >= 2:
            links = 0
            for a in range(k):
                for b in range(a + 1, k):
                    if adj[neigh[a]][neigh[b]]:
                        links += 1
            max_pairs = k * (k - 1) // 2
            sum_c += links / float(max_pairs)
    clustering = sum_c / float(n) if n > 0 else 0.0

    return n, m, density, r_gcc, clustering


def evaluate_level(n, density, r_gcc):
    """按规则判断风险等级(与 C++ RiskEngine::evaluate() 一致)。"""
    if n >= DANGER_N and (density >= DANGER_RHO or r_gcc >= DANGER_GCC):
        return "DANGER"
    if n >= WARN_N and (density >= WARN_RHO or r_gcc >= WARN_GCC):
        return "WARNING"
    return "SAFE"


def draw_annotation(img, boxes, level, feat):
    """绘制检测框 + 顶部色条，返回标注图(原地修改 img)。"""
    img_h, img_w = img.shape[:2]

    # 自适应尺寸(以 720 宽为基准缩放，限制字体/条高范围)
    bar_h = int(max(40, min(80, img_h // 12)))
    font_scale = max(0.6, min(1.2, img_w / 900.0))
    thickness = max(1, int(round(font_scale * 2)))
    text_h = int(bar_h * 0.7)

    # 检测框(只画 person)
    for (x1, y1, x2, y2), conf in boxes:
        cv2.rectangle(img, (int(x1), int(y1)), (int(x2), int(y2)),
                      (0, 255, 0), thickness)
        label = "person %.2f" % conf
        # 标签底色
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX,
                                      font_scale * 0.7, thickness)
        ty = int(y1) - 4
        if ty - th < bar_h:  # 避免与顶部条重叠
            ty = int(y1) + th + 6
        cv2.putText(img, label, (int(x1), ty),
                    cv2.FONT_HERSHEY_SIMPLEX, font_scale * 0.7,
                    (0, 255, 0), thickness)

    # 顶部色条
    color = LEVEL_COLOR.get(level, LEVEL_COLOR["SAFE"])
    cv2.rectangle(img, (0, 0), (img_w, bar_h), color, -1)
    info = "CROWD:%s n=%d rho=%.2f r_gcc=%.2f clustering=%.2f" % (
        level, feat["n"], feat["density"], feat["r_gcc"], feat["clustering"])
    cv2.putText(img, info, (10, text_h),
                cv2.FONT_HERSHEY_SIMPLEX, font_scale,
                (0, 0, 0), thickness)

    # 底部加一条细分隔线，提示等级颜色含义
    cv2.putText(img, "green=SAFE yellow=WARNING red=DANGER",
                (10, img_h - 10),
                cv2.FONT_HERSHEY_SIMPLEX, font_scale * 0.6,
                (255, 255, 255), thickness)

    return img


def process_image(model, img_path, out_dir, conf, imgsz, H, scale_mpp, device,
                  edge_dist):
    """处理单张图片，返回统计 dict(失败返回 None 并打印错误)。"""
    basename = os.path.splitext(os.path.basename(img_path))[0]

    img = cv2.imread(img_path)
    if img is None:
        print("[ERROR] 无法读取图片(可能损坏或格式不支持): %s" % img_path,
              file=sys.stderr)
        return None

    img_h, img_w = img.shape[:2]

    # 计算脚点 -> 地面坐标映射
    if H is None and scale_mpp is None:
        H = get_homography(img_w, img_h)

    def to_ground(x, y):
        if H is not None:
            return map_to_ground(x, y, H)
        # 近似比例尺：像素 * 米/像素
        return float(x) * scale_mpp, float(y) * scale_mpp

    # ---- 推理 ----
    try:
        results = model.predict(img, conf=conf, imgsz=imgsz, classes=[0],
                                device=device, verbose=False)
    except Exception as e:
        print("[ERROR] 推理失败 %s: %s" % (img_path, e), file=sys.stderr)
        return None

    boxes_xyxy = []
    detections = []
    pts = []  # 地面坐标(米)

    if results and len(results) > 0:
        r = results[0]
        if r.boxes is not None and len(r.boxes) > 0:
            xyxy = r.boxes.xyxy.cpu().numpy()
            confs = r.boxes.conf.cpu().numpy()
            clss = r.boxes.cls.cpu().numpy()
            for (x1, y1, x2, y2), c, cls_id in zip(xyxy, confs, clss):
                if int(cls_id) != 0:  # 只保留 person
                    continue
                boxes_xyxy.append(((x1, y1, x2, y2), float(c)))
                detections.append({
                    "bbox": [float(x1), float(y1), float(x2), float(y2)],
                    "conf": float(c),
                })
                # 脚点：底部中心
                foot_x = (x1 + x2) / 2.0
                foot_y = y2
                gx, gy = to_ground(foot_x, foot_y)
                pts.append((gx, gy))

    # ---- 网络特征与等级 ----
    n, m, density, r_gcc, clustering = compute_features(pts, edge_dist)
    level = evaluate_level(n, density, r_gcc)
    feat = {
        "n": n,
        "m": m,
        "density": round(density, 4),
        "r_gcc": round(r_gcc, 4),
        "clustering": round(clustering, 4),
    }

    # ---- 可视化并保存 ----
    annotated = img.copy()
    if n > 0:
        draw_annotation(annotated, boxes_xyxy, level, feat)
    else:
        draw_annotation(annotated, [], level, feat)

    vis_dir = os.path.join(out_dir, "vis")
    os.makedirs(vis_dir, exist_ok=True)
    vis_path = os.path.join(vis_dir, "%s_crowd.jpg" % basename)
    cv2.imwrite(vis_path, annotated)

    # ---- 打印 ----
    print("[%s] n=%d m=%d density=%.4f r_gcc=%.4f clustering=%.4f 等级=%s"
          % (basename, n, m, density, r_gcc, clustering, level))

    return {
        "image": os.path.abspath(img_path),
        "filename": basename,
        "size": [img_w, img_h],
        "level": level,
        "n": n,
        "m": m,
        "density": round(density, 4),
        "r_gcc": round(r_gcc, 4),
        "clustering": round(clustering, 4),
        "detections": detections,
        "visualization": os.path.abspath(vis_path),
    }


def collect_images(input_path):
    """若 input 是目录，递归收集常见图片文件；否则当作单个文件。"""
    img_exts = (".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff")
    if os.path.isdir(input_path):
        files = []
        for root, _, names in os.walk(input_path):
            for name in sorted(names):
                if name.lower().endswith(img_exts):
                    files.append(os.path.join(root, name))
        return files
    if os.path.isfile(input_path):
        return [input_path]
    return []


def main():
    parser = argparse.ArgumentParser(
        description="YOLO 行人检测 + 复杂网络拥挤度/风险等级测试")
    parser.add_argument("--input", required=True,
                        help="输入图片路径或包含图片的目录")
    parser.add_argument("--output", default="output",
                        help="输出目录(默认 output)，标注图存 output/vis/，"
                             "统计存 output/results.json")
    parser.add_argument("--conf", type=float, default=0.25,
                        help="检测置信度阈值(默认 0.25)")
    parser.add_argument("--weights", default="yolov8n.pt",
                        help="YOLO 权重路径或 ultralytics 内置名(默认 yolov8n.pt)")
    parser.add_argument("--imgsz", type=int, default=640,
                        help="推理输入尺寸(默认 640)")
    parser.add_argument("--homography", default=None,
                        help="自定义 3x3 单应矩阵文件(.npy/.txt/.csv)，"
                             "默认使用与 C++ 端一致的固定 H 并按图像尺寸缩放")
    parser.add_argument("--scale", type=float, default=None,
                        help="近似比例尺(米/像素)，提供则跳过单应矩阵")
    parser.add_argument("--device", default=None,
                        help="推理设备，如 'cpu'/'0'(默认自动选择)")
    parser.add_argument("--edge-dist", type=float, default=EDGE_DIST_M,
                        help="建边距离阈值(米，默认 2.0)")
    args = parser.parse_args()

    edge_dist = args.edge_dist

    out_dir = os.path.abspath(args.output)
    os.makedirs(out_dir, exist_ok=True)

    # ---- 加载模型(首次运行自动下载默认权重) ----
    print("[crowd_test] 加载模型: %s" % args.weights)
    model = YOLO(args.weights)

    # ---- 加载单应矩阵 ----
    H = None
    if args.homography:
        H = load_homography(args.homography)
        print("[crowd_test] 使用自定义单应矩阵: %s" % args.homography)
    elif args.scale is not None:
        print("[crowd_test] 使用近似比例尺: %.6f 米/像素" % args.scale)
    else:
        print("[crowd_test] 使用固定单应矩阵 H(720x480 标定，按图像尺寸缩放)")

    # ---- 收集图片 ----
    images = collect_images(args.input)
    if not images:
        print("[ERROR] 未找到任何图片: %s" % args.input, file=sys.stderr)
        return 1
    print("[crowd_test] 共找到 %d 张图片" % len(images))

    # ---- 逐张处理 ----
    all_results = []
    failures = []
    start = time.time()
    for idx, img_path in enumerate(images, 1):
        print("[%d/%d] 处理 %s" % (idx, len(images), img_path))
        try:
            rec = process_image(model, img_path, out_dir,
                                args.conf, args.imgsz, H, args.scale,
                                args.device, edge_dist)
        except Exception as e:
            print("[ERROR] 处理失败 %s: %s" % (img_path, e), file=sys.stderr)
            rec = None
        if rec is not None:
            all_results.append(rec)
        else:
            failures.append(img_path)

    # ---- 保存统计 ----
    summary = {
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "weights": args.weights,
        "conf": args.conf,
        "edge_dist_m": edge_dist,
        "total_images": len(images),
        "processed": len(all_results),
        "failed": failures,
        "results": all_results,
    }
    json_path = os.path.join(out_dir, "results.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print("\n[crowd_test] 完成:")
    print("  处理图片: %d / %d" % (len(all_results), len(images)))
    print("  失败图片: %d" % len(failures))
    print("  标注图目录: %s" % os.path.join(out_dir, "vis"))
    print("  统计文件: %s" % json_path)
    print("  耗时: %.2fs" % (time.time() - start))

    return 0 if not failures else 0


if __name__ == "__main__":
    sys.exit(main())