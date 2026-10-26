# Agent1 架构审查报告（项目总架构师）

> 只读审查：`src/luckfox_rtsp_yolov5/src/main.cc`、`include/*.h`、`src/algorithms/*.h`、`README.md`、`docs/ARCHITECTURE.md`、`docs/SYSTEM_PRINCIPLE.md`。已交叉核对 `scripts/`、`tests/`、`config/`、`results/edge_board/` 佐证集成状态。

## 一、实际完成度

**已真正集成进主循环**：RKNN YOLOv5 推理 → letterbox → IPM（`mapCoordinates`+`mapToGround`）→ `EnhancedDecision`（自研最近邻关联多目标 Kalman + GraphLaplacian 网络特征 + RiskFSM）→ 画面叠加 → RTSP 推流；配置 JSON 加载与失败清理链完整。真机实测 ~6.9FPS、RSS 12.5MB、CPU 42.5%（`results/edge_board/EDGE_TEST_REPORT.md:20-25`）。

**仅独立实现未进主循环**：`bytetrack_tracker.h`、`crowd_dbscan.h`、`ema_smoother.h`（仅 golden 测试引用 EMA）、`RiskEngine`（默认关闭）、`src/hardware/*`（无 .cc 实现）、`ipm_calibrate.py`。

## 二、具体问题

### 问题 1【高】图拉普拉斯“谱特征”只计算、未进入决策评分
- 证据：`enhanced_decision.cc:148-162` 算出 fiedler/谱半径/特征熵并写入结果，但 `:163-173` 构造 `FsmFeatures` 只填 `n/density/r_gcc/clustering/trend`；`risk_fsm.h:44-50` 的 `FsmFeatures` 无 fiedler 字段，`:135-145` 评分公式不含任何谱项；`main.cc:345-346,401` 中 fiedler 仅用于 printf 和叠加文字。Python 侧 `crowd_test_enhanced.py:107-111,120` 同样只存字典不参与 FSM。
- 影响：README:12/ARCHITECTURE:22 宣称谱分析提升风险决策，但谱特征对最终等级零贡献；消融实验“谱特征提升语义准确性”无代码依据，答辩深究即穿帮；每帧 Jacobi O(n³) 是纯开销（板端 CPU 已 42.5%）。
- 建议：将 Fiedler/代数连通度真正并入 FSM 评分并重跑 golden 回归；或如实降级为“谱特征仅可视化/诊断”。

### 问题 2【高】文档称“两条并行决策路径”，实际只有一条
- 证据：`ARCHITECTURE.md:31-33` 宣称双路径且 Mermaid（14-29 行）画成两路汇入 RiskEngine；实际 `main.cc:339-346` 由 EnhancedDecision 直接输出等级，`main.cc:352-366` RiskEngine 仅当 `enable_risk_engine_fallback=true` 时运行且“不覆盖增强输出”；`config/default_config.json:19` 默认为 false。
- 影响：架构文档与真实数据流不符；RiskEngine 实际是默认关闭的重复计算旁路（`risk_engine.cc:44-100` 与 `graph_laplacian.h:220-332` 功能重复，两套特征实现并存易漂移）。
- 建议：修正架构图为单主路径 + 默认关闭旁路；或删除旁路。

### 问题 3【高】ByteTrack/DBSCAN/EMA 宣称是系统组件，实际未接入任何决策路径
- 证据：`README.md:13`“Kalman/ByteTrack 多目标跟踪 + EMA 平滑”、`ARCHITECTURE.md:22,50` 画 DBSCAN 聚类；但 `enhanced_decision.cc:12` 自述用“最近邻贪心关联”而非 ByteTrack，全工程 grep 仅 `test_golden_regression.cc` 引用 `ema_smoother.h`，`bytetrack_tracker.h`/`crowd_dbscan.h` 零引用；`Makefile:21-23` ALGO_HEADERS 不含二者；Python 实验 `crowd_test_enhanced.py:23-25` 也未 import。
- 影响：核心创新点“DBSCAN 人群聚类”“ByteTrack 升级”只有头文件+单元测试，文档超前于实现。
- 建议：文档明确“独立实现/待集成”；或把 DBSCAN 簇特征接入 FSM。

### 问题 4【中】C++ 与 Python 多目标平滑语义不一致（幽灵点 vs 清空）
- 证据：`enhanced_decision.cc:58-62` 空输入直接 `tracks_.clear()`，`:126-137` 未匹配轨迹只 miss+1 不输出外推点；而 `kalman_tracker.py:414-416` 的 `states()` 返回所有活跃轨迹（含未匹配外推），`crowd_test_enhanced.py:101-102` 全量取用。
- 影响：PC 端漏检帧仍输出“幽灵点”，n/density 虚高、人群散去风险回落慢；板端 n 恒等于当帧检测数；两端结果不可直接对比，黄金回归未覆盖该路径。
- 建议：统一语义（Python 特征计算前过滤 misses>0 轨迹），并补多目标平滑黄金回归。

### 问题 5【中】PC 实验检测器与板端不一致
- 证据：`crowd_test.py:38,66` 用 ultralytics YOLOv8（根目录 `yolov8n.pt`），板端 `main.cc:139` 用 `./model/yolov5.rknn`（YOLOv5n INT8）。
- 影响：111 条视频/500 张图片的检出率、等级分布来自 YOLOv8，不能直接外推为板端表现。
- 建议：文档明示“PC 用 YOLOv8 近似验证算法流水线，板端为 YOLOv5n INT8”，或板端模型在 PC 重跑基线。

### 问题 6【中】真机只验证了空场景
- 证据：`results/edge_board/EDGE_TEST_REPORT.md:46-55` 日志全部 `level=SAFE n=0`（573 条），报告自承“当前摄像头前无行人”。
- 影响：WARNING/DANGER 端到端输出从未在目标硬件验证；6.9FPS 是空场景帧率，有行人时特征分解矩阵更大，帧率可能下降。
- 建议：答辩前用高密度人群图片/平板入镜做真机触发测试；否则在论文明确标注“有行人风险输出仅 PC 离线验证”。

### 问题 7【中】硬件扩展无实现、未编译
- 证据：`src/hardware/voice_alert.h:13-41` 声明类但仓库内无任何 `.cc` 定义；`voice_alert_integration_demo.cc:7-16` 直接调用未实现方法；`Makefile:89-94` 不编译 hardware；`main.cc` 未 include。
- 影响：README“可扩展硬件”被包装成系统能力，但当前无可运行实现。
- 建议：明确标注“硬件为方案/独立 demo，未集成主程序”。

### 问题 8【低】画框与风险计算使用不同过滤集合
- 证据：`main.cc:326-331` 风险路径按 `target_classes`+`class_conf_thresholds` 过滤；`main.cc:368-392` 画框遍历全部 `od_results` 未过滤。
- 影响：配置 target_classes=[0] 时画面仍画出车辆/动物框，叠加条人数与画面框数不一致。
- 建议：画框循环复用风险路径过滤条件。

## 三、结论

真正闭环：YOLOv5n→IPM→Kalman→网络特征→FSM→RTSP，真机可跑且工程健壮性（配置化/清理链/测试/黄金回归）扎实。但文档与实现边界严重不符：谱特征、DBSCAN、ByteTrack、EMA、硬件均未进入决策链，RiskEngine 为默认关闭的冗余旁路。建议优先修正文档口径（问题 1/2/3），再补齐真机有行人验证（问题 6），统一 C++/Python 平滑语义（问题 4）。
