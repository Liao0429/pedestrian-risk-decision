# 数据重跑记录（2026-11-01）

## 为什么重跑

仓库里 10-26 生成的全部实验结果，使用的是 `scripts/crowd_test.py` 的**脚本默认** `--conf 0.25`；
而板端运行时读取的是 `config/default_config.json` 里的 `conf_threshold = 0.35`。
**报告口径 ≠ 部署口径**，导致 README / 论文中的检出率比板端真实表现高 16.6 个百分点。

本次用 10-31 最新算法代码 + 板端一致的 `conf=0.35` 全量重跑全部素材。

## 重跑范围与产物

| 环节 | 命令 | 产物 |
|---|---|---|
| 500 张图片检测 | `crowd_test.py --input dataset/images500 --conf 0.35` | `results/rerun/images500_baseline/results.json` + `vis/` |
| 图片 GT 评估 | `evaluate_images500_gt.py` | `results/images500_gt_eval/{summary.json,report.md}` |
| 111 条视频增强 | `rerun_all_videos.py`（已改为从配置读 conf） | `results/rerun/video_enhanced/*/` |
| 111 条视频基线 | `rerun_video_baseline.py`（同上） | `results/rerun/video_baseline/*/` |
| 时间线聚合 | `aggregate_timelines.py` | `results/rerun/_aggregate_recomputed.json` → `_aggregate.json` |
| 增强/基线对比 | `regenerate_comparison.py` | `results/rerun/_comparison_summary.json` |
| 消融实验 | `ablation_study.py`（同上） | `results/rerun/_ablation.json` + `docs/figures/fig_ablation.png` |
| 图表重制 | `docs/figures/_style/make_figures.py` | `docs/figures/*.png`（14 张） |

运行环境：`G:\py311_gpu\Scripts\python.exe`（torch 2.5.1+cu121 / ultralytics 8.4.188 / GTX 1080 Ti）。

## 口径差异对比

### 图片基线（500 张，GT 3089 人）

| 指标 | 旧（conf=0.25） | 新（conf=0.35） |
|---|---|---|
| 检出框总数 | 2548 | 2034 |
| 总体检出率 | 82.49 % | **65.85 %** |
| 高密度（≥14 人）检出率 | 79.43 % | **49.71 %** |
| MAE / RMSE | 1.926 / 3.188 | **2.330 / 3.733** |
| Pearson r | 0.8022 | **0.7588** |
| 等级分布 | SAFE 414 / WARNING 61 / DANGER 25 | **SAFE 425 / WARNING 64 / DANGER 11** |

分桶检出率：1–3 人 94.46 % → **83.38 %**；4–9 人 73.85 % → **62.61 %**；
10–13 人 84.92 % → **65.81 %**；14–19 人 79.43 % → **49.71 %**。

### 视频全量（111 条 / 45021 帧）

| 指标 | 旧（conf=0.25） | 新（conf=0.35） |
|---|---|---|
| SAFE / WARNING / DANGER 帧 | 28214 / 8355 / 8452 | **30672 / 8789 / 5560** |
| 等级切换总次数 | 568 | **569** |
| 平均切换（中位数） | 5.12（4.0） | **5.13（2.0）** |
| 出现 DANGER 的视频 | 45 | **38** |
| 全程 SAFE 的视频 | 31 | **38** |
| Fiedler 均值 SAFE/WARNING/DANGER | 0.021 / 0.567 / 1.084 | **0.0225 / 0.709 / 1.0147** |
| Fiedler–等级 Spearman ρ | 0.692 | **0.729** |

增强 vs 基线（同 conf 公平对比）：切换总次数 **1874 → 569（↓69.6 %）**，平均 16.88 → 5.13；
DANGER 帧 1270 → 5560。

### 消融实验（6 条代表性视频）

| 配置 | 平均切换（旧 → 新） | DANGER 帧（旧 → 新） |
|---|---|---|
| A 基线 | 33.2 → **41.8** | 587 → **466** |
| B +Kalman | 8.3 → **10.5** | 687 → **608** |
| C +FSM | 4.3 → **4.3** | 1497 → **1237** |
| D 完整 | 4.3 → **4.3** | 1649 → **1360** |

## 一个必须记录的事实

用**同一套最新代码**、同样 `conf=0.25` 重跑 500 张图片，结果与 10-26 旧数据**逐位相同**
（82.49 % / 1.926 / 3.188 / 0.8022 / 检出 2548）。这说明 10-26 之后的算法改动
（NMS 下标修复、脚点去重、Jacobi 收敛、track-level counting 等）没有改变图片链路的最终统计，
**旧数字不是编造的，问题纯粹出在置信度阈值口径不一致**。

## 后续约束

- 任何离线实验脚本都必须从 `config/default_config.json` 读取 `conf_threshold`，不得硬编码。
- 论文 / README 引用实验结果时，须注明 conf 口径。
- `results/rerun/_aggregate.json` 由 `aggregate_timelines.py` 生成，`make_figures.py` 运行前后对其做快照保护，避免画图污染数据。
