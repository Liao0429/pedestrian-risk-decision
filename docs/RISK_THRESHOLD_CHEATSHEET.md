# 风险阈值速查页（RISK_THRESHOLD_CHEATSHEET）

> 用途：答辩现场应对评委关于「阈值怎么来的」「会不会误报漏报」的提问。
> 数据来源：`config/default_config.json`（板上与 Python 端一致）、`docs/algorithms.md` §9。
> 最后核对：2026-10-27（config 文件 `_comment` 与代码一致）。

## 一、两级判定架构

```
检测框 → 建图（edge_dist=2.0m 邻接）→ 特征(n, density, r_gcc, clustering)
   → ① RiskEngine 固定规则阈值（兜底，默认 enable_risk_engine_fallback=false）
   → ② RiskFSM 滞回状态机（生产路径）：评分 → 滞回阈值 → 三级等级
```

生产默认只走 **RiskFSM**（`enable_risk_engine_fallback=false`），规则引擎作为可选的兜底/对照。

## 二、综合拥挤度评分（RiskFSM）

```
score = w1 * norm(n) + w2 * density + w3 * r_gcc
norm(n) = min(n / n_ref, 1.0)
score += trend_bias * clamp(trend, -1, 1)      # 可选运动趋势加成
```

| 参数 | 默认值 | 含义 |
|---|---|---|
| w1 / w2 / w3 | 0.4 / 0.3 / 0.3 | 人数 / 密度 / 连通占比权重 |
| n_ref | 20 | 人数归一化参考值（≥20 人封顶为 1.0） |
| trend_bias | 0.05 | 运动趋势加成幅度（可选） |

## 三、FSM 滞回阈值（关键：防抖动）

状态转移要求 `score` 连续满足 `confirm_up`（升级）/ `confirm_down`（降级）帧，且 DANGER 有最小保持时间：

| 转移 | 阈值 | 默认值 | 说明 |
|---|---|---|---|
| SAFE → WARNING | `up_warning` | **0.40** | 升级阈值 |
| WARNING → DANGER | `up_danger` | **0.65** | 升级阈值 |
| WARNING → SAFE | `down_safe` | **0.28** | 降级阈值（< 升级阈值，形成滞回带） |
| DANGER → WARNING | `down_warning` | **0.55** | 降级阈值（< 升级阈值） |

| 参数 | 默认值 | 含义 |
|---|---|---|
| confirm_up | 3 | 升级需连续满足帧数 |
| confirm_down | 5 | 降级需连续满足帧数 |
| danger_hold_frames | 10 | 进入 DANGER 至少保持帧数 |
| danger_hold_ms | 1000 | 进入 DANGER 至少保持毫秒 |

**滞回一致性**：`up_warning(0.40) > down_safe(0.28)`，`up_danger(0.65) > down_warning(0.55)`，防止阈值附近逐帧闪烁。

## 四、标定依据（防"阈值拍脑袋"质疑）

- 阈值依据 `results/baseline/results.json` **真实检测结果**抽样标定：
  - DANGER 样本评分约 **0.67~0.68** → `up_danger=0.65`
  - WARNING 样本评分约 **0.62~0.66** → `up_warning=0.40`
- `danger_hold` 双维度（帧 + 毫秒）确保瞬时误检不会直接触发 DANGER，降级需 5 帧确认，避免漏报转「假安全」。
- DANGER 只能从 WARNING **阶梯式到达**，不允许 SAFE 单帧跳 DANGER。

## 五、RiskEngine 固定规则阈值（兜底路径）

| 等级 | n（人数） | rho（网络密度） | gcc（最大连通占比） |
|---|---|---|---|
| WARNING | ≥ 5 | ≥ 0.28 | ≥ 0.55 |
| DANGER | ≥ 10 | ≥ 0.55 | ≥ 0.80 |

- 建图邻接距离 `edge_dist_m = 2.0`（脚点欧氏距离 ≤2m 连边）。
- 检测目标 `target_classes=[0]`（COCO person），置信度可用 `class_conf_thresholds` 按类别差异化配置（默认空 = 全局阈值）。

## 六、常见答辩问答口径

**Q：阈值为什么这么定？会误报吗？**
A：阈值来自真实基线视频检测结果的评分分布（DANGER≈0.67 定 0.65，WARNING≈0.62-0.66 定 0.40）；滞回带 + 确认帧 + DANGER 保持时间三重机制抑制检测抖动与瞬时误检。

**Q：漏报怎么办？**
A：降级比升级更保守（升级 3 帧、降级 5 帧），且 DANGER 至少保持 1 秒，宁可多报一级也不瞬时回落成 SAFE；等级只升不跳（SAFE→WARNING→DANGER）。

**Q：参数能改吗？**
A：全部参数集中在 `config/default_config.json`，Python 与 C++ 端字段一致，改配置无需重编译（homography 等外部标定也走配置文件）。

**Q：规则引擎（固定阈值）和 FSM 什么关系？**
A：规则引擎是无状态硬切（阈值附近会闪），作为兜底/对照；生产用 FSM 状态机，消融实验证明 FSM 将链状聚集从瞬时 DANGER 压为 WARNING，减少抖动（见 `docs/review/IMPLEMENTATION_LOG.md` 阶段七）。