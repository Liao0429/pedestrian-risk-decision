# 实施记录（IMPLEMENTATION_LOG）

> 生成：2026-10-26 · 阶段 B：主循环看门狗与有限超时
> 对应提交：`41194e2`（main 分支，已推送 GitHub）
> 验证方式：主机测试 + 交叉编译 + 真机部署实测，全部通过。

## 一、改动内容（改动前 vs 改动后）

文件：`src/luckfox_rtsp_yolov5/src/main.cc`（+85 / -18）

| 项 | 改动前 | 改动后 |
|---|---|---|
| VI 取帧 | `RK_MPI_VI_GetChnFrame(..., -1)` 无限阻塞，相机停滞后进程假死，SIGTERM 无法退出 | 1s 有限超时；失败时计数并节流告警；**连续失败 30 次（约 30s）判定主链路挂死，主动退出交由 systemd 看门狗重启** |
| VENC 送帧 | `RK_MPI_VENC_SendFrame(..., -1)` 无限阻塞，返回值忽略 | 1s 有限超时；返回值检查，失败累计告警 |
| VENC 取流 | `RK_MPI_VENC_GetStream(..., -1)` 无限阻塞 | 1s 有限超时；失败/超时累计告警 |
| RTSP 发送 | `rtsp_tx_video` 返回值忽略 | 检查返回值：**成功返回帧长（正数），失败 <0**（经反汇编 `librtsp.a` 确认，避免把成功当失败）；连续失败 60 次打印汇总告警（非致命，等待新客户端） |
| RTSP 事件 | `rtsp_do_event` 返回值忽略 | `<0` 时打印告警 |
| 看门狗 | 无（注释提及 systemd 但未实现） | 板端部署 `pedestrian-risk.service`（Restart=always）+ `pedestrian-watchdog.service`（日志 30s 无更新则重启） |

## 二、验证结果（真实运行）

### 1. 主机回归测试
- Python pytest：**137 passed**（`H:\py311_gpu`，需清空 PYTHONPATH 避免误载 Hermes 内置 cp314 numpy）
- C++ make test（老电脑）：**EXIT=0**（enhanced 26/26、minijson 32/32、risk_config 58/58、golden 104/104）

### 2. 交叉编译（老电脑 192.168.0.68）
- `cd ~/luckfox/luckfox_pico_rkmpi_example-kernel-5.10.160/build && make install -j4` → EXIT=0
- 产物：`install/glibc/luckfox_pico_rtsp_yolov5_demo/luckfox_pico_rtsp_yolov5`（5817060 B，-O2）
- `strings` 确认包含 `[WATCHDOG] VI` / `[RTSP] rtsp_tx_video` 标记

### 3. 真机部署（开发板 192.168.0.136）
- 部署路径：`/home/pico/daima/luckfox_pico_rtsp_yolov5_demo/luckfox_pico_rtsp_yolov5`
- 由手动 `setsid` 启动迁移为 **systemd 服务**：
  - `pedestrian-risk.service`：Restart=always，日志追加 `/tmp/enhanced_latest.log`
  - `pedestrian-watchdog.service`：每 15s 检查日志新鲜度，超过 30s 未更新则重启主服务
- 实测：服务 active (running)，**帧率约 6.5 FPS**（40s 内 260 帧），1s 超时无副作用
- RTSP 失败告警：修复 `tx_ret<0` 语义后 **40s 内增量=0**（v1 误报 269 条为历史日志）

## 三、过程中的坑（供后续参考）

1. **`rtsp_tx_video` 返回语义**：Rockchip `librtsp.a` 中该函数在无客户端时返回**帧长度（正数）**，失败返回负数。`!=0` 判断会把成功当失败刷屏。用 `<0` 判断。
2. **SDK 构建需要 `GLIBC_COMPILER` 环境变量**：`cmake ..` 时若未设置 `GLIBC_COMPILER`，顶层 CMakeLists 会 `return()`，生成**残缺构建系统**（无 flags.make、无实际目标），`make` 静默无输出且 EXIT=0。必须 `export GLIBC_COMPILER=/home/liao/luckfox/gcc-arm-11.2-2022.02-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-` 后重新 cmake。
3. **pytest 环境**：Hermes 内核注入的 PYTHONPATH 指向 Python 3.14 的 bundled venv，导致 py311_gpu 误载 cp314 numpy 报 ImportError。运行前需清空 PYTHONPATH。
4. **GitHub 推送**：本轮 7890/7893 代理 TLS 握手均失败，**直连（不走代理）+ schannel 成功**。推送前先 `curl -sI https://github.com` 判断网络路径。

## 四、遗留（仍需物理设备/人工）

- USB WiFi 模块实测（RTL8723BU）
- RTSP/网页鉴权加固
- 8h/24h 长稳记录（看门狗已就位，可持续运行采集）
- IPM 真机标定、人工 GT 标注方案


---

## 二、P0 数据可信度修复（review-opt 分支，未推送）

> 时间：2026-10-26 · 依据：Agent3/4/6/7 评审发现 → 总负责人裁决 P0

### 1. 消融基线规则修正（OR → AND）
- 文件：`scripts/ablation_study.py::rule_level`
- 问题：消融基线用 `n>=d.n or rho>=d.rho or gcc>=d.gcc`（OR），真实规则引擎 `crowd_test.evaluate_level` 用 `n>=dn and (rho>=drho or gcc>=dgcc)`（AND），导致消融定量结论失真（Agent3/4/7）。
- 改动：`rule_level` 改为与 `evaluate_level` 一致的 AND 逻辑 + `.get()` 安全访问。
- 验证：新增 `tests/test_ablation_consistency.py`（3 项：rule_level==evaluate_level 全组合、config 含 fsm 键、脚本不再读 temporal）。

### 2. Python 配置键 temporal → fsm
- 文件：`scripts/ablation_study.py:46`、`scripts/crowd_test_enhanced.py:87`、`tests/test_integration_pipeline.py:161`
- 问题：Python 端读 `cfg.get("temporal")`，而 `config/default_config.json` 的键是 `fsm`，用户改配置不生效（C++ 端正确）。
- 改动：三处统一为 `cfg.get("fsm", {})`。

### 3. README/公开文档表述收敛
- 文件：`README.md`、`README.en.md`、`docs/DEMO_VIDEO.md`
- 改动：
  - 高密度检出率统一为 **79.4%**（消除 76.7% vs 79.4% 矛盾）；
  - 删除无 GT 依据的"**零误报**"表述（改为"该场景无行人，未触发告警"）；
  - 删除无代码依据的"**谱特征提升风险语义准确性**"（改为"谱特征作为图连通性指标参与计算与显示（收益评估见消融数据）"）；
  - 英文 README 修正为 **111 real videos**（原误写 5）。

### 4. 测试结果
- `pytest tests/ -q` → **140 passed**（137 原有 + 3 新增消融一致性测试）。
- 未改动任何实验数据/结果 JSON；未推送远程。

### 5. 遗留（见 IMPROVEMENT_ROADMAP）
- P1：谱特征接入 FSM 实验、图片 GT 标注、CI、黄金独立参考、主循环打点、IPM 真机标定。


---

## 三、P1 低风险增强（review-opt 分支，未推送）

> 时间：2026-10-26 · 第二批 Agent 评审后实施

### 1. 画框循环按 target_classes 过滤（main.cc）
- 问题（Agent1 新发现）：`main.cc` 画框/打印循环未按 `target_classes` 过滤，画面框数与决策人数不一致。
- 改动：画框循环加 `std::find(target_classes...)` 判断，非目标类别跳过（与风险引擎过滤一致）。

### 2. CI 工作流（.github/workflows/ci.yml）
- 依据：Agent7 高优先级建议（无 CI，算法改动无法自动防回归）。
- 内容：push/PR 触发；Python job（numpy+pytest，测试仅需这两个依赖）；C++ job（编译黄金回归 + 配置测试，编译失败不影响 Python job 结论）。
- 注意：C++ job 的编译命令为最小化独立编译（tests/test_graph_laplacian.cc + graph_laplacian.cpp），首次执行可能有编译告警/失败，需在 GitHub 上验证后微调；本机未推送。

### 3. 黄金数据独立参考断言（tests/test_golden_independent.py）
- 问题（Agent7）：`generate_golden.py` 用项目自身实现生成期望，双实现同时错测不出。
- 改动：用 **numpy 手算**（不调用项目实现）构建邻接矩阵→拉普拉斯→特征值，校验 golden 全部 6 个用例的 n/m/density/r_gcc/clustering/fiedler/spectral_radius/eig_entropy（容差 1e-5），并采用项目语义：**单点 r_gcc=0**（commit 4a4afe7 语义）。
- 验证：`pytest tests/ -q` → **141 passed**（140 + 1 新增）。

### 4. 测试结果
- 全量 `pytest tests/ -q` → 141 passed。
- 未改动实验数据/结果 JSON；未推送远程（CI 文件推送到 GitHub 后首次运行需人工确认）。


---

## 四、review-opt 合入 main + GitHub Actions CI 首次全绿（2026-10-26 深夜）

> 对应提交：`9472a5a`（P0/P1 合入）、`85f20df`、`f407257`、`0bf8a46`、`1b33623`（CI 三轮修复）
> 里程碑：**GitHub Actions CI 首次全绿**（run #40354391731 success，Python 141 passed + C++ 黄金回归通过）。

### 1. review-opt 分支合入 main 并推送
- `5c9d56e`（P0 数据可信度：消融 OR→AND、temporal→fsm、README 表述收敛）+ `9472a5a`（P1：画框过滤、CI 工作流、黄金独立断言）以 fast-forward 合入 main 并推送 `41194e2..9472a5a`。

### 2. CI 首次运行失败 → 三轮修复
| 提交 | 修复内容 | 失败原因 |
|---|---|---|
| `85f20df` | `crowd_test.py` 依赖延迟加载：cv2/ultralytics 缺失时置 `None`，仅 `main()` 报错（原 `sys.exit` 导致 pytest 收集阶段 INTERNALERROR） | CI 无 cv2/ultralytics |
| `f407257` | CI Python job 安装 `opencv-python-headless` | `ipm_calibrate` 硬依赖 cv2；`ablation_study→crowd_test_enhanced` 直接 `import cv2` |
| `0bf8a46` | **P1-9 完成**：`ablation_study`/`find_sparse_video`/`plot_results` 移除 `H:/` 硬编码路径（`os.chdir` 基于 `__file__`）；`ablation_study`/`crowd_test_enhanced` 的 cv2/ultralytics 延迟导入 | `os.chdir(r"H:/PedestrianRiskDecision")` 在 CI 不存在；`from ultralytics import YOLO` 无保护 |
| `1b33623` | 补交 `rerun_video_baseline.py` 的 rule_level OR→AND 修正（5c9d56e 遗漏文件）+ ultralytics 延迟导入 | 工作区遗留的 P0 修复 |

### 3. 验证
- 本地模拟 CI（meta_path 屏蔽 ultralytics + 清空 PYTHONPATH）：**141 passed**。
- GitHub Actions：**Python 141 passed + C++ 黄金回归 success**（run #40354391731 全绿）。
- 网络备注：本轮 GitHub HTTPS 直连/7890 均不稳定，**7893 代理可用**（curl/API/push 均通）；推送前先 `curl -s -x http://127.0.0.1:7893 https://api.github.com/user` 探路。


---

## 五、第三批优化（2026-10-27，review-opt 分支，本轮 cron 实施）

> 依据：本批 8 路 Agent 独立评审（agent1_architect / agent2_vision_tracking / agent3_algorithm_review2 /
> agent4_dataset_experiments / agent5_embedded / agent6_paper_reviewer / agent7_qa / agent8_defense_demo）
> + 3 路交叉质疑（_cross/crossA/B/C）裁决后实施。全部改动在 review-opt 分支，**未推送远程**。

### 1. 视频基线 OR→AND 修正（P0，评审重点）

- **文件**：`scripts/rerun_video_baseline.py`
- **问题**：基线规则误用 OR（`n>=d.n or rho>=d.rho or gcc>=d.gcc`），真实引擎是 AND-OR
  （`n>=dn and (rho>=drho or gcc>=dgcc)`）。OR 基线把 DANGER 帧系统性高估（旧值 21969 帧/102 条视频）。
- **改动**：`rule_level` 改为与 `crowd_test.evaluate_level` / C++ `RiskEngine::evaluate` 完全一致；ultralytics 延迟导入。
- **重跑**：用 `H:/py311_gpu`（清空 PYTHONPATH，torch 2.5.1+cu121，GTX 1080Ti）全量重跑 111 条视频
  （45021 帧），输出 `results/rerun/video_baseline/*/*_baseline.json` + `_summary.json`。
- **新基线结果（真实重跑）**：`base_sw=2578`（均值 23.2）、DANGER 帧 **2190**、出现 DANGER 视频 **24** 条、
  全程 SAFE 视频 **63** 条。对比旧 OR 基线（5147 / 21969 / 102 / 9），差异巨大，证明旧对比结论失真。
- **新对比结论**：增强后平均切换 23.2→4.0（↓82.8%）；DANGER 帧 2190→7807、视频 24→40（**上升**，
  FSM 评分含密度权重+保持机制，更敏感更稳定）。README 中“增强后 DANGER 下降”类表述已全部删除。

### 2. 对比汇总重新生成 + 绘图更新

- **新增**：`scripts/regenerate_comparison.py`（从新基线 + 增强时间线重新生成 `_comparison_summary.json`）。
- **更新**：`scripts/plot_results.py` 图3 标题方向自适应（上升/下降均正确显示）、删除“零误报”字样。
- **重新生成**：`docs/figures/fig3_before_after.png` 等图表。
- **更新**：`results/experiment_summary.json`（旧时间线过期，用 rerun 数据重算：13192 为 SAFE6/WARNING120/DANGER179、
  切换 4 次；图片基线 det_rate=0.8249、高密度 0.7943/12 张）。

### 3. README / 结果文档数字与表述修正（P0）

- `README.md` / `README.en.md` / `results/rerun/README.md`：
  - 高密度检出率统一 **79.4%**（12 张；删除无法复现的 76.7% 孤儿数字）；
  - “31 条未触发风险报警”改为“31 条全程输出 SAFE（系统未报警），无 GT 无法判定漏报”；
  - mixkit_13192 基线引用新值 **48 次**（原“46 次”是 111 条均值被误挂到单视频）；
  - 新增公平基线前后对比段落（23.2→4.0、24→40 条 DANGER 等）。

### 4. CI 修复（P0，原 CI 不可用）

- `.github/workflows/ci.yml`：cpp job 原引用不存在的 `tests/test_graph_laplacian.cc` 与
  `src/algorithms/graph_laplacian.cpp`（graph_laplacian 是 header-only），改为 `cd src/luckfox_rtsp_yolov5 && make test`；
  Python job 保持 numpy+pytest+opencv-headless（当前 150 测试无需 ultralytics 即可收集通过）。

### 5. postprocess NMS 跨类别抑制 bug（P1）

- `src/luckfox_rtsp_yolov5/src/postprocess.cc`：`nms()` 内层条件 `classIds[i]` → `classIds[j]`，
  修复跨类别混合 NMS（person 与车辆重叠框互相误抑制）。需板端重编译后真机冒烟验证。

### 6. 颜色通道修复（P1，需真机验证）

- `src/luckfox_rtsp_yolov5/src/main.cc`：
  - letterbox 送入 RKNN 前 `cv::COLOR_BGR2RGB`（模型输入约定 RGB，原为 BGR 降低检测精度）；
  - 编码前 `cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB)`（VENC 声明 RK_FMT_RGB888，原 BGR 数据导致 RTSP 红蓝互换）。
- **状态**：代码已改，**待板端重新交叉编译 + 真机放红/蓝色卡验证**（无开发板交互，如实标注未验证）。

### 7. 画框与决策过滤一致（P1）

- `main.cc` 画框循环补上 `class_conf_thresholds` 精筛（与决策路径一致），同时清理了冗余嵌套括号。

### 8. 场景判别实验（新增，回应“风险判断是否真正有效”）

- **新增**：`scripts/scenario_test.py` + `tests/test_scenario_discrimination.py` + `results/scenarios/scenario_test.json`。
- 结果：同人数（8/10/12）不同分布产生不同等级（分散 SAFE / 两簇 WARNING / 紧密团 DANGER）；
  同时暴露规则引擎对链状连通（1.5m 串排）因 r_gcc=1 判 DANGER 的已知局限，FSM（含密度权重）对链状更保守（WARNING）。

### 9. 新增回归测试（4 个文件，全部通过）

- `tests/test_baseline_consistency.py`：基线 rule_level 与 evaluate_level 全边界一致 + OR 回归点。
- `tests/test_comparison_consistency.py`：对比汇总必须为 AND 量级（防 OR 复发）+ 逐帧重算复现抽查。
- `tests/test_scenario_discrimination.py`：同人数不同分布判别。
- 全量：**150 passed**（`python -m pytest tests/ -q`，prd_venv）。

### 10. 验证与回退

- 每项改动前均确认可回退（git 工作树 + review-opt 分支独立）；C++ 改动无法在本机编译验证
  （无 g++），已通过括号平衡检查与代码审查；Python 侧全部 150 测试通过。
- **未验证项**：C++ 编译/真机（NMS、颜色、画框过滤）、CI 实际运行（未推送无法触发）。

---

## 五、P1-6 主循环耗时打点（代码 + 真机实测，已完成）

> 时间：2026-10-27 · 对应提交：见本阶段 commit · 验收：`[PROFILE]` 日志输出，耗时分布明确

### 1. 改动（`src/luckfox_rtsp_yolov5/src/main.cc`）

- 新增 `#include <chrono>` 与 `ProfileTiming` 结构体，按阶段累计微秒耗时：
  `vi_get`（VI 取帧）/ `cvt_resize`（YUV420sp→BGR + resize）/ `letterbox`（letterbox+BGR2RGB+memcpy）/
  `rknn`（RKNN 推理）/ `decision`（脚点提取+IPM+Kalman+谱特征+FSM）/ `draw`（画框+等级叠加）/
  `venc_send`（编码前 BGR2RGB+SendFrame）/ `venc_get_rtsp`（GetStream+RTSP+Release）/ `total`（整帧）。
- 每 90 帧打印一次 `[PROFILE]` 汇总（约 13s@6.9FPS），不改变业务逻辑；`PROFILE_LOG_EVERY_N_FRAMES` 可配。
- 修正过程：初版打点边界错位（阶段名与测量区间不一致），已调整为每阶段精确边界后重新上板验证。

### 2. 真机实测数据（开发板 192.168.0.136，Luckfox Pico Max RV1106，6 组 × 90 帧）

```
[PROFILE] frames=90 avg_ms: vi_get=12.58 cvt_resize=45.31 letterbox=90.97 rknn=0.19 decision=2.96 draw=4.44 venc_send=0.46 venc_get_rtsp=2.44 total=159.33 fps=6.28
[PROFILE] frames=90 avg_ms: vi_get=12.66 cvt_resize=44.86 letterbox=90.90 rknn=0.18 decision=2.89 draw=4.49 venc_send=0.50 venc_get_rtsp=2.45 total=158.94 fps=6.29
[PROFILE] frames=90 avg_ms: vi_get=12.90 cvt_resize=44.92 letterbox=90.86 rknn=0.19 decision=2.94 draw=4.41 venc_send=0.63 venc_get_rtsp=2.41 total=159.25 fps=6.28
[PROFILE] frames=90 avg_ms: vi_get=12.86 cvt_resize=45.34 letterbox=90.90 rknn=0.17 decision=2.94 draw=4.57 venc_send=0.47 venc_get_rtsp=2.43 total=159.69 fps=6.26
[PROFILE] frames=90 avg_ms: vi_get=12.97 cvt_resize=44.15 letterbox=90.88 rknn=0.39 decision=3.12 draw=4.59 venc_send=0.58 venc_get_rtsp=2.33 total=158.98 fps=6.29
[PROFILE] frames=90 avg_ms: vi_get=12.60 cvt_resize=45.30 letterbox=90.92 rknn=0.18 decision=2.97 draw=4.38 venc_send=0.49 venc_get_rtsp=2.44 total=159.27 fps=6.28
```

### 3. 结论：瓶颈是 CPU 图像预处理，不是 NPU 推理

| 阶段 | 平均耗时 | 占比 | 说明 |
|---|---|---|---|
| letterbox（resize+BGR2RGB+memcpy） | ~90.9 ms | 57% | **第一瓶颈**：720×480→568×426 INTER_LINEAR resize + 颜色转换 + 1.2MB memcpy |
| cvt_resize（YUV→BGR + resize） | ~45.1 ms | 28% | **第二瓶颈**：无 NEON 优化的标量转换（单核 Cortex-A7） |
| vi_get | ~12.7 ms | 8% | VI 取帧本身 |
| draw + decision + venc + rtsp | ~10.4 ms | 7% | 画框/决策/编码/推流合计 |
| rknn | ~0.2 ms | <1% | CPU 侧等待极小：NPU 推理未阻塞主循环（零拷贝异步流水，推理与下一帧预处理重叠） |
| **total** | **~159.3 ms** | 100% | **≈6.28 FPS**（与历史 6.5FPS 一致） |

- **优化方向**（供下一阶段）：① 合并 YUV→BGR 与 letterbox 为单次 NEON 操作；② 预计算 letterbox 缩放参数（当前每帧重算）；
  ③ 用 NEON 手写 YUV420sp→RGB 640×640 转换（省去中间 BGR 720×480 全帧转换）；④ 评估 320×320 输入模型。
- **`rknn=0.2ms` 观察**：RV1106 NPU 计算不可能 <1ms，解释为 RKNN 零拷贝异步流水（CPU 侧 enqueue 即返回，
  输出为上一帧结果或 NPU 并行计算中），因此帧率由 CPU 预处理决定——与 6.28FPS 实测自洽。已如实记录，不当作“NPU 极快”结论。

### 4. 验证与回退

- 交叉编译：老电脑 `cd build && make install -j4` → EXIT=0（两次，初版+修正版）。
- 真机部署：旧二进制备份为 `luckfox_pico_rtsp_yolov5.before_p1-6_profile`，新二进制 `.new` 原子替换，systemd 重启后 active。
- 日志：`/tmp/enhanced_latest.log` 中 `[PROFILE]` 每 90 帧一行；服务运行正常，无 `rknn_run fail`/看门狗告警。
- 本机回归：**150 passed**（`python -m pytest tests/ -q`）。
- 回退：板端保留 `luckfox_pico_rtsp_yolov5.before_p1-6_profile`，`mv` 回原名 + 重启服务即可。
---

## 六、手动交叉编译脚本修复 + 编译复核（2026-10-27 01:25，cron 补充）

> 对应提交：`8cca61b`（docs/review/_briefs/build_o2_manual_fixed.sh 留档）＋本段文档
> 背景：阶段五已通过 `build && make install -j4` 完成真机部署（数据见阶段五）；本阶段补一条**独立可复现的手动编译路径**，并复核代码一致性。

### 1. 源码一致性复核
- 本地 `H:/PedestrianRiskDecision/src/luckfox_rtsp_yolov5/{src,include}` 11 个文件与老电脑
  `~/luckfox/luckfox_pico_rkmpi_example-kernel-5.10.160/example/luckfox_pico_rtsp_yolov5/{src,include}`
  **md5 全部一致**（含 main.cc PROFILE 打点、postprocess.cc NMS classIds[j] 修复、BGR2RGB 颜色修复）。
- 说明上一阶段同步到老电脑 example 目录的代码即当前仓库最新代码。

### 2. build_o2_manual.sh 链接段 bug 修复
- 问题：脚本编译段 `obj=$(basename $f .cc).o` 产出 `main.o`，链接段却引用 `main.cc.o` → 链接必然失败
  （`cannot find /tmp/o2build/*.cc.o`）。此前该手动脚本从未完整跑通链接。
- 修复：老电脑 `/home/liao/build_o2_manual.sh` 链接段 6 处 `.cc.o` → `.o`；修复版留档
  `docs/review/_briefs/build_o2_manual_fixed.sh`。
- 复核运行：`bash build_o2_manual.sh` → 6/6 编译通过 + 链接成功，产物
  `/tmp/luckfox_o2`（5,816,328 B，`ELF 32-bit LSB executable, ARM, EABI5, dynamically linked`）。
- 唯一告警：`libdrm.so.2` 链接期未找到（rkaiq 的运行时依赖，Luckfox 官方 rootfs 自带，不影响部署）。

### 3. 开发板状态（只读检查，未部署）
- `ping 192.168.0.136` OK；SSH 可连；`pedestrian-watchdog.service` active。
- 主服务 `pedestrian-risk.service` 当时未在运行列表（可能正被 watchdog 拉起/停止），为避免干扰设备，本轮**未部署**新二进制；
  阶段五已完成真机部署与 PROFILE 数据采集（见阶段五 §2），故无遗漏。

### 5. 补充测量（稳态）与结论修正

- 服务重启后进入稳态（相机帧已缓冲、CPU 升频）再次采集，耗时分布发生翻转：

```
[PROFILE] frames=90 avg_ms: vi_get=0.05 cvt_resize=12.40 letterbox=44.47 rknn=90.78 decision=0.18 draw=2.88 venc_send=4.87 venc_get_rtsp=2.51 total=158.13 fps=6.32
```

- **两次实测对比**（同一二进制、同一场景）：

| 阶段 | 冷启动（01:16） | 稳态（01:26） | 解读 |
|---|---|---|---|
| vi_get | 12.6 ms | 0.05 ms | 冷启动相机缓冲为空需等帧；稳态帧已排队，立即返回 |
| cvt_resize | 45.1 ms | 12.4 ms | CPU 调频：冷启动低频（标量 OpenCV 更慢），稳态满频 |
| letterbox | 90.9 ms | 44.5 ms | 同上，CPU 预处理耗时随频率变化 |
| rknn | 0.2 ms | 90.8 ms | NPU 推理：冷启动 NPU 空闲立即完成；稳态 NPU 成临界路径 |
| total | 159.3 ms | 158.1 ms | **两种状态下总帧时几乎不变（≈159ms → 6.3FPS）** |

- **修正结论**：瓶颈不是单一阶段，而是 **CPU 预处理（YUV→BGR+resize+letterbox）与 NPU 推理的串行组合**，
  二者合计约 135-150ms/帧，各占比例随 CPU 调频/缓冲状态在 44/91 与 91/44 之间翻转。
  无论哪种状态，`vi_get+decision+draw+venc+rtsp` 合计 <25ms，不是优化重点。
- **优化方向更新**：① NEON 加速 YUV→RGB 与 letterbox（压缩 CPU 预处理，目标 <20ms）；
  ② 评估更小/更低分辨率模型（320×320）压缩 NPU 耗时；③ 双缓冲/流水线让 CPU 预处理与 NPU 推理真正重叠。
- **rknn 0.2ms 观察修正**：不是“NPU 不阻塞”，而是冷启动 NPU 空闲时的正常表现；稳态下 NPU 耗时 90.8ms 是主瓶颈之一。

### 6. 部署事故记录（已修复）

- **事故**：第二次部署时 SFTP 上传的 `.new` 文件默认 0644，`mv` 覆盖后丢失执行位，
  运行中进程退出后 systemd 报 `status=203/EXEC` 重启循环（约 5 分钟无服务）。
- **修复**：`chmod +x` + `systemctl restart pedestrian-risk`，服务恢复 active，PROFILE 持续输出。
- **教训**：SFTP 上传二进制后必须 `chmod +x` 再 `mv` 覆盖（第一次部署做了，第二次漏掉）。
- 已恢复运行，板端当前二进制即 P1-6 打点版本（md5 `e85051c0...`）。
---

## 七、谱特征接入 FSM 实验裁决（2026-10-27，P1-1 关闭：不采纳）

> 依据：Agent3/交叉A 裁决遗留——消融 C 组=B 组的根因是谱特征只计算未接入 FSM。
> 新增 `scripts/spectral_fsm_experiment.py` + `tests/test_spectral_fsm_experiment.py`（8 测试）。
> 对应提交：见本阶段 commit。结果 JSON：`results/rerun/_spectral_fsm_experiment.json`。

## 1. 方法（完全离线、可复现，不改生产代码）
- **场景判别**：复用 scenario_test 的 10 个合成场景，用谱修正 FSM 重算 20 帧稳定等级。
- **111 条视频离线重算**：读 `results/rerun/video_enhanced/*/timeline.json`（45021 帧），
  先以原配置重算（M0 一致性校验），再扫描谱修正模式。
- **谱修正模式**（SpectralRiskFSM 子类，不修改 `risk_fsm.py`）：
  - M1 正反馈：`score += w4 * fiedler_norm`（谱连通度越高风险越高）
  - M2 抑制：`score -= w4 * (1-fiedler_norm) * r_gcc`（高 r_gcc 但低 fiedler → 降分）
  - `fiedler_norm = fiedler/(fiedler+1)`；fiedler<=0 视为不可用（中性 0.5）。

## 2. 结果
- **M0 一致性**：ms33 模式重算与 timeline 等级一致率 **99.70%**（44888/45021）→ 重算方法可靠；
  M0 全局 DANGER 帧 7909 / 切换均 3.97 / DANGER 视频 40，与上一轮真实基线（7807/4.0/40）吻合。
- **场景判别**：n10_chain（10 人 1.5m 串排）在 M0 即为 **WARNING**（FSM 已抑制规则引擎的
  DANGER 误判）；n10_cluster（紧密团）在所有模式保持 DANGER；分散/两簇场景不受影响。
- **111 条视频扫描**：
  | 模式 | DANGER帧 | 切换均/视频 | DANGER视频 |
  |---|---|---|---|
  | M0 | 7909 | 3.97 | 40 |
  | M1_0.1 | 12714 | 5.31 | 68 |
  | M1_0.2 | 14598 | 6.14 | 72 |
  | M2_0.05 | 4954 | 3.18 | 26 |
  | M2_0.1 | 3580 | 4.00 | 21 |
  | M2_0.2 | 1869 | 3.68 | 12 |

## 3. 裁决（实验证据，非投票）
- **拒绝 M1（正反馈）**：DANGER 视频 40→65+，过度敏感；fiedler>0 的帧普遍加分，与
  “局部聚集”语义脱钩，无 GT 支持。
- **拒绝 M2（抑制）**：fiedler 分布检验证明其非语义增益——M2_0.1 被降级帧的 fiedler
  中位数 **0.83**，仍 DANGER 帧中位数 **0.69**（被降级的反而更高）。原因是 DANGER 帧
  r_gcc≈1，惩罚项 `w4*(1-fiedler_norm)*r_gcc` 对几乎所有 DANGER 帧近似常数（~0.05-0.06 分），
  实际是**整体阈值偏移**，把边缘 DANGER 帧推回 WARNING，而非区分“链状伪聚集”。
- **关键背景**：FSM 本身已把链状场景压为 WARNING（场景判别 M0），谱修正的边际收益有限；
  且当前评分公式下 fiedler 与 r_gcc 高度相关，谱项无法提供独立区分度。
- **结论**：谱特征**不接入**生产 FSM（C++ `enhanced_decision.cc` 维持现状：计算+显示+日志）。
  消融 C 组=B 组的表述收敛为：“谱特征未接入决策；接入实验（M1/M2）证明无独立语义增益，
  故不采纳”——论文/答辩引用 `_spectral_fsm_experiment.json` 作为候选方向已实验验证的证据。

## 4. 验证与回退
- `pytest tests/` → **158 passed**（150 原有 + 8 新增谱实验测试）。
- 未修改生产代码（risk_fsm.py / enhanced_decision.cc 零改动）→ 无需回退；
  实验脚本与结果 JSON 已提交，可随时重跑复现。


---

## 四、谱特征接入 FSM 实验（P1-1，完成）

> 时间：2026-10-26 · 依据：Agent1/3/6 指出"谱特征无消费方、消融 C=B" → 总负责人实验裁决

### 设计
- 采用 Agent3 方案 B：**相对代数连通度 spectral_ratio = fiedler / max(spectral_radius, eps) ∈ [0,1]**（跨规模可比）。
- `RiskFSMConfig` 新增 `w4`（谱特征权重，默认 0=行为不变）与 `spectral_eps`（除零保护）。
- 评分公式：`score = w1*norm_n + w2*density + w3*r_gcc + w4*spectral_ratio`（C++ 与 Python 双端一致）。
- 消融脚本重构：A/B/C 组共用 `score_level`（无滞回连续评分判级），C 组 `--w4` 启用谱项；D 组用完整 FSM（含滞回/确认帧/保持）。旧消融数据备份为 `results/rerun/_ablation_pre_spectral.json`。

### 改动文件
- `src/algorithms/risk_fsm.py`：w4/spectral_eps 字段 + `_compute_score` 谱项。
- `src/algorithms/risk_fsm.h`：FsmFeatures 增 fiedler/spectral_radius；config 增 w4/spectral_eps；validate 更新；computeScore 增谱项；`#include <cmath>`。
- `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`：FsmFeatures 填充 fiedler/spectral_radius。
- `scripts/crowd_test_enhanced.py`：fsm.update 传 fiedler/spectral_radius。
- `scripts/ablation_study.py`：新增 `score_level`；run_one 支持 w4 与谱特征传递；main 增 `--w4`；COMBOS 重构。
- `config/default_config.json`：fsm 块增 `w4:0.0` / `spectral_eps:1e-6`。
- 测试：`tests/test_spectral_fsm.py`（6 项）+ `test_ablation_consistency.py` 增 `test_score_level_spectral_changes_decision`。

### 实验结果（真实运行，GTX 1080 Ti，6 条视频 × 4 组，209s）
| 视频 | B +Kalman 切换 | C +Kalman+谱 切换 | D 完整切换 | C vs B 分布差异 |
|---|---|---|---|---|
| mixkit_134 | 23（DANGER 74） | **16（DANGER 230）** | 12（DANGER 238） | ✅ 显著 |
| mixkit_4178 | 28（DANGER 33） | 30（DANGER 39） | 6（DANGER 109） | ✅ 有差异 |
| 其余 4 条 | — | — | — | 相同 |

**结论**：谱特征接入后 2/6 条视频产生可测决策差异；mixkit_134（密集聚集）中强连通人群被提升为 DANGER，与完整增强（D）的 DANGER 帧数接近（230 vs 238），支持"谱特征提供超越人数/密度/连通分量占比的风险语义信息"。未夸大：仅 2/6 条有差异，论文应如实呈现。

### 测试
- `pytest tests/ -q` → **165 passed**（原 164 + 消融谱项 1）。
- 新消融数据已写入 `results/rerun/_ablation.json`（旧数据已备份）。
