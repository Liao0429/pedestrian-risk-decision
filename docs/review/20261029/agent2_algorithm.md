# A2 CV/算法专家 · 独立评审报告（2026-10-29 第二轮）

> 角色：CV/算法专家（A2）｜审查域：`src/algorithms/` 算法正确性、边界条件、数值稳定性、性能热点、双实现一致性、配置键一致性、生产消费关系
> 分支：`review-opt-20261029`｜方式：只读审查，未修改任何代码
> 验证动作：逐对通读 6 组算法（Python + C++ 头文件）及生产消费链（`enhanced_decision.cc/h`、`risk_config.h`、`main.cc`）、4 个消费脚本（`crowd_test_enhanced.py`/`ablation_study.py`/`spectral_fsm_experiment.py`/`scenario_test.py`）、黄金回归测试与 C++ 主机测试清单；用 Python 3.14.7 + numpy 2.5.4 实算复现关键数值用例。
> 说明：本机无 g++，C++ 侧为静态审查；C++ 主机测试（含 `test_golden_regression.cc`）由 CI/目标板覆盖。上一轮已闭环的问题（消融基线 OR→AND、配置键 temporal→fsm、谱特征 w4 未接入、黄金独立参考）不再重复。

---

## 一、算法清单与双实现一致性表

| # | 算法 | Python | C++ | 双实现一致性结论 | 生产消费 |
|---|------|--------|-----|----------------|----------|
| 1 | 风险有限状态机 | `risk_fsm.py` | `risk_fsm.h` | ✅ **完全一致**（阈值/滞回/确认帧/保持/w4 谱项/单人语义逐项核对一致，见第三节） | ✅ C++ `enhanced_decision.cc` + Python 4 脚本 |
| 2 | 图拉普拉斯谱特征 | `graph_laplacian.py` | `graph_laplacian.h` | ✅ 核心一致（建边 d<edge_dist、m、density、r_gcc、clustering、fiedler、谱半径、特征熵公式与容差一致）；⚠️ 2 处防御性差异（NaN 过滤、converged 标志，见 P2-1/P2-2） | ✅ C++ `enhanced_decision.cc:148-159`；Python 4 脚本 |
| 3 | 卡尔曼滤波/多目标跟踪 | `kalman_tracker.py`（KalmanFilter+KalmanTracker） | `kalman_filter.h`（单目标滤波器；多目标关联在 `enhanced_decision.cc::smoothPoints`） | ✅ 单目标核心一致（CV 模型、dt/process_noise=1.5/measurement_noise=0.5、P 初值、`P=(I-KH)P` 公式）；⚠️ 多目标输出语义不一致（幽灵点，见 P1-3）；⚠️ 奇异 S 处理不一致（P2-3） | ✅ C++ `smoothPoints`；Python `crowd_test_enhanced.py`/`ablation_study.py` |
| 4 | EMA 平滑 | `ema_smoother.py` | `ema_smoother.h` | ⚠️ 基本一致；维度不匹配时 Python 抛 `ValueError`、C++ 静默忽略（P2-4）；标量+dim>1 时 Python 抛异常、C++ 只更新第 0 维 | ❌ 无生产消费（README 标注可选扩展） |
| 5 | DBSCAN 人群聚类 | `crowd_dbscan.py` | `crowd_dbscan.h` | ❌ **噪声标签 -2 vs -1 反转**（P1-2）；其余算法逻辑一致 | ❌ 无生产消费（README 标注可选扩展） |
| 6 | ByteTrack 跟踪 | `bytetrack_tracker.py` | `bytetrack_tracker.h` | ✅ 一致（IOU 公式、`union<=1e-12` 防除零、Hungarian 代价、参数） | ❌ 无生产消费（README 标注可选扩展） |
| 附 | IPM 标定 | `ipm_calibrate.py` | 无头文件（C++ 侧经 `ipm_transform.h` 加载 `homography_file`） | — 单端工具 | ✅ 工具链（输出文本被 C++ 加载） |

**生产主链路**：`main.cc` → `EnhancedDecision::processFrame` → `kalman_filter.h`（多目标平滑）→ `graph_laplacian.h`（特征）→ `risk_fsm.h`（稳定等级）→ 画框颜色/文字。`risk_engine.cc` 仅在 `enable_risk_engine_fallback=true`（默认 false）时构建。**不存在“生产主循环中计算了但无人消费”的模块**；`ema_smoother`/`crowd_dbscan`/`bytetrack_tracker` 为文档明确标注的“可选扩展”，仅有测试消费方。

---

## 二、risk_fsm 专项核对（用户特别要求）

| 检查项 | Python `risk_fsm.py` | C++ `risk_fsm.h` | 结论 |
|--------|----------------------|------------------|------|
| 阈值 up_warning / up_danger / down_safe / down_warning | 0.40 / 0.65 / 0.28 / 0.55（行 92-95） | 0.40 / 0.65 / 0.28 / 0.55（行 64-67） | ✅ 一致 |
| 滞回 `raw_target` 状态表 | 行 238-252 | 行 160-171 | ✅ 逐分支一致 |
| 确认帧 confirm_up=3 / confirm_down=5 | 行 98-99；`_transition` 行 254-286 | 行 69-70；`transition` 行 173-204 | ✅ 一致（含：升级重置 downCount、降级重置 upCount、DANGER 保持期不累计降级、未达确认帧保持原等级） |
| DANGER 保持（帧+毫秒同时满足） | 行 288-302 | 行 206-218 | ✅ 一致（`danger_hold_frames=10`、`danger_hold_ms=1000`；`now_ms<0`/None 禁用毫秒项） |
| 谱特征权重 w4 | 行 228-232：`w4*fiedler/(fiedler+1)*r_gcc`，仅 `r_gcc>0 && fiedler>0` 生效 | 行 149-152：同式、同条件 | ✅ 一致 |
| 单人 r_gcc=0 语义 | `graph_laplacian.py:162`：n<=1 → r_gcc=0.0；FSM 得分 = w1*min(1/n_ref,1)=0.02 → SAFE | `graph_laplacian.h:228`：n<2 → r_gcc=0；`risk_fsm.h` 同得分 | ✅ 一致（实算：单人 score=0.0200，level=SAFE） |
| 配置键与 `default_config.json` | `RiskFSMConfig` 15 个字段与 `default_config.json` 的 `fsm` 块完全对应（行 5-18），脚本经 `cfg.get("fsm",{})` 传入 | `risk_config.h:209-279` 读取同一 `fsm` 对象并校验 | ✅ 一致 |

**结论：risk_fsm.py 与 risk_fsm.h 在阈值、滞回、确认帧、DANGER 保持、w4 谱项、单人 r_gcc=0 语义上完全一致，未发现双实现不一致。**

⚠️ 但存在一个**双端共有的评分校准问题**（非不一致），见 P1-1：两人相距 0.5m 时 score=0.7067 → 连续 3 帧即 DANGER；三人链状队形 score=0.61 反而只有 WARNING。

---

## 三、问题清单

### P1（优先改进）

**P1-1 ｜ FSM 评分校准倒挂：两人紧邻即 DANGER，三人链状队形反而 WARNING；单人语义正确但边界硬跳变**
- 文件：`src/algorithms/risk_fsm.py:223-232`、`src/algorithms/risk_fsm.h:140-156`（评分公式）；`config/default_config.json:5-18`（权重与阈值）
- 复现（Python 实算）：
  - 单人 `[(1,2)]`：n=1，r_gcc=0，fiedler=0 → **score=0.0200 → SAFE**（单人 r_gcc=0 语义正确）
  - 两人相距 0.5m `[(0,0),(0.5,0)]`：n=2, m=1, density=1.0, r_gcc=1.0, fiedler=2.0 → **score=0.7067 ≥ up_danger=0.65 → 连续 3 帧后 DANGER**
  - 三人链 `[(0,0),(1.5,0),(3,0)]`：n=3, m=2, density=0.667, r_gcc=1.0, fiedler=1.0 → **score=0.61 → 仅 WARNING**
- 根因：`w2=0.3` 的 density 对 n=2 全连接图直接取 1.0，`w3=0.3` 的 r_gcc 也取 1.0，加上 `w4=0.1` 谱项 0.0667，n 项（w1=0.4）只有 0.04，权重合计使 2 人图轻松越过 0.65；而 n=3 链状图 density=2/3，反而更低。同时，两人距离恰好 ≥2m 时无连边 → score=0.04（SAFE），**在 2m 边界产生 0.04→0.71 的硬跳变**。
- 影响：生产/实验中两人并肩站立即误报 DANGER，与规则引擎“DANGER 需 n≥10”（`risk_config.h:50`）语义冲突；答辩演示“两人站一起 → 报警”可能被视为误报。双实现一致，因此不会被现有黄金/一致性测试捕获。
- 修复建议（择一）：① 提高 `n_ref`（如 30-50）或增大 w1 使人数项主导；② 谱项/密度项对 n<3 的门控（如 `n>=3` 才启用 w4 或限制 density 权重）；③ 将 density 归一化改为 `m/(n*max(n-1,1)/2)` 之外的人群语义修正（如相对最大可容纳人数）；④ 在论文/文档中明确“两人即 DANGER”的语义边界并补充消融。

**P1-2 ｜ crowd_dbscan 噪声标签 Python(-2) 与 C++(-1) 反转，数据契约不一致**
- 文件：`src/algorithms/crowd_dbscan.py:34-35`（`UNCLASSIFIED=-1, NOISE=-2`）、`crowd_dbscan.py:58`（docstring 写“NOISE(-2)”，与实现一致）、**`crowd_dbscan.py:128`（docstring 写“-1 表示噪声”，与实现矛盾）**；`src/algorithms/crowd_dbscan.h:107-108`（`NOISE=-1, UNCLASSIFIED=-2`，噪声标签 -1）
- 复现：`dbscan_labels([(0,0),(5,0)], 1.0)` → Python 返回 `[-2,-2]`；同一输入 C++ 返回 `[-1,-1]`（均已实算验证 Python 侧）。
- 影响：同一算法两端输出的 `labels` 数组对“噪声”的取值相反，任何跨语言消费者/比对脚本按 `label==-1` 判断噪声会得到相反结论；虽然 `n_noise`/`n_clustered` 统计各自自洽，但数据契约不同。且该模块无跨语言 golden 测试（见 P1-4），此不一致未被任何测试捕获。
- 修复建议：统一哨兵值（推荐 Python 侧改为 `UNCLASSIFIED=-2, NOISE=-1` 与 C++ 对齐），并修正 `crowd_dbscan.py:128` docstring。

**P1-3 ｜ 多目标平滑输出语义不一致：Python 含漏检外推“幽灵点”，C++ 明确排除**
- 文件：Python `src/algorithms/kalman_tracker.py:323-327`（`_predict_all` 对全部轨迹外推）、`kalman_tracker.py:414-416`（`states()` 返回**所有**轨迹，含未匹配轨迹的预测位置）；消费端 `scripts/crowd_test_enhanced.py:110-111`（`smooth_pts = [(s[0],s[1]) for s in states.values()]` 全量使用）；C++ `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc:52-138`（`smoothPoints` 只输出“匹配修正点 + 新建点”，未匹配轨迹仅 `miss+1`（行 126-129），**不输出点**；空输入清空轨迹（行 59-62））。
- 影响：同一段存在漏检的视频，Python 路径的 `n/density/r_gcc` 会因漏检目标的外推点而保持偏高，C++ 路径会立即下降；FSM 输入不同 → 双端风险等级可能分叉。这是两条“生产路径”的语义不一致。
- 修复建议：给 `KalmanTracker` 增加“仅返回本帧被观测/新建的轨迹”的开关（如 `states(include_missed=False)`），`crowd_test_enhanced.py` 使用该模式；或反向在 C++ 侧保留漏检点并打标记（与现有“防幽灵点”设计冲突，不推荐）。

**P1-4 ｜ 跨语言黄金回归仅覆盖 3/6 对算法，crowd_dbscan 标签反转/bytetrack 无双端比对**
- 文件：`tests/test_golden_regression.py:24-26`（仅 graph_laplacian / ema_smoother / risk_fsm）、`src/luckfox_rtsp_yolov5/tests/test_golden_regression.cc`（同样三对）；`src/luckfox_rtsp_yolov5/tests/` 下无 crowd_dbscan/bytetrack 的 C++ 测试目标
- 影响：P1-2 这类数据契约不一致无法被现有黄金回归捕获；bytetrack 的 IOU/Hungarian 细节也无跨语言防线。
- 修复建议：将 `dbscan_crowd_features`（含 labels 原值）与 `ByteTrack` 的 IOU 矩阵/关联结果纳入 `generate_golden.py` 黄金用例，并在 C++ 主机测试侧增加对应断言。

### P2（可选）

**P2-1 ｜ graph_laplacian 对 NaN/Inf 点过滤不对称：Python 计数并保留，C++ 过滤**
- 文件：`src/algorithms/graph_laplacian.h:97-103`（`setPoints` 仅保留有限点，n 只统计有效点）；`src/algorithms/graph_laplacian.py:141-152`（`np.asarray` 后不过滤，n 计入 NaN 点）、`graph_laplacian.py:46-59`（`build_adjacency` 中 NaN 距离恒为无边）
- 复现（Python 实算）：`compute_laplacian_features([(0,0),(nan,0),(1,0)], 2.0)` → Python 返回 n=3、density=0.333、r_gcc=0.667；C++ 过滤 NaN 后 n=2、density=1.0、r_gcc=1.0。
- 影响：IPM 逆变换失败产生 NaN 时，Python 实验链与 C++ 板端特征不一致；Python 侧 n 被虚增。
- 修复建议：Python 侧在 `build_adjacency` 前过滤非有限点（与 C++ 行为对齐），或显式抛异常。

**P2-2 ｜ C++ Jacobi 未收敛时谱特征静默归零，`spectral_converged` 标志无人消费**
- 文件：`src/algorithms/graph_laplacian.h:298-305`（未收敛时 fiedler/谱半径/熵全部回退 0）、`src/luckfox_rtsp_yolov5/src/enhanced_decision.cc:159`（仅将 `converged` 存入 `res.spectral_converged`）；`enhanced_decision.h:105`（定义了该标志但 main.cc 不读取）
- 复现：构造 n 较大/病态图使 Jacobi 100 sweeps（`graph_laplacian.h:164`）不收敛。
- 影响：C++ 侧 fiedler 静默变 0（FSM 谱项关闭）而 Python 侧仍输出真实特征值，双端结果分叉且无告警。
- 修复建议：`enhanced_decision.cc` 在 `!feat.converged` 时打日志/告警；或增大 `max_sweeps`（100 对普通图足够，可保留但必须消费标志）。

**P2-3 ｜ Kalman 奇异 S 处理不一致；P 协方差长期非对称**
- 文件：C++ `src/algorithms/kalman_filter.h:111-116`（`det<1e-12` 返回零矩阵 → 增益为 0，状态不更新）；Python `src/algorithms/kalman_tracker.py:134`（`np.linalg.inv(S)`，奇异时抛 `LinAlgError`）；两侧 P 更新均用 `(I-KH)P` 非对称形式（`kalman_filter.h:210-219`、`kalman_tracker.py:136`）
- 影响：测量噪声为 0 或协方差退化时，C++ 静默跳过更新、Python 抛异常，行为不一致；长时间运行 P 失去对称性会累积数值误差。
- 修复建议：Python 侧加 `det<1e-12` 回退；两侧在更新后对称化 `P=(P+Pᵀ)/2`。

**P2-4 ｜ EMA 维度不匹配/标量输入行为不一致**
- 文件：Python `src/algorithms/ema_smoother.py:165-168`（非一维抛异常）、`ema_smoother.py:177-189`（维度不匹配抛异常）；C++ `src/algorithms/ema_smoother.h:136-159`（维度不匹配静默忽略返回旧值）、`ema_smoother.h:116-132`（标量+dim>1 时只更新第 0 维并初始化为 0）
- 影响：异常路径行为不同，跨语言移植/测试可能产生隐性差异（当前无生产消费，影响有限）。
- 修复建议：C++ 侧对齐 Python 的显式错误语义，或 Python 侧对齐 C++ 的容错语义，并在黄金测试中补充维度不匹配用例。

**P2-5 ｜ 板端性能热点：每帧 O(n³) Jacobi 特征分解 + O(n²) 距离矩阵**
- 文件：`src/luckfox_rtsp_yolov5/src/enhanced_decision.cc:150`（每帧 `computeFeatures`）；`src/algorithms/graph_laplacian.h:167-177`（每 sweep 全矩阵扫描找最大非对角元，100 sweeps 上限）；`graph_laplacian.py:56-57`（Python 侧 n×n×2 距离矩阵）
- 影响：行人 n=50+ 时板端每帧 O(n³) 全特征分解开销显著；Python 实验链几百帧 × 几十人时可接受。
- 修复建议：板端对参与特征计算的脚点做上限（如按 IPM 视野内最多 40 人截断，或只对近邻子图计算），或改用幂迭代/Lanczos 仅求第二小特征值；Python 侧无需优化。

**P2-6 ｜ `EnhancedConfig` 的 Kalman/平滑参数未暴露到 `default_config.json`**
- 文件：`src/luckfox_rtsp_yolov5/include/enhanced_decision.h:72-91`（`kalman_process_noise`/`kalman_measurement_noise`/`kalman_gate_m`/`kalman_max_miss`/`enable_smoothing`）；`src/luckfox_rtsp_yolov5/include/risk_config.h` 未解析这些键；`config/default_config.json` 无对应项
- 影响：板端调参需改头文件默认值重编译；与“阈值可配置”的工程承诺不完全对齐。
- 修复建议：在 `risk_config.h::loadRiskConfig` 中增加这些键的读取与校验（默认值与 `EnhancedConfig` 一致）。

---

## 四、边界/数值稳定性专项清单（核对通过项）

| 场景 | 结果 | 证据 |
|-----|------|------|
| 空输入 | ✅ n=0 全零特征 | `graph_laplacian.py:47-49,166-172`；`graph_laplacian.h:225-235`；`crowd_dbscan.py:66-68,133-140`；`crowd_dbscan.h:153-155` |
| n=1 单人 | ✅ r_gcc=0.0、fiedler=0.0、score=0.02 → SAFE | `graph_laplacian.py:162,182`；`graph_laplacian.h:228,230`；实算 score=0.0200 |
| 除零保护 | ✅ density（n>1 才除）、clustering（denom>0 mask）、特征熵（sum≤1e-12→0）、IOU（union≤1e-12→0）、Kalman S（C++ det<1e-12 回退） | `graph_laplacian.py:110-113,158-161,186-191`；`graph_laplacian.h:245-246,288-292,317`；`bytetrack_tracker.h:71`；`kalman_filter.h:113` |
| NaN 输入 | ⚠️ Python 计数 NaN 点（P2-1）；FSM 对 NaN fiedler 安全关闭谱项（`fiedler>0` 为 False） | `risk_fsm.py:230`；`risk_fsm.h:149` |
| 极端密度（K_n 全连接） | ✅ density=1、fiedler=n 但 score 被 `clamp01` 封顶 1.0 | `risk_fsm.py:236`；`risk_fsm.h:156` |
| 浮点累积 | ⚠️ eigvalsh 裁剪负值 vs Jacobi 容差 1e-12（差异 ≤1e-12，黄金容差 1e-4 覆盖）；Kalman P 非对称累积（P2-3） | `graph_laplacian.py:179`；`graph_laplacian.h:163`；`tests/test_golden_regression.py:31` |

---

## 五、结论

1. **risk_fsm / graph_laplacian / kalman 三对核心算法双实现一致性高**；risk_fsm 专项核对无任何阈值/滞回/确认帧/w4/单人语义不一致。
2. **无 P0 问题**（无崩溃、无数据损坏、无生产主链路不可用）。P1 共 4 项：评分校准倒挂（P1-1）、crowd_dbscan 标签反转（P1-2）、平滑幽灵点语义分叉（P1-3）、黄金回归覆盖缺口（P1-4）。
3. **两个最可能被评审/答辩质疑的点**：① 两人紧邻即 DANGER 的评分倒挂（有实算证据）；② crowd_dbscan 标签双端不一致但无测试防线（可选扩展，影响有限但暴露测试覆盖盲区）。
4. 上一轮已闭环的配置键、消融基线、谱特征接入问题本次确认保持闭环；`default_config.json` 与 `risk_fsm.py`/`risk_config.h` 键级一致。

> 审查依据全部为只读操作（读文件、grep、Python 实算）。未运行 C++ 主机测试（本机无 g++，CI/目标板覆盖）。