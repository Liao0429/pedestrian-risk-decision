# images500 子数据集说明

500 张 COCO 2017 真实含行人图片，用于实验验证（保证数据量、降低过拟合风险）。

## 来源
- COCO 2017 官方图片（images.cocodataset.org）
- 人数标注来自 Ultralytics coco2017labels.zip（YOLO 格式，class 0=person）

## 组成（按 ground-truth 人数分布）
| 密度档 | 人数 | 数量 |
|---|---|---|
| 分散 | 1-3 人 | 200 |
| 局部聚集 | 4-9 人 | 150 |
| 较拥挤 | 10-13 人 | 138 |
| 拥挤 | 14-19 人 | 12 |
| **合计** | — | **500** |

## 文件命名
`<image_id>_<person_count>.jpg`，如 `000000122672_1.jpg` 表示该图标注 1 人。

## 下载脚本
`dataset/_work/download_500_images.py`（可复现，并发下载，校验大小）。

## 下载时间
2026-10-25，全部 500 张校验通过（JPEG 头有效，文件 >1KB）。

## 用途
- 图片模式测试（`crowd_test.py --input dataset/images500`）
- 训练/评估检测模型（YOLO 行人检测）
- 与视频数据（dataset/videos50）配合，覆盖静态与动态场景

## GT 对比评估（自动化，可复现）
`scripts/evaluate_images500_gt.py` 将本目录每张图的 COCO 官方人数（文件名 `_<count>` 后缀）与检测输出 `n` 对比，生成：
- `results/images500_gt_eval/summary.json`（机器可读）
- `results/images500_gt_eval/report.md`（整体/分桶检出率、MAE/RMSE、Pearson 相关、欠检/过检 Top 10 个案清单）

当前基线（`results/rerun/images500_baseline/results.json`，yolov8n.pt conf=0.25）：整体检出率 **82.5%**（2548/3089），MAE 1.926，RMSE 3.188，Pearson r 0.802。最差个案清单可用于人工复核 COCO GT 与视觉实际一致性。

## 微小目标量化分析（GT 人工复核前置）

`scripts/analyze_images500_tiny_gt.py` 对全部 500 张图做 GT 数据质量与尺寸分布分析：

- **GT 一致性校验**：文件名后缀人数与 COCO 标注 person 实例数 **500/500 完全一致**，无缺失、无冲突。
- **GT 尺寸分布**：<16px 占 9.8%、<20px 占 13.5%、<32px 占 26.3%（原始分辨率）。
- **微小占比与检出率**：逐图「<20px 微小占比」与检出率 Pearson **r=-0.403**；微小占比 0% 的图像平均检出率 95.3%，>75% 的图像仅 27.8%——漏检主要由微小目标解释。
- **个案明细**：欠检/过检 Top10 的 GT 尺寸（中位高/最矮/微小占比）逐图列出，供人工复核分级：欠检优先看「微小占比低但漏检多」的图（如 `000000497261_19` 是 0% 微小仍漏 12 人的真缺陷）；过检个案全部需复核（区分误检 vs GT 漏标）。

产物：`results/images500_gt_eval/tiny_gt_analysis.md` + `tiny_gt_summary.json`，由 `python scripts/analyze_images500_tiny_gt.py` 一键复现。