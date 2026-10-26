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