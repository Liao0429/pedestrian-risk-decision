# Agent 4 数据集与实验设计审查报告

- 审查对象：`H:/PedestrianRiskDecision`（数据集 + 实验 + 指标）
- 审查日期：2026-10-26
- 审查方式：只读仓库文件与结果 JSON；用 `prd_venv` 编写核对脚本重算聚合数字（未重跑 YOLO 推理）
- 结论速览：**图片数据（500 张）与 GT 人数完全可复现、可核对；视频数据的全部"效果结论"都建立在系统自身输出之上，没有风险真值**；README 中可复现的主数字（82.5%、79.4%、45021 帧、4.00 次、40/31 条）均能由存档 JSON 重算，但存在 1 个不公平基线、1 份过期总结文件、1 个无支撑的谱特征结论。

---

## 1. 图片数据：来源、GT、质量、可重复性

### 1.1 来源与构成（可信）
- `dataset/images500/` 共 500 张：450 张 val2017 + 50 张 train2017，全部来自 COCO 2017 官方 CDN。
- 分布（按文件名后缀 `_N` 统计，并用官方标签核对）：
  - 1–3 人：200；4–9 人：150；10–13 人：138；14–19 人：12。
- 与 `dataset/images500/README.md` 声称的 200/150/138/12 完全一致。

### 1.2 GT 人数标注可复现（已逐张核对）
- 用 `dataset/_work/coco2017labels.zip`（Ultralytics YOLO 标签）对 500 个文件名逐一比对：
  - **500/500 文件名声明的行人数 = COCO 标签中 class 0 框数，0 处不一致**。
- 复现脚本齐全：`_work/select_person_images.py`（val 分桶）、`scan_train2017_dense.py`（train 密集）、`download_500_images.py`（seed=123 并发下载）、`download500.log` 存在。
- 说明：`selected_person_images.csv` 是全部 val 行人图（2693 行），不是 500 选样清单；真正决定 500 张的是 `download_500_images.py`（450 val + 50 train top-dense）。该脚本注释写"train 14–19 人"，实际 train 前 50 含 38 张 10–13 人图（train2017 中 ≥14 人只有 12 张），因此最终桶分布为 138/12。**最终数据与 README 一致，但脚本注释有误导**（低严重度）。

### 1.3 质量与可重复性
- JPEG 有效性：images500 README 声明全部 >1KB 且头有效；下载日志无失败。仓库未提供 images500 的 manifest CSV（`images_manifest.csv` 只覆盖最早的 30 张）。
- **文档口径不一致**：`dataset/README.md` 与 `images_manifest.csv` 只描述 30 张图片（早期子集），而项目 README 声称 500 张；且 30 张中有 18 张与 images500 重复。若把两者当作独立测试集使用会重复计数。
- 无像素级人工复核（dataset/README 已诚实说明视觉模型不支持图像输入）。

### 1.4 人数标注能否作为风险判断真值？——不能
- GT 只有"人数"，没有"聚集/风险"标注。`dataset/README.md` 按人数分档（1–3 SAFE、4–9 WARNING、10–19 DANGER）只是建议标签。
- **该分档与系统真实引擎定义冲突**：`crowd_test.py`/`config/default_config.json` 的风险规则是 `n>=10 且 (density>=0.55 或 r_gcc>=0.8) → DANGER`、`n>=5 且 (density>=0.28 或 r_gcc>=0.55) → WARNING`，即**必须同时满足人数与空间聚集条件**。一张 10 人但彼此分散的图按人数分档是 DANGER，按引擎规则是 SAFE。
- 因此：人数 GT 只能用来核对检测器"人数检出比"，**不能作为风险等级真值**来评估风险判断准确性。

---

## 2. 视频数据：真值、泄漏、独立性

### 2.1 没有风险真值
- 111 条 Mixkit 视频（`dataset/videos50/README.md` 有逐条来源、分辨率、时长；ffprobe 111/111、HOG 抽样 111/111 校验通过）。
- **全部 111 条没有任何逐帧风险等级 GT**。`results/rerun/video_enhanced/*_timeline.json` 只是系统自身输出。
- README 的"31 条未触发风险报警（含少量低照度场景检测器漏检，已用亮度+检测人数双重核对）"：`find_sparse_video.py` 用亮度>80 + 平均检测<0.5人/帧作为"真稀疏"代理。**这是启发式代理，不是真值**——亮度高不等于无风险，检测人少可能是漏检。且该脚本的核对结果（`avg_brightness/avg_detected`）没有任何存档 JSON，无法复核当时的具体输出。
- 结论：所有"零误报/未触发/稳定"类表述**没有外部对照**，只能说明"系统内部一致"，不能证明"判断正确"。

### 2.2 相邻帧相关性与评估偏差
- 视频按 25–30fps 逐帧处理，相邻帧高度相关；增强路径还有 Kalman 平滑 + FSM 滞回/保持（`confirm_up=3`、`confirm_down=5`、DANGER 保持 10 帧/1000ms），输出等级本身就是时序平滑结果。
- 因此**逐帧等级分布（如 SAFE 25669 / WARNING 11545 / DANGER 7807）不是 45021 个独立样本**。用它做比例、卡方或"准确率"会严重高估统计功效。
- 正确做法：以**视频为单位**统计（40/111 条出现 DANGER、31/111 条全程 SAFE、每条切换次数），对 111 条视频做配对检验（如 Wilcoxon 符号秩）；帧级分布只作描述性展示。

---

## 3. 指标定义核对

| 指标 | 定义 | 是否可由数据支撑 | 备注 |
|---|---|---|---|
| 检出率 82.5% | `det_total/gt_total = 2548/3089`（人数检出比） | ✅ 可复现 | 重算 0.8249；但**无 IoU 匹配、未扣误检**，是"人数级召回"，不是目标级 recall，更不能叫"风险检出率" |
| 高密度检出率 79.4% | `gt>=14` 的 12 张图：`hd_det/hd_gt` | ✅ 可复现 | 重算 0.7943；样本仅 12 张，置信区间极宽 |
| 等级分布（图） | SAFE 414 / WARNING 61 / DANGER 25 | ✅ 可复现 | 只是系统输出分布，**不是准确率** |
| 等级切换次数 | 相邻帧等级变化次数/视频 | ✅ 定义清晰 | 是稳定性指标，不是正确性指标；FSM 设计上必然降低切换 |
| 误报率/漏报率 | — | ❌ README 未报告（正确） | 无 GT 不可计算；若论文/README.en 出现必须删除 |
| "未触发风险报警 31 条" | 系统输出全 SAFE | ⚠️ 可复现但语义夸大 | 无 GT，不能称"零误报" |

结论：README 当前没有直接使用"误报率/漏报率/准确率"这类不能由数据支撑的指标，这一点已修正；但仍存在**用稳定性偷换准确性**的倾向（详见 §5）。

---

## 4. 消融实验是否支持 README 结论

用 `results/rerun/_ablation.json` 重算（6 视频 × 4 配置）：

| 配置 | 切换次数合计 | 平均/视频 |
|---|---|---|
| A 基线（无增强，AND 规则） | 211 | 35.17 |
| B +Kalman | 70 | 11.67 |
| C +Kalman+谱特征 | 70 | 11.67 |
| D 完整（+FSM） | 24 | 4.00 |

- **"Kalman 切换↓67%（35.2→11.7）"**：✅ 数字可复现（66.8%）。但注意这是**6 条人工挑选视频**上的结果，不是全量结论（见 §5）。
- **"FSM→4.0"**：✅ 可复现（D 平均 4.00）。有趣的是全量 111 条增强后平均也是 4.00（444/111）。
- **"谱特征提升风险语义准确性"**：❌ **无任何支撑，且被自己的消融数据否定**：
  - 6/6 条视频 B 与 C 的切换次数、等级分布**逐项完全相同**（`_ablation.json`：mixkit_134/13192/3428/4519/4178/50134 全部 B==C）。
  - 原因：`risk_fsm.py:_compute_score` 的评分只用 `n/density/r_gcc/trend`；`crowd_test_enhanced.py:120` 调用 `fsm.update()` 时也只传这三个量。**谱特征从未进入 FSM 决策**，只写入显示与 timeline 的 `fiedler` 字段。
  - README 当前措辞已改为"谱特征作为图连通性指标参与计算与显示（收益评估见消融数据）"——但消融数据恰恰显示**收益为零**，因此"收益评估见消融数据"仍属误导，应如实写"消融显示谱特征未进入决策、对输出无影响"。

---

## 5. 不公平基线 / 挑选 / 缺对照

### 5.1 【高】全量前后对比用了不公平基线（OR 规则）
- `scripts/rerun_video_baseline.py:26-30` 的基线规则是：
  ```python
  if n >= d["n"] or rho >= d["rho"] or gcc >= d["gcc"]: return "DANGER"
  if n >= w["n"] or rho >= w["rho"] or gcc >= w["gcc"]: return "WARNING"
  ```
  即 **n>=10 单独成立就 DANGER、n>=5 单独成立就 WARNING**。
- 而真实引擎 `crowd_test.py:evaluate_level`（也是消融脚本 `ablation_study.py:rule_level` 用的规则）是 **AND**：`n>=10 且 (rho>=0.55 或 gcc>=0.8)`。
- 后果：基线被显著恶化——全量基线 `base_sw=5147`（平均 46.37 次/视频）、DANGER 帧 21969、102 条视频出现 DANGER；增强后 444 次、DANGER 帧 7807、40 条。README"算法前后对比"（`_comparison_summary.json` 和 `fig3_before_after.png`）全部基于这个 OR 基线，**夸大了增强收益**。
- 另：README 称 mixkit_13192"逐帧规则基线为 46 次/视频"，但存档基线 JSON 中该视频 baseline `switches=18`（`_comparison_summary.json` 与 `video_baseline/mixkit_13192` 一致）。**"46"是 111 条平均，不是该视频的基线**——数字对不上。

### 5.2 【中】消融视频是事后挑选的子集
- `ablation_study.py:VIDEOS` 固定 6 条，无选择标准说明。其中 `mixkit_4178` 基线切换 154 次（全量平均仅 46），把这条拉进来使"67% 降幅"看起来很大。
- 67% 只代表这 6 条；全量（OR 基线）降幅 91.4%，但基线不公平，两者都不能作为严谨结论。
- 消融缺少 **FSM-only（无 Kalman）** 臂，Kalman 与 FSM 的贡献未完全分离（D vs B 只能给出"给定 Kalman 时 FSM 的增量"）。

### 5.3 【高】缺外部对照
- 无人工 GT、无与标准方法（如按官方人数密度标签）的对照、无检测器独立评价。所有"风险语义准确性/稳定/误报"结论都缺对照组。

---

## 6. experiment_summary.json 数值核对（venv 重算）

用 `prd_venv` 写脚本从存档 JSON 重算：

| 项目 | experiment_summary.json | 由 results/rerun 重算 | 一致？ |
|---|---|---|---|
| 图片基线 total/processed/failed | 500/500/0 | 500/500/0 | ✅ |
| 等级分布 | SAFE 414/WARNING 61/DANGER 25 | 同 | ✅ |
| gt_total/det_total/det_rate | 3089/2548/0.825 | 3089/2548/0.8249 | ✅ |
| 视频总帧数 | — | 45021（111 条 timeline 重算） | ✅（与 _aggregate.json 一致） |
| 增强等级分布 | — | SAFE 25669/WARNING 11545/DANGER 7807 | ✅ |
| 平均切换/视频 | — | 444/111=4.00 | ✅ |
| DANGER 视频数 / 全 SAFE 视频数 | — | 40 / 31 | ✅ |
| 基线重算 | — | frames 45021 / sw 5147 / DANGER 帧 21969 / 102 条 DANGER | ✅（与 _comparison_summary 一致） |
| **timelines（5 条）** | 13192: SAFE 1/WARN 40/DANG 264, sw 4 | rerun: SAFE 6/WARN 120/DANG 179, sw 4 | ❌ 不一致 |
| | 134: SAFE 598/WARN 2, sw 2 | rerun: SAFE 259/WARN 101/DANG 240, sw 10 | ❌ 不一致 |
| | 14084: WARN 166/SAFE 87/DANG 2, sw 30 | rerun: SAFE 85/WARN 170, sw 11 | ❌ 不一致 |
| | 151: SAFE 336, sw 0 | rerun: SAFE 287/WARN 49, sw 2 | ❌ 不一致 |
| | 13265: SAFE 281, sw 0 | 同 | ✅ |

- 结论：`experiment_summary.json` 的图片基线部分**完全可由脚本重算**；但其 `timelines` 是**过期结果**（与早期 `results/video_enhanced/` 输出一致，如 13192 的 SAFE 1/WARN 40/DANG 264），与 README 引用的 `results/rerun/` 不一致。仓库里两份"官方总结"打架，必须二选一或标注版本。
- 注意 `crowd_test.py` 本身不产出 `det_rate/gt_total`，这些由 `plot_results.py` 从文件名后缀计算——逻辑与重算一致，可复现。

---

## 7. 低成本人工标注方案与可执行实验设计

### 7.1 原则
- **只在有真值后计算**：混淆矩阵、各类别召回、误报率、漏报率、预警延迟。
- 无真值时，只报告"系统输出分布、切换次数"等描述性指标，并注明"无 GT，不代表准确性"。

### 7.2 图片风险 GT（低成本）
1. **分层抽样**：从 images500 按人数桶抽 100 张（1–3 人 40、4–9 人 30、10–13 人 20、14–19 人 10）。
2. **标注规范**（避免用人数当 GT）：标注员按"是否形成明显聚集簇/间距/人数"打 SAFE/WARNING/DANGER，附图示 rubic；2 名标注员独立标注 + 第 3 人仲裁，计算 Cohen's κ（预期 0.6–0.8）。
3. **评估指标**：3×3 混淆矩阵、每类召回/精确率、与"按人数分档"的对比（量化人数 GT 与风险 GT 的差异）。
4. **预算**：100 张 × 2 人 × ~1 分钟 ≈ 3.5 人时；若只做 50 张也可出初步结果但置信区间宽。

### 7.3 视频风险 GT 与延迟（低成本）
1. **关键帧抽样**：111 条 × 每 2 秒 1 帧 ≈ 1000–1500 帧；从 40 条 DANGER 视频全抽、31 条全 SAFE 抽 1/4、其余 40 条抽 1/4，保证三类均衡。
2. **事件段标注**：对每条视频标注"聚集开始帧/疏散开始帧"（用于延迟），并对抽样帧标风险等级。
3. **评估指标**：
   - 帧级 3×3 混淆矩阵（抽样帧上）；
   - 每类召回率 = 该类真值帧中被系统正确判定的比例；误报率 = 真值 SAFE 但系统报警（WARNING/DANGER）的帧比例；漏报率 = 真值 WARNING/DANGER 但系统输出 SAFE 的帧比例；
   - **预警延迟** = 系统首次进入目标等级的时刻 − 真值事件起始时刻（帧差换算秒），按视频汇总中位数；
   - 视频级多数投票后再算 per-video 准确率。
4. **统计**：以视频为样本做配对检验，不用逐帧卡方（相邻帧不独立）。

### 7.4 算法/实验修正（低成本、高优先级）
- **E1 公平基线**：把 `rerun_video_baseline.py:rule_level` 改为与引擎一致的 AND 规则，重跑 111 条基线，更新 `_comparison_summary.json`、README、fig3。这是最优先的修正。
- **E2 消融补组**：增加"FSM-only（无 Kalman）"臂；若保留谱特征，先把谱特征真正传入 `fsm.update()` 并解决 agent3 指出的 Fiedler 跨规模不可比问题，再重跑消融；否则删除 C 组与"收益评估"表述。
- **E3 数字统一**：删除/归档 `experiment_summary.json` 的过期 timelines，统一以 `results/rerun/` 为准；修正 README 中 mixkit_13192"基线 46 次/视频"→"18 次（存档）或注明为全量平均"。
- **E4 文档补齐**：补 images500 manifest CSV；在 README 中标注 dataset/README 的 30 张为早期子集，避免与 500 张混淆。
- **E5 存档核对输出**：把 `find_sparse_video.py` 的亮度/检测人数结果存成 JSON 放入 `results/rerun/`。

---

## 附：Top Findings（severity/issue/evidence）

1. **【高】视频无风险真值，"零误报/未触发"结论无外部对照**
   - Evidence：`results/rerun/video_enhanced/*_timeline.json` 全是系统自身输出；无任何 GT 字段；`find_sparse_video.py` 用亮度+检测人数作代理；仓库无 `avg_brightness` 存档。
2. **【高】全量对比基线不公平（OR 规则 vs 引擎 AND 规则）**
   - Evidence：`rerun_video_baseline.py:26-30`（`n>=10 or rho...`）与 `crowd_test.py:244-247`（`n>=10 and (...)`）不一致；`_comparison_summary.json`：base_sw=5147、base DANGER 帧 21969，全部基于 OR 基线。
3. **【高】谱特征对决策零贡献，"提升风险语义准确性"无支撑**
   - Evidence：`_ablation.json` 6/6 视频 B==C（切换、分布逐项相同）；`risk_fsm.py:_compute_score` 无谱项；`crowd_test_enhanced.py:120` 只传 n/density/r_gcc。
4. **【高】README 对 mixkit_13192 的基线数字错误**
   - Evidence：README 称"逐帧规则基线 46 次/视频"；存档 `video_baseline/mixkit_13192/mixkit_13192_baseline.json` 与 `_comparison_summary.json` 均为 **18**；46 是 111 条平均。
5. **【中】experiment_summary.json 时间线与 rerun 冲突（过期）**
   - Evidence：13192（SAFE 1/WARN 40/DANG 264 vs 6/120/179）、134（SAFE 598 vs 259/101/240）、14084、151 均不一致；只有 13265 一致。
6. **【中】消融 6 条视频为人工挑选子集，且缺 FSM-only 臂**
   - Evidence：`ablation_study.py:VIDEOS` 无选择标准；mixkit_4178 基线 154 次（全量均值 46）；组合只有 A/B/C/D，无"仅 FSM"。
7. **【中】人数 GT 不能当风险真值**
   - Evidence：images500 只有人数标签；dataset/README 按人数分档（4–9 WARNING/10–19 DANGER）与引擎空间规则（n AND density/gcc）冲突；30 张子集与 images500 重叠 18 张。
8. **【中】逐帧统计独立性不成立**
   - Evidence：45021 帧来自 111 条 25–30fps 视频，且经过 Kalman+FSM 滞回（confirm_up=3/down=5/hold 10 帧）平滑；帧级比例不可当作独立样本。
9. **【低】检出率是人数检出比，非风险检出率**
   - Evidence：det_total/gt_total=2548/3089 无 IoU 匹配、未扣误检；高密度 79.4% 仅 12 张图；`plot_results.py:27-31` 从文件名取 GT。
10. **【低】文档/脚本不一致**
    - Evidence：dataset/README 只写 30 张而项目 README 写 500 张；images500 无 manifest；download_500_images.py 注释"train 14–19 人"与实际（38 张 10–13 人）不符；README.en 无实验章节。

（复核脚本：`C:/Users/Administrator/verify_agent4.py`、`verify_gt.py`，仅读数据未修改仓库。）