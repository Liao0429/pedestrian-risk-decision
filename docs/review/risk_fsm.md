# 代码审查报告：`src/algorithms/risk_fsm.h`

- **审查对象**：`H:/PedestrianRiskDecision/src/algorithms/risk_fsm.h`（只读审查，未修改）
- **审查日期**：2026-10-26
- **审查范围**：滞回逻辑正确性、时间戳去抖、状态枚举与 `RiskEngine::CrowdLevel` 一致性、`n=0` 边界、与 Python 端 `src/algorithms/risk_fsm.py` 的接口一致性
- **验证方式**：逐行静态审查 + 以头文件逻辑镜像实现与 Python 端 `RiskFSM` 做逐帧对比测试（滞回、部分/完整去抖、DANGER 保持、`n=0`、负 `n`、200 帧随机序列、`now_ms<0` 等场景）

---

## 结论摘要

整体设计正确：滞回阈值带、确认帧去抖、状态保持（有效时间戳下）逻辑与 Python 端完全一致，枚举与 `CrowdLevel` 数值对齐，`n=0` 归一化处理安全。**发现 1 个真实 Bug**（`now_ms<0` 进入 DANGER 时帧保持被短路禁用），另有 2 项低危一致性/健壮性建议，2 项信息性结论。

---

## 发现明细

### 🔴 Bug（中危）：`holdElapsed()` 的 `entryMs_ < 0` 早退短路了帧保持

- **位置**：`src/algorithms/risk_fsm.h:176`
  ```cpp
  bool holdElapsed(int64_t now_ms) const {
      if (entryMs_ < 0) return true;  // 从未进入 DANGER
      ...
  }
  ```
- **现象**：当 DANGER 以 `now_ms < 0`（无时间戳）进入时，升级分支 `entryMs_ = now_ms` 会将其置为 `-1`（第 154 行）。此后每次 `holdElapsed()` 首行 `if (entryMs_ < 0) return true;` 立即返回 `true`，导致 **`danger_hold_frames` 帧保持被一并禁用**——而设计语义（头文件第 68、90 行）是 `now_ms<0` 仅禁用**毫秒**保持，帧保持应继续生效。
- **根因**：`entryMs_ < 0` 同时承担"从未进入 DANGER"与"无有效时间戳进入 DANGER"两种语义。而 `holdElapsed()` 仅在降级分支且 `current == RISK_DANGER` 时被调用（第 162 行），此时必然已进入过 DANGER，因此该早退在 `now_ms<0` 进入 DANGER 的场景下必然误触发。
- **实测**（镜像实现，`danger_hold_frames=10`、`confirm_down=5`）：
  - 以有效时间戳进入 DANGER 后：DANGER 保持 ≥10 帧才允许降级计数，行为正确；
  - 以 `now_ms=-1` 进入 DANGER 后：DANGER 仅保持 **7 帧**即降级（帧保持完全失效），与"至少保持 10 帧"不符。
- **修复建议**：删除该早退（`holdElapsed` 只在 `current==RISK_DANGER` 时调用，无需判断"是否进入过"）；或把早退条件改为 `level_ != RISK_DANGER`；帧保持的计算不应依赖 `entryMs_` 的可用性。修改后需同步 Python 端 `risk_fsm.py` 的 `_hold_elapsed`，保持两端口径一致。

---

### 🟡 低危：C++ 端缺少配置校验（对比 Python 端）

- **位置**：`src/algorithms/risk_fsm.h:52-71`（`RiskFSMConfig`）
- Python 端 `RiskFSMConfig.__post_init__`（`risk_fsm.py:102-121`）校验了：权重非负且和>0、`n_ref>0`、`confirm_up/down>=1`、阈值 ∈[0,1]、滞回一致性 `up_warning>down_safe` 且 `up_danger>down_warning`。
- C++ 端 `RiskFSMConfig` 为纯 POD，无任何校验。若配置违规（如 `up_warning<=down_safe` 导致无滞回带、或 `confirm_up=0` 导致去抖失效），在嵌入式端会静默产生异常行为且难排查。
- **建议**：在 `reset()` 或构造时做一次校验，或提供 `bool validate() const`，配合日志/断言输出违规项。

---

### 🟡 低危：Python/C++ 对"无时间戳"的语义不一致（接口一致性声明风险）

- 头文件注释第 7 行声明"与 Python 端接口一致"，但负时间戳路径行为不一致：
  - **C++**：以 `now_ms < 0` 禁用毫秒保持（文档化，第 90 行），且 `entryMs_ < 0` 早退（见上方 Bug）。
  - **Python**：仅以 `None` 禁用毫秒保持；负整数会被当作有效时间戳参与 `(now_ms - entry_ms)` 计算，可能产生基于负时间戳的错误保持行为。
- **建议**：明确两端口径。若统一以"负数/None = 无时间戳"为语义，则 Python 端应在 `_hold_elapsed` 中同样对负值短路，并避免把负时间戳写入 `_entry_ms`。

---

### ℹ️ 信息：状态枚举与 `RiskEngine::CrowdLevel` 一致性已满足

- `RiskLevel`（`risk_fsm.h:36-40`）：`RISK_SAFE=0 / RISK_WARNING=1 / RISK_DANGER=2`。
- `CrowdLevel`（`src/luckfox_rtsp_yolov5/include/risk_engine.h:16-20`）：`CROWD_SAFE=0 / CROWD_WARNING=1 / CROWD_DANGER=2`。
- 两者数值完全一致，`enhanced_decision.h:95` 亦注释确认"`0=SAFE 1=WARNING 2=DANGER`，与 `RiskLevel` 对齐"。
- **注意**：两者为**不同类型**，无隐式转换；`voice_alert.h` 仍使用 `CrowdLevel`，集成时需显式 `static_cast<CrowdLevel>`/`static_cast<RiskLevel>`，编译器会强制类型安全，不会静默错位。

---

### ℹ️ 信息：`n=0` 边界处理正确

- `computeScore`（第 115-117 行）：`n_ref > 0` 时 `norm(n) = min(max(n,0)/n_ref, 1)`，`n<=0` 归一化为 0；`n_ref <= 0` 时 `norm = 0.0` 防除零；负 `n` 被钳位到 0。实测 `n=0` 且 density/r_gcc 全零时稳定输出 SAFE。
- **调用方注意事项**：`score` 仍含 `density`/`r_gcc` 项，若上游图特征在 `n=0` 时未清零（理论上图模块应保证 `n=0` 时 density/r_gcc=0），`score` 可能非零并触发 WARNING。建议在 `enhanced_decision.h`/图特征模块中保证 `n==0` 时密度与连通分量占比恒为 0。

---

## 验证记录

以头文件逻辑的逐行镜像实现（含 `computeScore`/`rawTarget`/`transition`/`holdElapsed`/默认配置）与 Python 端 `RiskFSM` 逐帧对比，场景覆盖：

| 场景 | 结果 |
|---|---|
| 滞回：高分进入后持续回落 / 滞回带内停留 | 一致 |
| 去抖：升级仅 2 帧（不足 `confirm_up=3`）后回落 | 一致（未升级） |
| 去抖：连续 3 帧满足后升级 | 一致 |
| DANGER 保持：有效时间戳下帧/毫秒保持 | 一致 |
| `n=0` 全零特征 / 负 `n` | 一致（稳定 SAFE） |
| 200 帧随机特征序列 | 一致 |
| `now_ms=-1`（禁用毫秒保持） | **不一致**（C++ 帧保持被早退短路，见 Bug） |

---

## 修复优先级

1. **修复 `holdElapsed` 早退 Bug**（`risk_fsm.h:176`）——影响嵌入式端无时间同步场景下 DANGER 的强制保持，属行为缺陷。
2. 增加 C++ 端配置校验。
3. 统一 Python/C++ 对"无时间戳"的语义，并补测试覆盖 `now_ms<0` 进入 DANGER 的路径。

---

*报告路径：`docs/review/risk_fsm.md`*