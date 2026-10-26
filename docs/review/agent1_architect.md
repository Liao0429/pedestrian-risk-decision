# Agent 1 架构审查报告（项目总架构师）

> 独立只读审查（未修改任何源码）。审查范围：`src/luckfox_rtsp_yolov5/`（main.cc、luckfox_mpi.cc、yolov5.cc、postprocess.cc、risk_engine.cc、enhanced_decision.cc、include/*.h）、`src/algorithms/`（graph_laplacian/kalman/risk_fsm/crowd_dbscan/bytetrack/ema/ipm 的 .py/.h）、`scripts/`（crowd_test.py、crowd_test_enhanced.py、ablation_study.py、generate_golden.py、rerun_*.py）、`tests/`、`config/default_config.json`、`docs/`（README、ARCHITECTURE、SYSTEM_PRINCIPLE、algorithms、review/agent3_algorithm）、`results/`（含 edge_board 真机日志、rerun 实验 JSON）。
> 结论均以代码行号为证据；前一批 `agent3_algorithm.md` 的算法级发现仅作交叉引用。

---

## 一、现有系统真正实现了什么

**已完整集成进主循环（真实闭环）**：

1. **采集-推理-后处理链**：`main.cc:316-331` VI 取帧 → letterbox（`main.cc:54-74`）→ `inference_yolov5_model`（`yolov5.cc:193-208`）→ NMS 后处理（`postprocess.cc:381-477`）。
2. **IPM 几何校正**：`main.cc:96-100` 硬编码单应 H（720×480 梯形→6m×4m），`main.cc:351-353` 经 `mapCoordinates`+`mapToGround`（`ipm_transform.h:33-42`）得到地面米制脚点；支持外部标定文件（`main.cc:106-118`，`risk_config.h:58,131-136`）。
3. **增强决策链（当前唯一决策源）**：`main.cc:338-364` 收集 groundPts → `enhancedDecision.processFrame`（`enhanced_decision.cc:140-178`：Kalman 平滑 `smoothPoints` → `GraphLaplacian::computeFeatures` → `RiskFSM::update`）→ `er.level` 直接作为最终风险等级。
4. **输出**：检测框叠加（`main.cc:385-409`）+ 风险色条（`main.cc:412-423`）+ RTSP H.264 推流（`main.cc:442-507`）。
5. **工程健壮性**：JSON 配置加载与回退（`main.cc:102-118`、`risk_config.h:75-290`）、有限超时看门狗（`main.cc:32-41,306-310,426-440`）、资源清理链（`main.cc:166-177`）、主机单元测试（`Makefile:33-63`、`CMakeLists.txt:34-78`）、C++/Python 黄金回归（`tests/golden/*.json`、`test_golden_regression.cc`）。
6. **实验体系**：图片基线（`scripts/crowd_test.py`）、视频增强（`scripts/crowd_test_enhanced.py`：Kalman+谱特征+FSM）、消融（`scripts/ablation_study.py`）、111 条视频重跑（`scripts/rerun_all_videos.py`）、真机日志（`results/edge_board/`，573 条 `[EnhancedDecision]` 输出）。

**结论**：真正闭环的是 **YOLOv5n(RKNN) → IPM → Kalman → 图网络特征(n/density/r_gcc/clustering) → RiskFSM → 叠加/RTSP**。谱特征在闭环内被"计算并展示"，但**未进入决策**（见问题 P0-1）。

---

## 二、仅独立实现/未接线/未部署的模块（调用图证据）

| 模块 | 状态 | 证据 |
|---|---|---|
| **crowd_dbscan.{h,py}**（DBSCAN 人群聚类） | 完全未接入 | C++：全工程无任何 `.cc/.h` 引用 `crowd_dbscan.h`（Makefile:21-23 ALGO_HEADERS 不含它；CMakeLists 亦无）。Python：仅 `tests/test_crowd_dbscan.py` 导入 `crowd_dbscan`；`scripts/crowd_test_enhanced.py:23-25`、`ablation_study.py:19-21` 均未 import。 |
| **bytetrack_tracker.{h,py}**（ByteTrack 多目标） | 完全未接入 | C++：无任何编译单元引用 `bytetrack_tracker.h`。Python：仅 `tests/test_bytetrack_tracker.py` 导入。`enhanced_decision.cc:10-12` 自述用的是"最近邻贪心关联"，不是 ByteTrack。 |
| **ema_smoother.{h,py}**（EMA 平滑） | 仅测试/黄金回归 | C++：仅 `tests/test_golden_regression.cc` 与 `Makefile:59-60`（test-golden 目标）引用；**不在 ALGO_HEADERS，不进主路径**。Python：仅 `generate_golden.py`、`tests/test_ipm_ema.py`、`tests/test_golden_regression.py` 使用；`enhanced_decision.cc` 未 include。 |
| **RiskEngine（risk_engine.cc）** | 默认关闭的旁路 | `config/default_config.json:19` `enable_risk_engine_fallback=false`；`main.cc:369-383` 仅显式开启才运行且"不覆盖增强决策输出"；`main.cc:366-368` 注释明确"默认关闭"。 |
| **ipm_calibrate.py** | 独立工具 | 仅 `tests/test_ipm_ema.py` 使用；未进 scripts 实验链；仅被 `ipm_transform.h:11-13` 提及"兼容其输出格式"。 |
| **src/hardware/*** | 只有声明+demo | `voice_alert.h:13-41` 声明类但仓库内无 `.cc` 定义；`voice_alert_integration_demo.cc:7-16` 直接调用未实现方法；`Makefile:89-94` 不编译 hardware；`main.cc` 未 include。 |
| **谱特征（fiedler/谱半径/特征熵）** | 计算并透出，**未参与决策** | `enhanced_decision.cc:150-159` 算入 `EnhancedResult`；但 `:162-170` 构造 `FsmFeatures` 只填 `n/density/r_gcc/clustering/trend`；`risk_fsm.h:44-50` `FsmFeatures` 无 fiedler 字段，`:135-145` 评分公式不含谱项；`risk_fsm.h:48` clustering 注释"预留，当前不参与评分"。`main.cc:362,418` 仅打印/叠加显示 `fiedler`。Python 侧 `crowd_test_enhanced.py:107-111` 谱特征只存入 `feat` 字典，`:120` FSM 输入只有 n/density/r_gcc。 |

---

## 三、模块划分、数据流、依赖关系分析

### 3.1 数据流总体清晰
`摄像头 → VI → letterbox → RKNN → postprocess → 目标过滤(target_classes) → IPM 脚点 → EnhancedDecision{Kalman→Graph→FSM} → 等级 → 叠加+RTSP`。`risk_config.h` 统一了阈值/类别/FSM/单应配置入口，`risk_engine.h:13-22` 统一了等级枚举（CrowdLevel=RiskLevel 别名），消除了旧的双枚举。这部分设计是好的。

### 3.2 重复实现（同一算法多份代码）

| 算法 | 份数 | 位置 | 是否对齐 |
|---|---|---|---|
| 网络特征 n/m/density/r_gcc/clustering | **3 份** | `risk_engine.cc:44-100`；`crowd_test.py:156-219`；`graph_laplacian.h:220-332` / `graph_laplacian.py:117-203` | 有测试对齐（`tests/test_integration_pipeline.py:267-277` 断言一致），但三份独立实现仍易漂移 |
| Kalman | 2 份+1 集成 | `kalman_tracker.py`（含多目标）；`kalman_filter.h`（单目标）；`enhanced_decision.cc:52-138` 自研多目标关联 | **语义不一致**（见 P1-3） |
| FSM | 2 份 | `risk_fsm.py` / `risk_fsm.h` | 有黄金回归（`tests/golden/risk_fsm.json`），默认值一致 |
| DBSCAN | 2 份 | `crowd_dbscan.py` / `crowd_dbscan.h` | 噪声标签相反（Python NOISE=-2 vs C++ NOISE=-1，agent3 报告问题 5）；均无调用方 |
| ByteTrack | 2 份 | `bytetrack_tracker.py` / `.h` | 均无调用方 |
| EMA | 2 份 | `ema_smoother.py` / `.h` | 仅黄金回归对齐 |
| IPM | 4 处 | `main.cc:96-100` 硬编码、`ipm_transform.h:33-42`、`crowd_test.py:91-114`、`ipm_calibrate.py` | 主要入口已收敛到 `ipm_transform.h`，但 PC 侧仍保留 `crowd_test.py:get_homography` 独立实现 |

### 3.3 口径不一致（影响实验与板端可比性）
1. **FSM 配置键不一致**：`crowd_test_enhanced.py:87` 与 `ablation_study.py:46` 用 `cfg.get("temporal", {})`；`test_integration_pipeline.py:161` 用 `cfg.get("fsm", {})`；`config/default_config.json:4` 键是 `"fsm"`；C++ 端 `risk_config.h:209-274` 也读 `"fsm"`。→ **Python 实验链从未读取 JSON 的 fsm 配置**（默认值与 JSON 恰好相同，掩盖了问题；一旦用户修改 JSON 中 fsm 参数，Python 实验不生效）。
2. **FSM 输入特征不一致**：Python 端 `crowd_test_enhanced.py:120` 只传 `n/density/r_gcc`；C++ 端 `enhanced_decision.cc:162-169` 还传 `clustering` 并计算 `trend`（trend_bias 加成在 C++ 生效，Python 端永远不生效）。
3. **检测器不一致**：PC 实验用 ultralytics YOLOv8（`crowd_test.py:38,66`，根目录 `yolov8n.pt`）；板端用 YOLOv5n INT8（`main.cc:150`）。README 的检出率/等级分布不能外推为板端表现。
4. **frame_dt 不一致**：`config/default_config.json:3` frame_dt=0.1（10Hz）；Python 端 `crowd_test_enhanced.py:86` 用 `1/fps`（通常 1/25~1/30）；C++ 端 `main.cc:126` 用 JSON 值。两端 Kalman 预测步长不同。

---

## 四、文档与实现不一致清单

| # | 文档宣称 | 代码现状 | 证据 |
|---|---|---|---|
| D1 | README:12「邻接矩阵+图拉普拉斯谱分析（Fiedler/谱半径/代数连通度）+ **DBSCAN 人群聚类**」 | DBSCAN 零调用 | 见第二节 |
| D2 | README:13「Kalman/**ByteTrack** 多目标跟踪 + **EMA** 平滑 + FSM」 | ByteTrack/EMA 零主路径调用 | 见第二节 |
| D3 | README:90「**谱特征提升风险语义准确性**」 | 消融数据 B(+Kalman) 与 C(+Kalman+谱特征) **逐视频完全相同**（`results/rerun/_ablation.json`：mixkit_134 B/C 均 16 次切换、mixkit_13192 均 4 次、mixkit_4178 均 45 次等 6/6 视频完全一致）；FSM/评分公式无谱项 | `risk_fsm.h:135-145`、`_ablation.json` |
| D4 | ARCHITECTURE:31-33「**两条并行决策路径**」；Mermaid(14-29) 画 RiskEngine 汇入 | 实际单主路径（EnhancedDecision 直接输出），RiskEngine 默认关闭且"不覆盖增强输出" | `main.cc:366-383`、`default_config.json:19` |
| D5 | ARCHITECTURE:22,50 拓扑层含「**DBSCAN 人群聚类**」；SYSTEM_PRINCIPLE:22-28,36,92-102 流水线含 DBSCAN/ByteTrack/EMA | 均未接入 | 见第二节 |
| D6 | README:120「增强算法（谱特征/**ByteTrack**/FSM）的 C++ 头文件…可交叉编译集成」 | "可集成"表述勉强成立，但 ByteTrack 头文件从未被任何工程文件引用 | `Makefile:21-23`、`CMakeLists.txt` |
| D7 | README:130「测试覆盖：EnhancedDecision（Kalman+**谱特征**+FSM 确定性输出）」 | 谱特征不参与 FSM 输出 | `enhanced_decision.cc:162-170` |
| D8 | EDGE_TEST_REPORT.md「当有行人进入画面时…基于**谱特征**+FSM 滞回输出 WARNING/DANGER」 | 真机日志 573 条全部 `level=SAFE n=0`；WARNING/DANGER 从未在目标硬件验证 | `results/edge_board/enhanced_final_20261026.log`（level 分布 SAFE=573） |
| D9 | README:5「Luckfox Pico Max 上**实时**行人检测」；README:10「实时检测行人」 | 真机 6.9FPS（空场景），且无有行人真机数据 | `EDGE_TEST_REPORT.md` |
| D10 | README:49 目录结构把 algorithms 描述为完整算法模块库 | `src/algorithms/__init__.py` **只导出 risk_fsm**，其余模块需深路径导入 | `__init__.py:6-17` |

---

## 五、按严重度的问题清单

### P0（影响正确性 / 可信度 / 核心功能）

**P0-1 谱特征只计算、未进入决策，文档与消融实验宣称其贡献，构成可信度风险**
- 证据：`enhanced_decision.cc:150-159` 计算 fiedler/谱半径/特征熵，`:162-170` FSM 输入不含谱项；`risk_fsm.h:135-145` score 公式只用 n/density/r_gcc(+trend)；`main.cc:362,418` 仅打印/显示。消融数据直接证明：`results/rerun/_ablation.json` 中 6/6 视频 B(+Kalman) 与 C(+Kalman+谱特征) 的 switches/dist 完全相同。
- 影响：README:90「谱特征提升风险语义准确性」无数据支撑；论文核心创新点"谱特征"对最终等级零贡献；每帧 Jacobi O(n³) 是纯开销（真机 CPU 已 42.5%）。
- 建议：将 Fiedler 等真正并入 FSM 评分并重跑实验；或如实降级为"谱特征仅可视化/诊断"，修改 README/论文表述。

**P0-2 DBSCAN / ByteTrack / EMA 被文档包装为系统组件，实际完全未接入**
- 证据：`crowd_dbscan.h`、`bytetrack_tracker.h` 全工程零编译引用；Python 仅各自单测导入；`ema_smoother.h` 仅黄金回归引用；`Makefile:21-23` ALGO_HEADERS 不含三者。
- 影响：README:12-13、ARCHITECTURE:22,50、SYSTEM_PRINCIPLE:22-28 的核心能力宣称超前于实现；答辩/查重风险高。
- 建议：文档明确标注"独立实现/待集成"；或把 DBSCAN 簇特征接入 FSM 形成真实增量。

**P0-3 真机仅验证空场景，WARNING/DANGER 从未在目标硬件验证**
- 证据：`results/edge_board/enhanced_final_20261026.log` 573 条全部 `level=SAFE n=0`；`EDGE_TEST_REPORT.md` 自述"当前摄像头前无行人"，却断言"当有行人进入画面时…输出 WARNING/DANGER"。
- 影响：板端风险输出端到端未经验证；6.9FPS 为空场景帧率，有行人时图特征/谱分解开销增大，帧率可能下降。
- 建议：真机用密集人群图片/平板入镜触发 WARNING/DANGER；否则在论文明确标注"有行人风险输出仅 PC 离线验证"。

### P1（明显改进工程质量）

**P1-1 RiskEngine 是默认关闭的冗余旁路，文档却宣称"两条并行决策路径"**
- 证据：`default_config.json:19` 默认 false；`main.cc:366-383` 旁路"不覆盖增强决策输出"；`risk_engine.cc:44-100` 与 `graph_laplacian.h` 功能重复。
- 影响：架构图误导；两套特征实现并存有漂移风险；开启 fallback 时重复 computeFeatures 浪费算力。
- 建议：修正文档为"单主路径 + 可选对照旁路"，或删除旁路。

**P1-2 Python/C++ FSM 配置与输入不一致（temporal vs fsm；缺 trend/clustering）**
- 证据：`crowd_test_enhanced.py:87`、`ablation_study.py:46` 读 `cfg["temporal"]`；`test_integration_pipeline.py:161` 读 `cfg["fsm"]`；`default_config.json:4` 键为 `fsm`；Python 端 FSM 输入仅 n/density/r_gcc（`crowd_test_enhanced.py:120`），C++ 端含 clustering/trend（`enhanced_decision.cc:162-169`）。
- 影响：PC 实验与板端决策链参数不可比；用户改 JSON 的 fsm 参数对 Python 实验无效；trend_bias 在两端行为不同。
- 建议：统一键名为 `fsm`，Python 脚本补传 clustering/trend，并将 C++/Python 双端 FSM 配置纳入黄金回归。

**P1-3 Python/C++ 多目标 Kalman 平滑语义不一致（幽灵点 vs 清空）**
- 证据：C++ `enhanced_decision.cc:58-62` 空输入 `tracks_.clear()`、未匹配轨迹只 miss+1 不输出外推点（`:126-137`）；Python `kalman_tracker.py` 的 states 返回所有活跃轨迹（含外推），`crowd_test_enhanced.py:101-102` 全量取用。
- 影响：PC 端漏检帧 n/density 虚高、人群散去风险回落慢；板端 n 恒等于当帧检测数；两端结果不可直接对比。
- 建议：统一语义（Python 特征计算前过滤 misses>0 轨迹），补多目标平滑黄金回归。

**P1-4 PC 实验检测器（YOLOv8）与板端（YOLOv5n INT8）不一致**
- 证据：`crowd_test.py:38,66` ultralytics YOLOv8；`main.cc:150` YOLOv5n INT8。
- 影响：README 的检出率/等级分布/消融结论来自 YOLOv8，不能外推为板端表现。
- 建议：文档明示"PC 用 YOLOv8 近似验证算法流水线，板端为 YOLOv5n INT8"；或板端模型在 PC 重跑基线。

**P1-5 网络特征三份重复实现，维护漂移风险**
- 证据：`risk_engine.cc:44-100`、`crowd_test.py:156-219`、`graph_laplacian.h/.py`。
- 影响：虽有测试对齐（`test_integration_pipeline.py:267-277`），但改一处漏一处即产生口径漂移；DBSCAN/ByteTrack/EMA 同样双份实现。
- 建议：以 `graph_laplacian` 为唯一特征实现，其余改为调用/删除；或抽出共享 C++ 头文件。

**P1-6 硬件扩展模块无实现**
- 证据：`voice_alert.h` 声明无 `.cc`；`voice_alert_integration_demo.cc:7-16` 调用未实现方法；Makefile 不编译 hardware；main.cc 未 include。
- 影响：README:18「可扩展硬件」被表述为系统能力，但当前无可运行实现。
- 建议：明确标注"硬件为方案/独立 demo，未集成主程序"。

### P2（可选优化）

**P2-1 画框与风险计算使用不同过滤集合**
- 证据：风险路径按 `target_classes`+`class_conf_thresholds` 过滤（`main.cc:341-348`）；画框循环遍历全部 `od_results`（`main.cc:385-409`）未过滤。
- 影响：配置 target_classes=[0] 时画面仍画出车辆/动物框，人数与框数不一致。

**P2-2 `src/algorithms/__init__.py` 仅导出 risk_fsm**
- 证据：`__init__.py:6-17`。影响：算法包不完整，`from src.algorithms import *` 只能拿到 FSM。

**P2-3 Kalman 参数无法 JSON 配置**
- 证据：`kalman_process_noise/measurement_noise/gate/max_miss` 硬编码于 `enhanced_decision.h:82-90`，`risk_config.h` 无对应键。
- 影响：板端调参需重编译；agent3 报告亦指出。

**P2-4 叠加文字显示 `lambda2=` 但谱特征无决策语义，易误导观察者**
- 证据：`main.cc:418` 显示 fiedler，但等级仅由 FSM（无谱项）决定。

**P2-5 板端日志首行 `sh: RkLunch-stop.sh: not found`**
- 证据：`results/edge_board/enhanced_final_20261026.log` 首行；`main.cc:92` 无条件调用 `system("RkLunch-stop.sh")`。板端继续运行（非致命），但说明初始化环境假设未在仓库校验。

---

## 六、结论

**真实完成度**：主循环闭环（YOLOv5n RKNN → IPM → Kalman → 图网络特征 → RiskFSM → RTSP）工程扎实：配置化、看门狗、清理链、主机测试、黄金回归、111 条视频实验、真机空场景运行均有真实证据。

**核心风险**：文档/论文表述显著超前于实现——谱特征（P0-1）、DBSCAN/ByteTrack/EMA（P0-2）均未进入决策链，真机风险输出未验证（P0-3）。消融实验数据已直接暴露"谱特征零贡献"（B=C）。**建议优先级**：先修文档口径（P0-1/P0-2/P0-3 配套的表述），再补真机有行人验证，再统一 Python/C++ 决策链参数与平滑语义（P1-2/P1-3），最后清理冗余实现（P1-5/P2 系列）。