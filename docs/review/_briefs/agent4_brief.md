# Agent 4 数据集与实验设计专家 — 任务书

## 角色
独立审查 H:/PedestrianRiskDecision 的数据与实验。核心原则：不编造、可复现、指标定义正确、结论与证据匹配。

## 必读
- README.md 实验章节、dataset/README.md、dataset/images_manifest.csv
- scripts/crowd_test.py、crowd_test_enhanced.py、ablation_study.py、rerun_all_videos.py、rerun_video_baseline.py、find_sparse_video.py、plot_results.py
- results/experiment_summary.json、results/rerun/_ablation.json、results/rerun/_comparison_summary.json（如存在）
- docs/review/agent3_algorithm.md

## 审查问题
1. 500 张图片的来源、标注（GT 人数）、质量、可重复性；人数标注能否作为风险判断真值。
2. 111 条视频是否有可靠风险等级真值/聚集程度标签？相邻帧是否造成数据泄漏或评估偏差（同一视频逐帧统计独立性）。
3. 指标定义是否正确：检出率（det_total/gt_total）、等级分布、切换次数、误报率/漏报率（README 是否用了不能由数据支撑的指标）。
4. 消融实验（6 视频 4 配置）是否支持 README 结论：'Kalman 切换↓67%、FSM→4.0、谱特征提升风险语义准确性'。注意谱特征未进 FSM，'谱特征提升准确性'缺乏证据。
5. 是否有不公平基线（逐帧规则 vs FSM）、事后挑选参数/视频、缺对照。
6. 用 venv（H:/Hermes Portable/data/home/cache/scratch/prd_venv/Scripts/python.exe）核对 experiment_summary.json 中的数字能否由脚本重算（若重跑耗时长，可只核对脚本逻辑与 JSON 一致性）。
7. 提出低成本人工标注方案与可执行实验设计：风险等级混淆矩阵、各类别召回、误报率、漏报率、预警延迟；仅在有真值时计算并解释。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent4_dataset_experiments.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code
- 不要修改源码/数据
