# Agent 6 严格论文审稿报告

> 审稿人角色：毕设论文答辩评审（严格模式）
> 审查对象：`H:/PedestrianRiskDecision` 仓库（含 README、src、scripts、results、docs）与 `H:/毕设论文/` 论文初稿（6 章版 + `thesis_agent_draft/毕业论文.md` 8 章版 + 答辩问答手册）
> 审查方式：只读，未修改任何仓库文件
> 审查日期：2026-10-26
> 结论先行：**论文的核心叙事（"图拉普拉斯谱特征是创新点并提升风险语义准确性"）被仓库自身的消融数据推翻；评估体系用稳定性代替准确性、用检测效果代替风险判断，存在明显逻辑跳跃；多处"实现与描述不一致"会在答辩时被追问。**

---

## 一、总体判断（答辩评委最可能否决的角度）

1. **创新点不成立**：论文把"图拉普拉斯谱特征"当作复杂网络理论的核心创新，但代码中谱特征（Fiedler 值/谱半径/特征熵）只被计算并打印，**从未进入任何决策**。风险等级完全由 `n / density / r_gcc` 三个非谱的图结构指标决定。消融数据（B 组 = C 组）已自证谱特征零贡献。
2. **用稳定性偷换准确性**：全文以"等级切换次数下降"作为主要成功指标。但没有 GT，切换次数低既可能是"抑制误报"，也可能是"把真实 DANGER 全部压成 SAFE"（111 条视频中增强后 DANGER 帧从 21969 降到 7807，无法区分好坏）。
3. **用检测效果代替风险判断**：第五章用"高密度检出率 79.4%"来论证"复杂网络特征对聚集场景感知能力强"——检出率高只说明检测器找得到人，与"风险等级判得对不对"没有任何关系。
4. **实验检测器与部署模型不一致**：全部实验用 PC 端 `YOLOv8n`（FP32/PyTorch），部署的是 `YOLOv5n INT8`（RKNN/NPU）。论文的 82.5% 检出率从未在部署模型上验证过。
5. **文档内部自相矛盾**：摘要/第五章写"5 条视频"，README/8 章稿写"111 条视频"；高密度检出率出现 79.4%（论文）与 76.7%（README/rerun）两个版本；论文 4.4.1 规则表用 AND，代码用 OR。

仓库自己的 `docs/review/BASELINE_RESULTS.md` 与 `答辩问答手册_20261026.md` 其实已诚实承认了以上绝大多数问题（"无人工 GT""消融谱特征零贡献""IPM 未真机标定""Jacobi O(n³)"），但论文正文仍在夸大。**答辩策略应当是：论文措辞收敛到工程集成，主动承认局限，而不是继续吹"谱特征提升风险语义准确性"。**

---

## 二、关键证据与问题清单

### P1【严重】谱特征对风险决策零贡献，却被论文宣称为核心创新点

**证据路径**：
- `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`（`processFrame`）：计算 `SpectralFeatures feat`（含 fiedler/spectral_radius/eig_entropy）并写入 `res`，但构造 `FsmFeatures f` 时**只填了 `n/density/r_gcc/clustering/trend`**，谱特征从未传入 FSM。
- `src/algorithms/risk_fsm.py` 与 `src/algorithms/risk_fsm.h`：`score = w1*norm(n) + w2*density + w3*r_gcc`，注释明确写"clustering 预留，当前不参与评分"，无任何 Fiedler/谱半径/特征熵项。
- `scripts/ablation_study.py`：C 组"＋谱特征"把 `fiedler/spectral_radius/...` 加进 `feat` 字典后**没有任何消费方**，等级仍由 `rule_level(n,density,r_gcc)` 或 FSM 决定。
- `results/rerun/_ablation.json`：6 条视频中 **B（+Kalman）与 C（+Kalman+谱特征）的 switches 与分布逐字节完全相同**。
- 论文 `05_第四章_关键算法实现.md` 4.3 节、`01_摘要.md`、README 均把"图拉普拉斯谱特征"描述为系统核心创新并称"谱特征提升风险语义准确性"。

**严重度**：致命。答辩老师只要追问"Fiedler 值到底喂给了哪个决策分支？消融 B=C 怎么解释？"论文即失守。

**解决建议**：
- 若谱特征确实不用，就把"复杂网络拓扑特征（密度/连通分量/聚类系数）"作为创新点，把"谱特征"降级为"可选的补充观测量"，并删除"谱特征提升风险语义准确性"这句无依据结论。
- 若要保留谱特征的价值，必须设计一个谱特征**真正参与决策**的对照实验（如用谱间隙区分"长链人群 vs 团簇人群"），并给出可测量收益。当前论文没有任何这类实验。

---

### P2【严重】用稳定性代替准确性：111 条视频无 GT，"零误报""正确抑制误报"无法成立

**证据路径**：
- `results/rerun/_comparison_summary.json`：`base_lv.DANGER=21969 → enh_lv.DANGER=7807`，增强后 DANGER 帧减少约 64%；`base_danger=102 → enh_danger=40` 条视频出现 DANGER。这些重分类没有 GT 可判定对错。
- 论文 `06_第五章_实验与性能分析.md` 5.3 节结论"稀疏场景零误报""瞬时聚集被正确抑制"——"误报/正确"需要 GT，但 111 条视频无风险标注。
- `docs/review/BASELINE_RESULTS.md` 自认："无人工 GT、误报/漏报率无法计算（待处理）"。

**严重度**：严重。全文核心指标是切换次数，但"切换 4 次"可能意味着把 264 帧真实 DANGER 压成 179 帧（见 mixkit_13192：C 组 DANGER 269 → D 组 DANGER 179，119 帧被降为 WARNING）。

**解决建议**：至少对 6 条消融视频做**人工风险标注（每帧 SAFE/WARNING/DANGER）**，报告准确率/漏报率/报警延迟；在此之前，论文只能写"降低了切换抖动（稳定性指标）"，绝不能写"零误报/正确抑制"。

---

### P3【严重】用检测效果代替风险判断：检出率高 ≠ 风险判得对

**证据路径**：
- 论文 `06_第五章_实验与性能分析.md` 5.2 节："高密度（14 人以上）场景检出率达 79.4%，说明复杂网络特征对人群聚集场景具有较强的感知能力。"
- 该数据来自 `results/rerun/images500_baseline/results.json`：500 图中高密度子集仅 **12 张、175 个 GT 行人**（139 检出 = 79.4%），样本量极小。
- 检出率只是检测器召回率，与"SAFE/WARNING/DANGER 判断是否正确"无因果关系。

**严重度**：严重。答辩老师会问："你证明了检测找得到人，怎么证明风险分级是对的？"

**解决建议**：删除这一因果推理；把检出率只当作"检测模块性能"，风险决策的评估另立 GT 或人工核对。

---

### P4【严重】实验用 YOLOv8n，部署用 YOLOv5n INT8，检出率从未在部署模型上验证

**证据路径**：
- `scripts/crowd_test.py`、`scripts/crowd_test_enhanced.py`、`scripts/ablation_study.py`、`scripts/rerun_all_videos.py` 全部加载 `yolov8n.pt`（Ultralytics，PC/GPU，FP32）。
- 开发板部署的是 `yolov5.rknn`（YOLOv5n INT8，RKNN），见 `src/luckfox_rtsp_yolov5/src/main.cc` 的 `model_path="./model/yolov5.rknn"`。
- `results/edge_board/EDGE_TEST_REPORT.md`：真机日志全部为 `n=0`（摄像头前无人），**从未用真实行人验证过部署模型的检出率**。
- 论文 `06_第五章_实验与性能分析.md` 5.4 节"当行人进入摄像头视野时 n 将大于 0……"是推测，不是实测。

**严重度**：严重。论文的 82.5% 检出率与部署系统无关；且边缘端 INT8 量化通常有精度损失，论文未量化。

**解决建议**：用 INT8 RKNN 模型在开发板（或 PC 模拟 RKNN）上对 500 图或一个行人视频子集重跑，给出部署模型的实际检出率与量化精度损失。

---

### P5【严重】IPM 单应是硬编码默认值，未真机标定，论文却称"预先标定"

**证据路径**：
- `config/default_config.json`：`"homography_file": ""`（空串＝用默认硬编码单应）。
- `src/luckfox_rtsp_yolov5/src/main.cc`：`srcPts`/`dstPts` 是写死的梯形与 6m×4m 地面矩形，`cv::getPerspectiveTransform` 直接算 H；注释自己写"默认硬编码单应，相机位姿变化时配置此路径即可"。
- `scripts/crowd_test.py::get_homography`：对 COCO 图片/Mixkit 视频用**同一套 720×480 假定梯形按图像尺寸缩放**——这些素材的相机内外参完全未知，所谓"地面米制坐标"是虚构的。
- 论文 `05_第四章_关键算法实现.md` 4.2 节写"利用预先标定的单应矩阵 H"，`04_第三章_系统总体设计.md` 写"利用预先标定的单应矩阵 H 将脚点映射到地面米制坐标"。
- `docs/review/BASELINE_RESULTS.md` 自认："IPM 默认 H 未真机标定、广角畸变未校正（待处理）"。

**严重度**：严重。所有"2.0m 建边""6m×4m 地面""密度/连通度"都建立在未经标定的假设单应上，地面距离数值不可信。

**解决建议**：论文必须把 IPM 表述为"基于假定相机姿态的几何近似"，并明确"真机标定（`ipm_calibrate.py` + `homography_file`）是配置项/后续工作"；若时间允许，用棋盘格或实际地面尺寸做一次真机标定，量化误差。

---

### P6【中等】论文 4.4.1 规则表与代码不一致（AND vs OR）

**证据路径**：
- 论文 `05_第四章_关键算法实现.md` 4.4.1 表：`DANGER = n≥10 且 ρ≥0.55 且 r_gcc≥0.80`（AND）。
- 代码 `src/luckfox_rtsp_yolov5/src/risk_engine.cc::evaluate()` 与 `scripts/crowd_test.py::evaluate_level`：`DANGER = n≥10 && (ρ≥0.55 || r_gcc≥0.80)`（OR）。

**严重度**：中等。论文描述的规则比实际代码更严格，读者复现会得到不同的等级分布；答辩老师核对代码即发现矛盾。

**解决建议**：论文按实际 OR 逻辑改写表格，或在论文中说明"任一指标超阈值即判级"。

---

### P7【中等】DBSCAN / ByteTrack / EMA 并未接入实际流水线，论文却描述为系统组成部分

**证据路径**：
- grep 全仓库：`bytetrack`、`crowd_dbscan`、`ema_smoother` 在 `src/luckfox_rtsp_yolov5/`（部署工程）和 `scripts/`（实验脚本）中**零引用**，只以独立 header 存在（`src/algorithms/bytetrack_tracker.h` 等）。
- 实际部署链路（`main.cc` → `EnhancedDecision`）只包含：YOLOv5n + IPM + GraphLaplacian(仅算) + 逐目标 Kalman 最近邻关联 + FSM。
- `thesis_agent_draft/毕业论文.md` 摘要/正文写"经 Kalman/ByteTrack 多目标跟踪与 EMA 平滑抑制单帧抖动……并结合 DBSCAN 人群聚类刻画聚集结构"。
- `docs/ARCHITECTURE.md`、`docs/SYSTEM_PRINCIPLE.md` 的架构图也把 DBSCAN/ByteTrack/EMA 画进数据流。

**严重度**：中等偏重。答辩老师若要求"演示 ByteTrack 遮挡恢复"或"展示 DBSCAN 簇输出"，现场无法给出——因为根本没集成。

**解决建议**：论文严格区分"已实现并用于决策的模块"（YOLOv5n+IPM+图结构特征+Kalman+FSM）与"提供 header/待集成模块"（DBSCAN/ByteTrack/EMA），后者归入"扩展设计/后续工作"。

---

### P8【中等】谱特征在边缘端是 O(n³) Jacobi 纯开销，论文未评估其成本收益

**证据路径**：
- `src/algorithms/graph_laplacian.h`：Jacobi 特征分解，`max_sweeps=100`，每帧对 L 做 O(n³) 分解，且结果不用于决策。
- `docs/review/BASELINE_RESULTS.md`："性能 | 单线程串行、Jacobi O(n³)、每帧堆分配 | 待分析"。
- 真机帧率仅 6.9 FPS（调试版），`EDGE_TEST_REPORT.md` 承认瓶颈包括增强算法。

**严重度**：中等。论文大谈"适合边缘端在线运行"，但谱分解是每帧最重的计算之一，却对输出等级无贡献——成本收益为负。

**解决建议**：要么让谱特征真正影响决策并给出收益，要么从板端流水线移除谱分解以提速，并报告移除前后的帧率对比。

---

### P9【中等】论文规模数据自相矛盾：5 条 vs 111 条视频，79.4% vs 76.7%

**证据路径**：
- `H:/毕设论文/01_摘要.md` 与 `06_第五章_实验与性能分析.md`：写"在 5 条真实人群视频的增强实验"，表格只列 3 类场景。
- `README.md` 与 `results/rerun/README.md`：111 条视频全量重跑（45021 帧），`_comparison_summary.json` 有 111 对数据。
- 高密度检出率：论文/ARCHITECTURE/8章稿=**79.4%**（`results/rerun/images500_baseline/results.json` 复算 139/175=79.4%，12 张）；`README.md` 与 `results/rerun/README.md`=**76.7%**（9 张）。

**严重度**：中等。论文数字与仓库最新结果不一致，说明论文未随实验更新；答辩老师翻 README 即发现。

**解决建议**：统一全文为"111 条视频全量重跑 + 6 条消融"，统一高密度检出率口径（写明子集定义：GT≥14 还是检测≥14，多少张）。

---

### P10【中等】检出率指标粗糙：无 IoU 匹配、无精度、无 mAP

**证据路径**：
- `results/rerun/images500_baseline/results.json` 的"检出率"= 检测框总数 / GT 人数（2548/3089），未说明每个检测框是否与 GT 行人做 IoU 匹配，也未报告误检数/精度。
- 论文 `06_第五章` 5.5.1 只给"检出率"，无 precision/recall/F1/mAP。

**严重度**：中等。单靠"检出率"无法排除重复计数与误检；且 12 张高密度图（175 GT）样本过小，79.4% 的置信区间极宽。

**解决建议**：补 IoU=0.5 的匹配逻辑，报告 precision/recall；高密度结论改为"在 12 张图上初步验证"。

---

### P11【中等】消融只有 6 条视频、单指标（切换次数），无法区分各模块贡献

**证据路径**：
- `results/rerun/_ablation.json`：仅 6 条视频；唯一指标是 switches（另附等级分布）。
- 组合设计有缺陷：B→C 加谱特征但谱特征不进决策，故 C≡B；D 组（完整）相比 C 同时改变 FSM 阈值与状态机，其分布差异（如 mixkit_134 的 DANGER 382→241）被笼统归因于"FSM 稳定"，实际是**重新阈值化**而非纯去抖。
- 论文第五章没有正式呈现消融实验，README 却引用消融图。

**严重度**：中等。消融不能回答"谱特征带来什么可测量收益"，反而暴露其零贡献。

**解决建议**：把消融改为"每个组件真正参与决策"的正交组合（A 无 / B +Kalman / C +DBSCAN / D +FSM 等，且每个组件都实际影响输出），并用准确率（有 GT 后）或至少报告等级分布变化而非仅切换次数。

---

## 三、答辩最可能被追问的 5~10 个尖锐问题

以下问题按杀伤力排序，均已含【证据路径】【严重度】【解决建议】。

### Q1（致命）
> "你的图拉普拉斯谱特征（Fiedler 值）到底喂给了哪个决策函数？我在 `enhanced_decision.cc` 里看到它只写进了结果结构体，FSM 的输入只有 n/density/r_gcc。你的消融 B=C 是不是自证谱特征零贡献？那论文把它列为创新点依据是什么？"
- 【证据路径】`src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`、`src/algorithms/risk_fsm.py`、`results/rerun/_ablation.json`
- 【严重度】致命
- 【解决建议】删除"谱特征提升风险语义准确性"；把创新点改述为"图结构指标（密度/连通分量）驱动的边缘端风险决策"；或将谱特征真正接入决策并补实验。

### Q2（致命）
> "你的 111 条视频没有 GT。增强后 DANGER 帧从 21969 降到 7807、40 条视频才触发 DANGER（基线 102 条）。你怎么区分这是'抑制误报'还是'把真实危险压成了 SAFE'？'零误报'的 GT 在哪？"
- 【证据路径】`results/rerun/_comparison_summary.json`、论文 5.3 节
- 【严重度】致命
- 【解决建议】补人工 GT；在无 GT 前，所有"零误报/正确抑制"改为"切换抖动下降（稳定性指标）"。

### Q3（致命）
> "实验用的是 PC 上的 YOLOv8n（FP32），部署的是 YOLOv5n INT8。82.5% 检出率不是部署模型的指标。你如何保证 INT8 模型在开发板上仍有这个检出率？真机日志为什么全是 n=0？"
- 【证据路径】`scripts/crowd_test.py`（yolov8n.pt）、`main.cc`（yolov5.rknn）、`results/edge_board/EDGE_TEST_REPORT.md`（n=0）
- 【严重度】致命
- 【解决建议】用 INT8 模型重跑 500 图或行人视频，给出部署模型真实检出率与量化损失。

### Q4（严重）
> "你的 IPM 单应是硬编码的假定梯形（config 里 homography_file 是空），COCO 图片和 Mixkit 视频的相机参数完全未知。那'2.0m 建边'的地面距离是真实的吗？'6m×4m'从哪来的？论文写'预先标定'是不是不实？"
- 【证据路径】`config/default_config.json`、`scripts/crowd_test.py::get_homography`、`main.cc`
- 【严重度】严重
- 【解决建议】论文改称"假定相机姿态的几何近似"，真机标定列为后续工作；最好补一次真机标定。

### Q5（严重）
> "检出率 79.4% 说明检测器找到了行人，这跟'风险等级判得对不对'有什么关系？你的风险等级有没有任何准确率指标？"
- 【证据路径】论文 5.2 节、`results/rerun/images500_baseline/results.json`
- 【严重度】严重
- 【解决建议】把检出率限定为检测模块性能；风险决策单独用 GT 评估。

### Q6（严重）
> "论文第四章的规则表写 DANGER 需要 n≥10 且 ρ≥0.55 且 r_gcc≥0.80，可你的代码是 OR（任一即可）。以哪份为准？"
- 【证据路径】`05_第四章_关键算法实现.md` vs `risk_engine.cc::evaluate()` / `crowd_test.py::evaluate_level`
- 【严重度】中等偏重
- 【解决建议】统一为 OR 逻辑。

### Q7（中等偏重）
> "你说系统包含 ByteTrack、EMA、DBSCAN，能现场演示一下 ByteTrack 遮挡恢复或 DBSCAN 输出吗？这些在部署工程里根本没有被调用。"
- 【证据路径】grep 结果：`src/luckfox_rtsp_yolov5/` 与 `scripts/` 对 bytetrack/crowd_dbscan/ema 零引用
- 【严重度】中等偏重
- 【解决建议】论文区分已集成模块与"扩展设计"，不要写进系统架构。

### Q8（中等）
> "谱特征在板端每帧要做 O(n³) 的 Jacobi 分解（100 sweep），它却不影响任何决策。这个计算成本值得吗？帧率只有 6.9 FPS 与此有关吗？"
- 【证据路径】`graph_laplacian.h`、`docs/review/BASELINE_RESULTS.md`（Jacobi O(n³) 待分析）
- 【严重度】中等
- 【解决建议】要么让谱特征产出决策收益，要么移除谱分解，报告帧率提升。

### Q9（中等）
> "你的论文写'5 条视频'，README 写 111 条；高密度检出率论文是 79.4%，README 是 76.7%。哪个是对的？论文是不是没跟上实验？"
- 【证据路径】`01_摘要.md`、`06_第五章` vs `README.md`、`results/rerun/README.md`、`results/rerun/images500_baseline/results.json`
- 【严重度】中等
- 【解决建议】全文统一到 111 条/6 条消融、统一高密度口径并写明子集定义。

### Q10（中等）
> "'检出率'是怎么算的？检测框和 GT 行人做了 IoU 匹配吗？误检怎么算？12 张高密度图支撑 79.4% 是不是样本太小？"
- 【证据路径】`results/rerun/images500_baseline/results.json`（2548/3089 无 IoU 说明）
- 【严重度】中等
- 【解决建议】补 IoU=0.5 匹配与 precision/recall；高密度结论注明样本量。

---

## 四、给作者的修改优先级（供参考，不强制）

1. **立即删除/弱化**：摘要与正文中"谱特征提升风险语义准确性""零误报""预先标定"等无依据表述。
2. **统一数字**：111 条视频、高密度检出率口径、规则表 AND/OR。
3. **补充或删减**：若无法在答辩前做 GT 标注，就把实验章节改成"稳定性（切换次数）+ 资源占用 + 检测召回率"三类如实指标，并明确"风险准确率待 GT 评估"。
4. **架构描述**：系统实际 = YOLOv5n(INT8) + IPM(默认单应) + 图结构特征(n/density/r_gcc) + 逐目标 Kalman + FSM；DBSCAN/ByteTrack/EMA/谱特征一律降级为"可选/扩展模块"。
5. **答辩现场准备**：主动在 PPT 里写"已知局限"（无 GT、IPM 未标定、实验/部署模型不一致），比被老师逼问更有说服力。

---

*本报告仅作只读审查，未修改任何仓库文件。所有证据路径可直接打开复核。*