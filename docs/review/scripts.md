# 脚本只读审查报告

- 审查对象：`H:/PedestrianRiskDecision/scripts/crowd_test.py`（图片基线）、`H:/PedestrianRiskDecision/scripts/crowd_test_enhanced.py`（视频增强）
- 对照基准：`src/luckfox_rtsp_yolov5/src/risk_engine.cc`、`src/luckfox_rtsp_yolov5/include/risk_config.h`、`config/default_config.json`、`src/algorithms/graph_laplacian.py`、`src/algorithms/kalman_tracker.py`、`src/algorithms/risk_fsm.py`
- 方式：仅审查，未修改任何文件

---

## 1. 结论概览

| 维度 | crowd_test.py | crowd_test_enhanced.py |
|---|---|---|
| 命令行/--config | 良好，支持 `--config` | 有继承，但存在严重缺省行为偏差 |
| 结果可复现性 | 基本可用，缺少关键参数记录 | 时间线不自描述，无法复现 |
| 错误处理 | 较完善，退出码有 Bug | 无逐帧隔离，资源可能泄漏 |
| C++ 语义一致性 | 单帧规则完全一致 | 默认不走 IPM，且 FSM 实际失效 |

**最重要发现：** `crowd_test_enhanced.py` 中 FSM 时序平滑因传入 `SimpleNamespace` 而**静默失效**（详见 §4 问题 E1），且默认 `--scale=0.01` 绕过与 C++/基线一致的 IPM 单应映射（问题 E2）。

---

## 2. 命令行参数与 `--config` 支持

### 2.1 crowd_test.py（基线）
- 参数齐全：`--input/--output/--conf/--weights/--imgsz/--homography/--scale/--device/--edge-dist/--config`。
- `--config` 由 `load_config()`（第 223-232 行）加载 JSON，支持 `edge_dist_m`、`danger.{n,rho,gcc}`、`warning.{n,rho,gcc}`，结构与本仓库 `config/default_config.json` 一致。
- `evaluate_level()`（第 234-248 行）缺省用内置常量，与 C++ 默认一致。
- **优先级注意：** 第 434 行 `edge_dist = cfg.get("edge_dist_m", args.edge_dist)`，即配置文件中的 `edge_dist_m` **覆盖**命令行 `--edge-dist`。用户若期望 `--edge-dist` 优先会被静默忽略。

### 2.2 crowd_test_enhanced.py（增强）
- 复用 `load_config/compute_features/evaluate_level/get_homography/load_homography/map_to_ground`（第 22-23 行），并新增 `--spectral` 开关。
- `--config` 中 `temporal` 段传给 `RiskFSMConfig`（第 85 行）。
- **问题 E1（严重）：** 第 115 行用 `SimpleNamespace(n=..., density=..., r_gcc=...)` 作为 FSM 输入，但 `risk_fsm.py` 的 `_compute_score()`（第 204 行）调用 `features.get("n", 0)`——`SimpleNamespace` **没有 `.get()` 方法**，每次调用 `fsm.update(fs, ...)` 都会抛 `AttributeError`，被第 121-122 行的 `except Exception` 吞掉后回退到 `raw_level`。**FSM 的滞回/确认帧/保持逻辑从未生效**，`stable_level` 实际恒等于单帧 `raw_level`，增强脚本宣称的“时序平滑”是无效代码路径。
- **问题 E2（严重）：** 第 153 行 `--scale` 默认 `0.01`（基线默认是 `None`）。增强脚本中 `H` 仅在传 `--homography` 时非空（第 165-166 行），否则 `detect_pedestrians` 走 `foot * scale_mpp`（第 45 行）。`get_homography` 虽被 import 但**从未调用**。因此默认运行时不使用与 C++/基线一致的 IPM，地面坐标被当作“像素×0.01 米”，物理尺度错误，结果与 `crowd_test.py` 同场景不一致。
- **问题 E3（中）：** `RiskFSMConfig(**(cfg.get("temporal", {})))` 若配置文件含未知键会直接 `TypeError` 崩溃（该处无 try/except）；且 `temporal` 键名与基线的 `danger/warning` 语义不同，容易混淆。

---

## 3. 结果 JSON/CSV 可复现性

### 3.1 共同问题
- **两者都不输出 CSV**，只有 JSON（`results.json` / `*_timeline.json`）。若需求包含 CSV，需要补充。

### 3.2 crowd_test.py 的 `results.json`
- 记录 `generated_at/weights/conf/edge_dist_m/total_images/processed/failed/results`（第 479-488 行），包含检测框与可视化路径，基本可复现。
- **可复现性缺口（中）：** 未记录 `imgsz`、`device`、坐标映射方式（`homography` 路径 / `scale` 值 / 默认 IPM）、`--config` 路径、`YOLO` 版本。同一份 JSON 无法区分两次不同参数或不同配置的运行。
- `image` 与 `visualization` 使用 `os.path.abspath()` 绝对路径（第 377、387 行），移动目录后引用失效；跨机器不可移植。
- `density/r_gcc/clustering` 存为 `round(x,4)`（第 383-385 行），但等级判定用的是未舍入值。消费者若用 JSON 中舍入值重算等级，在阈值边界可能不一致（如 0.5500 vs 0.54999）。

### 3.3 crowd_test_enhanced.py 的 `*_timeline.json`
- 逐帧记录 `frame/level/n/density/r_gcc/fiedler`，精度未舍入，较基线更精细。
- **可复现性缺口（中）：** 时间线**完全不自描述**——未记录 `weights/conf/imgsz/H/scale/edge_dist/device/config/temporal 配置/视频元数据`，且无 `generated_at`。第三方拿到时间线无法重放或核对等级来源。
- `stable_level` 实际来自失效的 FSM（问题 E1），因此时间线中的 `level` 与 `raw_level` 无差别，不代表“时序稳定后的等级”。

---

## 4. 错误处理

### 4.1 crowd_test.py
- 优点：逐图 `try/except` 隔离（第 466-476 行），读取/推理失败记入 `failures` 并继续；`cv2`/`ultralytics` 缺失给出可操作提示；`collect_images` 对无效输入返回 `[]` 并提示。
- **问题（低）：** 第 500 行 `return 0 if not failures else 0` 恒为 0，失败时退出码不体现失败状态，不利于 CI 判断。应为 `return 0 if not failures else 1`。
- **问题（低）：** `load_config()` 解析失败仅打印 WARN 并回退默认，且**不校验配置语义**（见 §5）。
- **问题（低）：** `map_to_ground` 当 `w≈0` 时静默返回原像素坐标（第 151-153 行），极端情况下会混入错误地面点，未告警。

### 4.2 crowd_test_enhanced.py
- **问题（中）：** 视频内**无逐帧异常隔离**。`detect_pedestrians`（第 30 行 `model.predict`）一旦抛异常，整个视频处理中断，且 `VideoCapture`/`VideoWriter` 在异常路径下**不会 release**，可能残留未完成文件。
- **问题（中）：** `VideoWriter` 未检查 `writer.isOpened()`，若编解码器不可用会静默写坏文件。
- **问题（低）：** `main()` 中 `YOLO(...)`、`load_homography(...)` 均未包异常，错误以裸 traceback 退出；`process_video` 打开视频失败仅打印后 `return`，进程仍以 0 退出。

---

## 5. 与 C++ 端算法语义一致性

### 5.1 crowd_test.py（基线）——一致
- `compute_features`（第 156-219 行）与 `risk_engine.cc::computeFeatures()`（第 54-110 行）逐项一致：
  - 建边条件均为**严格小于** `d < edge_dist`（Python 第 176 行 / C++ 第 69 行）。
  - `density = m / (n*(n-1)/2)`、BFS 求 `r_gcc`、平均聚类系数公式一致。
  - 阈值默认值（10/0.55/0.80、5/0.28/0.55、2.0m）与 `default_config.json`、`risk_config.h` 一致。
- `evaluate_level` 判定顺序（先 DANGER 再 WARNING 再 SAFE）与 C++ `evaluate()`（第 112-126 行）一致。
- 默认 720x480 标定下 IPM 与 C++ 校准一致；非 720x480 时按比例缩放源四边形（第 106-111 行），为近似适配，**对非 4:3 分辨率（如竖图）物理映射会失真**，属已文档化的近似。

### 5.2 语义校验缺口（中）
- C++ `risk_config.h` 的 `loadRiskConfig` 会校验 `rho/gcc ∈ [0,1]`、`warning.n <= danger.n` 等，非法配置抛错并回退；Python `load_config`/`evaluate_level` **不做任何语义校验**。同一份配置（如 `warning.n > danger.n`）在 C++ 被拒绝、在 Python 静默接受并产生等级倒挂。建议 Python 端补充相同校验以保持行为一致。

### 5.3 crowd_test_enhanced.py——多处偏离
- **偏离 1（严重，E2）：** 默认 `scale=0.01` 绕过 IPM，地面坐标语义与 C++/基线完全不同。
- **偏离 2（中）：** Kalman 平滑输出的是**活跃轨迹状态**（含最多 `max_missed=5` 帧外推的“幽灵点”），`n` 为轨迹数而非当前帧实测人数，且检测无 ID 时退化为最近邻关联（`gate_dist=edge_dist`），可能发生身份切换。增强后的 `n/density/r_gcc` 与基线/C++ 的单帧实测值**不可直接对比**，属未文档化的语义偏差。
- **偏离 3（严重，E1）：** FSM 失效导致“时序稳定等级”实为单帧规则等级，但时间线/标注仍以 `stable_level` 名义输出，误导使用者认为时序平滑已生效。
- **偏离 4（低）：** `detect_pedestrians` 未按 `cls_id==0` 过滤（基线第 336 行有），仅依赖 `classes=[0]`。若自定义模型类别索引不同或该参数被忽略，可能混入非行人。

---

## 6. 问题清单（按严重程度）

| 编号 | 严重度 | 文件:行号 | 问题 |
|---|---|---|---|
| E1 | 严重 | `crowd_test_enhanced.py:115-122` + `risk_fsm.py:204` | `SimpleNamespace` 无 `.get()`，`fsm.update` 每帧抛异常被吞，FSM 时序平滑**完全失效**，稳定等级恒等于原始等级 |
| E2 | 严重 | `crowd_test_enhanced.py:153,165-166,45` | `--scale` 默认 `0.01`，未传 `--homography` 时绕过 IPM，地面坐标语义与基线/C++ 不一致 |
| E3 | 中 | `crowd_test_enhanced.py:85` | `temporal` 配置含未知键即 `TypeError` 崩溃；无异常隔离 |
| E4 | 中 | `crowd_test_enhanced.py:91-134` | 视频逐帧无异常隔离，异常路径不 `release` 视频资源；writer 未检查是否打开 |
| E5 | 中 | `crowd_test.py:479-488` / `crowd_test_enhanced.py:135-137` | JSON/时间线未记录 `imgsz/device/映射方式/config/权重` 等复现参数；时间线无 `generated_at`，不自描述 |
| E6 | 中 | `crowd_test.py` 与 `crowd_test_enhanced.py` | 两脚本均不输出 CSV；若需求含 CSV 需补充 |
| E7 | 中 | `crowd_test.py:223-248` / `risk_config.h:92-136` | Python 端不做配置语义校验（`warning.n<=danger.n`、`rho/gcc∈[0,1]`），与 C++ 对同一配置行为不一致 |
| E8 | 低 | `crowd_test.py:500` | `return 0 if not failures else 0` 恒为 0，失败时退出码不反映状态 |
| E9 | 低 | `crowd_test.py:151-153` | `map_to_ground` 在 `w≈0` 时静默返回像素坐标，无告警 |
| E10 | 低 | `crowd_test_enhanced.py:30-47` | 未按 `cls_id` 二次过滤行人，依赖 `classes=[0]` |
| E11 | 低 | `crowd_test_enhanced.py` 全文 | `get_homography`、`LEVELS` 等 import 后未使用，暗示设计意图未落地 |

---

## 7. 建议优先修复项

1. 将 `SimpleNamespace` 改为 `dict`（`{"n": n, "density": density, "r_gcc": r_gcc}`），使 FSM 真正生效，并在 `--spectral`/`--config` 输出中体现时序等级。
2. 将增强版 `--scale` 默认改为 `None`，并在未传 `--homography` 时调用 `get_homography(img_w, img_h)`，与基线/C++ 默认行为对齐。
3. 为 `results.json` / `*_timeline.json` 补齐运行参数（weights/conf/imgsz/device/H/scale/edge_dist/config 路径/temporal 配置/视频元数据），提升可复现性。
4. 修复退出码 `return 0 if not failures else 1`，并在增强版加入逐帧 try/except 与视频资源 `finally` 释放。
5. 在 Python `load_config` 中补充与 C++ `loadRiskConfig` 一致的语义校验（`warning.n <= danger.n`、`rho/gcc ∈ [0,1]`）。