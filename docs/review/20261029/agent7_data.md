# Agent7 实验数据专家审查报告（2026-10-29）

> 审查人：实验数据专家（只读审查，未修改任何项目数据文件；复现均在 scratch 目录 `agent7_recompute` 完成）
> 审查范围：`results/` 聚合文件、`docs/导读.md`、`README.md/README.en.md` 指标章节、`dataset/` 数据文件
> 说明：任务描述中提到的 `docs/results/` 目录不存在，结果文档实际位于 `results/rerun/README.md`、`results/images500_gt_eval/report.md`、`results/edge_board/EDGE_TEST_REPORT.md` 与 `docs/导读.md`，本报告按实际位置审查。

---

## 1. 数据可信度总表

| 数据/声明 | 文件存在 | 可重跑 | 复现结果 | 与 README 一致 |
|---|---|---|---|---|
| 500 张 COCO 图片（1~19 人，分散200/聚集150/较拥挤138/拥挤12） | ✅ 500 jpg 全部有效 | ✅ 独立校验 | 文件名人数与 COCO GT 500/500 一致、无冲突 | ✅ |
| 图片基线检出率 82.5%、高密度 79.4%、等级 SAFE 414/WARNING 61/DANGER 25 | ✅ summary.json | ✅ `evaluate_images500_gt.py` | **ALL_MATCH**（仅 generated_at/source_results 不同） | ✅ |
| 111 条视频 / 45021 帧 / SAFE 26885 / WARNING 6388 / DANGER 11748 | ✅ 111+111 时间线、_summary.json 111/111 ok | ✅ `plot_data_analysis.py` | **identical=True**（_aggregate.json 全字段一致） | ✅ |
| 平均切换 5.86 次、中位数 4.0、总 650 次 | ✅ _aggregate.json | ✅ 同上 | 5.8559 / 4.0 / 650 | ✅ |
| 66 条出现 DANGER、31 条全程 SAFE | ✅ | ✅ 同上 | 66 / 31 | ✅ |
| Fiedler 均值 SAFE 0.017 / WARNING 0.214 / DANGER 1.078 | ✅ | ✅ 由原始时间线重算 | 0.0174 / 0.2143 / 1.0779 | ✅ |
| Spearman ρ = 0.688（45021 帧） | ✅（仅图脚本计算） | ✅ `plot_professional_figures.py` | **0.6882 → 0.688** | ✅ |
| 前后对比：切换 23.2→5.9（↓74.8%）、基线 DANGER 2190→11748、24→66 条 | ✅ _comparison_summary.json | ✅ `regenerate_comparison.py` | **identical=True** | ✅ |
| 消融 A 33.2 / B 8.3 / C 4.3 / D 4.3，D 较 C DANGER +152（+10.2%） | ✅ _ablation.json | ⚠️ 需 GPU 重跑（>2min） | 算术复核一致；D 组与生产时间线逐帧一致 | ✅ |
| 密集场景 mixkit_13192：6/120/179、切换 4 | ✅ | ✅ | 与 aggregate 一致 | ✅ |
| 稀疏场景 mixkit_50134：388 帧全程 SAFE | ✅ | ✅ | 与 aggregate 一致 | ✅ |
| 真机 7.46 FPS、RSS ~10.5-10.9MB、28h+ 长稳 0 介入 0 错误 | ✅ 日志/快照存在 | ✅ 数据文件支持 | long_stability.jsonl 10 点：7.42~7.48 FPS、RSS 10800~11176KB、0 介入/0 错误 | ✅（RSS 口径见 §3-4） |
| 图片微小目标分析（GT 500/500、<20px 占 13.5%、r=-0.403） | ✅ tiny_gt_summary.json | ⚠️ `analyze_images500_tiny_gt.py` 单次重跑 >2min 超时 | GT 一致性 500/500 已独立验证；尺寸统计与相关系数未复现 | ✅（值来自提交产物） |

**总体结论：核心数字全部有据可查、口径统一、可重跑复现，未发现编造数据。** 主要问题集中在「历史生产数字无存档」与「离线实验与生产流水线结果并存但未解释差异」两点（见 §3）。

---

## 2. 消融组干净性评估（谱特征接入前后对比）

- **C（+FSM 无谱特征，w4=0）vs D（+谱特征，w4=0.1）：对比干净。** 同一 FSM 滞回参数、同一检测输入、同一批 6 条视频，唯一变量是谱修正项 w4，符合消融设计。
- **D 组与生产聚合完全一致**：6 条视频 D 组 dist/switches 与 `_aggregate.json` 中对应条目逐字段相同（已程序化核对），排除 D 组单独编造的可能。
- **A/B/C/D 增量逻辑正确**：A→B→C→D 逐级加组件，A 组（33.2 次）与 `_comparison_summary.json` 中 6 条视频的公平 AND 基线逐条一致，说明消融基线口径与前后对比基线一致（`_note` 也确认旧 OR 基线已弃用）。
- **谱特征作用的实质是 WARNING→DANGER 重分类**：D vs C 的 +152 DANGER 帧伴随 WARNING 减少 155 帧、SAFE 增加 3 帧（净平衡）。README 表述为「提升高风险识别」成立，但本质是「更多帧被判为 DANGER」而非「检测到更多行人」，建议论文表述精确化。
- **小瑕疵**：C/D 平均切换均为 4.3 次，谱特征对切换稳定性无增量贡献，README 已如实写「保持稳定」，未见夸大。

---

## 3. 口径不一致清单（按严重度排序）

1. **【高】`7807` 这一关键数字无存档、不可复现。** README 声称「111 条批量 DANGER 帧 7807 → 11748（谱特征提升高风险识别）」，但仓库内没有任何结果 JSON 含 7807（仅 `docs/工作日志.md` 提及）。当前可重跑的「无谱特征」口径是 `_spectral_fsm_experiment.json` 的 **M0 = 7906**，与 7807 相差 99 帧。审稿人无法从仓库重算出 7807。
2. **【高】离线实验与生产流水线结果并存但差异未解释。** `_spectral_fsm_experiment.json`（可完整复现）给出 M1_0.1：DANGER **12659** 帧、68 条视频；README/生产聚合给出 **11748 帧、66 条**。同是 w4=0.1，两者相差 911 帧。脚本自身打印的 M0 一致性仅为 **89.67%**（40372/45021 帧），说明「用时间线特征离线重算」与「生产实时推理」天然存在 ~10% 帧级偏差，但 README/结果文件均未记录这一差异，读者直接对比会误以为数据矛盾。
3. **【中】`docs/导读.md` 的引用不准确**：称 Spearman ρ=0.688 来自「`results/rerun/_aggregate.json` 与 `scripts/plot_professional_figures.py`」，但 `_aggregate.json` 中只有 `fiedler_by_level` 描述统计，**没有 Spearman 字段**，ρ 只由 plot 脚本计算。
4. **【中】真机 RSS 口径不一致**：`EDGE_TEST_REPORT.md` 写 RSS ≈12.5MB（ps 12824KB），`long_stability.jsonl` 为 proc_rss_kb 10800~11176（~10.5-10.9MB），README 采用后者。属测量方式不同（ps vs /proc），但未注明。
5. **【低】目录/文档命名历史遗留**：`dataset/videos50/` 实为 111 条视频（「50」为历史名）；`dataset/README.md` 只描述旧 30 张 `dataset/images/`，未提及 images500（images500 有独立 README）。不影响数字，但易造成「数据集多大」的口径混淆。
6. **【低】`coco2017labels.zip` 完整性受损**：32654 个 train2017 标签条目 CRC 校验失败（占 train2017 绝大多数）。images500 用到的 val2017 标签全部完好（已核对 500/500 无一条受影响），因此不损害现有结论，但下载/解压脚本的「可复现」声明应注明 zip 需重新下载。

---

## 4. 夸大/误导表述检查

- ✅ **如实披露**：31 条全程 SAFE 明确标注「无人工真值，无法判定是正确不报还是漏报」；增强后 DANGER 上升明确解释为 FSM 评分更敏感 + 保持机制，`_note` 还要求论文「严禁写成增强后 DANGER 下降」——方向正确。
- ⚠️ **需弱化/加注的表述**：
  - README「数据挖掘结论：Fiedler 与风险等级 Spearman ρ=0.688 显著正相关」——**fiedler 本身就是 FSM 评分输入（w4 项）**，等级是评分的函数，两者相关部分由构造决定（同源变量），且 45021 帧是时序非独立样本，ρ 的「显著性」易被高估。建议表述为「描述性相关：谱连通度与 FSM 输出等级存在正相关（输入特征与输出同源）」。
  - 「谱特征提升高风险识别 +152 帧」——实质是 WARNING→DANGER 重分类，建议写明。
  - README 末尾「所有实验数据…可复现」——过强：消融与 tiny GT 分析重跑 >2min（需 GPU/长时间），7807 无存档。建议改为「核心聚合/对比/图片 GT 评估均可用仓库脚本复现，标注了未复现项」。
  - `_spectral_fsm_experiment.json` 的 `M1_0.1 DANGER 帧 12659` 与 README 11748 并存——建议在文件或 README 加一行「离线重算与生产流水线存在 ~10% 帧级差异，README 采用生产口径」。

---

## 5. 可复现性结论（已验证 / 未验证）

**已验证（scratch 复跑，全部成功）：**
| 脚本 | 结果 |
|---|---|
| `scripts/evaluate_images500_gt.py` | **ALL_MATCH**（提交 summary.json 完整复现） |
| `scripts/plot_data_analysis.py` | **identical=True**（_aggregate.json 全字段复现） |
| `scripts/plot_professional_figures.py` | **Spearman 0.6882**（需 matplotlib） |
| `scripts/spectral_fsm_experiment.py` | **identical=True**（_spectral_fsm_experiment.json 全字段复现；打印 M0 一致性 89.24%/89.67%） |
| `scripts/regenerate_comparison.py` | **identical=True**（_comparison_summary.json 复现） |
| 数据集独立校验 | 500 张 JPEG 有效、GT 500/500 一致；111 视频 ffprobe/HOG 校验记录在案 |

**未验证（按要求跳过并标注原因）：**
| 脚本 | 原因 |
|---|---|
| `scripts/analyze_images500_tiny_gt.py` | 单次重跑 >180s 超时（逐图重复解压 zip），按「>2 分钟跳过」约定未验证；其核心结论「GT 一致性 500/500」已由本报告独立复核通过，尺寸分布/相关系数（13.5%、r=-0.403）未复现 |
| `scripts/ablation_study.py` | 需 GPU 重跑 6 视频 × 4 组合检测，远超 2 分钟；其 D 组已与生产时间线逐帧核对一致，C/D 增量算术已复核 |
| `scripts/rerun_all_videos.py` | 全量 111 视频重新推理，远超 2 分钟；其下游产物（时间线→_aggregate/_comparison）已全部复现，等于间接验证了时间线数据自洽 |

**总评**：数据真实性高——所有 README 核心指标均可由仓库内原始时间线/结果文件重算复现，未发现伪造、篡改或「未实测却写实测」的产物。

---

## 6. 最高价值问题（供后续追问/论文修改）

1. **7807 从哪来？** README 的关键提升数字「7807 → 11748」的前值无任何结果文件存档，离线重算 M0=7906 与之不符。能否补充该次生产运行的聚合 JSON，或在 README 标注「历史生产值，无存档，离线重算为 7906」？
2. **离线实验 vs 生产口径为什么差 911 帧？** `_spectral_fsm_experiment.json` M1_0.1=12659/68 条，README 生产=11748/66 条，M0 一致性仅 89.67%。是否应在 README/结果文件加一段「离线重算基于时间线回放+模拟 now_ms，与生产实时流水线存在 ~10% 帧级差异」的说明，避免审稿人复跑后质疑？
3. **Spearman ρ=0.688 的因果解读**：fiedler 是 FSM 评分输入（w4 项），ρ 是否只是「输入与输出同源」的构造性相关？论文中「数据挖掘结论/显著正相关」是否应弱化为「描述性相关」并补充独立验证（如仅用 fiedler 预测等级的消融/置换检验）？
4. **「+152 DANGER 帧」的语义**：谱特征贡献主要是 WARNING→DANGER 重分类（WARNING -155），论文是否应明确「重分类提升高风险判定灵敏度」，而非暗示检测能力提升？
5. **tiny_gt 分析为何 >2 分钟且 zip 有 3.2 万坏条目？** `analyze_images500_tiny_gt.py` 逐图重复解压 47MB zip，效率低；且 `coco2017labels.zip` 存在 32654 个 train2017 CRC 损坏条目（不影响 images500 的 val2017 标签）。是否应改用单次解压，并在 README 注明 zip 完整性？
6. **RSS 以哪个为准？** 真机内存 README 写 10.5-10.9MB，`EDGE_TEST_REPORT.md` 写 ~12.5MB，是否统一为一种测量口径（/proc vs ps）并注明？
7. **目录名误导风险**：`dataset/videos50` 装 111 条、`dataset/README.md` 只写 30 张旧图——外部审稿人按目录名/旧 README 会误判数据集规模，是否应在根 README 或目录重命名（videos111）消除歧义？
8. **「全部可复现」声明是否过强**：README 底部「所有实验数据均来自真实数据集，可复现」——消融、tiny GT、全量重跑均需 GPU/长时，且 7807 无存档。是否改为「核心结果可复现 + 复现清单（含耗时/依赖）」更稳妥？