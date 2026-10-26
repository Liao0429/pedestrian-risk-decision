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
