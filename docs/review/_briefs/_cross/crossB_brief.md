# 交叉质疑 B：论文表述与数据采信仲裁 — 任务书

## 角色
你是答辩风险仲裁员。阅读以下报告与数据，仲裁哪些批评成立、哪些是过度解读，并给出 README/论文必须改的清单。
- docs/review/agent6_paper_reviewer.md（本批审稿）
- docs/review/agent4_dataset_experiments.md（本批实验）
- docs/review/agent8_defense_demo.md（本批演示）
- README.md、README.en.md、docs/algorithms.md

## 必须仲裁的问题
1. **"用稳定性代替准确性"批评是否公允**：系统无 GT，无法算准确率。切换次数下降（35.2→11.7→4.0）在无 GT 情况下是否是唯一可报告且诚实的指标？论文应如何表述才不被评委打"偷换概念"？
2. **高密度检出率采信 76.7% 还是 79.4%**：README 写 79.4%（12 张），results/rerun/README 写 76.7%（9 张）。用 venv（H:/Hermes Portable/data/home/cache/scratch/prd_venv/Scripts/python.exe）解析 results/rerun/images500_baseline/results.json 与 results/baseline/results.json，核实哪个数字对应哪批数据、为何不同（阈值/图片集合/高密度定义），给出 README 应采用的唯一口径。
3. **mixkit_13192 基线数字 46 vs 18**：README 说"逐帧规则基线为 46 次/视频"，results/rerun/_comparison_summary.json 里是 18。46 是否来自 111 条平均？修正基线（AND 规则重跑）后该数字还会变。给出 README 应如何引用才能不自相矛盾。
4. **"31 条未触发风险报警"的表述风险**：无 GT 情况下"未触发报警"能说吗？建议的表述是什么？
5. **agent8 建议补 3 张图（风险曲线/IPM-BEV/网络图）+ 录 1 条真机 DANGER 视频**：按答辩收益/成本排序，哪些是必须（P0）、哪些可选？现场故障预案里哪 3 条最重要？

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/_cross/crossB_paper_data.md（中文，2-5KB）
- 最终回复：报告路径 + 3-5 条最关键仲裁结论
