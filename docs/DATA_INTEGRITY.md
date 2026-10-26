# 数据完整性与测试验证报告

> 生成时间：2026-10-26
> 项目：H:\PedestrianRiskDecision（边缘端轻量化行人感知与风险决策系统）
> 对应任务：t_b778ad43「Run tests and validate data integrity」

---

## 1. 结论摘要

- **测试套件全通过**：Python 133 项（121 原有单元测试 + 12 项新增集成测试）、
  C++ 主机单元测试 26/26 + 28/28 + 15/15 全部通过（Ubuntu g++ 11.4）。
- **数据完整**：500 张图片、111 条视频、30 张验证集清单、配置与全部结果 JSON
  均通过一致性 / 完整性审计，未发现损坏或缺失。
- **发现并修复 2 个真实缺陷**：`crowd_test_enhanced.py` 的 RiskFSM 调用存在两处错误，
  导致增强路径的 FSM 一直是死代码（静默回退到单帧规则判定）。已修复并由新的
  集成测试覆盖。
- **历史增强结果说明**：`results/video_enhanced/*` 在修复前生成，FSM 未实际生效
  （仍使用 Kalman + 谱特征，但等级为逐帧规则判定）。修复后脚本的 FSM 已生效，
  详见第 5 节。

---

## 2. 测试执行结果

### 2.1 Python 单元测试（回归）

```bash
cd H:\PedestrianRiskDecision
python -m pytest tests -q
# 结果：121 passed（原有 6 个测试文件）
```

覆盖：ByteTrack、DBSCAN、图拉普拉斯、IPM/EMA、Kalman、风险 FSM。

### 2.2 新增集成测试

新增 `tests/test_integration_pipeline.py`（12 项测试），把 `scripts/crowd_test.py`
（经典规则路径）与 `src/algorithms/*`（增强路径：谱特征 + Kalman + RiskFSM）作为
**真实组件**串联，在真实脚本使用的同一批函数上验证端到端行为，避免与单元测试重复：

| 测试 | 验证点 |
|---|---|
| `test_rule_engine_realistic_scenarios` | 规则引擎在空/稀疏/局部聚集/密集/双簇/长链场景输出正确等级 |
| `test_rule_engine_matches_plain_constants` | 运行时配置与内置常量结果一致 |
| `test_rule_engine_config_threshold_effect` | 调高 DANGER 人数阈值后密集场景降级为 WARNING（可配置生效） |
| `test_enhanced_pipeline_gather_then_disperse` | 增强流水线 SAFE→WARNING→DANGER→WARNING→SAFE 正确迁移 |
| `test_enhanced_pipeline_stays_safe_on_sparse` | 稀疏/无人场景全程 SAFE，零误报 |
| `test_enhanced_dense_video_timeline_structure` | 60 帧密集视频 timeline 结构正确、DANGER 主导 |
| `test_compute_features_matches_graph_laplacian` | 两条实现路径的 n/m/density/r_gcc/clustering 完全一致 |
| `test_ipm_maps_pixel_feet_to_ground` | IPM 单应映射（get_homography + map_to_ground）正确 |
| `test_config_matches_crowd_test_defaults` | config/default_config.json 与 crowd_test 内置常量一致 |
| `test_level_definitions_consistent` | RiskFSM 与规则引擎共享同一等级集合 |
| `test_process_video_end_to_end` | 直接运行真实 `crowd_test_enhanced.process_video`（fake 检测器），FSM 生效、timeline 正确 |
| `test_process_video_sparse_stays_safe` | 真实 process_video 在稀疏场景全程 SAFE |

```bash
python -m pytest tests/test_integration_pipeline.py -v   # 12 passed
python tests/test_integration_pipeline.py                # 独立运行同样 12/12 通过
```

### 2.3 C++ 主机单元测试（老电脑 Ubuntu 22.04，g++ 11.4）

```bash
cd ~/prd_verify/src/luckfox_rtsp_yolov5 && make test
# test_enhanced_decision  26/26 passed
# test_minijson           28/28 passed
# test_risk_config        15/15 passed（OpenCV 不可用，SKIP risk_engine_config）
```

> 说明：该 Ubuntu 环境无 OpenCV，故 `test_risk_engine_config`（17/17）在上一任务中已用
> fake-OpenCV 验证通过（见 git 历史）。本次源码未改动 C++，复跑通过。

---

## 3. 数据完整性审计

审计脚本：`audit_data.py`（本任务工作区），用 OpenCV 读取全部图片、ffprobe 校验全部视频、
程序化比对 JSON 与清单。

### 3.1 图片清单 `dataset/images_manifest.csv`（30 张验证集）

| 检查项 | 结果 |
|---|---|
| manifest 行数 | 30 |
| 磁盘 JPEG 数 | 30 |
| 清单有但磁盘缺 | 0 |
| 磁盘有但清单缺 | 0 |
| 清单内重复文件名 | 0 |
| 无法打开 | 0 |
| 尺寸与清单不符 | 0 |
| status != OK | 0 |

### 3.2 500 张基线集 `dataset/images500`

| 检查项 | 结果 |
|---|---|
| JPEG 数 | 500 |
| 非 jpg 文件 | 仅 `README.md` |
| 无法打开 | 0 |
| 文件名（`<id>_<count>.jpg`）格式不符 | 0 |
| 不同尺寸数 | 136（COCO 原图尺寸，合理） |

### 3.3 111 条真实视频 `dataset/videos50`

用 ffprobe 重新探测全部 111 个 mp4（时长/宽高/编码/可解码）：

| 检查项 | 结果 |
|---|---|
| mp4 数 | 111 |
| ffprobe 失败 | 0 |
| 与 `_validation/_validation.json` 不一致 | 0 |

`_validation.json` 与 `_hog_check.txt` 记录了 111/111 通过的校验证据。

### 3.4 配置 `config/default_config.json`

- JSON 解析成功；键：`edge_dist_m / danger / warning / _comment`。
- `danger.n=10 > warning.n=5`，阈值关系正确。
- 与 `crowd_test` 内置常量一致（`test_config_matches_crowd_test_defaults`）。

### 3.5 实验结果 JSON

| 文件 | 检查项 | 结果 |
|---|---|---|
| `results/experiment_summary.json` | 与 5 条 timeline 的 frames/dist/switches/avg_n 逐项比对 | 0 不一致 |
| `results/images500_baseline/results.json` | total=500, processed=500, failed=0 | 通过 |
| 同上 | 结果条目数 | 500 |
| 同上 | 等级分布 | SAFE 414 / WARNING 61 / DANGER 25 |
| 同上 | n 值越界 | 0 |
| 5 条 `*_timeline.json` | 结构含 frame/level/n/density/r_gcc/fiedler | 通过 |

---

## 4. 发现并修复的缺陷

### 缺陷 1：`RiskFSM.update` 收到 `SimpleNamespace` 而非 Mapping

`scripts/crowd_test_enhanced.py` 原来：

```python
fs = SimpleNamespace(n=n, density=density, r_gcc=r_gcc)
fsm.update(fs, now_ms)
```

`RiskFSM.update(features, now_ms)` 的 `_compute_score()` 用 `features.get(...)` 读取，
`SimpleNamespace` 没有 `.get`，每帧必然抛 `AttributeError`，被 `except` 吞掉并回退到
`raw_level`（单帧规则判定）。**后果：增强路径的 FSM 一直是死代码。**

修复：改为传 dict：

```python
fsm.update({"n": n, "density": density, "r_gcc": r_gcc}, now_ms)
```

### 缺陷 2：`fsm.level()` 被当方法调用，但 `level` 是属性

`RiskFSM.level` 是 `@property`（返回字符串）。脚本原来写 `lv = fsm.level()`，每帧必然抛
`TypeError: 'str' object is not callable`，同样被 `except` 吞掉回退 `raw_level`。

修复：

```python
lv = fsm.level   # 属性，返回 'SAFE'/'WARNING'/'DANGER'
```

### 影响说明

修复前，`results/video_enhanced/*_timeline.json` 的等级分布实际上来自**逐帧规则判定**
（Kalman 平滑与谱特征仍生效，但 FSM 时序稳定未生效）。历史结果文件保留原样（它们是真实
运行输出，仅未含 FSM 稳定）。修复后脚本的 FSM 已生效，见第 5 节复跑验证。

---

## 5. 真实场景行为验证（修复后）

### 5.1 规则引擎（经典路径）在真实场景

| 场景 | n | density | r_gcc | 期望 | 实际 |
|---|---|---|---|---|---|
| 空场景 | 0 | 0.000 | 0.000 | SAFE | ✅ SAFE |
| 稀疏 3 人分散 | 3 | 0.000 | 0.333 | SAFE | ✅ SAFE |
| 5 人局部聚集 | 5 | 1.000 | 1.000 | WARNING | ✅ WARNING |
| 12 人密集 | 12 | 0.985 | 1.000 | DANGER | ✅ DANGER |
| 双簇 2×6（相隔远） | 12 | 0.455 | 0.500 | WARNING | ✅ WARNING |
| 11 人长链 | 11 | 0.182 | 1.000 | DANGER | ✅ DANGER |

> 长链场景验证了 `r_gcc` 判据：即使密度低，只要形成单一连通分量即触发 DANGER。

### 5.2 增强流水线（Kalman + 谱特征 + FSM）迁移

模拟视频时间线：空(10帧)→稀疏(10帧)→局部聚集(10帧)→密集(10帧)→双簇(10帧)→空(60帧)。

- 前 20 帧保持 SAFE；
- 局部聚集阶段升到 WARNING；
- 密集阶段升到 DANGER；
- 最后空场景从 DANGER 阶梯式降回（经过 WARNING），最终回到 SAFE。

验证了滞回、确认帧（debounce）与 DANGER 保持期。

### 5.3 真实 `process_video` 端到端（fake 检测器）

直接调用修复后的 `crowd_test_enhanced.process_video`（注入恒定密集人群）：
40 帧输出 `{SAFE:2, WARNING:3, DANGER:35}`——前 2 帧为 FSM 防抖启动，之后稳定 DANGER，
**无 "FSM 更新失败" 告警**。稀疏场景 30 帧全程 SAFE。

### 5.4 真实视频复跑（修复后 FSM 生效）

用 `yolov8n.pt`（CPU 推理，本机无 CUDA）复跑 `dataset/videos50/` 中两条真实视频
（含 573 条真机日志之外的真实数据集视频），输出到 `results/video_enhanced_fsm/`：

| 视频 | 场景 | 修复前（`results/video_enhanced/`，FSM 死代码=逐帧规则） | 修复后（`results/video_enhanced_fsm/`，FSM 生效） |
|---|---|---|---|
| `mixkit_13192.mp4`（305 帧，密集人群 avg_n≈14.9） | 密集 | SAFE 1 / WARNING 40 / DANGER 264，切换 4 | SAFE 6 / WARNING 120 / DANGER 179，**切换 4** |
| `mixkit_13265.mp4`（281 帧，稀疏/无人） | 稀疏 | SAFE 281 | **SAFE 281（零误报）** |

修复后密集视频的 FSM 输出切换仍为 4 次，但等级分布更保守（更多 WARNING）：
这是 FSM 滞回/确认帧/保持期按设计生效的表现——从 SAFE 升 WARNING 需连续确认，
升 DANGER 需 score≥0.65 且连续 3 帧；人群密度在阈值附近波动时 FSM 会稳定在
WARNING 而不是逐帧闪跳。稀疏视频全程 SAFE，零误报。

> **注意**：README 中"密集人群持续输出 DANGER（264/305帧）"是修复前（FSM 死代码、
> 逐帧规则判定）的历史结果。修复后 FSM 生效，结果更保守（见上表）。论文引用增强
> 实验结果时，建议以 `results/video_enhanced_fsm/`（FSM 实际生效）为准。

---

## 6. 待跟踪项

| 项 | 状态 |
|---|---|
| `results/video_enhanced/*` 为修复前输出（FSM 未生效） | 保留历史；修复后已生成 `results/video_enhanced_fsm/`（FSM 生效） |
| README 对增强实验的描述（"密集 264/305 帧 DANGER"）为修复前数据 | 建议以 `results/video_enhanced_fsm/` 为准（见 5.4） |
| 论文中使用增强结果时，建议引用复跑后的 FSM 版本 | 建议 |

---

*本报告由测试与审计脚本自动生成数据、人工核对后撰写。所有数据均为真实运行/探测结果。*