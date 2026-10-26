# Agent 3 复杂网络与算法研究专家 — 任务书

## 角色
独立审查 H:/PedestrianRiskDecision 的图论/聚类/时序算法。目标板 RV1106（256MB/单核 A7 1.2GHz，资源受限）。

## 必读
- 先读 docs/review/agent3_algorithm.md（前一批报告，在此基础上增量，不要覆盖它；你的输出文件名为 agent3_algorithm_review2.md）
- src/algorithms/graph_laplacian.py、graph_laplacian.h（邻接矩阵、拉普拉斯 L=D-A、fiedler/谱半径/特征熵、Jacobi 分解）
- src/algorithms/crowd_dbscan.py、crowd_dbscan.h（eps=1.5, min_samples=2）
- src/algorithms/kalman_tracker.py、kalman_filter.h、ema_smoother.py/.h、risk_fsm.py/.h
- src/luckfox_rtsp_yolov5/src/risk_engine.cc（规则：n/rho/gcc）、enhanced_decision.cc（FSM 输入仅 n/density/r_gcc/clustering/trend）
- config/default_config.json（edge_dist_m=2.0、fsm 滞回参数）

## 审查问题
1. 建边规则 d<edge_dist（严格小于）与 DBSCAN dist<=eps（闭区间）的阈值语义冲突（2.0 vs 1.5）。
2. 谱特征数学性质：fiedler 随 n 漂移（完全图 K_n 的 fiedler=n）、图不连通时 fiedler=0、跨规模不可比；代数连通度与 fiedler 重复。用 Python 小实验验证（numpy eigvalsh）。
3. 归一化拉普拉斯 L_sym 的可行性：特征值 ∈[0,2]，连通图 fiedler∈(0,1]，跨规模可比；Jacobi 分解代码可否复用；计算成本。
4. DBSCAN min_samples=2 链式传播问题；与图连通分量是否等价。
5. Kalman/EMA/FSM 功能重复、参数冲突、响应延迟；FSM 滞回（confirm_up/down、danger_hold）是否合理。
6. 谱特征未进 FSM：如何设计低成本、可解释的接入方式（如相对代数连通度 fiedler/λmax、局部密度、邻域距离统计）。
7. 每个替代方案给出：原理、预期收益、计算成本（O(?)）、潜在缺陷、验证方法。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent3_algorithm_review2.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code；venv: H:/Hermes Portable/data/home/cache/scratch/prd_venv/Scripts/python.exe（有 numpy）
- 不要修改源码
