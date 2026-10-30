# Agent6 QA/测试专家审查报告

> 分支：`review-opt-20261029` ｜ 方式：只读审查（未修改任何源码/配置/测试文件，仅新增本报告）｜ 日期：2026-10-29
> 审查范围：`tests/` 全部 17 个 pytest 文件、`src/luckfox_rtsp_yolov5/tests/` 全部 11 个 C++ 主机测试、`src/luckfox_rtsp_yolov5/Makefile`、`src/luckfox_rtsp_yolov5/CMakeLists.txt`、`.github/workflows/ci.yml`、以及 P1 风险点对应的生产代码（`main.cc`、`yolov5.cc`、`sysfs_spi.cc`、`ili9341.h`、`xpt2046.h`、`crowd_dbscan.{h,py}`、`risk_fsm.py`）。

---

## 一、测试体系现状

### 1.1 实测结果（本机复跑，非仅读代码）

| 项目 | 结果 |
|---|---|
| `pytest tests/ -q`（Windows 主机） | **187 passed, 4 skipped**（5.86s） |
| C++ 主机测试（10 个 Windows 可跑目标） | **912/912 断言全部通过** |
| `test_sysfs_spi` | Linux-only（CI ubuntu 自动纳入），40 项 |

- 4 个 skip 是 `test_go2rtc_demo_config.py` 的 shell 脚本用例（Windows MSYS 跳过，Linux CI 会执行）。
- CI 上预期 pytest 约 190 passed + 1 skipped（`test_m0_replay_consistency` 依赖 gitignore 的本地 timeline，CI 无数据会 skip）。

### 1.2 pytest 用例统计（191 项）

| 测试文件 | 用例数 | 覆盖对象 |
|---|---:|---|
| test_crowd_dbscan.py | 32 | DBSCAN 标签/特征、参考实现对照、C++ 算法逻辑复刻 |
| test_ipm_ema.py | 22 | IPM 地面映射 + EMA 平滑 |
| test_bytetrack_tracker.py | 20 | ByteTrack 跟踪器 |
| test_graph_laplacian.py | 19 | 谱特征（n/density/r_gcc/fiedler） |
| test_go2rtc_demo_config.py | 18 | go2rtc demo 流 YAML 编辑（含 4 项 shell 测试，Linux CI 执行） |
| test_risk_fsm.py | 15 | FSM 状态机/滞回/保持/公式/配置校验 |
| test_kalman_tracker.py | 15 | Kalman 平滑 |
| test_integration_pipeline.py | 12 | 规则引擎+FSM 端到端场景 |
| test_spectral_fsm_experiment.py | 8 | 谱修正实验回归 |
| test_gt_eval.py | 8 | GT 检出率/MAE/RMSE/分桶/列联表 |
| test_tiny_gt_analysis.py | 6 | 微小目标 GT 分析 |
| test_scenario_discrimination.py | 5 | 场景判别（n=8/10/12） |
| test_golden_regression.py | 3 | 黄金数据独立性 |
| test_ablation_consistency.py | 3 | 消融脚本与 crowd_test 一致性 |
| test_comparison_consistency.py | 2 | 对比实验一致性 |
| test_baseline_consistency.py | 2 | 基线脚本一致性 |
| test_golden_independent.py | 1 | 黄金数据生成独立于实现 |
| **合计** | **191** | |

### 1.3 C++ 主机测试统计（952 项 = 主机 912 + Linux 40）

| 测试目标 | 断言数 | 覆盖 |
|---|---:|---|
| test_enhanced_decision | 26 | Kalman+FSM 确定性输出 |
| test_minijson | 32 | JSON 解析（黄金/容错） |
| test_risk_config | 58 | 配置读取/非法值抛异常/回退 |
| test_golden_regression | 104 | 黄金回归 graph_laplacian(6)+ema_smoother(3)+risk_fsm(3) |
| test_preprocess | 174 | NV12→BGR/RGB letterbox 黄金对照（NEON vs 标量，仅 ARM 下启用） |
| test_hardware_modules | 118 | LED/按键/舵机/云台/语音告警 |
| test_selftest | 80 | 开机自检 PASS/FAIL 判定与容错 |
| test_lcd_menu | 99 | 触摸屏菜单状态机 |
| test_ili9341 | 123 | ILI9341 SPI 显示驱动 |
| test_xpt2046 | 98 | XPT2046 SPI 触摸驱动 |
| test_sysfs_spi（Linux only） | 40 | spidev 生命周期/失败路径/DC/RESET/背光/PENIRQ |
| **合计** | **952** | |

### 1.4 CI 流水线阶段（`.github/workflows/ci.yml`，仅 38 行）

| Job | 运行环境 | 阶段 | 覆盖 |
|---|---|---|---|
| `python-tests` | ubuntu-latest | checkout → setup-python 3.11 → pip numpy/pytest/opencv-headless → `pytest tests/ -q` | 191 项 pytest |
| `cpp-tests` | ubuntu-latest | checkout → apt g++/make → `cd src/luckfox_rtsp_yolov5 && make test` | 952 项 C++ 断言（含 sysfs-spi） |

- 触发：`push: [main, review-opt]`、`pull_request`。**无** `workflow_dispatch`、`schedule`、路径过滤、并发取消、覆盖率采集、产物上传。
- **CI 没有任何交叉编译 job，没有任何板端/硬件 job。**

---

## 二、P1 风险点：现有测试能否捕获？

### 2.1 RKNN 返回值被忽略 + od_results 未初始化 —— **无法捕获（高风险盲区）**

- 生产缺陷：`src/luckfox_rtsp_yolov5/src/main.cc:382` 直接调用 `inference_yolov5_model(&rknn_app_ctx, &od_results)` 且**忽略返回值**；`:151` 的 `object_detect_result_list od_results;` 未置零初始化；`yolov5.cc:199-204` 在 `rknn_run` 失败时返回 -1 且**不写 `od_results.count`**。
- 影响：NPU 推理失败时 `od_results.count` 为垃圾值，`:397` 决策循环与 `:446` 画框循环遍历未初始化内存 → 未定义行为（崩溃/垃圾框）。看门狗（`:342-343`）只监控 VI 取帧失败，对 NPU 失败**无感知**。
- 现有测试：`main.cc` 依赖 Luckfox SDK/RKMPI，主机测试不编译；`test_enhanced_decision`/`test_golden_regression` 只测纯算法头文件。**没有任何测试能捕获该缺陷**。
- 缺什么：① 把 RKNN 推理封装成可注入接口（mock 返回 -1）后增加失败注入单测；② 板端故障注入冒烟（删模型文件/NPU 挂死）验证看门狗计数。

### 2.2 SPI CS 事务拆分 —— **无法捕获（FakeSpi 无 CS 语义）**

- 生产缺陷：`xpt2046.h:126-133`（`readRaw`）与 `ili9341.h:290-303`（`readId`）把“命令字节”和“数据读取”拆成两次独立 `transfer()`；`sysfs_spi.cc:59-73` 每次只发一个 `SPI_IOC_MESSAGE(1)` → **CS 在命令后释放、再拉低读数据**。XPT2046 在 CS 上升沿中止转换，ILI9341 RAMRD 要求 CS 连续保持 → 真机触摸/读 ID 失效。
- 现有测试：`test_ili9341.cc` 的 `FakeSpi`（`:46-65`）只做“写缓冲+读回放”，**不建模 CS 保持/事务边界**；`test_sysfs_spi.cc` 只测 ioctl 失败路径。121+98+40 项硬件测试全绿也无法暴露。
- 缺什么：① `SpiBus` 增加多段事务接口（`transaction(const SpiSegment*, size_t)`），`SysfsSpiBus` 用 `SPI_IOC_MESSAGE(N)` 一次完成；② `FakeSpi` 补充 CS 状态机语义（命令段后 CS 必须保持）；③ 新增“命令+数据同事务 CS 不释放”的断言用例；④ 真机回环冒烟。

### 2.3 crowd_dbscan 标签反转 —— **无法捕获（复刻测试用 Python 常量）**

- 生产缺陷：`src/algorithms/crowd_dbscan.h:107-108` C++ 端 `NOISE=-1, UNCLASSIFIED=-2`；`src/algorithms/crowd_dbscan.py:34-35` Python 端 `UNCLASSIFIED=-1, NOISE=-2`。同一噪声点在 C++ 为 -1、Python 为 -2，跨语言交换标签（JSON/日志/后续特征）会语义错乱。
- 现有测试：`tests/test_crowd_dbscan.py:365-440` `TestCppAlgorithmLogic` 在 Python 里用 `UNCLASSIFIED, NOISE = -1, -2`（Python 常量）复刻 C++ 算法，断言 `py_labels == cpp_labels` —— **因为两端用同一套常量，必然相等，永远无法发现真实 C++ 头文件的标签反转**。且 `crowd_dbscan.h` 未被 Makefile/CMake 任何测试目标引用（从未被编译测试）。
- 缺什么：① 新增 C++ 主机测试**直接编译 `crowd_dbscan.h`**，与 Python `dbscan_labels` 输出逐点比较（要求噪声标签跨语言一致）；② 或先统一双端 NOISE 语义，再把 DBSCAN 场景加入黄金数据。

### 2.4 评分校准倒挂 —— **无法捕获（双端一致，黄金数据不含该场景）**

- 生产缺陷：两人相距 0.5m `[(0,0),(0.5,0)]`：n=2, density=1.0, r_gcc=1.0, fiedler=2.0 → **score=0.7067 ≥ up_danger=0.65 → 3 帧后 DANGER**；三人链 `[(0,0),(1.5,0),(3,0)]`：n=3, density=0.667, r_gcc=1.0, fiedler=1.0 → **score=0.61 → 仅 WARNING**。与规则引擎“DANGER 需 n≥10”（`risk_config.h:50`）语义冲突。
- 现有测试：`test_risk_fsm.py::test_score_formula` 只验证公式数值本身；`test_scenario_discrimination.py` 只覆盖 n=8/10/12；`test_integration_pipeline.py` 只有 cluster5(5人)/dense12/chain11；`test_golden_regression` 的 risk_fsm 黄金仅 3 个场景（无该边界）。**没有任何测试断言两人场景的等级或 score 单调性**。
- 缺什么：① 新增评分校准回归测试：断言 n=2 紧邻的等级/score（按裁决语义）；② 断言“三人链 score > 两人紧邻 score”的单调性（或 `n<3 不得 DANGER` 门控）；③ 将该场景加入 C++/Python 黄金数据，防双端同时漂移。

### 2.5 核对结论

| P1 风险点 | 现有测试能捕获？ | 缺什么测试 |
|---|---|---|
| RKNN 返回值未检查 + od_results 未初始化 | ❌ 否 | 可注入推理接口 + rknn_run 失败注入单测；板端故障冒烟 |
| SPI CS 事务拆分 | ❌ 否 | 多段事务接口 + FakeSpi CS 语义 + 事务保持断言 |
| crowd_dbscan 标签反转（-1 vs -2） | ❌ 否（复刻测试必然相等） | 真编译 crowd_dbscan.h 的跨语言一致性测试 |
| 评分校准倒挂（两人 DANGER vs 三人链 WARNING） | ❌ 否（双端一致） | 校准回归测试 + 单调性断言 + 黄金场景 |

**四个 P1 全部无测试防线，且现有测试全绿正是问题所在（测试与缺陷使用同一套假设/常量）。**

---

## 三、测试盲区清单

### P0（无：未发现崩溃级但完全无防线的缺陷）

| # | 盲区 | 缺什么测试 | 建议用例 |
|---|---|---|---|
| P0-1 | RKNN 返回链未定义行为（见 2.1）——性质上接近 P0，但因不可达不列 P0，列 P1 最高优先级 | 失败注入单测 | `mock_inference_returns(-1)` 后断言主循环跳过本帧、不访问 `od_results`、看门狗计数递增 |

### P1（生产主链路缺陷且无测试防线）

| # | 盲区 | 缺什么测试 | 建议用例 |
|---|---|---|---|
| P1-1 | RKNN 返回值（同 P0-1） | 见 P0-1 | — |
| P1-2 | SPI CS 事务语义 | 多段事务接口 + CS 状态机 Fake | `readRaw`/`readId` 命令+数据必须同一事务；`transaction` 内 CS 不释放；跨段失败回滚 |
| P1-3 | 评分校准倒挂 | 校准回归 | 两人 0.5m 等级断言；三人链 > 两人 score 单调性；边界进黄金 |
| P1-4 | crowd_dbscan 标签反转 | 跨语言一致性 | 编译 `crowd_dbscan.h`，噪声标签必须等于 Python `NOISE`；双端 labels 逐点相等 |
| P1-5 | 主循环异常路径（初始化失败/看门狗只监 VI） | 可注入故障的主循环单测 | RKNN init 失败、ISP init 失败、VENC SendFrame 超时、连续 30 帧取帧失败退出 |

### P2（构建/校验/硬件驱动次级盲区）

| # | 盲区 | 缺什么测试 | 建议用例 |
|---|---|---|---|
| P2-1 | `scripts/crowd_test.py:224` `load_config` **零校验**（裸 `json.load`）：`warning.n > danger.n`、负阈值等非法配置被静默接受；而 C++ `loadRiskConfig` 会抛异常——双端行为不一致且无测试 | Python 配置校验 + 双端一致性 | 同一份非法 JSON 分别喂 Python/C++，断言都抛异常；`danger.n < warning.n` 抛 ValueError |
| P2-2 | CMake 测试覆盖远小于 Makefile：`CMakeLists.txt` 仅 5 个测试 target，缺 preprocess/selftest/lcd-menu/ili9341/xpt2046/sysfs-spi；README 宣称“等价 CMake 方式”不成立 | CI 增加 `cmake -S . -B build && ctest` job + 补齐 target | ctest 必须达到 11 个 target / 952 断言 |
| P2-3 | `test_risk_config` 的 `test_risk_engine_config`/`test_load_homography_file` 依赖 OpenCV，CI 只装 g++/make 不装 OpenCV → **这两个用例在 CI 上恒被跳过** | CI 安装 libopencv-dev 或改为无 OpenCV 也可测 | 让 `RiskEngine::evaluate()` 使用运行时配置的验证在 CI 真正执行 |
| P2-4 | 硬件驱动异常路径：`selftest.h:266-274` 忽略 `m_gpio->write()` 返回值（GPIO 写失败自检仍可能 PASS）；`selftest_main.cc:47-54` `atoi` 无校验 | 故障注入 | FakeGpio 写失败时自检必须 FAIL；`argv` 非数字报错 |
| P2-5 | 黄金数据覆盖窄：仅 graph_laplacian(6)/ema(3)/risk_fsm(3)/preprocess(174)，无 DBSCAN/Kalman/Bytetrack/IPM 跨语言黄金 | 扩展 golden 生成器 | 新增 dbscan、kalman、bytetrack 黄金用例，C++ 与 Python 对照 |
| P2-6 | NEON 路径无 CI 编译：`test_preprocess` 的 NEON 对照仅 `__ARM_NEON` 下启用，CI 无 ARM 环境 | qemu-user 或 ARM runner | `qemu-arm` 下编译运行 NEON vs 标量一致性比对 |
| P2-7 | 无覆盖率统计，盲区只能靠人工推断 | CI 接入 coverage.py | `--cov=src/scripts` 并设门禁（如 ≥80%） |

---

## 四、CI 门禁评估

### 4.1 现状结论

- **不做板端构建（交叉编译）**：CI 只 `make test` 编译 x86_64 主机测试；`BUILD_LUCKFOX_DEMO=ON` 的交叉编译路径（CMakeLists:96-122、Makefile `luckfox` 目标）在 CI 中**完全没有被执行**。
- **不做板端测试**：无自托管 runner、无硬件接入、无真机 job；`sysfs-spi` 真机验证仅存在于 README 手工记录。
- **门禁缺口**：
  1. **push 分支不含 `review-opt-20261029`**（ci.yml:5 仅 `[main, review-opt]`）——当前审查分支直接 push 不触发 CI，只有开 PR 才触发；
  2. CMake 构建路径完全无 CI 保障（一旦 README 宣称的 CMake 方式回归，无人知晓）；
  3. 无 `workflow_dispatch`，板端/交叉编译 job 想手动触发也没有入口；
  4. 无覆盖率门禁、无产物上传（测试日志/黄金数据 diff 不存档）。

### 4.2 门禁建议

| 优先级 | 建议 |
|---|---|
| P0 | PR 合入 `main`/`review-opt` 必须 python-tests + cpp-tests 全绿（现有） |
| P1 | 修复 RKNN 返回码 + SPI 多段事务时，**必须配套失败注入单测**，否则不允许合入（新增 review 检查项） |
| P1 | push 分支增加 `review-opt-*`，或强制审查分支走 PR 流程 |
| P2 | CI 增加第三个 job：`cmake -S . -B build && cmake --build build && ctest`，确保 CMake 路径与 Makefile 同等覆盖 |
| P2 | CI 的 cpp-tests job 安装 `libopencv-dev`，解除 `test_risk_engine_config` 恒跳过 |
| P2 | 新增 `workflow_dispatch` 手动触发的交叉编译 job（自托管 runner + Luckfox SDK 缓存），至少保证交叉编译可重复构建 |
| P2 | 板端冒烟 job（手动触发）：`/dev/spidev0.0` 回环、`selftest`、RKNN 推理冒烟、结果回传 artifact |

---

## 五、最高价值问题清单（8 条）

1. **【RKNN】主循环忽略 `inference_yolov5_model` 返回值且 `od_results` 未初始化（main.cc:151,382 + yolov5.cc:202），是未定义行为。** 是否接受把 RKNN 推理封装为可注入接口，并新增 rknn_run 失败注入单测 + 看门狗计数断言，作为 P1 合入门禁？
2. **【SPI】命令/数据拆两次 transfer 与 spidev CS 行为冲突（xpt2046.h:126-133、ili9341.h:290-303、sysfs_spi.cc:59-73），FakeSpi 无 CS 语义，121+98+40 项硬件测试全绿也无法发现。** 是否按 agent3 建议增加多段事务接口并在 FakeSpi 中建模 CS 保持？
3. **【校准】两人紧邻 score=0.7067 判 DANGER、三人链 0.61 仅 WARNING，双端一致，黄金/一致性测试不可能捕获。** 是否把“n<3 不得 DANGER”或 score 单调性断言加入 Python/C++ 回归测试与黄金数据？
4. **【标签】crowd_dbscan C++ NOISE=-1 vs Python NOISE=-2；现有 TestCppAlgorithmLogic 用 Python 常量复刻 C++ 算法，必然相等，无法发现反转，且 C++ 头文件从未被编译。** 是否新增真编译 crowd_dbscan.h 的跨语言一致性测试？
5. **【配置】Python `load_config` 零校验（crowd_test.py:224），`warning.n > danger.n` 被静默接受；C++ `loadRiskConfig` 会抛异常。** 是否统一双端校验规则并加“同一非法配置双端都抛异常”的一致性用例？
6. **【构建】CMakeLists 仅 5 个测试 target（缺 preprocess 等 6 个），CI 也不跑 ctest，README“等价 CMake 方式”的覆盖承诺不成立。** 是否补齐 target 并在 CI 增加 ctest job？
7. **【CI 触发】push 分支只含 [main, review-opt]，当前 `review-opt-20261029` 分支 push 不触发 CI。** 是否加入 `review-opt-*` 或强制 PR 门禁？
8. **【板端】CI 无交叉编译、无板端测试、无 workflow_dispatch。** 是否规划手动触发的交叉编译 + 真机冒烟 job（自托管 runner + SDK 缓存）？