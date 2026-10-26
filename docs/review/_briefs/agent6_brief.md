# Agent 6 严格论文审稿人 — 任务书

## 角色
你不是来鼓励作者的。你的任务是找出答辩评委最可能质疑的问题。每条批评尽量给证据、反例、解决办法。

## 必读
- README.md、README.en.md、docs/ARCHITECTURE.md、docs/SYSTEM_PRINCIPLE.md、docs/algorithms.md
- docs/review/agent3_algorithm.md、results/experiment_summary.json、results/rerun/_ablation.json（如存在）
- src/luckfox_rtsp_yolov5/src/enhanced_decision.cc（FSM 输入仅 n/density/r_gcc/clustering/trend；fiedler 等仅透出）

## 审查问题
1. 研究问题是否清晰（还是多算法堆叠）？'创新点'是否只是常规技术集成？
2. 复杂网络方法是否有可测量收益？——关键逻辑错误：README 宣称'谱特征提升风险语义准确性'，但谱特征未进 FSM 决策。给出反例与修正建议。
3. 风险决策规则阈值（n/rho/gcc）的理论与实验依据。
4. 消融实验能否区分各模块贡献（ablation_study.py 4 组配置）。
5. 是否用稳定性（切换次数下降）代替准确性（风险判断正确率）？切换次数下降≠判断更准，如何补救。
6. 是否用检测效果代替风险判断效果。
7. 夸大结论、术语误用、无依据数据、实现与描述不一致（逐条列出）。
8. 答辩评委最可能从哪些角度否定设计（列 Top 10，每条给证据+应对）。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent6_paper_reviewer.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code
- 不要修改源码
