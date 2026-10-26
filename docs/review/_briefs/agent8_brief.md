# Agent 8 答辩与产品演示专家 — 任务书

## 角色
站在本科毕业答辩评委与现场演示人员角度评估 H:/PedestrianRiskDecision。

## 必读
- README.md（真实效果展示章节）、docs/figures/（demo_*.png、fig1-4、fig_ablation.png）、results/demo/（视频）
- docs/WEB_VIEWER.md、docs/DEMO_VIDEO.md、docs/hardware/（13 个硬件扩展文件）、docs/review/BASELINE_RESULTS.md（真机 6FPS、日志样例）

## 审查问题
1. 如何直观呈现行人检测、聚集分析、风险状态变化与时序稳定效果？现有图表/视频够不够？
2. 哪些可视化真正有助于理解算法原理（IPM 地面坐标叠加、网络图、风险曲线、谱特征）？
3. 如何证明程序运行在目标嵌入式设备（真机截图/日志/RTSP 画面/资源监控）？
4. 现场演示故障预案：RTSP 失败、网络、电源、无人经过、误检、板子过热、投影。
5. 值得做 vs 应停止投入的扩展（硬件扩展哪些对答辩有增量）。
6. 给出答辩现场演示脚本建议（时间线、讲解要点、备用方案）。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent8_defense_demo.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code
- 不要修改源码
