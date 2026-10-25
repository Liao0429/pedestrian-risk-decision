# 审查报告：risk_engine 基础规则引擎与阈值配置

- 审查对象（只读审查，不修改代码）：
  - `src/luckfox_rtsp_yolov5/include/risk_engine.h`
  - `src/luckfox_rtsp_yolov5/src/risk_engine.cc`
  - `config/default_config.json`
  - 关联上下文：`include/risk_config.h`（JSON 加载/校验）、`include/enhanced_decision.h`、`src/algorithms/risk_fsm.h`、`src/luckfox_rtsp_yolov5/src/main.cc`、`scripts/crowd_test.py`
- 审查范围：阈值默认值合理性、JSON 加载与缺省/非法值处理、levelName 枚举一致性、与 FSM 的配合。
- 审查日期：2026-10-26

---

## 1. 结论摘要

整体结构清晰，默认阈值在 C++ 默认值、`default_config.json`、Python 端 `crowd_test.py` 三处保持一致，JSON 加载具备基础合法性校验与缺省回退，等级语义（WARNING 不高于 DANGER）在 `n` 上得到校验，`levelName` 字符串在三套枚举间完全一致。但存在 4 个值得关注的问题：

1. `setConfig` 的非法配置回退会把 `warning` 阈值错误重置成 DANGER 默认值，导致回退后 WARNING 等级永久失效。
2. `loadRiskConfig` 只校验了 `warning.n <= danger.n`，未校验 `rho`/`gcc` 的等级单调性，语义颠倒配置可绕过校验。
3. `loadRiskConfigOr` 静默回退且 `main.cc` 使用相对路径，现场配置缺失/损坏时无任何日志提示，难以察觉配置未生效。
4. 默认阈值与 6m×4m 地面监控区域搭配时偏保守（尤其 `edge_dist_m=2.0`），且 FSM 增强路径与规则路径使用两套互不关联的阈值配置，`default_config.json` 在增强路径下基本失效。

---

## 2. 阈值默认值合理性

`default_config.json` 当前值：

```json
{
  "edge_dist_m": 2.0,
  "danger":  { "n": 10, "rho": 0.55, "gcc": 0.8  },
  "warning":{ "n": 5,  "rho": 0.28, "gcc": 0.55}
}
```

三端一致性核对（全部一致）：

| 项 | C++ 默认（risk_config.h） | default_config.json | crowd_test.py 常量 |
|---|---|---|---|
| edge_dist_m | 2.0 | 2.0 | 2.0 |
| danger n/rho/gcc | 10 / 0.55 / 0.80 | 10 / 0.55 / 0.80 | 10 / 0.55 / 0.80 |
| warning n/rho/gcc | 5 / 0.28 / 0.55 | 5 / 0.28 / 0.55 | 5 / 0.28 / 0.55 |

合理性评估：

- 等级单调性正确：`warning.n(5) < danger.n(10)`，rho 0.28<0.55，gcc 0.55<0.80，DANGER 门槛严格高于 WARNING。
- **偏保守倾向**：监控区域为 6m×4m=24m²（main.cc IPM 标定目标四边形），`edge_dist_m=2.0` 意味着任意两个相距 2 米内的行人都建边。10 人处于 24m²（人均 2.4m²、平均间距约 1.5m）时，2 米建边基本使全图连通，`gcc` 会迅速接近 1.0，DANGER 较易触发。该取值对"拥挤"判定偏敏感，适合作为现场调参起点而非固定生产值。
- **rho 与 gcc 为"或"关系**（`density>=rho || r_gcc>=gcc`），二者本身高度相关（高连通通常伴随高密度），"或"语义会进一步放大灵敏度。若希望更稳健，可考虑"且"或加权评分。
- 建议：现场按实际机位/画面覆盖标定 `edge_dist_m`（1.0~1.5m 更贴近行人物理间距），并据实景标注数据校准 n/rho/gcc。

---

## 3. JSON 加载与缺省/非法值处理

`risk_config.h::loadRiskConfig` 已实现：

- 文件无法打开 → 抛 `runtime_error`；
- JSON 解析失败 / 根节点非对象 → 抛 `runtime_error`；
- `edge_dist_m` 必须为正数（`<=0` 抛错）；
- `n` 必须非负，`rho`/`gcc` 必须在 `[0,1]`；
- 语义校验 `warning.n <= danger.n`，否则抛错；
- 支持部分配置：缺失的 `danger`/`warning` 子对象或字段自动使用默认值。

发现的缺口：

- **[中] 只校验了 `n` 的等级单调性，未校验 `rho`/`gcc`**：若配置 `danger.rho=0.2`、`warning.rho=0.8`（warning 比 danger 更严格），`warning.n<=danger.n` 检查通过，但等级语义颠倒，WARNING 可能比 DANGER 更难触发。同理 `gcc` 未校验。建议补充 `warning.rho<=danger.rho`、`warning.gcc<=danger.gcc` 的校验。
- **[中] `n` 允许为 0，且未约束 `rho=0`/`gcc=0` 的组合**：`danger.n=0` 时 `n>=0` 恒成立；若同时 `danger.rho=0`（或 `gcc=0`），`evaluate()` 会恒判 DANGER。虽然默认值无此问题，但非法值处理可更严格（建议 `n>=1`，并禁止 rho/gcc 同时为 0 且 n=0 的退化配置）。
- **[低] `n` 使用 double**：语义上人数应为整数，JSON 中 `n=5.7` 会被接受（`isNumber` 且 `>=0`），`evaluate()` 中 `m_feat.n(整数) >= 5.7` 实际等效于 6 人。不构成错误，但建议限制为整数或明确文档说明。
- **[低] 未知字段被静默忽略**：`default_config.json` 含 `_comment` 字段，加载器忽略未知键，属合理行为，但若配置里字段名拼错（如 `danger.n` 写成 `danger.N`）不会报错，只是回退默认值。建议对完全缺失的必需字段给出告警。

### `setConfig` 回退逻辑缺陷（重点）

`risk_engine.cc` 第 20-27 行：

```cpp
void RiskEngine::setConfig(const pedestrian::RiskConfig &cfg) {
    m_cfg = cfg;
    if (m_cfg.warning.n > m_cfg.danger.n) {
        m_cfg.warning = pedestrian::RiskRule();  // 默认构造
        m_cfg.danger  = pedestrian::RiskRule();  // 默认构造
    }
}
```

- `pedestrian::RiskRule()` 的默认构造值是 **DANGER 默认值 10/0.55/0.80**（见 risk_config.h 第 45-46 行），不是 WARNING 默认值 5/0.28/0.55。
- 因此触发该回退后，`warning` 与 `danger` 阈值完全相同；而 `evaluate()` 先判 DANGER 再判 WARNING，导致 **WARNING 等级永远无法被触发**（所有达标场景都直接落入 DANGER）。
- 正常使用路径（`loadRiskConfigOr` + 合法 JSON）不会触发此分支，但一旦配置非法（如手工改动 JSON 或直接构造 `RiskConfig` 传入），回退结果反而破坏等级语义。
- 建议：回退应显式使用 `RiskConfig` 的默认值（warning=5/0.28/0.55、danger=10/0.55/0.80），而非 `RiskRule()` 的 DANGER 默认。

### 配置加载入口问题

- `main.cc` 第 96-97 行使用 `loadRiskConfigOr("./config/default_config.json", RiskConfig())`：
  - **相对路径**：依赖进程工作目录为可执行文件上级目录。若从其他目录启动程序，配置必然加载失败并静默回退。
  - **静默回退**：`loadRiskConfigOr` 不打印任何警告（对比 Python 端 `crowd_test.py::load_config` 失败时打印 `[WARN]`）。现场难以判断配置是否真正生效。
  - 建议：改用基于可执行文件路径定位的绝对路径，或 `loadRiskConfigOr` 失败时打印一条 stderr 告警。

---

## 4. levelName 枚举一致性

三套等级枚举/字符串核对：

| 模块 | 枚举 | 值映射 | levelName 返回 |
|---|---|---|---|
| risk_engine.h | `CrowdLevel` | CROWD_SAFE=0, CROWD_WARNING=1, CROWD_DANGER=2 | "SAFE" / "WARNING" / "DANGER" |
| risk_fsm.h | `RiskLevel` | RISK_SAFE=0, RISK_WARNING=1, RISK_DANGER=2 | 同上 |
| enhanced_decision.h | `EnhancedResult.level`（int） | 0 / 1 / 2（注释声明与 RiskLevel 对齐） | `RiskFSM::levelName` |

- 三套枚举数值完全对齐（0/1/2），字符串完全一致；`levelName` 的 `default` 分支均返回 "SAFE"。`main.cc` 中 `crowd = (CrowdLevel)er.level;` 与 `RiskEngine::levelName(crowd)` 混合使用，当前无偏差。
- **[低] 枚举类型不统一、依赖裸 int 强转**：`EnhancedResult.level` 是 `int`，`main.cc` 用 `(CrowdLevel)er.level` 强转，`enhanced_decision.h::levelName(int)` 再 `static_cast<RiskLevel>(lvl)`。缺少编译期类型约束，若将来任一枚举调整数值/插入值，强转会静默产生错误等级。建议统一为一个公共枚举（如都使用 `RiskLevel`），或增加 `static_assert` 断言两枚举数值一致。
- **[低] default 分支掩盖非法值**：对未定义枚举值（如 `(CrowdLevel)5`），`levelName` 返回 "SAFE"，但 `m_level` 仍是非法值。`evaluate()` 内部只会写入合法值，故当前安全；对外部传入非法 int 的健壮性一般。

---

## 5. 与 FSM 的配合

`main.cc` 通过 `#ifdef USE_ENHANCED_DECISION` 在两条路径间**编译期二选一**：

- 默认路径（未定义宏）：`RiskEngine` 固定阈值规则（clear → addPedestrian → computeFeatures → evaluate），逐帧硬切。
- 增强路径（定义宏）：`EnhancedDecision::processFrame` → Kalman 平滑 → GraphLaplacian 特征 → `RiskFSM` 滞回/去抖/保持。

发现的问题：

- **[高] 两套阈值配置脱节**：`default_config.json` 的 `danger`/`warning`（n/rho/gcc）只在默认路径生效；增强路径下这些值**完全不参与**——`EnhancedConfig` 仅复用了 `edge_dist_m`（main.cc 第 101 行），FSM 评分使用独立默认配置（`RiskFSMConfig`：w1=0.4/w2=0.3/w3=0.3、n_ref=20、up_warning=0.40、up_danger=0.65 等）。结果是：同一现场，切换编译开关会得到不同的风险等级输出，而调 `default_config.json` 无法影响增强路径的行为。
- **[中] 默认规则引擎无滞回/去抖**：`risk_fsm.h` 注释明确说明其动机是解决 `RiskEngine::evaluate()` 逐帧抖动。默认路径（未定义 `USE_ENHANCED_DECISION`）没有 FSM 平滑，阈值附近的帧可能频繁翻转等级。
- **[低] 增强路径下 `RiskEngine` 实例闲置但始终构造**：不影响正确性，仅资源冗余。

配合建议（供后续方案参考，不在本次审查中实施）：

1. 让增强路径读取 `default_config.json` 的规则阈值，并将 FSM 的评分/阈值映射为同一套配置（或将 `danger`/`warning` 规则作为 FSM 的软约束）。
2. 若保留两套配置，至少在文档中明确"增强路径不受 default_config.json 的 danger/warning 影响"。
3. 默认路径考虑引入简单的滞回去抖（如连续 N 帧同等级才切换），或直接默认启用 FSM 路径。

---

## 6. 其他健壮性观察

- `mapToGround` 在 `m_H.empty()` 时直接返回，`addPedestrian` 坐标保持像素值，`computeFeatures` 会把像素当米制距离计算，产生错误特征。`main.cc` 总是设置 H，但 API 对未设置 H 的调用缺乏保护/告警。
- `computeFeatures` 在 n==1 时 `r_gcc=1.0`；若配置 `danger.n=1` 且 gcc 阈值较低可能误判（默认 10 无影响）。
- `default_config.json` 中 `_comment` 字段与 `risk_config.h` 头注释、`crowd_test.py` 常量注释三方互相引用，维护性良好。

---

## 7. 问题清单（按优先级）

| 级别 | 位置 | 问题 | 建议 |
|---|---|---|---|
| 高 | `risk_engine.cc:23-26` | 非法配置回退把 warning 重置为 DANGER 默认值，WARNING 永久失效 | 回退到 `RiskConfig()` 的完整默认值（warning=5/0.28/0.55） |
| 高 | `main.cc:97` + `risk_config.h:139` | 增强路径不读取 danger/warning 规则阈值，`default_config.json` 部分失效；两编译开关输出不一致 | 统一配置来源或在文档中明确差异 |
| 中 | `risk_config.h:131-134` | 只校验 warning.n<=danger.n，未校验 rho/gcc 的等级单调性 | 增加 rho/gcc 单调性校验 |
| 中 | `risk_config.h:97,116` | n 允许为 0，且未禁止 rho=0/gcc=0 退化配置（可恒判 DANGER） | 要求 n>=1，并拒绝退化组合 |
| 中 | `main.cc:97` + `loadRiskConfigOr` | 相对路径 + 静默回退，现场难以察觉配置未生效 | 绝对路径定位 + 回退时打印 stderr 告警 |
| 低 | 多处 | 三套等级枚举靠裸 int 强转桥接，缺类型安全 | 统一枚举或加 static_assert |
| 低 | `risk_config.h` | n 用 double 存整数人数 | 限定整数或文档说明 |

---

*报告结束。本次为只读审查，未修改任何源代码或配置文件。*