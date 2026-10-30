# 最终鉴定 F1：代码与测试

- **审查人**：终检员 F1（代码与测试方向）
- **日期**：2026-10-29
- **审查范围**：全量 pytest 回归、核心算法代码质量（risk_fsm / graph_laplacian / crowd_dbscan）、pan_tilt.h 限幅、RKNN 返回值检查、测试同步性、CI 配置
- **审查方式**：只读审查 + 运行测试 + 临时脚本实证；**未修改任何代码**
- **基线 commit**：`284de8d`（分支 `review-opt-20261029`，工作树干净）

---

## 1. 运行本地全量 pytest

**结论：【通过】**

```text
$ PYTHONPATH= G:/py311_gpu/Scripts/python.exe -m pytest tests -q
187 passed, 4 skipped in 6.61s
```

- 与任务基线完全一致：**187 passed / 4 skipped**。
- 4 个 skip 全部来自 `tests/test_go2rtc_demo_config.py:235`（Windows MSYS 路径转换与 Linux 不同，shell 测试由 CI 覆盖），与本次修复无关，属预期跳过。
- 以 `PYTHONPATH=`（空）运行，杜绝了本机 site-packages 干扰，结果可信。

---

## 2. 核心算法代码质量审查

### 2.1 `src/algorithms/risk_fsm.py` — 人数门控 `min_danger_n`

**结论：【通过】**
**测试覆盖：【需注意】（缺直接单元测试，详见 §7 风险 1）**

- 实现：`src/algorithms/risk_fsm.py:246-247`

  ```python
  # P0-4 人数门控：行人数少于 min_danger_n 时，即使密度/连通度爆表，
  # score 也封顶在 up_danger 之下（1-2 人的偶遇/伴行不允许判为 DANGER）。
  if int(n) < cfg.min_danger_n:
      score = min(score, cfg.up_danger - 0.01)
  ```

- 逻辑正确性（已实证）：
  - 默认 `min_danger_n=3`、`up_danger=0.65`，封顶值 `0.64` 严格低于升级阈值，且高于 `down_warning=0.55`（DANGER 保持期内人数回落的帧仍可保持，符合设计语义）。
  - 临时脚本验证：`n=2, density=1.0, r_gcc=1.0` 连续 30 帧，score 恒为 0.64，等级停在 WARNING 不误判 DANGER；`n=3` 同特征 score=0.66，正常进入 DANGER。**门控边界行为正确**。
- 配置校验完整（`__post_init__`：权重非负、n_ref>0、滞回一致性 `up_warning>down_safe`、`up_danger>down_warning` 等），默认值与 C++ `src/algorithms/risk_fsm.h` 一致（实证：C++ 头文件 `min_danger_n=3`、`up_danger=0.65`，Python 相同）。
- 注释为中文，对背景（P0-4 裁决）、语义（"真实人群聚集"）、边界行为说明充分。

### 2.2 `src/algorithms/graph_laplacian.py` — 重复点去重

**结论：【通过】**
**测试覆盖：【需注意】（缺直接单元测试，详见 §7 风险 2）**

- 实现：`src/algorithms/graph_laplacian.py:152-158`

  ```python
  # 去重完全重合的坐标点（P0-4 修复）：同一行人被重复检测/跟踪会产生完全
  # 相同的脚点，直接建图会虚增密度与连通度（如 [(0,0),(0,0)] 得 density=1、
  # r_gcc=1，评分倒挂到 DANGER）。先合并重复脚点再计算特征，保持首次出现顺序。
  if pts.shape[0] > 1:
      _, first_idx = np.unique(pts, axis=0, return_index=True)
      if first_idx.size < pts.shape[0]:
          pts = pts[np.sort(first_idx)]
  ```

- 逻辑正确性（已实证）：
  - `np.unique(axis=0, return_index=True)` 取唯一行 + 首次出现索引，`np.sort(first_idx)` 保持原始顺序，去重后 `n/m/density/r_gcc/clustering` 全部基于真实人数计算。
  - 临时脚本验证：输入 `[(0,0),(0,0),(1,0),(1,0)]` 去重后 `n=2, m=1, density=1.0, r_gcc=1.0`，与真实 2 点输入 `[(0,0),(1,0)]` 的特征**完全一致**。
  - 空点集/非法形状分支均先于去重处理，无边界问题。
- 注释中文，说明了修复动机（重复脚点导致评分倒挂）与实现策略。

### 2.3 `src/algorithms/crowd_dbscan.py` — 哨兵值

**结论：【通过】**

- 实现：`src/algorithms/crowd_dbscan.py:34-35`

  ```python
  UNCLASSIFIED = -2   # 内部哨兵：尚未处理（算法结束前应全部被替换）
  NOISE = -1          # 噪声点(不属任何簇)，与 C++ 端 crowd_dbscan.h 对外语义一致
  ```

- 与 C++ `crowd_dbscan.h` 语义一致（-1 为对外噪声标签）；`dbscan_labels` 初始化为 `UNCLASSIFIED`，算法结束前全部被替换为簇号或 `NOISE`，哨兵不会泄漏到对外特征（实证：`labels` 只含簇号与 -1）。
- 闭区间 `dist <= eps`、核心点扩张、边界点归属逻辑与经典 DBSCAN 一致；注释中文，说明充分。

---

## 3. `src/hardware/pan_tilt.h` — TILT 限幅 60°~120°

**结论：【通过】**

- 实现：`src/hardware/pan_tilt.h:66-74`

  ```cpp
  // 机械限幅：PAN 全行程 10°~170°（水平旋转无遮挡约束）
  if (targetPan  < 10) targetPan  = 10;
  if (targetPan  > 170) targetPan = 170;
  // TILT 限幅 ±30°（相对水平 90° → 60°~120°）：
  // 俯仰行程过大既增加 FOV 覆盖论证难度（见 tracking_servo.md 4.3 FOV 校验），
  // 又让舵机更容易碰结构件/线缆。±30° 与摄像头垂直半角 ~29.6° 匹配，
  // 保证任意下俯角下画面下边界仍覆盖近处地面（h/d ≥ tan(垂直半角+下俯+10°)）。
  if (targetTilt < 60) targetTilt = 60;
  if (targetTilt > 120) targetTilt = 120;
  ```

- **限幅正确**：90° ± 30° = 60°~120°，与标题一致。
- 注释为中文，给出了设计依据（FOV 覆盖论证、机械安全）与文档引用，质量高。
- 缩进为 4 空格，与 PAN 限幅、角速度限幅、低通滤波等周边代码风格完全一致。

---

## 4. `src/luckfox_rtsp_yolov5/src/` — RKNN 返回值检查

**结论：【通过】**

- `yolov5.cc`：
  - `init_yolov5_model()`：`rknn_init`（54-58）、每个 `rknn_query`（62-67、86-91、104-109）、`rknn_set_io_mem`（122-126、131-135）**逐处检查**返回值，失败立即 `return -1`；`io_num.n_input==0 || n_output==0` 先校验再分配（72-76），避免 C99 VLA 越界。
  - `inference_yolov5_model()`：**P0-5 修复** —— 先 `memset(od_results, 0, ...)` 清零（224），`rknn_run` 失败返回 -1（227-230），保证调用方不会读到未初始化结果结构。
  - 小瑕疵（不影响正确性）：`yolov5.cc:234` 的 `out:` 标签无任何 `goto` 引用，属死标签；成功路径 `return ret` 依赖 `rknn_run` 返回 0 的语义。建议后续清理，但不构成缺陷。
- `main.cc`：
  - `init_yolov5_model` 返回值检查（197-202）：失败打印 `[FATAL]` + `cleanup()` + `return -1`。
  - `inference_yolov5_model` 返回值检查（382-385）：`infer_ret != 0` 打印告警，且因结果已清零，本帧 `count=0`，后续循环/决策路径安全跳过。
  - 其余 MPI 调用（VI/VENC/RTSP）均带有限超时与返回值检查，`cleanup()` 按 `*_ready` 标志幂等释放，资源管理严谨。

---

## 5. 测试文件同步性

**结论：【通过】**

| 测试文件 | 要求 | 核查结果 |
|---|---|---|
| `tests/test_crowd_dbscan.py` | 哨兵值 | **同步**。导入并使用 `NOISE`/`UNCLASSIFIED` 常量断言（35、143、234、325-327）；`TestCppAlgorithmLogic` 中 `cpp_style_dbscan` 明确 `UNCLASSIFIED, NOISE = -2, -1`（379），注释"与 crowd_dbscan.h 一致"，实现与 Python 端逐行对应 |
| `tests/test_spectral_fsm_experiment.py` | 门控断言 | **同步**。`test_m0_replay_consistency`（120-131）读取 `min_danger_n`，断言 `bad == 0`（n<3 的帧不得从非 DANGER 状态升级为 DANGER），并正确注释 DANGER 保持期内人数回落属设计行为 |
| `src/luckfox_rtsp_yolov5/tests/test_hardware_modules.cc` | 9.4 tilt 断言 | **同步**。9.4 节（420-433）以左上角极端目标 `(0,0)` 连喂 200 帧，`CHECK(t >= 60 && t <= 120)`，注释"tilt 软件限幅 ±30°"，与 `pan_tilt.h` 限幅一致 |

---

## 6. CI 配置审查（`.github/workflows/ci.yml`）

**结论：【通过】**

- **python-tests**：ubuntu-latest + Python 3.11，安装 `numpy/pytest/opencv-python-headless`，运行 `python -m pytest tests/ -q`。与本地命令一致；测试文件通过 `sys.path.insert` 自管理导入，无需 PYTHONPATH 环境变量，干净环境可直接通过。
- **cpp-tests**：`cd src/luckfox_rtsp_yolov5 && make test`。Makefile `test` 目标覆盖 10 个主机测试（enhanced/minijson/risk-config/golden/preprocess/hardware/selftest/lcd-menu/ili9341/xpt2046），仅需 `g++`；Linux 下自动追加 `test-sysfs-spi`。CI 安装 `g++ make` 足够。
- **cmake-tests**：**合理**。CMakeLists 中确有 **10 个 `add_test`**（`test_enhanced_decision` / `test_minijson` / `test_risk_config` / `test_hardware_modules` / `test_golden_regression` / `test_preprocess` / `test_selftest` / `test_lcd_menu` / `test_ili9341` / `test_xpt2046`），与 job 名称"ctest 10 目标"一致。默认构建**无需 OpenCV/RKNN**（`find_package(OpenCV QUIET)` 未找到时 test_risk_config 自动降级为纯 JSON 读取验证），CI 仅装 `cmake g++` 即可编译运行全部 10 个目标。命令序列 `cmake -S ... -B build` → `cmake --build build -j2` → `ctest --test-dir build --output-on-failure` 正确。
- 历史记录显示 CI 门禁已恢复全绿（`5a36c83` "P2-1 CI 门禁恢复全绿"），与本次静态审查结论一致。

---

## 7. 总体结论与剩余风险

### 总体结论：【通过】

6 项检查全部通过；P0×6/P1×16/P2×7 的修复在代码质量、逻辑正确性、测试同步、CI 配置四个维度均达到交付标准。核心算法经临时脚本实证行为正确，全量 pytest 187 passed / 4 skipped 与基线一致，git 工作树干净（HEAD=284de8d）。

### 剩余风险清单（均为低风险，不阻塞交付）

| # | 风险 | 等级 | 说明与建议 |
|---|---|---|---|
| 1 | `min_danger_n` 门控缺少直接单元测试 | 低 | `tests/test_risk_fsm.py` 的 HIGH/LOW 用例 n 分别为 20/0，未覆盖 n<3 封顶场景；门控断言仅存在于 `test_spectral_fsm_experiment.py` 的 `test_m0_replay_consistency`，而该用例依赖本地 timeline 数据（`.gitignore` 排除），**在 CI 上会被 skip**。建议在 `test_risk_fsm.py` 增加 n=2 不触发 DANGER / n=3 可触发的边界用例，使门控有 CI 常驻回归保护 |
| 2 | `graph_laplacian.py` 重复点去重缺少直接单元测试 | 低 | `tests/test_graph_laplacian.py` 无重复/重合坐标用例，去重逻辑目前无回归保护。建议补充 `[(0,0),(0,0),(1,0),(1,0)]` 与真实 2 点特征一致的断言 |
| 3 | `yolov5.cc` `out:` 死标签 | 极低 | 无 `goto` 引用的标签 + 成功路径 `return ret` 依赖 `rknn_run` 返回 0 的隐式语义，可读性略差；不影响正确性，建议后续 PR 顺手清理 |
| 4 | C++ 算法/硬件测试未在本机复跑 | 说明项 | 本机（Windows）无 `g++/make/cmake` 工具链，`make test` 与 ctest 无法本地执行；前序评审已确认基线 C++ 算法 26/26、硬件模块 118/118，且 CI 的 cpp-tests / cmake-tests job 会持续门禁，风险可控 |

---

*本报告由只读审查、全量测试运行与临时脚本实证得出；未修改任何仓库文件。*