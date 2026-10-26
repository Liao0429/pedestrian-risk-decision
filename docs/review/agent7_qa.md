# 测试与质量保障只读审查报告（Agent7 / QA）

> 审查角色：测试与质量保障专家
> 审查方式：只读审查 + 本机实际运行验证。未修改任何仓库文件。
> 审查日期：2026-10-26
> 审查对象：`tests/`（Python 137 项）、`src/luckfox_rtsp_yolov5/tests/`（C++ 4 个测试程序）、`scripts/`（实验/结果统计脚本）、`config/default_config.json`、`tests/golden/*.json`、`.github/`（CI）、`results/`（实验汇总与板端报告）。

---

## 一、现状盘点

### 1.1 Python 测试资产（`tests/`，本会话实测 **137 passed**）

| 测试文件 | 覆盖模块 | 主要验证点 |
|---|---|---|
| `test_graph_laplacian.py`（~19 项） | 图拉普拉斯谱特征 | 网络特征、谱特征、JacobI 算法与 numpy 一致性、风险引擎兼容 |
| `test_kalman_tracker.py`（~14 项） | Kalman 跟踪 | 匀速外推、噪声抑制、多目标、超时剪枝 |
| `test_risk_fsm.py`（13 项） | RiskFSM | 滞回、确认帧、DANGER 保持、配置校验、C++ 默认值一致 |
| `test_crowd_dbscan.py`（~20 项） | DBSCAN | 聚类/噪声、边界 eps、C++ 算法等价复刻 |
| `test_bytetrack_tracker.py`（16 项） | ByteTrack | IoU、匈牙利、ID 稳定、参数校验 |
| `test_ipm_ema.py`（~15 项） | IPM 标定 + EMA | 单应往返、RANSAC、EMA 递推、时间戳模式 |
| `test_golden_regression.py`（3 项） | C++/Python 双实现 | 读 `tests/golden/*.json` 验证 Python 不漂移 |
| `test_integration_pipeline.py`（~11 项） | 端到端流水线 | 真实 `crowd_test`/`crowd_test_enhanced` + 假检测器、规则/增强/配置/时间线 |

### 1.2 C++ 测试资产（`src/luckfox_rtsp_yolov5/tests/`，`make test`）

| 测试程序 | 断言数（据 BASELINE_RESULTS） | 验证点 |
|---|---|---|
| `test_enhanced_decision` | 26/26 | 稀疏 SAFE、密集→DANGER、确定性、建边阈值、Kalman 平滑、reset |
| `test_minijson` | 32/32 | JSON 解析、转义、代理对、错误抛异常、深度限制 |
| `test_risk_config` | 58/58 | 配置读取/默认回退/非法值、class_conf_thresholds、fsm/frame_dt 校验、单应文件 |
| `test_golden_regression` | 104/104 | C++ 读同一批 `tests/golden/*.json`，与 Python 参考一致 |

### 1.3 黄金数据（`tests/golden/`）
- `graph_laplacian.json`：6 个小图（empty/single/dispersed_4/path_p5/complete_k4/two_clusters）
- `ema_smoother.json`：3 个用例（标量、向量逐维 alpha、时间戳模式）
- `risk_fsm.json`：阶梯升降 + 双保持用例（frame 中 `trend` 均为 0）

---

## 二、本会话实际验证结果（如实记录）

- ✅ **Python 全套 137 项测试已在本机实测通过**：`H:/py311_gpu/Scripts/python.exe -m pytest tests/ -q` → `137 passed in 1.21s`（清空 PYTHONPATH 避免环境污染后）。
- ✅ `test_risk_fsm.py` 独立运行 14/14 通过。
- ⚠️ **C++ 测试本机无法运行**：当前机器无 `g++`/`make`/`cmake`（`shutil.which` 均为 None）。C++ 四项 26/32/58/104 的通过结果来自 `docs/review/BASELINE_RESULTS.md`（老电脑运行记录），**未在本会话独立复跑**。
- ⚠️ 运行时环境差异：`test_golden_regression.py`/`test_graph_laplacian.py` 需要 numpy；本机 Hermes 捆绑的 numpy 为 cp314，与 py311 不兼容，清空 PYTHONPATH 后使用 py311 自带 numpy 2.4.6 才正常——这提示**测试运行环境必须隔离**（见 Q1/Q5）。

---

## 三、问题与建议（按严重度排序）

### Q1【高】无 CI，所有回归依赖手动，算法改动无自动防回归

- 【证据路径】仓库无 `.github/workflows/`（`docs/review/BASELINE_RESULTS.md` 记录"本机 137 / 老电脑 make test"均为手动）；根目录无 `pytest.ini`/`tox.ini`/`pyproject.toml`。
- 【严重度】高。任一算法修改（如 agent3 指出的 `rule_level` 逻辑）若无人手动跑两套测试，C++/Python 一致性立刻漂移且不被发现。
- 【建议测试用例 / 机制】
  1. 新增 GitHub Actions：`ubuntu-latest` 上 `apt install libopencv-dev pkg-config g++` → `python -m pytest tests/ -q`（Python 3.11 + 3.14 双版本矩阵）+ `cd src/luckfox_rtsp_yolov5 && make test`。golden 数据已入库，CI 可直接跑。
  2. 加入 `pytest-cov` 覆盖率门禁（见 Q5）。
  3. 在 `README.md` 加 "CI 徽章 + 一键复现测试" 小节。

### Q2【高】黄金数据"参考实现即被测实现"，无法发现 Python 与 C++ 同时错

- 【证据路径】`scripts/generate_golden.py:60-80` 用 **Python 实现**生成期望值；`tests/test_golden_regression.py:44-112` 读同一批 JSON 测 Python；`test_golden_regression.cc` 读同一批 JSON 测 C++。若 Python 的 `compute_laplacian_features` 有 bug（如 r_gcc 语义错），生成的期望也是错的，两侧测试会**同时通过**——黄金数据本身无独立来源。
- 【严重度】高。golden 只能防"C++ 相对 Python 漂移"，不能防"Python 自身语义错"。
- 【建议测试用例】
  1. 在 `generate_golden.py` 中增加**独立参考断言**：谱特征期望值用 `numpy.linalg.eigvalsh` 直接计算并与写入 JSON 的值比对；`r_gcc`/`clustering` 用手算小图（K2/K3/P3）验证；`risk_fsm` 期望用手推转移表验证。
  2. 在 `test_golden_regression.py` 增加一组**手工构造（非 Python 生成）**的固定期望用例，作为黄金数据的独立锚点。

### Q3【高】消融脚本与真实规则引擎不一致且无测试保护，论文定量结论不可复现

- 【证据路径】`scripts/ablation_study.py:33-40` `rule_level` 用 **OR** 逻辑（`n>=d.n or rho>=d.rho or gcc>=d.gcc`）；`scripts/crowd_test.py:244` `evaluate_level` 用 **AND**（`n>=dn and (density>=drho or r_gcc>=dgcc)`）。两者不一致会使消融基线虚高。另 `ablation_study.py:46` 与 `crowd_test_enhanced.py:87` 用 `cfg.get("temporal", {})` 读 FSM 配置，而 `config/default_config.json` 的键是 `"fsm"`，Python 端改配置不生效（agent3 已详述）。
- 【严重度】高。消融是论文核心工作量，基线非真实引擎 + 配置键错位 = 结论失真。
- 【建议测试用例】
  1. 新增 `tests/test_ablation_consistency.py`：对多组 `(n,rho,gcc)` 断言 `ablation_study.rule_level == crowd_test.evaluate_level`（当前会失败，暴露 OR/AND 不一致）。
  2. 断言 `load_config("config/default_config.json")["fsm"]` 存在且脚本不再读 `temporal`。
  3. 用假检测器跑 1 帧视频，验证 C 组（+谱特征）与 B 组（+Kalman）决策是否相同；若谱特征未进 FSM，应修正脚本或文档（见 agent3 P4）。

### Q4【中】C++ 测试本机无法运行，OpenCV 相关分支可能被 SKIP，结果无 artifact 留档

- 【证据路径】`Makefile` `test-risk-config` 用 `pkg-config --cflags opencv4` 条件编译；无 OpenCV 时 `test_risk_engine_config`/`test_load_homography_file` 打印 `SKIP`；BASELINE_RESULTS 记录来自老电脑。本机无 g++。
- 【严重度】中-高。CI/新机器上 OpenCV 分支静默跳过，`RiskEngine::setConfig` 驱动 `evaluate()` 的断言实际未跑。
- 【建议测试用例 / 机制】CI 用 `libopencv-dev` 让 OpenCV 分支真实执行；在仓库 `docs/review/` 或 CI artifact 留存每次 C++ 测试输出（`26/26 32/32 58/58 104/104`），形成可追溯记录。

### Q5【中】覆盖率未度量，无法定位未测分支

- 【证据路径】无 `.coveragerc`/`pyproject.toml`；`requirements.txt` 无 `pytest-cov`。
- 【严重度】中。`crowd_test.py`/`crowd_test_enhanced.py`/`ablation_study.py` 的异常分支是否被测完全不可见。
- 【建议测试用例 / 机制】`pytest --cov=src/algorithms --cov=scripts --cov-fail-under=85`，并将覆盖率报告作为 CI 门禁；优先补足脚本层异常分支（见 Q6）。

### Q6【中】脚本层 IO/异常分支无测试

- 【证据路径】`crowd_test.py`：`load_homography`（L117，文件不存在/非 3x3）、`map_to_ground`（L147，`w≈0` 退化）、`collect_images`（L391，空目录）、`process_image`（L294，图片损坏返回 None）、`load_config`（L223，坏 JSON 回退默认）——均无对应测试。
- 【严重度】中。板端/用户输入异常时这些路径决定程序是否静默失败。
- 【建议测试用例】
  1. 损坏图片 → `process_image` 返回 `None` 且 stderr 有错误。
  2. 退化 H（第三行全 0）→ `map_to_ground` 返回原坐标（不崩溃）。
  3. `collect_images` 空目录 → `[]`；`load_config` 传坏 JSON → 返回 `{}`（回退内置默认）。
  4. `VideoCapture` 打不开 → `process_video` 返回而不写文件。

### Q7【中】性能回归无基准测试，算法改动可能悄悄拖慢板端帧率

- 【证据路径】板端 6.9 FPS 是 `results/edge_board/EDGE_TEST_REPORT.md` 的一次性实测；仓库无任何耗时断言测试。图拉普拉斯 Jacobi 是 O(n³)（`graph_laplacian.h` 注释）。
- 【严重度】中。若引入更慢的特征分解/死循环，CI 不会发现。
- 【建议测试用例】新增 `tests/test_performance_benchmark.py`：对 `compute_laplacian_features` 在 n=32/64 随机点集，断言单次耗时 < 宽松上限（如 <100ms）并记录基线；C++ 侧在 `test_enhanced_decision` 加 `clock()` 计时断言（宽松阈值），捕获 O(n³) 退化。

### Q8【中】C++/Python 对 NaN/Inf 坐标语义不同，缺测试锁定差异

- 【证据路径】`scripts/generate_golden.py:54-56` 明确"C++ 过滤 NaN/Inf，Python 原型不过滤，语义有意不同，黄金回归只收录一致用例"；`graph_laplacian.py:58` 对 NaN 点不拒绝。
- 【严重度】中。Python 原型喂 NaN 会产生 NaN 特征，与板端行为不同，且无测试记录该差异，未来可能被误当作 bug 或悄悄"修齐"导致行为反转。
- 【建议测试用例】新增 `test_nan_semantics.py`：①Python `compute_laplacian_features([(nan,0),(0,0)],2)` 记录当前行为（返回含 NaN 特征）；②C++ `addPoint` 忽略 NaN → n=1（在 C++ 侧加断言）；③文档明确该差异为已知，决定是否统一。

### Q9【中】配置/阈值边界组合覆盖不足，`>=` 临界值未系统测试

- 【证据路径】`test_rule_engine_config_threshold_effect` 只把 `danger.n` 调高到 20；`test_risk_config.cc` 测了 `warning.n>danger.n` 抛异常。但 `n == DANGER_N`、`rho == DANGER_RHO`、`gcc == DANGER_GCC` 恰好达标的组合（`crowd_test.py:244` 的 `>=` 语义）无参数化覆盖。
- 【严重度】中。阈值边界抖动是监控误报/漏报的高发区。
- 【建议测试用例】
  1. Python 参数化：`n ∈ {DANGER_N-1, DANGER_N, DANGER_N+1}`、`rho ∈ {DRHO-ε, DRHO, DRHO+ε}`、`gcc` 同理，断言 `evaluate_level` 符合 `>=` 语义。
  2. C++ `test_risk_config.cc` 增加 `setDanger(10,0.55,0.8)` 恰好达标的 `CROWD_DANGER` 断言。

### Q10【中】结果统计/实验汇总缺 schema 校验与可移植复现

- 【证据路径】`scripts/ablation_study.py:13-14` 硬编码 `sys.path.insert(0, r"H:/PedestrianRiskDecision/scripts")` 与 `os.chdir(r"H:/PedestrianRiskDecision")`，换机器/CI 无法运行；`results/experiment_summary.json` 与各 `*_timeline.json` 无 schema 测试。
- 【严重度】中。脚本不可移植 = 论文实验无法复现；结果 JSON 结构变化不会被捕获。
- 【建议测试用例】
  1. 新增 `test_results_schema.py`：校验 `results/**/experiment_summary.json`、各 timeline 字段齐全（`frame/level/n/density/r_gcc/fiedler`）且 `level ∈ {SAFE,WARNING,DANGER}`、`frames>=0`。
  2. 将 `ablation_study.py` 的绝对路径改为基于 `PROJECT_ROOT` 的相对路径；新增 1 帧假视频的冒烟测试跑通 4 组合。

### Q11【低】黄金用例规模小且缺趋势/较大图覆盖

- 【证据路径】`tests/golden/graph_laplacian.json` 仅 6 个小图（最大 n=6）；`risk_fsm.json` 帧的 `trend` 全为 0；`ema_smoother.json` 仅 3 个用例。
- 【严重度】低-中。大矩阵 Jacobi 收敛、trend 偏置分支、EMA 时间戳倒退分支（`ema_smoother.py:207` `if dt<=0`）均未被黄金数据锁定。
- 【建议测试用例】扩充 golden：①n=20/50 完全图/随机图（验证 C++ Jacobi 大矩阵精度与收敛）；②`risk_fsm` 增加 `trend>0`/`trend<0` 黄金用例；③EMA 增加时间戳相同/倒退用例。

### Q12【低】C++ `test_enhanced_decision` 未覆盖单人 `r_gcc=0.0` 语义

- 【证据路径】git commit `4a4afe7` 修正"单人场景 r_gcc=0.0"并统一 C++/Python/脚本；但 `test_enhanced_decision.cc` 只用 4 人分散（`test_sparse_is_safe`）与 2 人（`test_edge_distance`），无单人断言。
- 【严重度】低。单人语义已在 Python golden（`single` 用例）覆盖，C++ 侧缺对应回归锚点。
- 【建议测试用例】在 `test_enhanced_decision.cc` 增加 `test_single_person_rgcc_zero`：喂 1 点，断言 `r.n==1 && r.r_gcc==0.0 && r.level==SAFE`。

---

## 四、可重复验证机制建设（落地清单汇总）

1. **CI 自动化**：GitHub Actions 双版本 Python + C++ `make test`（含 OpenCV），全仓一键回归（对应 Q1/Q4）。
2. **黄金数据独立参考**：`generate_golden.py` 内嵌 numpy/手算期望校验，杜绝"参考=被测"闭环（Q2）。
3. **覆盖率门禁**：`pytest-cov --cov-fail-under=85`，重点覆盖脚本层异常分支（Q5/Q6）。
4. **配置边界矩阵**：`evaluate_level`/`RiskEngine::evaluate` 的 `n/rho/gcc` 临界值参数化测试（Q9）。
5. **性能基准**：Python/C++ 双侧耗时断言，防止算法改慢（Q7）。
6. **结果 schema 校验**：`results/**` 与 timeline 结构自动校验；实验脚本改为相对路径可移植（Q10）。
7. **NaN/异常语义锁定**：明确 C++/Python 对 NaN 的差异并加测试（Q8）。
8. **消融一致性测试**：`ablation_study` 与真实规则引擎对齐并纳入回归（Q3）。
9. **C++ 单人/边界场景补测**：`r_gcc=0.0`、DANGER 恰好达标（Q12/Q9）。
10. **测试运行环境隔离**：文档化用 `H:/py311_gpu` 独立解释器 + 清空 PYTHONPATH 运行测试，避免 Hermes 捆绑 numpy(cp314) 污染（见第二节）。

---

## 五、无法验证项（如实记录）

- ❌ **C++ 测试本机未能复跑**：本机无 `g++`/`make`/`cmake`，`make test` 四项（26/32/58/104）通过结果引用自 `BASELINE_RESULTS.md`（老电脑运行）。**建议在老电脑或 CI 上复跑并留存输出**。
- ⚠️ **消融/结果统计脚本未在本会话运行**：`ablation_study.py`/`rerun_all_videos.py` 需要真实 YOLO 权重与 6 个视频数据集，且硬编码绝对路径，本机不可移植运行。
- ⚠️ 部分边缘行为（NaN、边界阈值、性能耗时）当前无测试，属于"未验证"而非"已验证"。

---

## 六、总结

现有测试体系**质量较高**：137 项 Python（本会话实测全过）、4 个 C++ 测试程序、C++/Python 共享 golden 数据、端到端集成测试用假检测器跑真实脚本，都是很好的基础。但存在三处结构性缺口：

1. **无 CI**——所有回归依赖手动，算法改动无法自动防回归；
2. **黄金数据缺独立参考**——只能防 C++ 漂移，防不了 Python 自身语义错；
3. **消融/实验/结果统计脚本无测试且存在真实引擎不一致**——直接影响论文定量结论的可复现性。

建议按"CI 先行（Q1）→ 黄金独立参考（Q2）→ 消融一致性（Q3）→ 覆盖率与边界/异常/性能补测（Q5-Q10）"的顺序落地，即可把当前"测试完备但靠手动"升级为"可持续自动防回归"。
