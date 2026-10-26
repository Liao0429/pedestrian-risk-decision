# 真实数据集全量重跑结果（RERUN）

> 日期：2026-10-26。使用当前最新代码（含全部优化/修复）对**全部真实数据集**重跑，数据真实可复现。

## 1. 图片基线（500 张 COCO 真实含行人图）

运行命令：
```
python scripts/crowd_test.py --input dataset/images500 --output results/rerun/images500_baseline --device 0 --weights yolov8n.pt
```

真实结果（解析 `results/rerun/images500_baseline/results.json`，500 张全部处理，0 失败）：

| 指标 | 结果 |
|---|---|
| 处理图片 | **500 / 500**（0 失败） |
| ground-truth 总人数 | 3089 |
| 检测总人数 | 2548 |
| **总体检出率** | **82.5%** |
| 高密度（≥14人）图检出率 | **76.7%**（9 张） |
| 平均每图检测人数 | 5.10 |
| 等级分布 | **SAFE 414（82.8%）/ WARNING 61（12.2%）/ DANGER 25（5.0%）** |

- 逐图明细：`results/rerun/images500_baseline/results.json`（含每图检测框/置信度/风险等级/可视化图）
- 可视化：`results/rerun/images500_baseline/vis/`（真实标注图）

## 2. 视频增强（111 条真实 Mixkit 视频）

运行命令：
```
python scripts/rerun_all_videos.py
```

- 逐条运行 `crowd_test_enhanced.py`（Kalman + 谱特征 + FSM 增强路径），输出时间线 JSON
- 汇总：`results/rerun/video_enhanced/_summary.json`（每条视频状态/耗时/时间线路径）、`_aggregate.json`（聚合统计）
- **增强结果（111/111 条成功，45021 帧）**：
  - 等级分布：**SAFE 25669 / WARNING 11545 / DANGER 7807**
  - 平均每条等级切换 **4.00 次**（FSM 滞回稳定）
  - 出现 DANGER 视频 40 条；全程 SAFE（零误报）31/111 条
  - 密集场景示例：mixkit_4519 DANGER 520/603 帧（86%）、mixkit_3428 99% 帧 DANGER 且切换仅 2 次
- **基线对比（算法前逐帧规则判定）**：运行中，完成后在 `docs/COMPARISON.md` 给出前后对比表

## 复现
- `scripts/rerun_all_videos.py`：全量视频批量脚本（加载一次模型循环 111 条）
- 数据集：`dataset/images500/`（500 图）、`dataset/videos50/`（111 视频，ffprobe+HOG 双重校验）
- 权重：`yolov8n.pt`（COCO 80 类预训练，真实推理）