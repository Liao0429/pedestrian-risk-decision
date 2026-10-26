# 交叉质疑 A：算法与实验争议仲裁 — 任务书

## 角色
你是评审仲裁员。阅读 docs/review/ 下以下报告，对关键争议做**对抗式交叉质疑**，指出各报告中的错误、过度推断或相互矛盾之处，并给出仲裁结论。
- agent3_algorithm.md（前批算法报告）
- agent3_algorithm_review2.md（本批算法报告，含新数值实验）
- agent4_dataset_experiments.md（本批实验报告）
- agent6_paper_reviewer.md（本批审稿报告）
- agent1_architect.md、agent2_vision_tracking.md

## 必须仲裁的争议
1. **归一化拉普拉斯 L_sym 是否值得做**：agent3_review2 声称前批报告"μ₂∈(0,1]"错误，实测 K3 μ₂=1.5、K4=1.333、K2=2.0，正确值域 (0,2]，且 4 点全分散图 L_sym=I、μ₂=1.0 会污染孤立点语义。请复算核验（numpy eigvalsh），并判断：接入 FSM 前，相对代数连通度 fiedler/λ_max 与 L_sym 哪个更稳？在 RV1106 上 Jacobi O(n³) 是否仍是障碍（n<30）？
2. **消融 B==C 的真正含义**：agent3/4/6 说"谱特征零贡献"。但看 ablation_study.py：C 组 use_spectral=True 只把谱特征放进 feat 字典，FSM 输入仍是 n/density/r_gcc。所以 B==C 是因为**消融设计根本没把谱特征接进决策**，还是谱特征本身无效？如果要证明"谱特征无效"需要什么实验？现有报告是否把"未接入"错误表述为"无效"？
3. **FSM 与规则引擎阈值语义漂移**：scenario_test.py（scripts/scenario_test.py，已运行，结果在 results/scenarios/scenario_test.json）显示 n8_cluster 规则 WARNING 但 FSM 最终 DANGER；n10_chain 规则 DANGER 但 FSM WARNING。这说明什么？FSM 的 score 阈值（up_danger=0.65）与规则阈值（n>=10 and ...）是两个不同标准，论文应如何表述？这是缺陷还是特性？
4. **基线 OR→AND 修正的影响**：rerun_video_baseline.py 已改为 AND（与引擎一致），正在重跑 111 视频。旧数据 base_sw=5147/DANGER 21969 会显著下降。评审"增强后 DANGER -64%"的结论是否建立在失真基线上？修正后论文还能说什么？
5. **阈值标定反例是否成立**：agent6 用 results/baseline/results.json（30 图）算出 SAFE 样本最高分 0.66 > up_danger 0.65，断言阈值标定不可信。这个推理有无问题（30 图样本量、0.01 余量、FSM 有确认帧）？

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/_cross/crossA_algorithm_experiment.md（中文，2-5KB 即可）
- 每条给出：争议双方观点 -> 你的复算/证据 -> 仲裁结论（支持哪方/都不支持/新方案）
- 最终回复：报告路径 + 3-5 条最关键仲裁结论
