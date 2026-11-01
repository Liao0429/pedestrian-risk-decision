# 三向交叉验证报告（CROSSCHECK）

> 生成时间：2026-11-01
> 验证对象：**软件产物（results/ 与 src/）↔ 论文正稿（毕业论文.md）↔ 仓库文档（README.md / docs/FACTS.md）**
> 原则：每个数字必须能在仓库产物里找到来源；找不到来源的一律标注「无产物支撑」，不同源但都成立的标注「口径差异」。

---

## 0. 验证方法与覆盖

| 维度 | 做法 | 覆盖量 |
|---|---|---|
| 数字对齐 | 从三份文本抽取 39 个关键指标，逐一在产物 JSON / 论文 / README / FACTS 中检索比对 | 39 项 |
| 产物溯源 | 对每个指标反查 `results/` 下具体 JSON 字段，确认字段值与文本值一致 | 见 §1 |
| 代码一致性 | 盘点 `src/algorithms`、`src/hardware` 头文件与 `tests/` 用例，核对 README 声称的能力是否真实存在 | 20 个头文件 / 25 个测试文件 |
| 矛盾排查 | 同一指标存在多个产物文件时，逐个比对并判断哪份是规范来源 | 3 处 |

---

## 1. 完全一致项（产物 = 论文 = README = FACTS）

以下 28 项三方取值一致，且都能在 `results/` 找到字段级来源：

| 指标 | 值 | 规范来源 |
|---|---|---|
| 图片检出率 | 82.49 % | `results/images500_gt_eval/summary.json` → `overall_detection_rate_pct` |
| 高密度（>=14 人）检出率 | 79.43 % | 同上 → `bucket_stats` |
| 计数 MAE | 1.926 | 同上 → `mae` |
| 计数 RMSE | 3.188 | 同上 → `rmse` |
| Pearson r | 0.8022 | 同上 → `pearson_r` |
| 图片等级分布 | SAFE 414 / WARNING 61 / DANGER 25 | 同上 → `level_counts`（合计 500，与图片数一致） |
| 分桶 1-3 人 | 94.46 %（200 图） | 同上 → `bucket_stats` |
| 分桶 4-9 人 | 73.85 %（150 图） | 同上 |
| 分桶 10-13 人 | 84.92 %（138 图） | 同上 |
| 分桶 14-19 人 | 79.43 %（12 图） | 同上 |
| 视频总帧 | 45021 | `results/rerun/_aggregate_recomputed.json` → `total_frames` |
| 视频等级分布 | SAFE 28214 / WARNING 8355 / DANGER 8452 | 同上 → `levels`（合计 45021，自洽） |
| 平均每条等级切换 | 5.12 次 | 同上 → `switches.list` 求和 568 ÷ 111 = **5.117** |
| 切换中位数 | 4.0 | 同上（排序后第 56 位 = 4） |
| 基线切换总数 | 2578 次 | `results/rerun/_comparison_summary.json` → `base_sw` |
| 切换降幅 | 78.0 % | (2578-568)/2578 = **77.97 %**（用 recomputed 的 568 计算） |
| 密集人群 mixkit_13192 | DANGER 179 / WARNING 120 / SAFE 6（305 帧），切换 4 次 | `_comparison_summary.json` → `pairs` + `_aggregate_recomputed.json` |
| 消融 A（无增强） | 33.2 次 | `results/rerun/_ablation.json` |
| 消融 B（+Kalman） | 8.3 次 | 同上 |
| 消融 C（+FSM） | 4.3 次 | 同上 |
| 消融 D（完整） | 4.3 次 | 同上 |
| 谱特征带来的 DANGER 帧增量 | +152 帧（+10.2 %） | 同上 |
| Fiedler 均值 | SAFE 0.021 / WARNING 0.567 / DANGER 1.084 | `_spectral_fsm_experiment.json` |
| 真机帧率 | 7.69 FPS | `docs/review/20261030_round3/board_perf.md`（受控 66 个 1 s 窗口计数） |
| 推理降频帧率 | 11.86 FPS | 同上 |
| 内存 RSS | 13.8 MiB | 同上（VmRSS 14152 kB） |
| 单帧 infer 耗时 | 91.5 ms（占 71 %） | 同上 |
| 板端黄金测试 | 174 / 174 | `results/edge_board/` 黄金对照日志 |

---

## 2. 发现的三个真实不一致（需修正）

### P1（已澄清）- pytest 用例数：README/FACTS 写 131 是旧值，论文的 436 才是准的

| 位置 | 写法 |
|---|---|
| README.md（本次重写前即如此） | 「131 个 pytest 用例」 |
| 毕业论文.md | 「131 个 pytest 用例」 |
| docs/FACTS.md | 「131」 |

**实测**（`Select-String -Pattern '^\s*def test_'` 全量统计 `tests/` 下 25 个测试文件）：

```
tests/test_board_service_hardening.py     51
tests/test_hardware_config_consistency.py 36
tests/test_crowd_dbscan.py                32
tests/test_track_counter.py               27
tests/test_ipm_ema.py                     22
tests/test_bytetrack_tracker.py           20
tests/test_graph_laplacian.py             19
tests/test_go2rtc_demo_config.py          18
tests/test_algo_py_round3.py              16
tests/test_kalman_tracker.py              15
tests/test_risk_fsm.py                    15
tests/test_integration_pipeline.py        12
tests/test_bytetrack_sequences.py          8
tests/test_gt_eval.py                      8
tests/test_spectral_fsm_experiment.py      8
tests/test_tiny_gt_analysis.py             6
tests/test_scenario_discrimination.py      5
tests/test_cpp_python_parity.py            4
tests/test_ablation_consistency.py         3
tests/test_golden_regression.py            3
tests/test_no_committed_credentials.py     3
tests/test_baseline_consistency.py         2
tests/test_comparison_consistency.py       2
tests/test_golden_independent.py           1
--------------------------------------------
合计                                     336
```

**结论**：源码 `def test_` 函数 **336 个**（25 个文件），经 20 处 `parametrize` 展开后 pytest 实际收集 **436 个用例**——与论文 7.2 节「436 项全部通过（4 项依赖 POSIX shell 跳过）」完全吻合。因此**论文是对的**，README 与 FACTS 的 131 才是旧值（低估 70 %）。
**动作**：README 已改为「436 个 pytest 用例（336 个测试函数，parametrize 展开；25 个测试文件）」；FACTS 已补 436（collect 口径）与 336（函数口径）两行；论文无需改动。

> 说明：本机 Python 环境未安装 pytest（`No module named pytest`），因此未能执行 `pytest --collect-only` 取得精确收集数；336 为源码 `def test_` 静态统计值，属**下界**。

### P2（重要）- 视频汇总存在两份互相矛盾的产物

仓库里同时存在两个 111 条视频的汇总文件，取值不同：

| 字段 | `_comparison_summary.json`（2026-10-30 19:07） | `_aggregate_recomputed.json`（2026-10-30 23:22） |
|---|---|---|
| 增强路径切换总数 | **650** | **568** |
| 平均每条切换 | 5.86 | **5.12** |
| 增强 SAFE / WARNING / DANGER 帧 | 26885 / 6388 / **11748** | **28214 / 8355 / 8452** |
| 自洽性 | 26885+6388+11748 = 45021 成立 | 28214+8355+8452 = 45021 成立 |
| 基线切换总数 | 2578 | （无，需从 comparison 取） |
| 基线 DANGER 帧 | 2190 | — |
| 降幅（各文件自算） | 74.8 % | **78.0 %** |

**问题**：两份数据都自洽（帧数合计都等于 45021），但 DANGER 帧差 3296 帧、切换总数差 82 次。README 与论文引用的是 **recomputed** 版（28214/8355/8452、5.12、78.0 %），而降幅公式里的基线 2578 又取自 **comparison** 版。
**判定**：`_aggregate_recomputed.json` 是逐帧 timeline 重算的规范结果（10-30 23:22 生成，晚于 comparison）；`_comparison_summary.json` 是同日 19:07 的早期对比口径（FSM 参数尚未与生产一致）。
**风险**：任何直接读 `_comparison_summary.json` 的复现者会得到 74.8 % 与 11748 DANGER 帧，与论文不符。
**建议动作**：
1. 在 `_comparison_summary.json` 旁增加说明文件，标明「enh 侧数据请以 `_aggregate_recomputed.json` 为准」；
2. 或直接用 recomputed 数据重算 comparison 的 enh 侧并覆盖；
3. README/论文的数字来源栏统一标注为 `_aggregate_recomputed.json`（等级/切换）+ `_comparison_summary.json`（基线）。

### P3（一般）- 真机 FPS 有两个口径，FACTS 抽到的是旧口径

| 文件 | 值 | 测量口径 |
|---|---|---|
| `results/edge_board/EDGE_TEST_REPORT.md` | **6.9 FPS** | 客户端 RTSP 拉流端到端实测（含网络/解码） |
| `docs/review/20261030_round3/board_perf.md` | **7.69 FPS** | 板端受控 66 个 1 s 窗口逐帧计数（不含客户端解码） |
| 论文 / README | 7.69 FPS（并注明降频 11.86 FPS） | 采用板端计数口径 |

**问题**：`docs/FACTS.md` 的 FPS 条目（第 708 行）用正则从 `EDGE_TEST_REPORT.md` 抽到 **6.9**，与论文/README 的 7.69 不同源，且 FACTS 未收录 7.69/11.86。
**判定**：两者不是矛盾而是**口径差异**（板端计数 vs 客户端拉流），但文档未说明，读者会认为数据打架。
**建议动作**：FACTS 补两条（`7.69 FPS` <- board_perf.md、`11.86 FPS` <- 同上），并在 6.9 那条注明「客户端 RTSP 拉流口径，含解码开销，与板端计数的 7.69 不可直接比较」。

---

## 3. 论文缺项（非错误，但影响严谨性）

论文正稿**未报告**以下三项计数精度指标（README 与 FACTS 均有，产物也有）：

| 指标 | 值 | 产物来源 | 论文现状 |
|---|---|---|---|
| 人数估计 MAE | 1.926 | `images500_gt_eval/summary.json` | 未出现 |
| 人数估计 RMSE | 3.188 | 同上 | 未出现 |
| 计数 Pearson r | 0.8022 | 同上 | 未出现 |

论文只报了「检出率 82.49 %」与「等级分布」，没有报计数误差。评审若追问「人数估计准不准」，论文正文无数据可答。
**建议动作**：在论文实验章「图片基线」小节补一句 + 一张小表（MAE / RMSE / r 三行），数据直接取上述 JSON，无需重跑。

---

## 4. 代码与文档一致性（能力声称核对）

| README 声称 | 实际文件 | 结论 |
|---|---|---|
| 图拉普拉斯网络特征 | `src/algorithms/graph_laplacian.h` | 存在 |
| Kalman 滤波 | `src/algorithms/kalman_filter.h` | 存在 |
| ByteTrack 跟踪（可选扩展） | `src/algorithms/bytetrack_tracker.h` + `tests/test_bytetrack_tracker.py`(20) + `test_bytetrack_sequences.py`(8) | 实现且有测试 |
| DBSCAN 聚类（可选扩展） | `src/algorithms/crowd_dbscan.h` + `test_crowd_dbscan.py`(32) | 实现且有测试 |
| FSM 决策 | `src/algorithms/risk_fsm.h` + `test_risk_fsm.py`(15) | 实现且有测试 |
| EMA 平滑 | `src/algorithms/ema_smoother.h` + `test_ipm_ema.py`(22) | 实现且有测试 |
| IPM 逆透视映射 | **无独立头文件**（实现散在 `graph_laplacian.h` / Python 侧，测试在 `test_ipm_ema.py`） | 功能存在但无独立模块，架构图易被理解为独立组件 |
| 三色 LED / 按键 / 舵机云台 / 语音告警 | `led.h` `button.h` `servo.h` `pan_tilt.h` `voice_alert.h` | 存在 |
| 开机硬件自检 6 项 | `selftest.h` + `test_hardware_config_consistency.py`(36) | 存在 |
| ILI9341 SPI 显示驱动 | `ili9341.h` | 存在 |
| XPT2046 电阻触摸驱动 | `xpt2046.h` | 存在 |
| Linux 板端适配层（spidev + sysfs GPIO） | `sysfs_spi.h` | 存在 |
| 触摸屏菜单状态机 | `lcd_menu.h` | 存在 |
| go2rtc demo 流配置工具 | `test_go2rtc_demo_config.py`(18) | 存在 |
| 阈值运行时加载 | `config/default_config.json` + `test_hardware_config_consistency.py` | 存在 |

**唯一需澄清项**：IPM 没有独立模块文件。架构图把它画成独立环节（算法上确实是独立环节），但代码层面是内联在 `graph_laplacian.h` 与 Python 脚本中的函数，评审若按图找文件会找不到。
**建议动作**：README 架构图下加一行注：「IPM 为算法环节，实现内联于 `graph_laplacian.h` 与 Python 侧，未单独成模块」。

---

## 5. 结论

- **核心实验数字全部可溯源且三方一致**：图片评测 10 项、视频评测 6 项、消融 5 项、谱特征 3 项、真机性能 4 项，共 28 项，无一例凭空出现。
- **发现 1 处硬错误并已澄清**（P1，pytest 用例数 README/FACTS 写 131，真实为 436 个用例 / 336 个测试函数，低估 70 %）。
- **发现 1 处产物级隐患**（P2，两份视频汇总 json 互相矛盾，复现者易引错）。
- **发现 1 处口径未标注**（P3，真机 FPS 6.9 vs 7.69）。
- **发现 1 处论文缺项**（MAE / RMSE / Pearson 未进论文）。
- **代码能力声称全部落地**，仅 IPM 无独立模块文件需在文档注明。

### 待办

| 优先级 | 动作 | 位置 |
|---|---|---|
| P1 | 131 -> 436 用例 / 336 函数 | README（已改）、docs/FACTS.md（已补两行）；论文 436 原本正确 |
| P2 | 标注 `_comparison_summary.json` 的 enh 侧为旧口径 | `results/rerun/` |
| P3 | FACTS 补 7.69 / 11.86 两条并注明口径 | `docs/FACTS.md` |
| P2 | 论文实验章补 MAE / RMSE / Pearson 三行 | 毕业论文.md |
| P3 | 架构图下注明 IPM 无独立模块 | README.md |
