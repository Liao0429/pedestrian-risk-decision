# round3 算法审计报告 —— Python / 数值一致性（algo-py, task-2）

日期：2026-10-30　执行：teammate \`algo-py\`　写 scope：
\`src/algorithms/*.py\`、\`tests/**\`（不含 \`src/luckfox_rtsp_yolov5/tests/\`）、
\`config/default_config.json\`、\`scripts/*.py\`。

本报告中**每条结论都标注证据**：要么给出可复现命令 + 实测输出，要么明确标"未验证/推断"。
旧报告的"已修复"标记一律重新在磁盘代码上验证。

---

## 0. 一句话结论

1. 报表基线 2 个失败都不是"算法精度"问题：一个是**过期测试断言**（conf_threshold 0.60 vs 0.35），
   一个是**旧 timeline 数据早于 min_danger_n 门控**导致的 State 漂移。两者都已按证据修正，
   且改成了比原来**更精确**的断言（不是放水）。
2. 真 bug 共 6 处（5 处在 Python 侧已修并有回归测试；1 处属 C++ 侧待 T1/board-perf 修）。
3. 发现 **C++ 侧 3 个从未被任何测试覆盖**的缺陷：\`bytetrack_tracker.h\` **根本编译不过**、
   \`graph_laplacian.h\` 脚点去重语义与 Python 不同、\`crowd_dbscan.h\` 并列簇顺序不可复现。
   根因是仓库里"用 Python 复刻 C++"的等价性测试**复刻错了**（噪声标签写成 -2，与真实头文件 -1 不一致），
   本轮补上了**真实编译**的 parity 测试来堵这个洞。
4. 111 条聚合重算发现 \`results/rerun/_aggregate.json\` **无法从当前 timeline 复现**
   （206 处差异），README:79 三个数字相加 ≠ 总帧数。真源见 \`_aggregate_recomputed.json\`。
5. cProfile 定位到纯 Python 去重循环是离线全量路径最大热点，向量化后同负载 **1.46s → 0.96s（-34%）**。

---

## 1. 测试基线（原文）

### 1.1 修改前：\`python -m pytest tests/ -q\`

\`\`\`text
........................................................................ [ 26%]
.ssss................................F.................................. [ 53%]
........................................................................ [ 80%]
............................................F.........                   [100%]
FAILED tests/test_hardware_config_consistency.py::TestConfThreshold::test_default_config_file_loads
FAILED tests/test_spectral_fsm_experiment.py::test_m0_replay_consistency - As...
2 failed, 264 passed, 4 skipped in 9.54s
\`\`\`

关键失败原文：

\`\`\`text
>       assert cfg["conf_threshold"] == pytest.approx(0.60)
E       assert 0.35 == 0.6 �� 6.0e-07
...
>       assert rate >= 0.99, f"M0 ����һ���Ե��� 99%: {rate:.4%}"
E       AssertionError: M0 重算一致性低于 99%: 98.3186%
\`\`\`

### 1.2 修改后：\`python -m pytest tests/ -q\`（两种编码环境都跑过）

\`\`\`text
........................................................................ [ 22%]
x...............................................ssss.................... [ 45%]
........................................................................ [ 68%]
........................................................................ [ 90%]
.............................                                            [100%]
312 passed, 4 skipped, 1 xfailed in 11.65s
\`\`\`

（另一环境 \`PYTHONIOENCODING\` 未设置：\`312 passed, 4 skipped, 1 xfailed in 11.62s\`）

\`x\` = 严格 xfail（strict=True）：\`bytetrack_tracker.h\` 无法独立编译，属已知缺陷（归 board-perf）。
头文件修好后该用例会 XPASS → strict 模式下自动失败，**强制**删除标记，防止"缺陷已修但清单还写着坏"。

### 1.3 \`cd src/luckfox_rtsp_yolov5; mingw32-make test\`（原文摘录，EXIT=0）

\`\`\`text
40/40 tests passed
32/32 tests passed
58/58 tests passed
104/104 tests passed
结果: 174 通过, 0 失败
=== test_hardware_modules === passed=118 failed=0
=== test_selftest === passed=80 failed=0
===== lcd_menu 测试结果: 99 passed, 0 failed =====
test_ili9341: 123 passed, 0 failed
test_xpt2046: 98 passed, 0 failed
EXIT=0
\`\`\`

---

## 2. Bug 表

| # | 文件:行 | 现象 | 根因 | 修复 | 验证方式 |
|---|---------|------|------|------|----------|
| P1 | \`src/algorithms/crowd_dbscan.py:34-38\` | Python 噪声标签 = **-2**，板端 \`crowd_dbscan.h:107\` = **-1**；下游按 -1 判噪声会全部漏判 | 两侧常量各自定义，且测试里"Python 复刻 C++"的副本也写 -2，掩盖了分歧 | \`NOISE=-1, UNCLASSIFIED=-2\`（\`dbscan_crowd_features\` 文档本来就写"-1 表示噪声"） | \`tests/test_algo_py_round3.py::TestDbscanNoiseLabel\`（含直接读 .h 比对常量）+ parity 用例 \`db_noise/db_minpts3\` 现在与真实 C++ 一致 |
| P2 | \`src/algorithms/crowd_dbscan.py:166-168\` | 簇大小并列时 \`cluster_centers\` 顺序不确定（可视化/下游漂移） | \`np.argsort(sizes)[::-1]\`：argsort 默认非稳定排序，再整体反转 | 改为确定序 \`sorted(range(n), key=lambda c: (-sizes[c], c))\`（size 降序、并列按簇编号升序） | \`TestDbscanDeterministicOrder\`（3 例）；parity 用例 \`db_many_tied_clusters\` 证明 C++ 侧仍不稳定 |
| P3 | \`src/algorithms/ipm_calibrate.py:124-133\` | 4 个**像素**标定点共线时 \`cv2.getPerspectiveTransform\` 不报错，返回近零退化 H → 所有脚点映射到 (0,0) → 去重后只剩 1 人 → **永远不报警**，且现场极难排查 | 只校验了 ground 点共线，漏了 pixel 点；也没有校验返回矩阵是否退化 | 新增 pixel 共线校验 + \`_check_nondegenerate_homography()\`（NaN/Inf、全零矩阵）；RANSAC 分支同样调用 | \`TestIpmCalibrationValidation\`（3 例，含"正常标定仍能往返"）；实测修复前返回 \`~1e-31\` 量级的 H、映射结果全为 \`[0,0]\` |
| P4 | \`src/algorithms/risk_fsm.py:257-262\`（C++ \`risk_fsm.h:175-178\` 同） | 注释/commit 声称"n 掉到门槛以下**立即退出** DANGER，不受 hold 保护"，实测仍需 \`holdElapsed()\` + \`confirm_down\` 才降级 | 注释与代码不符（代码是对的，注释是错的） | 更正注释；用测试把真实行为钉死 | \`TestMinDangerNGate::test_hold_path_needs_hold_and_confirm_down\`（精确断言 \`DANGER*4 + WARNING*3 + SAFE*2\`） |
| P5 | \`tests/test_hardware_config_consistency.py:51,178\` | 断言 \`config/default_config.json\` 的 conf_threshold = 0.60，磁盘是 0.35 → 基线红 | **断言过期**：0.60 来自后被 revert 的"按键可调"特性（e1feef8→a920704），fdece0d 有意定为 0.35；且该键**当前运行时无人消费** | 常量与断言改 0.35，并写明语义："JSON 声明值，板端运行时消费（2026-10-30 起接入 risk_config.h）；离线原型默认 0.25" | \`tests/test_hardware_config_consistency.py\` 全绿；全仓库 grep 证明只有该测试引用这个键 |
| P6 | \`tests/test_spectral_fsm_experiment.py:96-117\` | \`test_m0_replay_consistency\` 只有 98.3186% → 基线红 | **数据陈旧**：111 条 timeline 是旧板端构建产物（早于 P0-7 min_danger_n 门控），其中 1203/45021 帧是"stored=DANGER 但 n<3"，当前配置下不可能出现 | 断言改为三条更精确的契约（见 §3） | 实测 111/111 视频全部满足 |
| P7 | \`src/algorithms/graph_laplacian.py:153-167\` | 去重循环是离线全量路径最大热点（30 次 n=120 调用耗时 0.36s tottime） | 每点对都调 \`np.hypot\` 的纯 Python 双重循环 | 平方距离向量化（语义等价 \`d<tol ⟺ d²<tol²\`）；新增 \`tests/test_algo_py_round3.py::TestFootPointDedup\` 钉住语义 | 见 §6 性能；3 个去重语义用例 + 全量测试全绿 |
| P8 | \`src/algorithms/kalman_tracker.py:33,118,155\` | \`correct()\` 每帧重复分配 \`np.eye(4)\`、重复转置、调 \`np.linalg.inv\` | 原型实现未做常量缓存 | 缓存 \`_I4/_Ht\`；2x2 显式求逆 \`_inverse_2x2\`（与 \`kalman_filter.h\` 的 \`mat22Inverse\` 同策略：奇异时返回零矩阵→本帧不修正，而不是抛 LinAlgError 打断整帧） | parity 用例 \`kal_basic/kal_dt01\` 对 16 个 P 元素逐值比对（rel 1e-6），证明优化未改数值 |
| P9 | \`tests/test_go2rtc_demo_config.py:25,202-218\` | 只要调用方设了 \`PYTHONIOENCODING=utf-8\`（CI/常见配置）该用例就因 GBK 解码子进程输出而 \`UnicodeDecodeError\` 变红 | \`subprocess.run(capture_output=True, text=True)\` 未指定编码 → 用 locale 解码，与子进程输出编码不一致 | 新增 \`_run()\` 包装：两侧都固定 UTF-8（\`encoding="utf-8"\` + 子进程 \`PYTHONIOENCODING=utf-8\`） | 实测：设 \`PYTHONIOENCODING=utf-8\` 时修复前 1 failed，修复后 14 passed, 4 skipped |

---

## 3. 两个基线失败的根因与修法（详细证据）

### 3.1 conf_threshold：**测试断言过期**（裁决已定：板端 0.35）

证据（全仓库 grep，非推断）：

- 唯一解析 JSON 的板端代码是 \`src/luckfox_rtsp_yolov5/include/risk_config.h\`，其中
  **没有 conf_threshold 解析分支**（只有 \`class_conf_thresholds\`、\`edge_dist_m\`、fsm 等）。
- Python 侧 \`scripts/crowd_test.py\` / \`rerun_all_videos.py\` / \`ablation_study.py\` 也都不读该键。
- git 沿革：\`e1feef8\`（0.60 可配置）→ \`a920704\` revert → \`fdece0d\` 定为 0.35（提交信息"平衡灵敏度与误检"）。
- 因此"0.60"只是测试自己维护的常量，与 config 真值冲突；**config 的 0.35 是正确的**。
- 附带风险：\`results/edge_board/default_config.json\`（部署快照）仍是 0.6 + 旧注释，
  且仓库内**找不到 \`BOX_THRESH\` / \`NMS_THRESH\` 的定义**（只有 \`yolov5.cc:196-197\` 的引用）——
  已请 board-perf 核实它们来自 SDK 还是编译命令行。

处置：\`tests/test_hardware_config_consistency.py\` 常量与断言改 0.35 + 语义注释；
\`config/default_config.json\` 的 \`_comment\` 与 \`scripts/crowd_test.py:419-423\` 补注释
（板端 0.35 / 离线原型 0.25，两者刻意不同）。

### 3.2 M0 replay 一致性：**旧数据 + 过期阈值断言**

数据前提：111 条 timeline 由旧板端构建生成，早于 P0-7 \`min_danger_n\` 门控（66b4ef0）。

实测（复现命令见 §7-b）：

| 指标 | 实测 |
|------|------|
| 全程逐帧一致率（旧断言的 99% 口径） | **98.3186%**（44264/45021） |
| "stored=DANGER 且 n<3" 的帧（当前配置不可能出现） | **1203 / 45021 = 2.67%** |
| **首个门控前帧之前的"纯净前缀"逐帧一致率** | **0/38751 不一致 = 100%** |
| 纯净前缀覆盖率 | 86.07% |
| 剔除门控前帧后整体一致率 | **99.4705%** |

结论：分叉点**恰好始于**第一条"旧数据与自己门控矛盾"的帧，之后是 State 漂移的连锁效应；
这不是当前算法的不一致。旧断言 ≥99%（全程）对这份数据本身就不成立 —— 属**断言过期**，
不是"放水"：新断言加了"前缀必须逐帧完全一致（=0）"与"覆盖率 ≥85%"两个更难的约束。

---

## 4. 历史 4 项 P0 复验（以磁盘代码为准）

| 项 | 结论 | 证据 |
|----|------|------|
| (a) NMS \`classIds\` 下标 | **已确认修复** | \`postprocess.cc:118\` 用 \`classIds[order[i]]\`；\`:125-126\` 用 \`int m = order[j]; if (m == -1 \|\| classIds[m] != filterId)\` —— 两处都走间接索引。（T1 scope，我只报告） |
| (b) 脚点 2cm 容差去重 | **Python 侧语义正确并有测试**；**C++ 侧实现与 Python 不同** | Python：\`TestFootPointDedup\`（链式 0/1.5/3cm→2 点；1cm 内 3 点→1 点；恰好 2cm→不合并）。C++：parity 实测 \`lap_chain_dedup_3cm\` C++ n=1 vs Python n=2（见 §5-D1） |
| (c) \`min_danger_n=3\` 门控 | **两端都已实现且一致**；**注释与代码不符（P4）** | \`TestMinDangerNGate\`（2 人即使 score>0.65 也只能 WARNING；3 人可 DANGER）；parity 用例 \`fsm_min_n_gate\` C++/Python levels+scores 完全一致 |
| (d) Jacobi 循环收敛 | **已确认修复且数值正确** | 真实编译 \`graph_laplacian.h\` 与 \`np.linalg.eigvalsh\` 对 n=4/5/10/12/20/30 的点集逐值比较：\`fiedler\`/\`spectral_radius\`/\`eig_entropy\` 全部在 rel 1e-6 内一致，\`converged=1\`（\`lap_p5_chain\`、\`lap_k4\`、\`lap_n10\`、\`lap_rand30\`、\`lap_grid20\` 等用例） |

---

## 5. C++ / Python 不一致清单（交 T1 / board-perf）

编号 D1-D5，全部由**真实编译**的 parity 测试实测得到，并登记在
\`tests/test_cpp_python_parity.py::KNOWN_DIVERGENCES\`（双向契约：新分歧会让用例红；
分歧消失也会让用例红并提示删除条目）。

### D1 \`graph_laplacian.h:232-243\` 脚点去重语义不同（**建议修**）

- 现象：\`(0,0),(0.015,0),(0.030,0)\`，edge_dist=2.0 → Python \`n=2\`，C++ \`n=1\`。
- 根因：C++ 内层 \`for (size_t j = 0; j < i; ++j)\` 比较**全部历史点**（含已被丢弃的重复点），
  Python 只与**已保留点**比较。C++ 因此做成了单链传递合并，会多吞点（低估人数 → 低估风险）。
- 修法：把内层改成遍历 \`dedupPts\`（已保留点），与 doc 语义"\u540c\u4e00\u4f4d\u7f6e(误差<2cm)\u89c6\u4e3a\u91cd\u590d\u68c0\u6d4b\uff0c\u53ea\u4fdd\u7559\u4e00\u4e2a"一致。
- **本次已用临时补丁副本验证过**：只改这一处不影响其它数值（parity 其余 LAP 用例全绿）。

### D2 \`graph_laplacian.h\` 用 float 存点、double 比较，2cm 边界行为不同（建议加注释或统一类型）

- 现象：\`(0,0),(0.02,0),(0.041,0)\` → Python \`n=3\`，C++ \`n=2\`。
- 根因：\`addPoint(float x, float y)\` → \`0.02f = 0.0199999996 < 0.02\` 被判重复；
  Python float64 下 0.02 恰好不满足严格小于。
- 影响：只在"恰好落在容差边界"时出现，属边界抖动；建议**注释说明**或统一为 double 存储。

### D3 \`crowd_dbscan.h:194-195\` 并列簇顺序不可复现（**建议修**）

- 现象：20 个大小并列（每簇 2 人）的簇，C++ 输出顺序为
  \`50.25, 95.25, 90.25, 85.25, ...\`（\`std::sort\` 非稳定分区），Python 为簇编号升序 \`0.25, 5.25, ...\`。
- 根因：\`std::sort(order, [&sizes](a,b){return sizes[a]>sizes[b];})\` 只比较 size；
  元素 >16 时走内省排序分区路径，等价元素相对顺序由实现决定（<16 时等价插入排序，故 2 簇的小用例是一致的）。
- 修法：比较器补 tie-break：\`if (sizes[a] != sizes[b]) return sizes[a] > sizes[b]; return a < b;\`
  （\`cluster_centers\` 顺序随 STL/编译器漂移会让图表与下游取值不可复现）。

### D4 \`bytetrack_tracker.h:206,652\` **头文件编译不过，从未被编译**（P0，归 board-perf）

\`\`\`text
src/algorithms/bytetrack_tracker.h:652:73: error: invalid initialization of reference of type
  'const std::array<double, 4>&' from expression of type 'std::array<double, 8>'
  652 |  bt_detail::Box box() const { return bt_detail::xyahToBox(kf.mean()); }
\`\`\`

- 根因：\`xyahToBox(const std::array<double,4>&)\` 的唯一调用点是 8 维 \`kf.mean()\`（函数体只取 x[0..3]）。
- 全仓库**没有任何 .cc include 它**（只有 docs 与我的 parity 工具），\`make test\` 因此永远发现不了 →
  README/文档里"ByteTrack C++ 独立实现并通过单测"不成立。
- 一行修复：形参改为 \`const std::array<double, 8>&\`（已在临时副本上验证可编译，且与 Python 数值一致）。
- parity 测试用 \`@pytest.mark.xfail(strict=True)\` 守住这条，修好后会 XPASS 提醒摘掉标记。

### D5 EMA 时间戳量纲不一致（**建议明确契约**）

- Python：\`EMASmoother.update(value, t)\`，\`t\` 为 float（文档写"秒或帧号"，example 用 \`t=1,2\`）。
- C++：\`update(double value, int64_t t = -1)\`，整数（毫秒/帧号）。
- 现象（parity 用例 \`ema_ts_float_seconds\`）：\`alpha=0.5, tau=0.5, t=0,0.5,1.5\` → C++ 把 0.5 截断成 0，
  退化为固定 alpha → 输出 \`1, 1.5, 2.797\`；Python 输出 \`1, 1.632, 2.815\`。
- 板端风险：tau 若按"秒"配置（如 1.0）而时间戳是毫秒，则 \`alpha = 1-exp(-dt_ms/tau) ≈ 1\`，
  **平滑完全失效**（等于不做 EMA）。
- 建议：C++ 侧把单位写进参数名（\`now_ms\`）或在文档/头注释里明确"t 与 tau 必须同量纲（推荐毫秒）"，
  Python 侧同样在 docstring 写明。整数时间戳下两端数值一致（parity 用例 \`ema_ts_int_ms\`、\`ema_ts_tau05\` 全绿）。

### 其它已核对但**一致**的项（无需修）

Kalman 全链路（含 4x4 协方差逐元素）、FSM 等级序列 + score 序列（含滞回/确认帧/DANGER 保持/trend）、
DBSCAN 簇数/标签/噪声数/聚类点数、图拉普拉斯谱特征（fiedler/谱半径/特征熵，n=4..30）。
DBSCAN 的 eps 闭区间语义（\`dist <= eps\`）两端一致。

---

## 6. 性能（cProfile，离线原型路径）

负载（同一脚本，前后各跑一次）：
111 条 timeline 的 M0 回放（45021 帧，生产配置）+ 30×\`compute_laplacian_features\`(n=120)
+ 20×\`dbscan_crowd_features\`(n=150) + 40 目标 × 300 帧 \`KalmanTracker\`。

| 指标 | 优化前 | 优化后 | 变化 |
|------|--------|--------|------|
| 总耗时（cProfile） | 1.461 s（1471013 calls） | **0.963 s**（1242483 calls） | **-34%** |
| \`compute_laplacian_features\` tottime | 0.361 s | **0.047 s** | **-87%** |
| \`KalmanFilter.correct\` cumtime | 0.429 s | **0.228 s** | -47% |
| \`np.linalg.inv\` 调用 | 11960 次（0.153 s） | 0（改显式 2x2） | 消除 |
| \`np.eye\` 调用 | 11960 次（0.039 s） | 0（缓存 \`_I4\`） | 消除 |

改动只有两处、都很小：去重循环向量化（P7）、Kalman 常量缓存 + 2x2 显式求逆（P8）。
**刻意未做**的优化：\`crowd_dbscan\` 的邻域枚举（cProfile 里 \`dbscan_labels\` 20 次 n=150 仅 0.036 s，
不是热点）、\`ByteTrack\` 匈牙利算法（不在主循环）。理由：Python 是原型/离线评估，
主循环在板端 C++，过度优化会损害可读性。

---

## 7. 新增工具与复现命令

### a) 真实编译的 C++/Python parity 测试（本轮最重要新增）

- \`tests/cpp_parity/parity_harness.cc\`：一个 stdin/stdout 小工具，直接 \`#include\` 六个头文件，
  \`LAP/DBSCAN/KAL/EMA/FSM/BT\` 六类命令，输出 \`%.12g\` 全精度。
- \`tests/test_cpp_python_parity.py\`：定位 g++（支持 \`CPP_PARITY_GXX\` 环境变量、PATH、
  \`G:\DevTools\mingw\w64devkit\bin\g++.exe\`）→ 编译 → 逐值比较；
  无 g++ 时整文件 skip（CI/板端不受影响）；\`KNOWN_DIVERGENCES\` 双向契约见 §5。
- 复现：\`python -m pytest tests/test_cpp_python_parity.py -q\` → \`30 passed, 1 xfailed\`。

  为什么需要它：原有的"Python 复刻 C++"式测试**复刻错了**（\`test_crowd_dbscan.py\` 副本写 \`NOISE=-2\`，
  真实头文件是 \`-1\`），导致 D1/D3/D4 这类分歧整整一轮没被发现。真实编译是唯一可靠的交叉验证。

### b) 111 条视频聚合重算（论文数字真源）

- \`scripts/aggregate_timelines.py\`（纯标准库，不依赖 matplotlib）→
  \`results/rerun/_aggregate_recomputed.json\`，字段与旧 \`_aggregate.json\` 逐一对齐。
- 复现：\`python scripts/aggregate_timelines.py --compare\`（\`--check\` 有差异时退出码 1）。

**实测结果（当前磁盘 timeline，111 条 / 45021 帧）**：

\`\`\`text
levels: SAFE 28214 / WARNING 8355 / DANGER 8452   (sum = 45021 ✓ 自洽)
switches: total 568, mean 5.1171, median 4.0, min/max 0/26
videos_with_danger: 45      all-SAFE videos: 31
fiedler: SAFE mean 0.0213 / WARNING mean 0.567 / DANGER mean 1.0842
\`\`\`

**发现的数据完整性问题（results/、README 归 lead）**：

- 旧 \`results/rerun/_aggregate.json\`（mtime 10-30 19:07）为
  \`SAFE 26885 / WARNING 6388 / DANGER 11748\`、切换 650（均值 5.8559）、"出现 DANGER" 66 条，
  与重算结果有 **206 处差异** → 该文件**无法从当前 timeline 复现**（timeline mtime 05:53–06:03）。
- \`README.md:79\` 的 "SAFE 26885 / WARNING 6388 / DANGER 8452" 相加 = **41725 ≠ 45021**，
  混用了旧值与新 DANGER；同句"66 条出现 DANGER"实际是**没有** DANGER 的条数（111-45=66）。
- 出处推断（**未验证**）：旧聚合可能是跨机拷贝或在另一份 timeline 上生成的。

### c) 本轮新增/修改的测试

- 新增 \`tests/test_algo_py_round3.py\`（21 例：DBSCAN 标签/顺序、脚点去重语义、
  min_danger_n 门控与保持路径、IPM 退化标定拦截）。
- 新增 \`tests/test_cpp_python_parity.py\`（31 例）。
- 修改 \`tests/test_hardware_config_consistency.py\`（conf_threshold 0.35 + 语义注释）、
  \`tests/test_spectral_fsm_experiment.py\`（replay 契约精确化）、
  \`tests/test_crowd_dbscan.py\`（使其"C++ 复刻"副本真的与 C++ 一致：\`UNCLASSIFIED, NOISE = -2, -1\`）、
  \`tests/test_go2rtc_demo_config.py\`（子进程编码健壮化）。

**没有为了让测试变绿而放宽任何实现**：所有失败用例都是"先写失败测试 → 改实现/改过期断言"。

---

## 8. 未验证 / 待办

1. \`BOX_THRESH\` / \`NMS_THRESH\` 在仓库内无定义（只有 yolov5.cc 引用）——**未验证**它们来自
   SDK 头文件还是编译命令行；请 board-perf 核实（否则板端 yolov5.cc 无法编译，
   而板子能跑说明构建机上的树与仓库可能不一致）。
2. \`results/edge_board/default_config.json\` 仍是 \`conf_threshold: 0.6\` + 旧注释，
   与 config 真源 0.35 不一致（该文件不在我 scope，**未验证**它是否仍被部署脚本使用）。
3. D1/D2/D3/D4/D5 的 C++ 侧修复未实施（不属我 scope），只给了 patch 方向与复现证据；
   D4 的一行修复我在**临时副本**上验证过可编译且数值与 Python 一致。
4. 聚合重算只覆盖 \`video_enhanced\` 的等级/切换/fiedler 统计；
   \`_comparison_summary.json\`（增强 vs 基线 23.2 → 5.9）与 "DANGER 帧 2190 → 8452" 的基线口径
   **未复核**（需基线 timeline，超出本轮范围）。
5. 板端真实性能（RV1106 上的 FPS/内存）**未测**；本轮性能结论只针对 Python 离线原型路径。
6. 111 条 timeline 与本轮重算均**无人工真值**，因此"漏报/误报率"无法计算；
   本报告只谈"算法内部一致性与正确性"，不宣称识别精度提升。
7. \`risk_config.h\` 接入 \`conf_threshold\` 后，板端实际生效值需在真机复查（归 board-perf）。

---

## 9. 收尾复验（2026-10-30 23:3x）

- lead 已把 `results/rerun/_aggregate.json` 覆盖为重算值并加 `_comment` provenance。
  `python scripts/aggregate_timelines.py --check` 现在输出
  "**与 results/rerun/_aggregate.json 逐字段一致（重算可复现，数字可信）**"，退出码 0
  （比对已忽略 `_` 前缀的说明字段，避免 provenance 注释造成假差异）。
- `docs/review/` 在 `.gitignore:52` 被忽略 → 本报告是本地评审产物，不会进 git；需要进仓库的话请 lead 处理。
- 最终 `python -m pytest tests/ -q` → `312 passed, 4 skipped, 1 xfailed`（两种编码环境各跑一次）；
  `mingw32-make test` → `EXIT=0`，全部 C++ 套件 0 失败。
- 本轮**未做** git add/commit/push（遵守铁律），未改 README。