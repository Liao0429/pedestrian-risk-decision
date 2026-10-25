# 文档与可复现性审查报告

> 审查范围：`README.md`、`README.en.md`、`docs/ARCHITECTURE.md`、`docs/INTEGRATION_PLAN.md`、`.gitignore`、`dataset/` 下载脚本。
> 性质：只读审查（除本报告文件外未修改任何仓库内容）。
> 审查时间：2026-10-26。

---

## 结论概览

- **数据本体已全部提交到 git**（500 张 COCO 图片 + 111 条 Mixkit 视频 + 30 图早期子集 + 校验文件），这是数据集可复现性最强的保障，第三方克隆仓库即可拿到数据。
- **主要问题集中在"文档与代码不一致"与"下载/部署脚本依赖本机私有环境"**：INTEGRATION_PLAN.md 已过时但仍标注为"未实现方案"，PC 实验脚本实际用 YOLOv8n 而文档统一宣称 YOLOv5n，视频下载脚本硬编码本机代理与未提交的 id 清单文件。

---

## 1. README.md 与 README.en.md

### 1.1 双语一致性：✅ 良好
- 两份 README 结构、章节、数字、链接完全对应（特性 / 架构 / 目录结构 / 数据集 / 实验 / 部署 / 文档索引 / 扩展模块 / 说明）。
- 实验指标一致：检出率 82.5%、高密度 79.4%、SAFE 414 / WARNING 61 / DANGER 25、密集视频 264/305、零误报。

### 1.2 与代码/实验结果一致性：⚠️ 一处关键偏差
- README 与 `docs/ARCHITECTURE.md`、`results/edge_board/EDGE_TEST_REPORT.md` 均宣称板端为 **YOLOv5n + INT8**。
- 但 PC 实验脚本 `scripts/crowd_test.py`（L38、L416-417）与 `scripts/crowd_test_enhanced.py`（L150）默认加载 **`yolov8n.pt`（YOLOv8n，ultralytics 自动下载）**；仓库根目录亦存在 `yolov8n.pt`。
- 即 README「部署方法 → PC 端测试」中给出的命令实际运行的是 **YOLOv8n**，与全文统一的 "YOLOv5n 轻量化检测" 表述不符。**建议**：在 README 明确区分「板端 = YOLOv5n INT8 / RKNN」「PC 实验 = YOLOv8n（ultralytics）」，并说明 82.5% 检出率来自哪个模型。

### 1.3 目录结构描述
- README 目录树与实际一致（`dataset/images500`、`dataset/videos50`、`scripts/`、`src/algorithms/`、`src/hardware/`、`src/luckfox_rtsp_yolov5/`、`config/`、`results/`、`tests/`、`docs/` 均存在）。
- README 未提及 `dataset/images/`（早期 30 图子集实际输出目录）与 `dataset/_work/` 的存在，属次要遗漏，不影响正确性。

---

## 2. docs/ARCHITECTURE.md

### 2.1 与代码一致性：✅ 基本一致
- 模块清单中列出的 `src/algorithms/graph_laplacian.h`、`kalman_filter.h`、`risk_fsm.h`、`bytetrack_tracker.h`、`crowd_dbscan.h`、`ema_smoother.h`、`ipm_calibrate.py` 及 `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc` 均实际存在，且已提交 git。架构图、数据流、配置阈值与 `config/default_config.json` 数值一致。

### 2.2 配置键名错误：⚠️ 一处笔误
- §7 配置说明表格将邻接距离阈值写为 **`edge_dist`**，实际 `config/default_config.json` 键名为 **`edge_dist_m`**（`INTEGRATION_PLAN.md` 中则正确写作 `edge_dist_m`）。建议统一。

---

## 3. docs/INTEGRATION_PLAN.md —— 严重过时，与现状冲突

- 文档头仍标注「**文档状态：方案设计（不落实现有代码，仅给出改造点与关键代码片段）**」。
- 文档多次声称：
  - §0「`src/algorithms/` 目录存在但**为空**（已核实）」；
  - §0 引用的 `scripts/crowd_test.py` 行号/功能（如"无视频模式"）已与现状不符（`crowd_test_enhanced.py` 已实现视频增强）；
  - §3「**本仓库内无既有构建脚本**，需在 Luckfox SDK 外部工程的 CMakeLists/Makefile 中追加」。
- 但当前仓库**已包含全部落地实现**：`src/algorithms/` 下 8 个 `.py` + 8 个 `.h`（graph_laplacian / kalman_tracker / risk_fsm / bytetrack_tracker / crowd_dbscan / ema_smoother / ipm_calibrate / minijson），Luckfox 工程已有 `enhanced_decision.cc`、`CMakeLists.txt`、`Makefile`（含 `make luckfox` 交叉编译目标）。
- 而 `README.md` / `ARCHITECTURE.md` 又把上述模块当作**已实现事实**引用。于是三份文档相互矛盾：README/ARCHITECTURE 说"已实现"，INTEGRATION_PLAN 说"方案未落地"。
- **建议**：将 INTEGRATION_PLAN.md 更新为「已落地」状态（或明确标注为历史设计快照），并同步其 `src/algorithms/` 为空、无构建脚本等过时断言；同时更新 §0 中 `crowd_test.py` 的行号引用。

---

## 4. 部署步骤可复现性

### 4.1 板端部署（Luckfox RV1106）
- 现状：`src/luckfox_rtsp_yolov5/` 提供 `Makefile`（`make luckfox`，需设置 `LUCKFOX_SDK_ROOT`、`OPENCV_ROOT`，依赖 RKNN/RTSP/OpenCV 库）与 `CMakeLists.txt`（含交叉编译注释与主机 `ctest` 单元测试路径）。README 部署命令「scp 产物到板子 → sudo ./luckfox_pico_rtsp_yolov5」与之配套。
- 缺口：
  - 模型权重 **`yolov5.rknn` 未包含在仓库**（也不在 `.gitignore` 的 `*.engine`/`*.pt` 覆盖范围内，但未提交）；文档未提供 RKNN 量化/转换脚本，第三方无法从仓库独立产出该模型。
  - `results/edge_board/EDGE_TEST_REPORT.md` 的"复现"命令依赖作者私有板环境路径 `/home/pico/daima/luckfox_pico_rtsp_yolov5_demo` 与静态 IP `192.168.0.136`，非通用可复现。
  - 依赖外部 Luckfox SDK，仓库无 SDK 版本/工具链版本锁定（Makefile 默认 `arm-none-linux-gnueabihf-`）。硬件项目可接受，但 README 未明示这些前置依赖。
- **建议**：在 README 部署章节列出前置依赖（Luckfox SDK、板端 OpenCV、`yolov5.rknn` 的来源/转换步骤），或提供模型转换脚本并说明权重获取方式。

### 4.2 PC 端实验
- `requirements.txt` 锁定主版本下限（ultralytics/opencv/numpy/pytest），`python scripts/crowd_test.py --input dataset/images500 ...` 可直接运行；YOLOv8n 权重由 ultralytics 自动下载。可复现性良好。

---

## 5. 数据集下载脚本可复现性

### 5.1 图片下载：✅ 可复现
- `dataset/_work/download_500_images.py`：使用 COCO 官方 CDN（`http://images.cocodataset.org/{val2017|train2017}/<id>.jpg`），固定随机种子 `random.Random(123)`，依赖已提交的 `selected_person_images.csv` 与 `train2017_dense.csv`。可复现。
- `dataset/_work/download_coco_images.py`（早期 30 图版）：seed 42、桶均衡、同样基于已提交 CSV。可复现。
- `dataset/README.md` 详细说明了标签来源（Ultralytics coco2017labels.zip 已提交）与挑选逻辑。

### 5.2 视频下载：⚠️ 不可复现 / 来源冲突
- **`dataset/videos50/download_mixkit.sh`**：
  - 引用 id 清单文件 **`/h/PedestrianRiskDecision/mixkit_good_ids_lf.txt`** —— **该文件未提交到仓库**（已在仓库根与全仓库确认不存在），第三方执行必失败。
  - 硬编码本机代理 **`-x http://127.0.0.1:7890`**，非通用环境不可用。
- **`dataset/_work/download_50_videos.py`**：
  - 通过 **YouTube 搜索**（`ytsearch{n}:{term}`，yt-dlp）下载，搜索结果不确定、视频可能下架，且涉及 YouTube 内容，与 README「111 条均来自 **Mixkit**」的来源说法冲突。
  - 同样硬编码代理 `127.0.0.1:7890` 与绝对输出路径 `H:/PedestrianRiskDecision/dataset/videos50`。
- **`dataset/videos50/check_people.py`**：硬编码绝对路径 `H:/PedestrianRiskDecision/dataset/videos50`，跨平台不可移植。
- **缓解**：`dataset/videos50/README.md` 已给出全部 111 条视频的**官方直链清单**（`https://assets.mixkit.co/videos/<id>/<id>-360.mp4` 与分类页），据此可生成提交内 id 清单；且 111 条视频**已全部提交 git**，下载脚本仅供追溯，不影响已交付数据的可复现性。
- **建议**：将 `download_mixkit.sh` 改为读取仓库内提交的 id 清单（或从 README 直链表生成），去掉硬编码代理/绝对路径；删除或明确标注 `download_50_videos.py`（YouTube 版）为历史脚本，避免与 Mixkit 来源冲突。

---

## 6. .gitignore 合理性

- **整体合理**，特别是：
  - `*.log` 全局忽略 + **`!results/edge_board/*.log`** 白名单：真机运行日志作为真实数据证据**允许上传**，已确认 `results/edge_board/enhanced_final_20261026.log` 确实被 git 跟踪。符合预期。
  - `*.pt`、`*.engine` 忽略大权重文件（`yolov8n.pt` 未被跟踪，由脚本自动下载），合理。
  - `*.tmp`/`*.part`/`__pycache__`/`.venv`/`cache/` 等常规项齐全。
- **次要问题**：
  - `results/diag*` 规则与已跟踪文件冲突：`results/diag/results.json` 与 `results/diag/vis/` 已在规则加入前被提交，仍被跟踪；新文件会被忽略，历史文件不受影响。若意图是不跟踪诊断结果，应同时 `git rm --cached` 清理。
  - `*.html` 忽略范围过宽，可能误伤未来需跟踪的文档/报告（当前无 html 文档，风险低）。

---

## 7. 改进优先级建议

| 优先级 | 事项 | 类型 |
|---|---|---|
| P0 | 更新 `INTEGRATION_PLAN.md` 状态为"已落地"，修正"`src/algorithms/` 为空""无构建脚本"等过时断言 | 文档一致性 |
| P0 | README/ARCHITECTURE 统一说明：板端 YOLOv5n INT8 vs PC 实验 YOLOv8n | 文档-代码一致 |
| P1 | `download_mixkit.sh` 改用仓库内 id 清单、去掉硬编码代理/绝对路径；处置 YouTube 版 `download_50_videos.py` 来源冲突 | 可复现性 |
| P1 | 板端部署补充 `yolov5.rknn` 获取/量化步骤与 Luckfox SDK 依赖说明 | 部署可复现性 |
| P2 | ARCHITECTURE §7 `edge_dist` → `edge_dist_m`；`check_people.py` 绝对路径改相对；`.gitignore` 处理 `results/diag` 遗留跟踪与 `*.html` 过宽 | 小修正 |

---

*报告完*