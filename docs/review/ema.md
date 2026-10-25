# ema_smoother.h 审查报告

- **审查对象**：`src/algorithms/ema_smoother.h`（EMA 时序平滑器，C++ header-only）
- **关联文件**：
  - `src/algorithms/ema_smoother.py`（Python 对照实现）
  - `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`（EnhancedDecision 实现）
  - `src/luckfox_rtsp_yolov5/include/enhanced_decision.h`（EnhancedDecision 头文件）
  - `src/luckfox_rtsp_yolov5/src/main.cc`（板端主流程）
  - `tests/test_ipm_ema.py`（Python 侧单元测试，含对 C++ 头文件默认值的一致性文本检查）
  - `docs/ALGORITHM_UPGRADE.md` / `docs/ARCHITECTURE.md`（设计文档）
- **审查性质**：只读审查，未修改任何文件。
- **审查重点**：alpha 参数、多序列（多变量）支持、初始化边界、与 EnhancedDecision 集成情况。

---

## 一、模块概览

`ema_smoother.h` 实现帧级固定 alpha 的指数滑动平均（EMA）：

```
s_0 = x_0                                    （首帧初始化）
s_t = alpha * x_t + (1 - alpha) * s_{t-1}     （alpha ∈ (0,1]）
```

- 支持标量 / 多变量（向量）特征，各分量独立平滑；
- 支持每维独立 alpha（`std::vector<double>`）；
- 支持时间感知模式 `use_timestamp=true`：`alpha_t = 1 - exp(-dt / tau)`；
- 提供 `reset()` 用于场景 / 轨迹切换。

单文件、C++11、仅依赖标准库，整体代码结构清晰、注释充分，可直接嵌入 Luckfox 边缘端工程。

---

## 二、alpha 参数审查

### 2.1 合法性与回退策略

- 标量构造：`EmaSmoother(double alpha=0.3, size_t dim=1, ...)` → `setAlpha(alpha, dim)`。
  - `setAlpha` 校验 `alpha > 0.0 && alpha <= 1.0`，非法输入静默回退到默认 `0.3`。
- 向量构造：`setAlpha(const std::vector<double>&)` 逐分量校验，非法分量替换为 `0.3`；空向量回退为 `setAlpha(0.3, 1)`。
- `dim == 0` 被修正为 `1`。

**评价**：参数校验与工程健壮性处理到位。但 C++ 侧对非法 alpha 采取「静默回退」，而 Python 侧 `_prepare_alpha` 直接抛 `ValueError`。行为不一致——C++ 静默接受会掩盖配置错误（例如 `alpha=2.0` 被悄悄改成 `0.3`），在调试期可能造成困惑。属文档声明「接口与 Python 一致」下的偏差，建议在头文件注释中明示，或提供严格校验选项。

### 2.2 时间感知模式下 alpha 的语义

- `effectiveAlpha()`：`use_timestamp=true` 且 `t >= 0 && m_lastT >= 0 && t > m_lastT` 时，用
  `a_t = clamp(1 - exp(-dt / tau), 1e-9, 1.0)`，并**标量广播到所有维度**。
- 此时用户配置的每维 `alpha` 完全被忽略（仅作无有效时间差时的回退）。

**评价**：时间感知模式下每维独立 alpha 不生效，文档已注明「标量广播到所有维度」，可接受；但若用户同时传入 `alpha=[...]` 与 `use_timestamp=true`，预期外行为应显式提示。

### 2.3 特殊边界

- `tau` 校验：`setTau` 仅当 `tau > 0` 才生效，否则回退 `1.0`。合理。
- 时间感知模式存在「冷启动」：若首帧以默认 `t=-1` 调用，`m_lastT=-1`，则下一帧即使 `t>=0`，因 `m_lastT < 0` 仍回退固定 alpha，直到再下一帧才启用时间感知。行为正确但值得在文档注明。

---

## 三、多序列（多变量）支持审查

按任务「多序列支持」从两个层面评估：

### 3.1 多变量（向量特征）支持 —— 良好

- 单实例可维护一个 `dim` 维状态向量 `m_s`，各分量独立递推，支持每维独立 alpha。
- 维度推断：构造时给定 `dim`；未给（`m_dim==0` 的防御分支）时由首帧推断并广播 `0.3`。
- 首帧维度不匹配时，「退化为仅拷贝输入」并置初始化，避免状态损坏。

### 3.2 多独立序列（多目标 / 多轨迹）支持 —— 缺失（设计范围外）

- 该平滑器是一个**单一状态向量**，不原生支持多条独立序列同时平滑。
- 文档将其定位为「场景 / 轨迹切换时调用 `reset()`」——即**单序列复用**，而非**多序列并行**。
- 若板端需要为多个行人目标各自维护一条 EMA 特征序列，需外部管理「每目标一个 `EmaSmoother` 实例」，类本身未提供容器 / 按 id 索引的能力。

**评价**：作为特征级降噪器，单状态向量设计合理；「多序列」应理解为多变量特征支持，而非多目标轨迹支持。如需多目标独立平滑，应在外层封装，本类不宜直接承担。

---

## 四、初始化与边界行为审查

### 4.1 首帧初始化（正常路径）—— 正确

- 首帧 `m_s = value`，`m_lastT = t`，`m_initialized = true`，原样返回。与 Python 一致。
- `initialized()` / `smoothed()` / `smoothedAt(i)` 查询接口齐全。

### 4.2 【高】标量接口在 dim>1 时的首帧调用可能导致越界（UB）

`update(double value, int64_t t=-1)` 包装为 `std::vector<double> v(1, value)` 后走向量接口。

- 文档注释：「标量版本（dim 须为 1，否则取第 0 维）」。
- 但**首帧**若对 `dim>1` 的实例调用标量接口：
  1. `value.size()==1 != m_dim(>1)` → 进入「维度不匹配」分支：`m_s = {value}`（size=1），`m_initialized = true`，`m_dim` 仍为 3。
  2. 下一帧若用完整 3 维向量 `update({a,b,c})`，`value.size()==3 == m_dim` → 走正常递推：
     `for (i=0; i<m_dim; ++i) m_s[i] = ...`，此时 `m_s` 只有 1 个元素，`m_s[1]`、`m_s[2]` **越界访问（未定义行为）**。
- 初始化后的标量调用（dim>1）则走「维度不匹配 → 返回 m_s」分支，等效于只读第 0 维、**不吸收新值**，并非真正「对第 0 维平滑」。

**风险**：头文件被单独复用（如板端调试 / 其他模块）时，若误用标量接口首帧初始化高维实例，会触发 UB。建议：
- 标量接口首帧与 `m_dim` 不一致时，应重置 `m_dim=1` 并重建 `m_alpha`，或直接断言 / 抛错；
- 或在标量接口内显式处理「取第 0 维」语义（仅更新第 0 分量，其余保持）。

### 4.3 【中】运行期 `setAlpha` 变更维度不重置状态，可能导致越界

`setAlpha(double alpha, size_t dim)` 会改写 `m_alpha` 与 `m_dim`，但**不重置 `m_s` / `m_initialized`**。

- 若实例已初始化（`m_s` 旧长度 L），随后 `setAlpha(0.2, dim_new)` 且 `dim_new > L`，下一次 `update` 的 `value.size()==dim_new` 通过校验，但循环 `i=0..dim_new-1` 访问 `m_s[i]` 时会越界（`m_s` 只有 L 个元素）。
- 这是配置变更与状态生命周期未解耦导致的问题。建议：当新 `dim` 与当前 `m_dim` 不一致时，自动 `reset()` 或拒绝变更。

### 4.4 【中】`smoothedAt(i)` 未初始化 / 越界返回 `0.0`，静默掩盖错误

- `smoothedAt(i)`：`!m_initialized || i >= m_s.size()` 时返回 `0.0`。
- 风险：真实平滑值恰好为 `0.0` 时与「未初始化」不可区分，调用方难以察觉状态未就绪或维度误用。建议改为返回可选 / 带哨兵，或提供带状态返回的接口。

### 4.5 【低】C++ 与 Python 维度不一致处理策略不同

- C++：非首帧维度不匹配时**静默忽略本帧并返回当前状态**（不抛错）。
- Python：非首帧维度不匹配**抛 ValueError**。
- 测试 `test_ipm_ema.py::test_dim_mismatch_raises` 只覆盖 Python 侧；C++ 侧无对应行为测试。头文件注释声明「接口与 Python 一致」，但错误处理策略不同，属行为偏差。板端静默忽略可能掩盖特征维度配置漂移，建议至少加日志 / 计数告警。

### 4.6 【低】线程安全

- 类内部无锁，`update` / `smoothed` 均访问同一状态。EnhancedDecision 注释已声明单线程使用；若多线程共享实例需外部加锁。符合当前使用场景。

---

## 五、与 EnhancedDecision 集成情况【核心问题】

### 5.1 结论：ema_smoother.h 未被 EnhancedDecision 实际使用

在 `src/luckfox_rtsp_yolov5/` 全目录内搜索 `EmaSmoother` / `ema_smoother`：

- `enhanced_decision.h` 仅 include 三个算法头文件：`graph_laplacian.h`、`kalman_filter.h`、`risk_fsm.h`——**未 include `ema_smoother.h`**。
- `enhanced_decision.cc` 中 **`EmaSmoother` / `ema_smoother` 出现次数为 0**。
- `EnhancedDecision::processFrame()` 流程为：
  1. `smoothPoints()` 用**多目标 Kalman** 平滑脚点（不是 EMA）；
  2. `graph_.computeFeatures()` 直接得到 `n / density / r_gcc / clustering / fiedler ...`（未经过 EMA）；
  3. 直接用这些原始特征构造 `FsmFeatures f` 送入 `RiskFSM::update()`。
- `main.cc` 仅实例化 `EnhancedDecision`，无 EMA 使用。

### 5.2 文档与实现的落差

- `docs/ALGORITHM_UPGRADE.md` 第 392 行明确写：「**C++ 侧同样在 FSM 前加一层 `EmaSmoother`，将平滑后的 `FsmFeatures` 送入 `RiskFSM::update`**」。
- `docs/ARCHITECTURE.md` 第 84 行将 `ema_smoother.h` 列入架构模块清单（时序平滑）。
- 但 `EnhancedDecision` 的实际实现**没有**在 FSM 前加入 EMA 层，仅依赖 Kalman 轨迹平滑 + FSM 自身的滞回 / 去抖 / 保持来抑制抖动。
- 换句话说：**EMA 平滑作为「已实现的架构能力」被文档描述，但并未接入板端增强决策链路**。当前 `ema_smoother.h` 处于「独立可用的工具类，但无实际调用方」状态。

### 5.3 影响评估

- **功能**：FSM 输入特征（尤其 `n` 的检测抖动、`r_gcc` 的 IPM 误差）未获得 EMA 层降噪，仅靠 Kalman（位置域）+ FSM 滞回（等级域）平滑，特征域的高频抖动仍可能引起 FSM 输入波动。文档宣称的「特征进入 FSM 之前做 EMA 进一步降噪」收益**尚未落地**。
- **风险**：`ema_smoother.h` 缺少真实调用方，意味着 4.2 / 4.3 节指出的边界缺陷不会在现网触发；但也意味着该头文件未经板端真实编译 / 运行验证，其「已在 x86 验证接口与数值」的声明仅来自 Python 侧文本比对，无 C++ 单元测试。
- **测试覆盖**：`tests/test_ipm_ema.py` 的 `test_cpp_header_defaults_consistent` 仅读取头文件文本比对默认值字符串，**不编译、不运行 C++ 头文件**，无法发现上述越界 / 行为偏差问题。

### 5.4 建议

1. **二选一**：要么在 `EnhancedDecision::processFrame()` 中按 `ALGORITHM_UPGRADE.md` 第 2.5 节方案，在构造 `FsmFeatures` 前插入 `EmaSmoother`（对 `n/density/r_gcc` 做 EMA）；要么修订文档，明确当前增强决策不采用 EMA 层，消除文档与实现落差。
2. 若接入 EMA，需先修复 4.2 / 4.3 节的两个越界隐患，并补充 C++ 侧单元测试（首帧 / 维度切换 / 标量 vs 向量 / 时间感知 / setAlpha 变更维度）。
3. 在头文件注释中注明 C++ 与 Python 在「非法 alpha 回退」「维度不匹配处理」上的行为差异，避免跨端语义混淆。

---

## 六、问题汇总（按严重程度）

| 级别 | 位置 | 问题 |
|---|---|---|
| 高 | `update(double)` 标量接口 | dim>1 实例首帧标量调用后，下一帧全维向量更新触发 `m_s` 越界（UB） |
| 中 | `setAlpha(alpha, dim)` | 运行期变更维度不重置状态，`dim` 增大后更新可能越界 |
| 中 | `smoothedAt(i)` | 未初始化 / 越界静默返回 0.0，与合法值 0.0 不可区分 |
| 中 | `docs` vs 实现 | `ema_smoother.h` 未接入 `EnhancedDecision`，文档宣称的 FSM 前 EMA 层未落地 |
| 低 | `setAlpha` 校验 | C++ 静默回退 0.3 vs Python 抛 ValueError，行为不一致 |
| 低 | 维度不匹配 | C++ 静默忽略 vs Python 抛错；无 C++ 侧测试覆盖 |
| 低 | 线程安全 | 无锁，仅适用于单线程（当前场景满足） |
| 低 | 测试覆盖 | `test_ipm_ema.py` 对 C++ 头文件仅做文本比对，不编译运行 |

---

## 七、结论

`ema_smoother.h` 本身是一个实现干净、文档充分、接口友好的 C++11 header-only EMA 平滑器，alpha 参数校验、多变量独立平滑、时间感知模式等核心设计均合理。**主要问题集中在边界健壮性与集成落差**：

1. 标量接口在 dim>1 时的首帧初始化路径存在越界（UB）隐患，`setAlpha` 变更维度亦可能越界——这两个问题在「无实际调用方」现状下未暴露，但一旦接入即可能触发。
2. **与 EnhancedDecision 的集成未完成**：`enhanced_decision.cc` 中没有任何 EMA 使用，文档（ALGORITHM_UPGRADE.md / ARCHITECTURE.md）声称的「FSM 前加 EmaSmoother」目前只是纸面设计。当前增强决策链路实际只依赖 Kalman 位置平滑 + FSM 滞回/去抖/保持。
3. C++ 侧缺少真正的编译 / 运行测试，Python 测试仅做文本默认值比对，无法捕捉上述缺陷。

**建议后续行动**：修复 4.2 / 4.3 两个越界问题 → 按文档将 `EmaSmoother` 接入 `EnhancedDecision::processFrame()`（或修订文档）→ 补充 C++ 单元测试（可用 g++ 编译一个宿主测试）→ 在板端重新编译验证。

---

*本报告由只读审查生成，未修改任何源代码。路径均使用正斜杠。*