# 测试与质量保障审查报告（Agent7 / QA）

> 审查角色：测试与质量保障专家
> 审查方式：只读审查 + 本机实际运行验证（未修改任何仓库源码）
> 审查日期：2026-10-26
> 审查对象：`tests/`（10 个 pytest 文件，共 141 项测试）、`src/luckfox_rtsp_yolov5/tests/`（4 个 C++ 测试程序）、`tests/golden/`（黄金数据）、`scripts/generate_golden.py`、`scripts/rerun_*.py`、`scripts/ablation_study.py`、`src/luckfox_rtsp_yolov5/Makefile` 与 `CMakeLists.txt`、`.github/workflows/ci.yml`、`config/default_config.json`、`results/`（实验汇总）

---

## 一、测试体系现状盘点

### 1.1 Python 测试资产（`tests/`，10 个文件 / 141 项）

任务书说"8 个 pytest 文件"，当前实际为 **10 个**（新增 `test_golden_independent.py`、`test_ablation_consistency.py`，均为本角色历史建议的落地）。

| 测试文件 | 测试数 | 类型 | 覆盖模块 |
|---|---|---|---|
| `test_graph_laplacian.py` | 19 | 单元 | 谱特征、Jacobi 与 numpy 一致性、风险引擎兼容 |
| `test_kalman_tracker.py` | 15 | 单元 | Kalman 滤波、平滑、多目标、漏检外推、ID |
| `test_risk_fsm.py` | 14 | 单元 | 滞回、确认帧、DANGER 保持、配置校验、C++ 默认值一致 |
| `test_crowd_dbscan.py` | 32 | 单元 | DBSCAN 聚类/噪声/边界、参考实现交叉验证、C++ 算法复刻 |
| `test_bytetrack_tracker.py` | 20 | 单元 | IoU、匈牙利、遮挡 ID 稳定、参数校验 |
| `test_ipm_ema.py` | 22 | 单元 | IPM 标定/单应往返、RANSAC、EMA、C++ 默认值一致 |
| `test_golden_regression.py` | 3 | 回归 | Python 侧读 `tests/golden/*.json` 防漂移 |
| `test_golden_independent.py` | 1 | 回归 | numpy 独立参考锚定 graph_laplacian 黄金数据 |
| `test_ablation_consistency.py` | 3 | 回归 | 消融 `rule_level` 与真实 `evaluate_level` 一致、fsm 配置键 |
| `test_integration_pipeline.py` | 12 | 集成 | 真实 `crowd_test`/`crowd_test_enhanced` 端到端（假检测器） |

合计 **141 项**（其中 `test_ablation_consistency.py` 3 项在无 ultralytics 环境下无法收集，见第二节）。

### 1.2 C++ 测试资产（`src/luckfox_rtsp_yolov5/tests/`）

| 测试程序 | 验证点 |
|---|---|
| `test_enhanced_decision.cc` | 稀疏 SAFE、密集→DANGER、确定性、建边阈值、Kalman 平滑、reset |
| `test_minijson.cc` | JSON 解析/转义/代理对/错误抛异常/false 布尔回归 |
| `test_risk_config.cc` | 配置读取/默认回退/非法值、class_conf_thresholds、fsm/frame_dt 校验、单应文件、RiskEngine::setConfig |
| `test_golden_regression.cc` | C++ 读同一批 `tests/golden/*.json` 与 Python 参考一致 |

`Makefile` 的 `make test` 目标正确串联 4 个程序；`CMakeLists.txt` 也通过 `ctest` 暴露 4 个测试。但 **本机无 C++ 编译器，无法实际编译运行**（见第二节 F3）。

### 1.3 黄金数据（`tests/golden/`）

- `graph_laplacian.json`：6 个用例（empty/single/dispersed_4/path_p5/complete_k4/two_clusters），覆盖空图/单点/分散/路径/完全图/双簇。
- `ema_smoother.json`：3 个用例（标量 alpha=0.3、向量逐维 alpha、时间戳模式 tau=2.0）。
- `risk_fsm.json`：3 个用例（阶梯升降+双保持、无时间戳 null 路径、滞回带）。

---

## 二、实际运行结果（test_run_result，真实输出）

### 2.1 任务书指定命令（真实结果：**收集失败**）

```
H:/Hermes Portable/data/home/cache/scratch/prd_venv/Scripts/python.exe -m pytest tests/ -q
（工作目录 H:/PedestrianRiskDecision）
```

退出码 **2**，输出摘要：

```
ERROR collecting tests/test_ablation_consistency.py
ImportError while importing test module 'H:\PedestrianRiskDecision\tests\test_ablation_consistency.py'
tests\test_ablation_consistency.py:23: in <module>
    from ablation_study import rule_level
scripts\ablation_study.py:15: in <module>
    from ultralytics import YOLO  # noqa: E402
E   ModuleNotFoundError: No module named 'ultralytics'
ERROR tests/test_ablation_consistency.py
!!!!!!!!!!!!!!!!!!! Interrupted: 1 error during collection !!!!!!!!!!!!!!!!!!!!
1 error in 0.45s
```

**根因**：`test_ablation_consistency.py` 导入 `scripts/ablation_study.py`，而 `ablation_study.py` 在模块顶层硬导入 `ultralytics`；指定的 prd_venv（以及 CI 按 `ci.yml` 只装 numpy+pytest）没有该包。**"一键跑全套测试"目前是断的。**

### 2.2 补充运行（诊断）

| 命令/环境 | 结果 |
|---|---|
| `pytest tests/ -q --continue-on-collection-errors` | **138 passed, 1 error** in 1.44s（唯一 error 即上述收集失败） |
| 临时给 PYTHONPATH 注入 ultralytics 桩（不修改仓库）后 `pytest tests/ -q` | **141 passed** in 1.34s；重复 3 次均为 141 passed（1.28~1.33s，无 flaky） |
| 仅 `tests/test_ablation_consistency.py`（带桩） | **3 passed** in 0.17s（测试逻辑本身正确，纯粹是导入依赖问题） |
| 推荐解释器 `H:/py311_gpu/Scripts/python.exe -c "import numpy"` | **失败**：`No module named 'numpy._core._multiarray_umath'`（该环境 numpy 安装已损坏，不可用） |

### 2.3 C++ 测试

- 本机无任何 C++ 编译器：`where g++ / gcc / clang++ / cl` 全部返回 1，常见 MinGW/MSYS 路径均不存在。
- 因此 `make test` / `ctest` **本机无法运行**；`docs/review/BASELINE_RESULTS.md` 记录的 C++ 四项通过（26/32/58/104）来自老电脑，本会话**未能独立复跑**。

---

## 三、审查问题逐项回答

### Q1 单元/集成/回归测试覆盖情况

- **单元**：核心算法模块（graph_laplacian、kalman、risk_fsm、crowd_dbscan、bytetrack、ipm/ema）均有较高质量单测，共 122 项。多数带"独立运行入口"（`python tests/xxx.py`），不依赖 pytest。
- **集成**：`test_integration_pipeline.py` 质量高——用假检测器跑真实 `process_video`，验证 timeline 结构、FSM 生效、规则/增强双路径一致、IPM 映射、配置一致性。
- **回归**：golden 回归（Python 侧 3 项 + C++ 侧 1 程序）+ 独立参考锚定（graph_laplacian）+ 消融一致性（3 项）。
- **覆盖缺口**：脚本层 IO/异常路径（Q4 详述）、C++ ByteTrack/DBSCAN 头文件无主机测试、覆盖率未度量。

### Q2 golden regression 机制

- **生成**：`scripts/generate_golden.py` 用 **Python 参考实现**生成 3 个 JSON 到 `tests/golden/`。
- **一致性机制**：C++（`test_golden_regression.cc`）与 Python（`test_golden_regression.py`）**读取同一批 JSON** 分别比对，容差 `FEAT_TOL=1e-4`（Jacobi vs numpy 数值差）、`EMA_TOL=1e-6`，从机制上防双实现漂移。
- **边界/默认/输出规则**：`risk_fsm.json` 覆盖无时间戳（JSON null ↔ C++ -1 哨兵）、滞回、帧+毫秒双保持；`test_risk_fsm.test_cpp_header_defaults_consistent`、`test_ipm_ema.test_cpp_header_defaults_consistent`、`test_risk_config.cc::test_fsm_missing_uses_default` 共同锁定 Python/C++ 默认参数一致；`test_ablation_consistency` 锁定消融脚本与真实规则引擎输出一致。
- **已知弱点**：
  1. 黄金数据"参考=被测"闭环：期望值由被测试的 Python 实现生成。`test_golden_independent.py` 已用 numpy 独立参考补上 graph_laplacian 锚点，但 **ema_smoother / risk_fsm 仍无独立参考**。
  2. 用例规模小：graph_laplacian 最大 n=6；risk_fsm 的 trend 全为 0；ema 时间戳用例 dt 远大于 tau，实际退化为"输出=输入"，未覆盖典型平滑区间与时间戳倒退。

### Q3 阈值与配置变化的可复现测试

- **C++ 侧**：`test_risk_config.cc` 覆盖有效加载、缺失字段回退默认、非法配置抛异常（warning.n>danger.n、非 JSON、文件不存在、class_conf_thresholds 非法键/值、fsm 滞回破坏/负权重/越界阈值、frame_dt 非法），并通过 `RiskEngine::setConfig` 验证配置驱动 evaluate（无 OpenCV 时 SKIP）。
- **Python 侧**：`test_risk_fsm.test_config_validation` 覆盖滞回带、确认帧、权重、n_ref、保持帧数；`test_integration_pipeline.test_rule_engine_config_threshold_effect` 验证调高 danger.n 阈值后 DANGER 降为 WARNING；`test_ablation_consistency` 遍历 n/rho/gcc 阈值两侧组合。
- **缺口**：`evaluate_level` 的 `>=` 精确边界（如 n==10 且 rho==0.55 恰好达标）没有参数化测试；C++ `RiskEngine::evaluate` 的边界断言依赖 OpenCV，无 OpenCV 环境被 SKIP。另发现 **`frame_dt` 配置跨实现不一致**（见 F6）。

### Q4 测试缺口

| 缺口 | 现状 | 严重度 |
|---|---|---|
| 异常处理（脚本层 IO） | `crowd_test.py` 的 `load_homography`（文件不存在/非3x3）、`map_to_ground`（w≈0 退化）、`process_image`（损坏图片）、`collect_images`（空目录）、`load_config`（坏 JSON 回退）均无测试 | 中 |
| 性能回归 | 无任何耗时断言；Jacobi O(n³) 无基准保护 | 中 |
| 长稳 | 无 8h/24h 长稳测试；BASELINE_RESULTS 明确"长稳待处理" | 中 |
| 内存泄漏 | C++ 测试未启用 ASAN/LSAN；无 RSS 断言 | 中 |
| 并发 | 无线程安全/并发测试（板端单线程串行，风险低但未验证） | 低-中 |
| 随机种子 | 各测试固定种子（0/7/42/99/2024/20261026 等），确定性好；但无跨种子矩阵/模糊测试 | 低 |
| 真机 | 仅 `results/edge_board/EDGE_TEST_REPORT.md` 一次性记录；无自动化真机回归 | 中（硬件依赖） |

### Q5 实际运行测试

见第二节。结论：**在指定 venv 下 `pytest tests/ -q` 因 ultralytics 缺失收集失败；注入桩后 141 项全部通过且 3 次重复稳定；C++ 测试因无编译器无法在本机运行。**

### Q6 可执行的低成本改进（按性价比排序）

1. **修 `test_ablation_consistency.py` 收集失败**（最高优先，一行级改动）：把 `ablation_study.py` 顶层的 `from ultralytics import YOLO` 改为与 `crowd_test.py` 相同的延迟导入（`try/except ImportError → YOLO=None`）；或在测试文件内注入桩。改后 `pytest tests/ -q` 即可在仅 numpy+pytest 的环境一键运行。
2. **修 `ci.yml`**：
   - python job 改为 `pip install -r requirements.txt`（含 ultralytics）或至少让新测试可收集；
   - cpp job 从引用不存在的 `tests/test_graph_laplacian.cc` 改为 `cd src/luckfox_rtsp_yolov5 && make test`（或 `cmake -S . -B build && cmake --build build && ctest --test-dir build`），并在 ubuntu 上 `apt install g++ make`（如需 OpenCV 分支加 `libopencv-dev pkg-config`）。
3. **补 `frame_dt` 一致性回归**：新增测试断言 Python `KalmanTracker(dt=...)` 与 `config/default_config.json` 的 `frame_dt` 一致（或让 Python 端读取该配置），消除 C++/Python 行为分歧。
4. **补脚本层异常路径测试**：损坏图片→`process_image` 返回 None；退化 H→`map_to_ground` 不崩溃；空目录→`collect_images` 返回 []；坏 JSON→`load_config` 返回内置默认。
5. **加性能基准**：`tests/test_performance_benchmark.py` 对 `compute_laplacian_features` n=32/64 随机点集断言单次耗时 < 宽松上限（如 100ms），并记录基线。
6. **扩充 golden**：graph_laplacian 加 n=20/50 随机图/完全图；risk_fsm 加 trend>0/trend<0 用例；ema 加时间戳相同/倒退用例。
7. **覆盖率门禁**：`pytest-cov --cov=src/algorithms --cov=scripts --cov-fail-under=85`。
8. **结果 schema 校验**：新增测试校验 `results/**/experiment_summary.json`、`*_timeline.json`、`_summary.json` 字段齐全且 level ∈ {SAFE,WARNING,DANGER}。
9. **文档化测试环境**：`H:/py311_gpu` 已损坏（numpy 缺失），建议在 README 写明用 `prd_venv`（或新建干净环境）+ `pip install -r requirements.txt` 后再跑测试。

---

## 四、Top Findings（severity / issue / evidence）

| # | 严重度 | 问题 | 证据 |
|---|---|---|---|
| F1 | **高** | 指定命令 `python -m pytest tests/ -q` 在指定 venv 收集失败，全套测试无法一键运行 | `prd_venv\Scripts\python.exe -m pytest tests/ -q` → rc=2，`ModuleNotFoundError: No module named 'ultralytics'`（`test_ablation_consistency.py:23` → `scripts/ablation_study.py:15` 顶层导入）。带 `--continue-on-collection-errors` 时 138 passed, 1 error |
| F2 | **高** | CI 工作流存在但两个 job 实际不可用 | `.github/workflows/ci.yml`：python job 只装 numpy+pytest → 必然复现 F1 收集失败；cpp job 引用不存在的 `tests/test_graph_laplacian.cc` 与 `../src/algorithms/graph_laplacian.cpp`（实际 C++ 测试在 `src/luckfox_rtsp_yolov5/tests/`，graph_laplacian 为 header-only），g++ 必然报错 |
| F3 | **高** | C++ 测试（含 C++ 侧黄金回归 104/104）无可持续自动验证途径 | 本机无 g++/gcc/clang/cl（`where` 均失败）；`make test` 需 g++；CI cpp job 又指向不存在的文件；通过记录仅来自 `docs/review/BASELINE_RESULTS.md`（老电脑） |
| F4 | **中** | 黄金数据"参考=被测"闭环部分残留 | `generate_golden.py` 用 Python 实现生成期望；`test_golden_independent.py` 仅锚定 graph_laplacian；`ema_smoother.json`/`risk_fsm.json` 无独立参考；且 golden 用例规模小（最大 n=6，risk_fsm trend 全 0，EMA 时间戳用例退化为输出=输入） |
| F5 | **中** | 文档推荐解释器 `H:/py311_gpu` 已损坏 | `H:/py311_gpu/Scripts/python.exe -c "import numpy"` → `No module named 'numpy._core._multiarray_umath'` |
| F6 | **中** | `frame_dt` 配置跨实现不一致，无测试覆盖 | `config/default_config.json` 设 `frame_dt: 0.1`；C++ `risk_config.h`/`main.cc` 读取该值（`enhanced_decision.cc:117`）；Python 侧 `crowd_test_enhanced.py` 硬编码 `dt=1.0/30.0`，从不读 frame_dt；C++(10FPS) 与 Python(30FPS) Kalman 行为会分歧 |
| F7 | **中** | 脚本层异常/IO 路径无测试 | `crowd_test.py` 的 `load_homography`(L118-145)、`map_to_ground`(L148-154)、`process_image`(L295)、`collect_images`、`load_config`(L224-233) 均无对应测试 |
| F8 | 中 | 性能/长稳/内存/并发测试全部缺失 | 仓库无 benchmark、无长稳脚本、无 ASAN/LSAN 配置、无并发测试；`BASELINE_RESULTS.md` 明确"长稳待处理" |
| F9 | 低 | 随机种子固定但无种子矩阵/模糊测试 | 各测试用固定 `default_rng(seed)`（0/7/42/99/2024 等），确定性好；DBSCAN 随机交叉仅 50+30 组固定样本 |
| F10 | 低 | `ablation_study.py` 不可移植且是 F1 的根因 | `ablation_study.py:13-14` 硬编码 `r"H:/PedestrianRiskDecision/scripts"` 与 `os.chdir(...)`；L15 顶层 `from ultralytics import YOLO` |
| F11 | 低 | 结果 JSON 无 schema 校验 | `results/**/*_timeline.json`、`experiment_summary.json`、`_summary.json` 结构无测试保护 |
| F12 | 低 | C++ ByteTrack/DBSCAN 头文件无主机测试 | `bytetrack_tracker.h`、`crowd_dbscan.h` 无对应 C++ 测试程序；`test_crowd_dbscan.py::TestCppAlgorithmLogic` 仅为 Python 复刻 |

---

## 五、正面结论

- Python 测试主体质量高：**141 项在 ultralytics 可用环境下全部通过**（3 次重复稳定，~1.3s），固定随机种子、独立运行入口、参数校验覆盖到位。
- `test_golden_independent.py` 与 `test_ablation_consistency.py` 已落实上一轮报告的两条核心建议（独立参考锚定、消融一致性）。
- 端到端集成测试用假检测器跑真实 `process_video`，能捕获 FSM 回退告警与 timeline 结构问题，价值很高。
- `results/rerun/` 下 111 条视频全量重跑结果（`_summary.json`：111/111 ok）与 500 图基线真实存在，为论文实验可复现提供了数据基础。

---

## 六、总结

测试体系**主体扎实但"一键可运行"与"CI 可用性"当前是断的**：指定命令因 `ablation_study.py` 顶层导入 ultralytics 而收集失败；CI 两个 job 均无法通过；C++ 测试无自动验证途径；`frame_dt` 存在跨实现配置分歧。建议按"修复收集失败（改延迟导入）→ 修复 CI（python 依赖 + cpp job 指向 `make test`）→ 补 frame_dt/异常/性能/覆盖率 → 扩充 golden"的顺序落地，成本低、收益直接。