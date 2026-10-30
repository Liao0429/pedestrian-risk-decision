# 独立评审报告：算法/统计严谨性（B1）

- 评审对象：G:/PedestrianRiskDecision（行人风险决策毕设项目）
- 评审日期：2026-10-30
- 评审范围：`src/algorithms/` FSM 与谱特征实现 vs 文档口径；一致率结论复现；Spearman 相关、检出率、Fiedler 单调性统计表述；新增统计/算法问题
- 复现命令：`cd /g/PedestrianRiskDecision && env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" -m pytest tests -q`（190 passed, 4 skipped）；`env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" scripts/reproduce_consistency.py`
- 复现结果：M0_ms33=89.6737%（40372/45021）、PROD_ms33=91.2974%（41103/45021）、PROD_frame=90.5911%（40785/45021）、PROD_fps=91.6350%（41255/45021）——与 `results/rerun/_consistency.json` 完全一致。

---

## 一、总体结论

核心实现层面：Python 与 C++ 的 FSM 逻辑（评分公式、滞回、确认帧、保持、阶梯升降级、`min_danger_n` 门控）基本一致，`graph_laplacian.py` 与 `graph_laplacian.h` 的谱特征计算（含 P0-4 去重）也已对齐；单元测试 190 项全部通过。一致率数字（89.6737%/91.2974%/90.5911%/91.6350%）与 README、`_consistency.json` 完全可复现；消融实验 D 组 vs C 组 DANGER 帧 +152（+10.2%）可复现；检出率 82.5%（2548/3089）可复现。

**但发现一个影响论文核心统计的系统性问题（P0）：`results/rerun/video_enhanced/*/*_timeline.json` 生成于 P0-4 门控修复之前，其中 34.4% 的 DANGER 帧（4041 帧，48 条视频）在当前生产 FSM 语义下不可能产生（含 173 帧 n=0 仍判 DANGER）。README 的全部头部统计（11748 DANGER 帧、66 条出现 DANGER、切换均值 5.86、Fiedler 单调均值、Spearman 0.688）以及 `_aggregate.json`、`_spectral_fsm_experiment.json` 均建立在这份过期时间线上；"7807 → 11748（谱特征提升高风险识别）"的归因也混入了门控修复带来的差异。这是第一轮"门控修复"后的遗留：代码修了，但下游统计没有随修复重算（修复后仍存在）。**

其余新发现问题集中在：文档公式/配置表缺 w4 与门控、Python 原型主特征路径未去重（板端已去重，两者不一致）、Spearman 稳健性表述、M2 实验目标未达成、检出率只报召回、M0 重算对 now_ms 模式敏感等。详见 P0/P1/P2 清单。

---

## 二、问题清单

### P0

#### P0-1（修复后仍存在）：核心结果时间线早于 P0-4 门控修复，34.4% 的 DANGER 帧在当前生产 FSM 下不可复现，README 生产口径数字失真

**证据**

- 时间线文件 `results/rerun/video_enhanced/mixkit_134/mixkit_134_timeline.json` 修改时间 2026-10-28 15:16，早于含 P0-4 修复的提交 `284de8d`（2026-10-29 21:32，`git log src/algorithms/risk_fsm.py`）。README 中"历史生产快照"提交 `a92e4a5` 为 2026-10-27。
- 对 111 条存档时间线逐帧统计：存档等级分布 SAFE 26885 / WARNING 6388 / DANGER 11748（`results/rerun/_aggregate.json:4-8`）。其中 **DANGER 帧有 4041 帧 n<3（占 DANGER 34.40%），涉及 48 条视频**；细分：n=0 有 173 帧、n=1 有 1075 帧、n=2 有 2793 帧（本次评审实测）。
- 当前生产配置（`config/default_config.json` fsm 段，w4=0.1）按真实 fps 回放 45021 帧：一致率 91.6350%（与 `_consistency.json:22-27` 一致），等级分布变为 SAFE 28214 / WARNING 8355 / DANGER 8452，DANGER 视频数 66 → 45，切换均值 5.86 → 5.12。
- 存档中 4041 帧 n<3 的 DANGER 里，当前 FSM 回放**仍输出 DANGER 的只有 1163 帧**（滞回保持，属设计行为），**其余 2878 帧当前输出为 WARNING/SAFE**——即门控修复使这些帧不可能再判 DANGER。
- 具体实例：`mixkit_134_timeline.json` frame 41-52，n=2、density=1.0、r_gcc=1.0、fiedler=2.0，存档等级 DANGER；当前代码回放等级 WARNING（n<3 门控将 score 封顶在 0.64 < up_danger=0.65，见 `src/algorithms/risk_fsm.py:244-248`）。
- 不一致帧的归因：当前回放共 3845 帧不一致，其中 n<3 帧占 3215（83.6%），fiedler>0 帧占 1877（48.8%）。`_consistency.json:32` 记载"差异原因待查"——本次评审定位为**存档时间线早于门控修复**。

**影响**

README L79-80、L83 的"等级分布 11748 / 66 条出现 DANGER / 31 条全程 SAFE / 切换 5.86 次 / DANGER 帧 2190 → 11748"，`_aggregate.json` 全部基于过期时间线；`_spectral_fsm_experiment.json` 的 M0/M1/M2 对比也以该时间线等级为参照。第一轮已修复门控，但这些下游统计数字没有随修复重算——属于"修复后仍存在"。

**修复建议**

用当前 `scripts/crowd_test_enhanced.py`（已传 fiedler 并含门控，见 L129-130、L139-141）重新生成 111 条时间线，再重算 `_aggregate.json`、`_spectral_fsm_experiment.json`、`_ablation.json`、`_consistency.json` 与图 4-9；对外口径统一用重算后的生产配置数字（本次回放提示 DANGER 帧约 8452、DANGER 视频数 45，重算后以此为准）。

#### P0-2："7807 → 11748（谱特征提升高风险识别）"归因错误，把门控修复引入的差异算成了谱特征贡献

**证据**

- README L80："111 条批量 DANGER 帧 7807 → 11748（谱特征提升高风险识别）"。
- 实测（本次评审，fps 模式，45021 帧）：当前代码 M0（w4=0）DANGER=7801；当前代码生产（w4=0.1）DANGER=8452。**谱特征在当前生产语义下的净贡献 ≈ 651 帧（+8.3%）**。
- 11748 − 8452 = 3296 帧来自旧代码（无门控）的不可复现输出；而 README 把 7807→11748 的 +3941 帧全部表述为"谱特征提升高风险识别"。
- `_spectral_fsm_experiment.json` 中 M0（ms33）=7906 与生产存档 11748 的差 3842 帧同样包含门控差异。

**影响**

论文若引用"谱特征使 DANGER 帧大幅提升（约 50%）"的表述，与当前生产代码的实际行为不符；真实谱项贡献约 +8% 帧数（在门控语义下）。

**修复建议**

用重算后的 M0 vs 生产配置 DANGER 帧差作为谱特征贡献，并注明差值中门控/版本差异的份额；对外不再使用 11748 作为"生产口径"。

---

### P1

#### P1-1：算法文档 `docs/algorithms.md` 的 FSM 评分公式与配置表缺少 w4 谱项与 min_danger_n 门控

**证据**

- `docs/algorithms.md:302-304`：`score = w1 * norm(n) + w2 * density + w3 * r_gcc`（无 w4 项）；9.4 配置表（L344-356 附近）列出 w1/w2/w3、n_ref、阈值、确认帧、保持、trend_bias，**无 `w4`、无 `min_danger_n`**（`grep -n "w4\|min_danger" docs/algorithms.md` 无任何命中）。
- 实现：`src/algorithms/risk_fsm.py:27,88,92,236-240` 含 w4 谱项；`:94,244-248` 含 min_danger_n 门控；`src/algorithms/risk_fsm.h:63,159-161` 同。README L80 声称"w4=0.1 已接入生产 FSM"。
- 后果：读者按 `docs/algorithms.md` 复现评分公式会漏掉谱特征项与门控，文档口径与生产实现不一致（任务第 1 项"FSM 与谱特征实现是否与文档口径一致"检查失败）。

**修复建议**

在 `docs/algorithms.md` 9.2 公式与 9.4 配置表补充 `w4`（0.1，`score += w4 * fiedler/(fiedler+1) * r_gcc`）与 `min_danger_n`（3），并注明"n<min_danger_n 时 score 封顶于 up_danger-0.01"。

#### P1-2（修复后仍存在）：Python 原型 FSM 的 n/density/r_gcc 未去重，而 C++ 板端已去重——P0-4 去重只完成了一半

**证据**

- Python 原型：`scripts/crowd_test_enhanced.py:113-130` 中 `n/density/r_gcc` 来自 `compute_features`（`scripts/crowd_test.py`，**无任何去重逻辑**），仅 `fiedler` 单独来自 `compute_laplacian_features`（`src/algorithms/graph_laplacian.py:152-156` 去重）。FSM 收到的 n 是未去重的原始人数。
- C++ 板端：`src/luckfox_rtsp_yolov5/src/enhanced_decision.cc:148-166` 将 `graph_.computeFeatures()` 的 `feat.n/density/r_gcc/fiedler` 全部送入 FSM；`graph_laplacian.h:222-233` 在 computeFeatures 内去重（"P0-4 同步"）。
- 后果：同一帧视频，Python 原型与板端送入 FSM 的 n 可能不同（重复脚点时），而论文全部统计来自 Python 原型；去重修复（P0-4）在 Python 主特征路径上未落地——"修复后仍存在"。

**修复建议**

在 `scripts/crowd_test.py::compute_features` 入口做与 `graph_laplacian.py:152-156` 相同的完全重合点去重，或统一改为调用去重后的谱特征结果，保证 Python 与板端输入一致。

#### P1-3：按视频聚合的 Spearman 数值口径需要补全稳健性数字（31 条无变化视频按 0 计入；fiedler>0 子集 ρ 大幅下降）

**证据**

- 本次评审实测：全量 45021 帧 Spearman ρ=0.6882（复现 README 0.688）；按视频 80 条有效（等级有变化）均值 0.3154、中位 0.2849；31 条全程 SAFE（无变化）视频若按 ρ=0 计入，均值 0.2273（与 cross_R10 记录一致）。
- fiedler>0 的 10814 帧子集上 ρ 仅 0.3503——全量 0.688 主要由"SAFE 帧 fiedler≈0 vs DANGER 帧 fiedler>0"的组间差异驱动，帧内连续单调关系明显更弱。
- README L95 只给出 0.688，并已有同源构造性免责；但未披露按视频聚合与正连通子集数字。

**影响**

ρ=0.688 容易让读者高估"Fiedler 越高风险越高"的逐帧单调强度；论文若引用，建议给出上述稳健性数字。

**修复建议**

在论文/README 补充：按视频聚合均值 0.23~0.32、fiedler>0 子集 ρ≈0.35、31 条无变化视频的处理方式，并明确 0.688 描述的是组间差异。

---

### P2

#### P2-1："Fiedler 均值随等级单调上升"对中位数不成立，且均值远小于标准差

**证据**

- `results/rerun/_aggregate.json:130-154`：SAFE mean 0.0174 / median 0.0 / std 0.1841；WARNING mean 0.2143 / median 0.0 / std 0.5721；DANGER mean 1.0779 / median 0.814 / std 1.1382。
- SAFE 与 WARNING 的中位数均为 0（WARNING 帧中 fiedler>0 仅 2250/6388≈35%）；"单调上升"只对均值成立。README L80 与 fig8 标题"谱连通度随风险等级单调上升"为因果口吻。

**修复建议**

表述改为"Fiedler 均值随等级升高而增大（分布高度右偏，SAFE/WARNING 中位数为 0）"；图注补充中位数与正连通帧占比。

#### P2-2：M2 实验的降级帧 fiedler 并不低于保留帧，与"抑制链状伪聚集（低 fiedler）"预期不符

**证据**

- `results/rerun/_spectral_fsm_experiment.json` m2_vs_m0_analysis.fiedler_stats：M2 仍 DANGER 帧 mean 1.11 / median 0.6901；M2 被降级（原 DANGER）帧 mean 1.0933 / median 0.817。
- 被降级帧的中位 fiedler 反而更高，说明 M2 并非特异作用于低 fiedler 帧；M2 的降级 4360 帧横跨整个 fiedler 分布。

**影响**

若论文引用 M2 支持"谱项能特异抑制链状伪聚集"，数据不支持该机制描述。

**修复建议**

论文如需引用 M2，应如实表述"未观察到对低 fiedler 帧的特异性"，或重新设计 M2 的抑制条件后再实验。

#### P2-3：检出率只报召回（82.5%），无精度/误检率，且存在明显过检

**证据**

- `results/images500_gt_eval/summary.json:5-7`：gt_total 3089、det_total 2548、overall_detection_rate_pct 82.49——即 det/gt（召回口径）。
- 同一文件 `worst_over_detected`（L126-187）：如 000000538291_13 GT=13 det=25（+12）、000000270071_13 GT=13 det=22（+9）等，但没有任何总体 precision/过检率指标。
- README L70-73 仅给出"检出率（总体）82.5%"。

**修复建议**

补充 precision（或过检率）指标，至少在图注注明 82.5% 为召回口径（报告已部分说明，README 建议同步）。

#### P2-4：M0 重算的 DANGER 帧数对 now_ms 模式敏感，实验脚本按"与存档一致率更高"选择模式，形成对过期存档的隐式过拟合

**证据**

- `_spectral_fsm_experiment.json`：now_ms_mode=ms33，M0 DANGER=7906；本次评审用生产 fps 模式重算 M0 DANGER=7801（差 105 帧）。
- `scripts/spectral_fsm_experiment.py:178-191`：在 frame/ms33 之间选择与存档时间线 M0 一致率更高的模式（best_mode），而非直接采用生产 fps 模式。由于存档本身是过期版本（见 P0-1），这种选择是在向过期参考对齐。

**修复建议**

统一使用生产 fps 模式（`reproduce_consistency.py` 已支持），并在报告中给出不同模式的波动区间而非单一值。

#### P2-5："高密度图（14+人）检出率 79.4%"基于 12 张图、175 个 GT 的小样本

**证据**

- `results/images500_gt_eval/summary.json:35-40`：bucket "14-19" images=12、gt=175、det=139、rate_pct=79.43。

**修复建议**

在论文中注明样本量（12 张图/175 人）或给出置信区间；不建议与其他桶做显著性对比。

#### P2-6：门控只约束 DANGER 的"进入"，不约束"保持"——当前代码回放仍有 1203 帧 n<3 的 DANGER（滞回保持）

**证据**

- `src/algorithms/risk_fsm.py:244-248` 仅将 score 封顶在 up_danger-0.01，降级仍依赖 down_warning=0.55；两人紧贴（density=1、r_gcc=1）时 score=0.64 > 0.55，DANGER 可一直保持。
- 本次评审：当前生产配置回放仍有 1203 帧 n<3 的 DANGER（其中 1163 帧与存档 n<3 DANGER 重合）。
- `risk_fsm.py` 文档（L29-31）说"1-2 人的偶遇/伴行不可能触发 DANGER"——"触发"准确，但读者可能误以为 n<3 时不会输出 DANGER。

**修复建议**

文档补充"门控仅禁止 n<3 时进入 DANGER；进入后因滞回/保持仍可能短暂保持"；若语义要求 n<3 立即降级，需另行设计强制降级逻辑（不建议）。

---

## 三、未验证项

1. **用当前代码重新生成 111 条时间线**：需要 YOLO 模型 + GPU 推理，本次评审未执行。上文"当前回放分布"是基于存档特征的重算，与完整重跑可能略有差异（检测/跟踪环节相同则应当一致）——**未验证**。
2. **C++ 板端（Luckfox）实际运行**：无板端环境，`graph_laplacian.h` 的 Jacobi 特征分解在 ARM/RKNN 板端的数值行为**未验证**（`graph_laplacian.h:17-19` 亦已注明）。
3. **消融实验 6 条代表性视频的差异显著性**：未做统计检验，**未验证**是否显著。
4. **31 条全程 SAFE 视频**的"正确不报 vs 漏报"：README 已声明无人工真值，**未验证**。
5. **检出率 82.5% 的 GT 一致性**：`tiny_gt_summary.json` 显示文件名后缀人数与 COCO person 实例数 500/500 一致（gt_consistency.ok=true），但该一致性校验脚本的独立复核**未验证**。

---

## 四、附：本次评审实测命令与输出摘要

- `env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" scripts/reproduce_consistency.py` → `M0_ms33: 40372/45021 = 89.6737%`、`PROD_ms33: 91.2974%`、`PROD_frame: 90.5911%`、`PROD_fps: 91.6350%`
- 全量存档统计：SAFE 26885 / WARNING 6388 / DANGER 11748；DANGER(n<3)=4041（n=0:173, n=1:1075, n=2:2793），涉及 48 条视频
- 当前生产配置回放（真实 fps）：SAFE 28214 / WARNING 8355 / DANGER 8452；DANGER 视频 45；全程 SAFE 31；切换均值 5.12、中位 4；与存档一致率 91.6350%；存档 DANGER(n<3) 帧中当前仍 DANGER 1163、非 DANGER 2878
- 当前 M0 回放（w4=0，真实 fps）：SAFE 28249 / WARNING 8971 / DANGER 7801；DANGER 视频 40；与存档一致率 89.8425%
- Spearman：全量 ρ=0.6882；按视频 80 条有效均值 0.3154、中位 0.2849；31 条计 0 后均值 0.2273；fiedler>0 子集（10814 帧）ρ=0.3503
- 检出率：2548/3089 = 82.49%；14-19 桶 12 图 175 GT 79.43%
- pytest：190 passed, 4 skipped