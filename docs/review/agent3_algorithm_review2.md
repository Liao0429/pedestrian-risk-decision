# Agent 3 复杂网络与算法研究专家 — 第二轮增量审查报告

- 审查对象：`graph_laplacian.{h,py}`、`crowd_dbscan.{h,py}`、`kalman_tracker.py`、`kalman_filter.h`、`ema_smoother.{h,py}`、`risk_fsm.{h,py}`、`risk_engine.cc`、`enhanced_decision.cc/.h`、`config/default_config.json`
- 目标平台：Luckfox RV1106（256MB / 单核 A7 1.2GHz）
- 前序报告：`docs/review/agent3_algorithm.md`（本报告在其基础上增量，含实验验证与 3 处纠正）
- 实验方法：numpy 2.4.3 实测（`np.linalg.eigvalsh` 谱分解、Python 逐行复刻 C++ Jacobi、DBSCAN 等价性随机试验）
- 结论严重度分级：高（必须处理）/ 中（建议处理）/ 低（可选）

---

## 审查问题 1：建边规则 `d < edge_dist`（严格小于）与 DBSCAN `dist <= eps`（闭区间）阈值语义冲突

**结论：中。两模块"邻近"语义矛盾 + 参数不一致（2.0 vs 1.5），真实密集场景会得出相反结论。**

### 证据
- 图建边：`graph_laplacian.py:58` `A = (dist < edge_dist)`；`graph_laplacian.h:138` `if (d < edgeDist)`；`risk_engine.cc:59` `if (d < m_cfg.edge_dist_m)`。严格小于。
- DBSCAN：`crowd_dbscan.py:40` `dist[i] <= eps`；`crowd_dbscan.h:119` `if (std::sqrt(...) <= eps)`。闭区间。
- 默认参数不一致：`default_config.json` `edge_dist_m=2.0`，`crowd_dbscan.h:76` 默认 `eps=1.5`。

### 实验数据
| 场景 | 图（edge_dist=2.0, d<2.0） | DBSCAN（eps=1.5, d<=1.5） |
|---|---|---|
| 两点相距 1.8m | 1 条边，density=1.0（判为聚集） | 0 簇，2 噪声（判为分散） |
| 三点两两 1.8m（紧凑簇） | density=1.0 全连接 | 0 簇，3 噪声 |
| 两点相距恰 2.0m | 0 边（2.0<2.0 为 false） | eps=2.0 时 1 簇（2.0<=2.0 为 true） |

1.8m 是典型人群间距（两人并肩/擦肩），图特征判为"高密度"，DBSCAN 判为"全噪声"，两个模块对同一输入给出相反语义；闭/开区间差异在恰好等于阈值时翻转。特征无法融合。

### 建议
统一半径与区间语义（二选一）：
- 推荐：`eps = edge_dist_m = 2.0`，并统一为同一边界语义（建议都采用 `<=`，DBSCAN 已是 `<=`，将图建边改为 `<=` 并同步 Python/C++/risk_engine 三处），消除 1.8m 场景矛盾。
- 或保持开区间，但 DBSCAN 改为 `dist < eps`（需修改 Python/C++ 两处，并与 sklearn 行为不一致，不推荐）。

---

## 审查问题 2：谱特征数学性质（fiedler 跨规模漂移 / 不连通恒 0 / 与代数连通度重复）

**结论：高。已用 numpy 实测证实前批报告的判断；并补充证实代数连通度字段完全重复。**

### 证据
- 完全图：`graph_laplacian.py:182` `fiedler = eigvals[1]`；`graph_laplacian.py:200-201` 与 `graph_laplacian.h:310-311` 均将 `algebraic_connectivity` 直接赋值为 `fiedler`（同一值两份输出）。

### 实验数据（numpy eigvalsh）
| 图 | λ₂(fiedler) | λmax | λ₂/λmax | r_gcc |
|---|---|---|---|---|
| 完全图 K4 | **4.0** | 4.0 | 1.0 | 1.0 |
| 完全图 K8 | **8.0** | 8.0 | 1.0 | 1.0 |
| 完全图 K16 | **16.0** | 16.0 | 1.0 | 1.0 |
| 路径 P5（间距1.5m） | 0.382 | 3.618 | 0.106 | 1.0 |
| 路径 P10 | 0.098 | 3.902 | 0.025 | 1.0 |
| 路径 P20 | 0.025 | 3.975 | 0.006 | 1.0 |
| 路径 P30 | 0.011 | 3.989 | 0.003 | 1.0 |
| 两簇 K4 相距 5m | **0.0** | 4.0 | 0.0 | 0.5 |
| 4 点完全分散 | **0.0** | 0.0 | 0/0 | 0.25 |

结论：
1. **fiedler 随 n 强烈漂移**：同样"全连接"，K4=4、K16=16（fiedler=n，理论值）；同样 1.5m 等距直线，人数翻倍 λ₂ 缩约 4 倍。跨帧/跨规模直接比阈值无意义，"人多反而不聚集"的错误结论必然出现。
2. **不连通图 fiedler≡0**：两簇 K4（density=0.429、r_gcc=0.5，典型高风险场景）λ₂=0，与 4 点全分散（λ₂=0）不可区分。λ₂>0 ⟺ 图连通 ⟺ r_gcc=1，谱特征不提供增量信息。
3. **代数连通度与 fiedler 完全重复**：两个输出字段是同一数值，无独立信息。

---

## 审查问题 3：归一化拉普拉斯 L_sym 的可行性

**结论：中。L_sym 跨规模可比性显著优于 L（实测），但前批报告"连通图 μ₂∈(0,1]"的说法有误；且 L_sym 在"分散图"上出现 μ₂=1 的混淆缺陷，Jacobi 收敛问题依旧。**

### 实验数据
| 图 | μ₂(L_sym) | 说明 |
|---|---|---|
| 完全图 K4 | **1.333** | 理论 n/(n-1)=4/3，∈(1,2) |
| 完全图 K8 | **1.143** | 理论 8/7 |
| 完全图 K16 | **1.067** | 理论 16/15 |
| 路径 P5 | 0.293 | |
| 路径 P10 | 0.060 | L_sym 特征值范围 [0, 2] 实测成立（P10 max=2.0，二分图） |
| 路径 P20 | 0.014 | |
| 路径 P30 | 0.006 | |
| 两簇 K4 相距 5m | **0.0** | 与"不连通"语义一致 |
| 4 点完全分散 | **1.0** | ⚠ 全孤立图 L_sym=I，μ₂=1 |

要点：
1. **值域**：L_sym 特征值 ∈[0,2]，连通图 μ₂∈(0,2]。**纠正前批报告**：K3 μ₂=1.5、K4 μ₂=1.333、K2 μ₂=2.0，均 >1，"连通图 fiedler∈(0,1]"不成立。μ₂=2 当且仅当图含二分连通分量。
2. **跨规模漂移**：K_n 的 μ₂=n/(n-1)→1，远好于 λ₂=n 的线性增长；路径图 μ₂ 仍随 n 减小（P5=0.293 → P30=0.006），但量级固定于 [0,2]，阈值可设计。
3. **新缺陷（前批未提）**：全分散图（无边）L_sym=单位阵，μ₂=1.0，反而与"中等连通"混淆；含孤立点+连通分量的混合图 μ₂ 会被孤立点对应的特征值 1 污染。**谱特征用于不连通/稀疏场景的语义仍然不可靠**。
4. **Jacobi 复用**：`jacobiEigenvalues` 接受任意实对称矩阵，L_sym 可直接传入（构建 L_sym 只需 `I - D^{-1/2}AD^{-1/2}`，O(n²)），代码可复用。但 Jacobi 收敛问题不因换矩阵而消失（见问题 3 实验）。
5. **计算成本**：构建 O(n²)+ Jacobi 每 sweep O(n³)，与现实现同阶；n≤64 时板端仅适合低频（1~5Hz）或论文离线分析。

---

## 审查问题 3 附：Jacobi 收敛性复刻验证（确认前批报告）

按 `graph_laplacian.h:155-218` 逐行复刻（tol=1e-12, max_sweeps=100）：
- P5：35 sweeps 收敛，λ₂ 与 numpy 一致（0.381966）。
- P8：95 sweeps 收敛（逼近 100 上限）。
- P10：100 sweeps **不收敛**（200 sweeps 内 161 次收敛）。
- P12 / P16 / P20：200 sweeps **仍不收敛**。

**高**。排队/稀疏长链（n≥10）即触发 `converged=false`，`graph_laplacian.h:300-305` 将 fiedler/谱半径/特征熵全部静默回退 0；`enhanced_decision.cc:159` 虽有 `spectral_converged` 标志但 FSM 未消费，板端会在不知情时使用 0 值。

---

## 审查问题 4：DBSCAN min_samples=2 链式传播与连通分量等价性

**结论：高。min_samples=2 时 DBSCAN 簇 ≡ eps 闭区间图的非平凡连通分量（实测 3/3 随机试验完全一致），提供零增量信息，且链式传播会把长队桥接成"聚集"。**

### 证据与实验
- `crowd_dbscan.h:107-108`：NOISE=-1、UNCLASSIFIED=-2；`crowd_dbscan.py:34-35`：UNCLASSIFIED=-1、NOISE=-2（标签相反，前批已报告）。
- 链式传播：10 点直线链（间距 1.4m，eps=1.5, min_samples=2）→ 1 簇，**首尾相距 12.6m 被桥接为同一簇**。
- 等价性：3 组随机 15 点试验，DBSCAN(min_samples=2) 簇分组与 eps=1.5 闭区间图的非平凡连通分量 **3/3 完全一致**。因此 `largest_cluster_ratio` 与 `r_gcc`（开区间图连通分量占比）仅在闭/开区间边界点有差异，信息重复。

### 建议
- `min_samples` 提升至 3~4：引入"核心点/边界点"区分，链式单线（每点仅 2 邻居）不再成簇，DBSCAN 才提供图连通分量之外的**局部密度**信息。
- 半径与图建边统一（见问题 1）。
- 若 min_samples≥3，需同步验证 C++/Python 两实现与 sklearn 的三方一致性。

---

## 审查问题 5：Kalman/EMA/FSM 功能重复、参数冲突、响应延迟

**结论：中。Kalman（位置级）+ FSM（等级级）已构成两级防抖；EMA 未接入任何运行路径；若按 docs 建议再加 EMA 会叠加 0.28s 延迟。FSM 滞回参数本身合理，但触发阈值整体比 legacy 规则严格，需文档化。**

### 证据
- 增强决策流水线：`enhanced_decision.cc:52-138` Kalman 平滑脚点 → `:147-159` 图特征 → `:161-173` FSM。
- FSM 已有防抖：`default_config.json` `confirm_up=3`、`confirm_down=5`、`danger_hold_frames=10`、`danger_hold_ms=1000`；`risk_fsm.h:65-69` 同。
- EMA：`ema_smoother.h/.py` 仅出现在黄金回归测试（`tests/test_golden_regression.py:60-86`），未接入 `enhanced_decision.cc` 任何运行路径；`docs/algorithms.md:410` 仍建议"特征 EMA 平滑（alpha≈0.4~0.6）再送入 FSM"。
- 参数：`default_config.json` `frame_dt=0.1`（10Hz）；`kalman_filter.h:145-147` 默认 1/30。

### 延迟叠加（10Hz 实测计算）
- EMA(alpha=0.3) 时间常数 τ = -dt/ln(1-α) ≈ **0.28s（2.8 帧）**。
- 若叠加：升级路径延迟 ≈ 0.28 + confirm_up 3帧(0.3s) = **0.58s**；DANGER 最小保持 1.0s；降级延迟更大。
- 当前板端（无 EMA）：升级延迟 0.3s；降级延迟 = DANGER 保持 1.0s + confirm_down 0.5s。

### FSM 滞回合理性
- 滞回带：SAFE↔WARNING [0.28, 0.40]（带宽 0.12）、WARNING↔DANGER [0.55, 0.65]（带宽 0.10）。升级阈值严格高于降级阈值，方向正确，参数合理。
- **保持期双条件**：`risk_fsm.h:196-207` 要求帧数 AND 毫秒同时满足。10Hz 下 10帧=1000ms 恰同时满足，等效保持 1.0s；若帧率低于 10Hz（如 5Hz），10帧=2s 会把保持期拉长到 2s。建议文档明确"保持期 = max(帧数时间, ms)"。
- **FSM 触发阈值整体严于 legacy**：n=10 时需 (density+r_gcc)/2 ≥ 0.75 才达 DANGER；实测 n=10, rho=0.6, gcc=0.5 → legacy=DANGER（`risk_engine.cc:106-107`），FSM score=0.53 → WARNING。`enable_risk_engine_fallback=false` 时 FSM 是唯一决策源，报警阈值比 legacy 高，需在论文/文档中说明，否则对照实验会显得"增强决策不报警"。

### 参数冲突纠正（相对前批报告）
- **前批称"frame_dt 默认 1/30 与 JSON 0.1 冲突导致按 30Hz 外推"已不成立**：`main.cc:126` 已将 `enhancedCfg.frame_dt = riskCfg.frame_dt`，板端实际使用 JSON 的 0.1s。
- 仍存在的问题：`kalman_process_noise / kalman_measurement_noise / kalman_gate_m / kalman_max_miss` 无 JSON 键，硬编码于 `enhanced_decision.h:82-90`（1.5 / 0.5 / 2.0 / 5），运行时不可调。建议补 JSON 键并启动时打印生效参数。

### EMA 建议
维持前批结论：**三选二**（保留 Kalman + FSM，放弃 EMA）。若论文需要 EMA 数据，则必须将 confirm_up 降至 1~2、danger_hold 保持 0，避免三级滤波叠加 0.6s+ 延迟。

### 附：Python/C++ EMA 标量行为不一致（低）
- `ema_smoother.py:195`：dim>1 时传入标量，`v.shape[0]=1 != dim` → 抛 `ValueError`。
- `ema_smoother.h:116-131`：dim>1 时标量输入静默只更新第 0 维。
- 跨语言黄金测试未覆盖该路径，建议统一（Python 对齐 C++ 或反之）并补用例。

---

## 审查问题 6：谱特征未进 FSM — 低成本可解释接入方式

**现状确认**：`enhanced_decision.cc:150-159` 将 fiedler/spectral_radius/algebraic_connectivity/eig_entropy 全部复制到结果结构体，但 `:161-167` 送入 FSM 的只有 n/density/r_gcc/clustering/trend。**谱特征对决策零贡献，仅作为输出字段存在**。

以下方案按推荐优先级给出（原理 / 预期收益 / 计算成本 / 潜在缺陷 / 验证方法）。

### 方案 A（推荐）：谱特征不进 FSM，决策用已有特征组合
- **原理**：λ₂>0 ⟺ 图连通 ⟺ r_gcc=1（实验已证）；λ₂ 数值随 n 漂移不可比。决策所需的"聚集/连通/局部密度"语义已由 density、r_gcc、clustering（+DBSCAN min_samples≥3 的 largest_cluster_ratio）覆盖。
- **预期收益**：删除 Jacobi 全部开销；消除静默回退 0 的隐患；FSM 输入保持可解释。
- **计算成本**：O(1)（移除）；clustering 已是 O(n²)（复用距离矩阵）。
- **潜在缺陷**：论文若依赖谱特征章节，需用离线数据替代；`EnhancedResult` 中谱字段可保留为"论文分析用"但标记 `spectral_converged` 并在文档声明不参与决策。
- **验证**：构造 {两簇、单长链、均匀分散、完全聚集} 四类场景，断言 FSM 等级与人工标注一致；确认删除谱特征后输出不变（因为本来就不参与）。

### 方案 B（论文需要谱特征时）：改用归一化拉普拉斯 μ₂
- **原理**：μ₂∈[0,2]，连通图 μ₂>0，K_n μ₂=n/(n-1)→1，跨规模漂移小（实测 K4=1.333→K16=1.067）。
- **预期收益**：跨帧/跨规模可比，可作为论文中的"连通质量"指标。
- **计算成本**：构建 L_sym O(n²) + Jacobi 每 sweep O(n³)，与现实现同阶；仅建议 n≤32、1~5Hz 低频计算或离线。
- **潜在缺陷**：① 全分散图 μ₂=1.0（单位阵特征值），孤立点+簇混合场景 μ₂ 被污染，语义混淆（实验 4 点分散 μ₂=1.0）；② Jacobi 收敛问题依旧（P10 需 161 sweeps）；③ 值域非 [0,1]，阈值需重新标定；④ 前批"μ₂∈(0,1]"说法错误，勿按此设计阈值。
- **验证**：与 numpy eigvalsh 比对 μ₂；构造"4 孤立点 + K4"混合图检查 μ₂ 是否被孤立点特征值 1 抬升。

### 方案 C：相对代数连通度 λ₂/λmax
- **原理**：比值消除 K_n 的 n 缩放（K_n=1），不连通=0；实测 P5=0.106、P10=0.025、P20=0.006、P30=0.003，比 λ₂ 稳定但仍随 n 减小。
- **预期收益**：得到一个 [0,1] 的归一化连通度。
- **计算成本**：λmax 可用幂迭代 O(iter·m)（m=边数，约 20~50 次迭代）；λ₂ 需反幂/平移迭代或仍跑 Jacobi，实现复杂，成本不低于现方案。
- **潜在缺陷**：λ₂=0 时比值=0 与 r_gcc<1 完全重复；λmax=0（全分散）时 0/0 需特殊处理；收益有限。
- **验证**：实验 6 表格数据已给出各场景比值，可在此基础上做阈值标定。

### 方案 D（若必须给 FSM 增加"聚集度"维度）：局部密度统计特征
- **原理**：距离矩阵已计算，直接统计每个点的邻域性质，如：平均/中位最近邻距离、半径 edge_dist 内邻域点数均值、邻域距离标准差。这些都是 O(n²) 内可得的可解释特征。
- **预期收益**：提供谱特征想表达的"局部紧密度"，且不依赖特征分解；作为 FSM 第 4 维（如 `score += w4 * local_density_norm`）可解释（"平均邻近距离 < 1m 表示密集"）。
- **计算成本**：O(n²)（复用距离矩阵，无新增）。
- **潜在缺陷**：与 density 部分相关（邻域点数均值 ≈ density 的线性相关量），需做相关性分析避免共线性。
- **验证**：固定 n 变化空间分布（均匀 5m 网格 vs 1m 密集簇），比较该特征与 density 的响应差异。

### 接入 FSM 的最简路径
若坚持接入：`score += w4 * r_gcc * min(λ₂/λmax, 1)`，其中 `r_gcc` 已经承载连通性，谱比值只是缩放因子，实际增量极小。**可解释性最强的接入其实是直接用 r_gcc 与 clustering**——它们已是 [0,1] 且无计算成本。

---

## 对前一批报告（agent3_algorithm.md）的增量修正

1. **纠正**：前批称归一化拉普拉斯"连通图 fiedler∈(0,1]"——实测 K3=1.5、K4=1.333、K2=2.0，正确值域 (0,2]；K_n 的 μ₂=n/(n-1)∈(1,2]。跨规模可比性结论不变，但阈值设计不能按 [0,1] 假设。
2. **纠正**：前批称"frame_dt 漏配时 Kalman 按 30Hz 外推"——`main.cc:126` 已将 JSON `frame_dt` 传入 EnhancedConfig，该问题已修复；剩余问题是 kalman_* 四个参数无 JSON 键（硬编码）。
3. **补充**：L_sym 在"全分散/多孤立点"图上的 μ₂=1.0 混淆缺陷（前批未提），以及 Python/C++ EMA 标量行为不一致（低）。

---

## Top Findings（按严重度排序）

| # | 严重度 | 问题 | 证据 |
|---|---|---|---|
| 1 | 高 | fiedler 跨规模不可比且不连通恒 0：K4/K8/K16 λ₂=4/8/16（=n），P5→P30 λ₂=0.382→0.011，两簇 K4 与全分散同为 0 | 本报告实验 2 表；graph_laplacian.py:182 |
| 2 | 高 | Jacobi max_sweeps=100 不收敛：P10 需 161 sweeps，P12/P16/P20 在 200 sweeps 内仍不收敛，谱特征静默回退 0 且 FSM 不消费 converged 标志 | 复刻 graph_laplacian.h:155-218，:300-305；enhanced_decision.cc:159 |
| 3 | 高 | DBSCAN min_samples=2 退化为 eps 图连通分量（随机试验 3/3 一致），与 r_gcc 信息重复；10 点链（间距1.4m）首尾 12.6m 桥接为一簇 | crowd_dbscan.h:107-119；本报告实验 4 |
| 4 | 中 | 图建边 d<2.0 与 DBSCAN d<=1.5 语义+参数双重冲突：1.8m 点对图 density=1.0 而 DBSCAN 全噪声 | graph_laplacian.h:138 vs crowd_dbscan.h:119；default_config.json |
| 5 | 中 | 前批"μ₂∈(0,1]"错误：K3=1.5/K4=1.333/K2=2.0，L_sym 正确值域 (0,2]；且全分散图 μ₂=1.0 语义混淆 | 本报告实验 2c |
| 6 | 中 | Kalman+FSM 已两级防抖，EMA 未接入；若加 EMA(α=0.3) 升级延迟 0.3s→0.58s | ema_smoother.h 仅黄金测试；docs/algorithms.md:410；本报告实验 5 |
| 7 | 中 | FSM 触发阈值严于 legacy：n=10,rho=0.6,gcc=0.5 时 legacy=DANGER、FSM=WARNING；kalman_* 参数无 JSON 键 | risk_engine.cc:106-107 vs risk_fsm.h:149-159；enhanced_decision.h:82-90 |
| 8 | 低 | Python/C++ EMA 标量输入行为不一致（Python 抛异常、C++ 静默只更新第 0 维）；Python/C++ DBSCAN 噪声标签相反（NOISE=-2 vs -1） | ema_smoother.py:195 vs ema_smoother.h:116-131；crowd_dbscan.py:34-35 vs crowd_dbscan.h:107-108 |

**总体建议**：板端最终方案 = Kalman（位置级）+ 共享距离矩阵的 density/r_gcc/clustering + DBSCAN(min_samples≥3, eps=edge_dist) 局部密度 + FSM（去 EMA，保持现滞回参数）。谱特征不作为实时决策输入；若论文需要，改用 L_sym 的 μ₂ 仅做离线/低频（n≤32, 1~5Hz）分析，并避开"全分散 μ₂=1"的语义陷阱。