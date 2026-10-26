# 交叉质疑 C：工程修复与演示投入仲裁 — 任务书

## 角色
你是工程安全仲裁员。阅读以下报告，仲裁工程修复建议的安全性与优先级，并评估演示投入。
- docs/review/agent5_embedded.md（本批嵌入式）
- docs/review/agent2_vision_tracking.md（本批 CV）
- docs/review/agent1_architect.md（本批架构）
- docs/review/agent8_defense_demo.md（本批演示）

## 必须仲裁的问题
1. **NMS 跨类别 bug 修复**：postprocess.cc:126 已从 classIds[i] 改为 classIds[j]（同类别 NMS）。这个修复方向对不对？会不会改变检测数量/风险等级？是否需要板端重新编译验证？在无真机验证前应不应该合入？
2. **VENC 颜色格式怀疑**：agent5 怀疑 main.cc 输出 BGR 但声明 RK_FMT_RGB888 导致 RTSP 红蓝翻转。阅读 main.cc/luckfox_mpi.cc 判断：是真实 bug 还是误读？如果不确定，应如何低成本验证（在板端抓帧 vs 看代码）？
3. **画框与决策过滤不一致**：agent1 说画框仅按 target_classes、决策按 class_conf_thresholds 精筛，画面与决策人数可能不一致。是否真实？修复成本？
4. **CI 修复**：ci.yml 已改为 make test（编译 src/luckfox_rtsp_yolov5/tests 下 4 个 C++ 测试）。有没有坑（如 risk_config 测试依赖 OpenCV 的条件编译）？在 Ubuntu CI 上 make test 能否通过——检查 Makefile 的 OPENCV 条件逻辑，给出预判。
5. **性能瓶颈**：agent2/5 都认为瓶颈在单线程串行 + 软件预处理，Jacobi O(n³) 不是瓶颈（n<30）。是否有反例（密集 50 人时）？"先打点再优化"是否仍是最优策略？
6. **agent8 建议砍掉 PCB/LVGL/语音大方案**是否合理？保留哪些低成本高收益？

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/_cross/crossC_engineering.md（中文，2-5KB）
- 最终回复：报告路径 + 3-5 条最关键仲裁结论
