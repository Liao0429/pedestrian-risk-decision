# 实验与标注方案（EXPERIMENT_PLAN）

> 生成：2026-10-26 · 依据 Agent4 实验审查 + Agent7 QA 建议

## 一、风险真值（GT）标注方案（低成本）

### 图片（500 张，约 2 小时）
- 标注内容：**只标整体风险等级**（不标框）：`SAFE / WARNING / DANGER`。
- 依据：图片当前仅有"人数桶（分散/聚集/拥挤）"，人数≠风险真值。
- 输出：`dataset/images500/risk_gt.csv`（`filename,risk_level,annotator`）。
- 双人独立标注 + 仲裁，计算 Cohen's Kappa（>0.6 视为可用）。

### 视频（111 条，抽帧约 3000 帧，12-18 小时）
- 每 2 秒抽 1 帧，逐帧标 `SAFE/WARNING/DANGER`。
- 输出：`results/risk_gt/*_risk_gt.json`（`{frame_idx, risk_level}`）。
- 若时间不足：抽 10-20 条代表性视频（含密集/稀疏/低照度/普通）标注。

## 二、评估指标（仅在有 GT 后计算）

| 指标 | 定义 |
|---|---|
| 3×3 风险混淆矩阵 | GT 等级 × 系统输出等级 |
| 加权/macro F1 | 各等级 F1 的加权平均 |
| 每等级召回率 | 该等级 GT 帧被正确输出的比例 |
| 误报率 | SAFE GT 帧被输出为 WARNING/DANGER 的比例 |
| 漏报率 | DANGER GT 帧被输出为 SAFE/WARNING 的比例 |
| 预警延迟 | 风险起始帧 → 首次输出对应等级帧的时间（秒） |

**注意**：切换次数只作稳定性描述，不作正确性指标（Agent4/6）。

## 三、消融实验修正（P0）

- 基线/消融 A 组：统一用 `evaluate_level`（AND 规则，与 `risk_engine.cc` 一致），**不用 OR 规则**（`ablation_study.py:33` 当前用 OR）。
- 逐组件消融：A 基线 → B +Kalman → C +Kalman+谱特征 → D 完整增强（FSM）。
- 预期问题已证实：C 组=B 组（谱特征未进 FSM）。已用 `spectral_fsm_experiment.py` 做接入实验裁决：M1 正反馈过度敏感、M2 抑制是整体阈值偏移（被降级帧 fiedler 中位数 0.83 ≥ 保留帧 0.69），非语义增益 → **不采纳接入**。消融结果按“未接入 + 实验证据”如实呈现，无需重跑。
- 样本量：6 条视频 → 扩展至至少 20 条代表性视频（Agent4 建议 111 条全量重跑成本低，优先全量）。

## 四、数据独立性 / 偏差控制

- **帧级样本非独立**：45021 帧相邻帧强相关。指标**按视频聚合**（每视频一个等级分布/切换数），再对视频集合做统计（均值 ± 标准差或 bootstrap 置信区间），避免把帧当独立样本（Agent4）。
- **训练分布泄漏**：500 图含 50 张 COCO train2017（YOLOv8n 训练集），报告需标注"含训练集图片"或剔除后重算。
- **不公平基线**：基线用 OR、增强用 FSM 评分——**修正为同规则**后再报告"切换次数 ↓x%"。

## 五、可复现性

1. 所有脚本改为**基于 PROJECT_ROOT 的相对路径**（`ablation_study.py`/`find_sparse_video.py`/`plot_results.py` 当前硬编码 `H:/...`）。
2. 新增结果 schema 校验：`results/**` 的 timeline/JSON 字段齐全性测试。
3. 消融一致性测试：断言 `ablation_study.rule_level == crowd_test.evaluate_level`、配置键为 `fsm`。

## 六、新增测试用例清单（Agent7 汇总）

- `tests/test_ablation_consistency.py`：OR/AND 一致、配置键 `fsm`、1 帧假视频跑通 4 组消融。
- `tests/test_config_loading.py`：`temporal`→`fsm` 键、缺省回退、非法配置抛异常。
- `tests/test_golden_independent.py`：numpy 手算谱特征期望与 golden JSON 比对。
- `tests/test_risk_gt.py`：GT CSV/JSON schema 校验。
- C++ 补测：单人 `r_gcc=0.0`、DANGER 恰好达标边界、OpenCV 分支。
- 性能基准：Python/C++ 双侧耗时断言（防止算法改慢）。


---

## 七、基线修正已完成（2026-10-27）

- `scripts/rerun_video_baseline.py` 已改 AND 规则并全量重跑 111 条视频（45021 帧）：
  - 基线：平均切换 **23.2 次/视频**、DANGER 帧 **2190**、24 条出现 DANGER、63 条全程 SAFE。
  - 增强：平均切换 **4.00 次/视频**、DANGER 帧 7807、40 条出现 DANGER、31 条全程 SAFE。
- `results/rerun/_comparison_summary.json` 已重生成；`scripts/regenerate_comparison.py` 可复现。
- **注意**：公平基线下“增强后 DANGER 帧/视频数上升”是 FSM 评分（含密度权重 + 保持机制）更敏感所致，
  论文不得再写“增强后 DANGER 下降”。
- 新增场景判别实验（`scripts/scenario_test.py`）作为“空间分布→风险等级”判别有效性的可复现证据；
  链状连通（1.5m 串排）在规则引擎下 r_gcc=1 判 DANGER 的局限已在结果中如实记录。
