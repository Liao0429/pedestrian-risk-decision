# 交叉质疑 B 仲裁报告：论文表述与数据采信

> 仲裁员：交叉复核 README/README.en、docs/algorithms.md、agent4/6/8 报告与全部存档 JSON（venv 实测重算）。日期：2026-10-27。

## 一、"用稳定性代替准确性"：批评部分成立，需限定

- 成立面：切换次数下降（5147→444）≠"判断更准"。FSM 确认帧（3）+ DANGER 保持（10帧/1000ms）设计上必然降切换；DANGER 帧-64%、报警视频 102→40 也可能是保守化/漏报。当准确率证据讲会被打"偷换概念"。
- 不成立面：无 GT 时切换次数是**唯一可诚实报告的客观指标**，作为"稳定性/用户体验"指标合法；问题只在表述，不在指标本身。
- 必改表述：① 每次出现切换次数加限定："无风险真值（GT），仅表征时序稳定性，不代表判断准确率"；② 删一切暗示正确性的词（"零误报""未触发风险报警"）；③ 以视频为单位做配对检验（Wilcoxon，111 条）；帧级分布仅作描述性展示；④ 可选低成本 GT：关键帧抽样人工标注三档 + 混淆矩阵/F1。

## 二、高密度检出率：唯一可信口径 79.4%（12 张），76.7%（9 张）删除

- 实测：`results/rerun/images500_baseline/results.json` 与 `results/images500_baseline/results.json`（两批 500 张）逐图一致：gt=3089/det=2548，总体 82.5%；**gt≥14 共 12 张（gt=175/det=139）→ 79.4%（0.7943）**。
- **76.7%（9 张）在仓库任何存档 JSON 均无法复现**（老 30 张批 gt≥14 为 10 张/80.3%），是孤儿数字，仅存于 `results/rerun/README.md`。
- **唯一口径**：统一 **79.4%（12 张）**，并注明"样本仅 12 张，统计功效弱，仅作参考"。整改 `results/rerun/README.md` 第 20 行。

## 三、mixkit_13192 基线 46 vs 18：46 是全量平均，且 18 也已过期

- **46** = 旧 OR 基线 5147/111 = 46.37（`_comparison_summary.json`），是 111 条全量平均，README 误挂到单条视频。
- **18** = 旧口径（`_comparison_summary` OR 与 `_ablation.json` AND 均为 18）。
- **AND 规则重跑后已变**：`results/rerun/video_baseline/_summary.json`（2026-10-27 00:13，111 条，AND 规则）中 mixkit_13192 基线 = **48 次**（SAFE 90/WARNING 53/DANGER 162），全量平均 23.2 次/视频。18 已过时。
- **README 正确引用**：不再写"该视频基线 46 次"。先以最新 AND 基线重生成 `_comparison_summary.json`/fig3，再统一引用"全量基线平均 23.2 次/视频（AND 规则，results/rerun/video_baseline/_summary.json）"；若引单条视频写 48，并注明规则与存档路径。

## 四、"31 条未触发风险报警"：无 GT 下不可说

- "未触发**风险**报警"隐含正确性，无 GT 不成立。
- **合规表述**："31/111 条视频在增强路径下全程输出 SAFE（系统未产生报警）；无风险真值，无法判定是否漏报。"
- 严禁"零误报"（`results/rerun/README.md` 仍残留，须删）；`find_sparse_video.py` 的亮度/检测人数核对结果须存档 JSON（现无存档）。

## 五、agent8 建议排序（成本-收益）

- **P0-1 真机 DANGER 触发视频**（1~2h）：唯一端到端"真人入镜→风险触发"证据；现日志全 n=0 SAFE，现场最大硬伤。
- **P0-2 风险-时间曲线图**（半天）：timeline JSON 已齐，最便宜，直接展示 FSM 滞回。
- **P0-3 IPM-BEV 俯视图 + 网络拓扑图**（0.5~1天）：IPM/图论两大卖点唯一视觉证明。
- **P1**：网页观看截图 + top/ss 监控截图（0.5h）。可选：视频烧字幕、谱特征曲线（不得宣称谱特征降切换，消融 B==C 贡献为零）。
- **现场故障预案最重要的 3 条**：① 真机/RTSP 失败→立即切预录 `mixkit_13192_enhanced.mp4` 兜底；② 无人经过→平板播放人群视频放镜头前；③ 网络不通→网线直连（192.168.0.136）或全程离线 PPT+预录视频。

## 六、论文必须改的清单（仲裁认定成立）

1. **谱特征未进决策**：`enhanced_decision.cc:162-169` 的 FsmFeatures 只填 n/density/r_gcc/clustering/trend，谱特征仅 printf 显示；`risk_fsm.py` 评分无谱项；消融 C 组死代码（B==C 逐位相同）。**删除"谱特征提升风险语义准确性/参与增强决策"表述**，改为"谱特征作为可视化辅助指标输出"。
2. **docs/algorithms.md 9.2 数据错误**：实测 WARNING 评分 0.515~0.660（文档写 0.62~0.66 错）；1 个 SAFE 样本（3 人紧挨，rho=1/gcc=1）评分 0.660≥up_danger=0.65，边界不可分。修正区间、公开 30 张标定集与敏感性分析。
3. **基线口径统一**：`rerun_video_baseline.py` 已改 AND，但 `_comparison_summary.json`/fig3/README 仍是 OR 结果，须用 `video_baseline/_summary.json` 重生成。
4. **README.en 过期**：13192"264/305 帧 DANGER"来自过期 `experiment_summary.json`，与 rerun（179/305）冲突；README.en 无实验章节，需补齐对齐。
5. **`experiment_summary.json` timelines 过期**（4/5 条与 rerun 不一致），标注版本或删除，统一以 results/rerun 为准。