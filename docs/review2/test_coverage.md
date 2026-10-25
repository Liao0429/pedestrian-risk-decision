# 单元测试覆盖评估：第 1 轮审查修复（8 项 Bug）是否被测试跟上

- **评估对象**：`tests/`（133 个 Python 单测，`python -m pytest tests -q` 全过，1.2s）
- **对照范围**：`src/algorithms/*.py`、`src/luckfox_rtsp_yolov5/*.{h,cc}` 及 `src/luckfox_rtsp_yolov5/tests/*.cc`（C++ 主机测试，`make test`）
- **评估方式**：只读；比对 commit `fe041e4` 的修复 diff 与测试代码，并对 Python 端做行为复验（未修改任何源文件）
- **结论**：**修复基本未被对应测试覆盖**。133 个 Python 测试全部通过，是因为修复主要落在 C++ 头文件/实现，而 Python 侧测试要么只做文本比对、要么用高 IoU 场景掩盖阈值问题、要么根本没有覆盖修复行为；且发现 **Python `risk_fsm.py` 仍存在与 C++ 相同的"帧保持失效" Bug**。

---

## 一、修复项 × 测试覆盖对照

| 修复项 | 修复位置 | 对应测试 | 是否覆盖修复行为 |
|---|---|---|---|
| graph_laplacian 收敛检测（jacobiEigenvalues 返回 `false` → 谱特征回退 0） | `graph_laplacian.h` | `test_graph_laplacian.py::TestJacobiAlgorithmLogic` | ❌ 复刻的是**旧算法**（无收敛标志/无失败返回）；Python 版用 `np.linalg.eigvalsh` 恒收敛，无收敛路径可测 |
| graph_laplacian NaN/Inf 输入过滤、负特征值全截断 | `graph_laplacian.h` | 同上 | ❌ Python 版不做 NaN 过滤；C++ 行为无任何测试 |
| risk_fsm 保持（`holdElapsed` 早退短路 → `level_ != RISK_DANGER`） | `risk_fsm.h` | `test_risk_fsm.py`（13 项） | ❌ 所有保持测试均用**正时间戳**或 `danger_hold_ms=0`；`now_ms=None`/负值进入 DANGER 的路径**无测试** |
| bytetrack 阈值反转（`1.0-match_thresh` → `match_thresh`） | `bytetrack_tracker.py` + `.h` | `test_bytetrack_tracker.py` | ⚠️ 仅间接覆盖：遮挡场景帧间位移 3~4px、IoU≈0.9，新旧阈值均通过，**无法区分修复前后** |
| ema 越界（标量接口 dim>1、setAlpha 变更维度重置） | `ema_smoother.h` | `test_ipm_ema.py::test_cpp_header_defaults_consistent` | ❌ 只做**文本比对默认值字符串**，不编译不运行 C++；C++ 行为无运行测试 |
| risk_engine setConfig 回退到完整默认（而非 DANGER 默认） | `risk_engine.cc` | `test_risk_config.cc::test_risk_engine_config` | ⚠️ 只测**合法配置**驱动 evaluate()；未测"warning.n>danger.n 触发回退后 WARNING 仍可用"这一修复点；且依赖 OpenCV，`pkg-config` 找不到时整段跳过 |
| crowd_dbscan 输入校验/噪声标签 | `crowd_dbscan.h` | `test_crowd_dbscan.py`（32 项） | ⚠️ Python 版本身无此 Bug；C++ 行为无运行测试 |
| enhanced_decision Kalman 先 predict 再 correct | `enhanced_decision.cc/.h` | `test_enhanced_decision.cc` | ⚠️ 高层行为（平滑/等级）覆盖，但未针对"关联前 predict"这一修复点做断言 |

**关键结论**：5 项被评估的修复（graph_laplacian 收敛检测 / risk_fsm 保持 / bytetrack 阈值 / ema 越界 / risk_engine setConfig）中：
- **0 项**有能验证修复行为的运行测试；
- **3 项**（graph_laplacian、ema、risk_engine）只有"文本比对 / 合法路径 / 依赖可选"的弱覆盖；
- **1 项**（bytetrack）测试场景无法区分修复前后；
- **1 项**（risk_fsm）不仅无测试，Python 端修复本身还没落地（见下）。

---

## 二、重要发现：Python `risk_fsm.py` 的等价 Bug 仍在

commit `fe041e4` 只改了 `risk_fsm.h`（`if (entryMs_ < 0) return true` → `if (level_ != RISK_DANGER) return true`），**未同步 `risk_fsm.py`**。而 `risk_fsm.py::_hold_elapsed`（第 271 行）仍是：

```python
if self._entry_ms is None:
    return True  # 尚未进入过 DANGER，无需保持
```

当以 `now_ms=None`（无时间戳）进入 DANGER 时，`_entry_ms` 被置为 `None`，此后每次 `_hold_elapsed` 立即返回 `True` → **`danger_hold_frames` 帧保持被完全禁用**。这正是一轮审查在 C++ 侧定位、并已修复的同一缺陷。

实测复验（`danger_hold_frames=4, confirm_down=2`，进入 DANGER 后喂低分）：

```
期望（帧保持生效）：  ['DANGER','DANGER','DANGER','DANGER','WARNING','WARNING']
实际（Python 现实现）：['DANGER','WARNING','WARNING','SAFE','SAFE','SAFE']   ← 帧保持失效，立即降级
```

`test_risk_fsm.py` 的保持测试（`test_danger_hold_frames` / `test_danger_hold_ms`）都使用正时间戳或 `danger_hold_ms=0`，**没有覆盖 `now_ms=None` 路径**，因此该 Bug 未被任何测试捕获。

---

## 三、测试覆盖缺口汇总

1. ****risk_fsm.py `now_ms=None` 帧保持路径缺失**（修复未落地 + 测试缺口双重问题）。
2. **graph_laplacian 收敛检测无测试**：C++ `jacobiEigenvalues` 返回 `false` 回退 0 的逻辑无任何 C++ 运行测试；Python 镜像测试复刻的是旧算法。
3. **bytetrack 阈值边界无测试**：现有场景 IoU≈0.9，无法验证 `match_thresh` 语义（要求 IoU≥0.2 而非 ≥0.8）。
4. **ema_smoother.h 越界修复无运行测试**：`test_cpp_header_defaults_consistent` 仅比对字符串，无法发现越界/行为偏差；且 C++ 端无 ema 单测文件。
5. **risk_engine setConfig 回退行为无测试**：只测合法配置，未测非法配置回退后 WARNING 阈值恢复为默认 5/0.28/0.55；且 OpenCV 缺失时该测试整体跳过。
6. **C++ 主机测试（`make test`）只覆盖 enhanced_decision / minijson / risk_config 三个文件**，`graph_laplacian.h`、`ema_smoother.h`、`bytetrack_tracker.h` 均无对应的 `.cc` 运行测试。

---

## 四、补测建议（按优先级）

1. **【必修】修复 `risk_fsm.py` 并补测试**：将 `_hold_elapsed` 的早退条件改为 `if self._state != "DANGER": return True`（与 C++ 对齐）；新增用例 `test_danger_hold_frames_without_timestamp`：`now_ms=None` 进入 DANGER 后，帧保持应照常生效（复现上文期望序列）。
2. **【高】补 bytetrack 阈值区分测试**：构造帧间 IoU 落在 (0.2, 0.8) 的匀速目标（如 IoU≈0.45），断言轨迹 ID 不漂移、低分二次关联仍匹配；或直接对 `associate_detections_to_tracks` 用 IoU≈0.43 的框断言 `thresh=0.8` 时应匹配（修复前不匹配，修复后匹配）。
3. **【高】为 C++ 修复补运行测试**：新增 `test_graph_laplacian.cc`（NaN/Inf 点过滤、非法 edgeDist 回退、jacobi 收敛/回退路径）、`test_ema_smoother.cc`（dim>1 标量首帧调用、setAlpha 变更维度重置、维度不匹配不越界）、`test_bytetrack.cc`（阈值语义）；扩展 `test_risk_config.cc` 增加 `setConfig` 非法回退断言（`warning.n>danger.n` 后 `evaluate` 在 5 人场景应落 WARNING，而非 DANGER）。
4. **【中】降低 OpenCV 依赖**：`test_risk_engine_config` 目前无 OpenCV 即跳过，无法在 CI 兜底；可抽出 `RiskConfig` 校验/回退逻辑为不依赖 OpenCV 的纯函数再测。
5. **【中】Python/C++ 行为对齐测试**：`test_risk_fsm.py` / `test_ipm_ema.py` 的 `test_cpp_header_defaults_consistent` 从"文本比对"升级为"编译并运行 C++ 头文件"（`make test` 纳入 CI），或至少显式断言 Python 与 C++ 在 `now_ms=None`、维度不匹配等路径行为一致。

---

*本报告仅评估，未修改任何源文件。Python 单测实测 133 通过（2026-10-26）。*