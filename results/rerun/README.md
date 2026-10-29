# 真实数据集全量重跑结果（RERUN）

> 日期：2026-10-26；2026-10-28 接入谱特征评分（w4=0.1）后使用最新代码全量重跑。对**全部真实数据集**重跑，数据真实可复现。

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
| 高密度（≥14人）图检出率 | **79.4%**（12 张） |
| 平均每图检测人数 | 5.10 |
| 等级分布 | **SAFE 414（82.8%）/ WARNING 61（12.2%）/ DANGER 25（5.0%）** |

- 逐图明细：`results/rerun/images500_baseline/results.json`（含每图检测框/置信度/风险等级/可视化图）
- 可视化：`results/rerun/images500_baseline/vis/`（真实标注图）
- **GT 对比评估**（COCO 官方人数 vs 检测人数）：`results/images500_gt_eval/report.md`——整体/分桶检出率、MAE 1.926、RMSE 3.188、Pearson r 0.802、欠检/过检 Top 10 个案清单（供 GT 人工复核）；由 `scripts/evaluate_images500_gt.py` 自动生成，可复现。
- **微小目标量化分析**（GT 人工复核前置）：`results/images500_gt_eval/tiny_gt_analysis.md`——GT 一致性 500/500 通过；<20px 微小 GT 占 13.5%；「微小占比 vs 检出率」Pearson r=-0.403（0%微小桶 95.3% → >75%微小桶 27.8%），欠检主要由微小目标解释；欠检/过检 Top10 的 GT 尺寸明细已逐图列出（`000000497261_19` 为 0% 微小仍漏 12 人的真缺陷，建议优先人工复核）。由 `scripts/analyze_images500_tiny_gt.py` 一键复现。

## 2. 视频增强（111 条真实 Mixkit 视频）

运行命令：
```
python scripts/rerun_all_videos.py
```

- 逐条运行 `crowd_test_enhanced.py`（Kalman + 谱特征 + FSM 增强路径，谱特征已接入评分 w4=0.1），输出时间线 JSON
- 汇总：`results/rerun/video_enhanced/_summary.json`（每条视频状态/耗时/时间线路径）、`_aggregate.json`（聚合统计）
- **增强结果（111/111 条成功，45021 帧）**：
  - 等级分布：**SAFE 26885（59.7%）/ WARNING 6388（14.2%）/ DANGER 11748（26.1%）**
  - 平均每条等级切换 **5.86 次**（中位数 4.0，FSM 滞回稳定）
  - 出现 DANGER 视频 **66 条**；全程输出 SAFE（系统未报警）31/111 条（无 GT，不称“零误报”）
  - 密集场景示例：mixkit_4519 DANGER 598/603 帧（99.2%）、mixkit_3428 451/456 帧（98.9%），切换均仅 2 次
  - 谱特征相关性：Fiedler 与风险等级 **Spearman ρ = 0.688**（45021 帧全量），均值随等级单调上升 0.017 → 0.214 → 1.078
- **基线对比（算法前逐帧规则判定，AND 规则与真实引擎一致，2026-10-27 全量重跑）**：
  - 基线：平均切换 **23.2 次/视频**（总 2578）、DANGER 帧 2190、24 条出现 DANGER、63 条全程 SAFE
  - 增强：平均切换 **5.86 次/视频**（总 650，↓74.8%）、DANGER 帧 11748、66 条出现 DANGER、31 条全程 SAFE
  - 说明：增强后 DANGER 帧/视频数上升，是因为 FSM 评分含密度权重 + 谱特征修正且带保持机制（比逐帧规则更敏感、更稳定），
    **不是**“增强后更保守”。逐视频对比见 `_comparison_summary.json`，汇总脚本 `scripts/regenerate_comparison.py`。

## 复现
- `scripts/rerun_all_videos.py`：全量视频批量脚本（加载一次模型循环 111 条）
- 数据集：`dataset/images500/`（500 图）、`dataset/videos50/`（111 视频，ffprobe+HOG 双重校验）
- 权重：`yolov8n.pt`（COCO 80 类预训练，真实推理）