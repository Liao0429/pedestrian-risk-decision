# 板端集成审查报告：main.cc × enhanced_decision

> 审查方式：只读静态审查，未修改任何代码。
> 审查对象：
> - `src/luckfox_rtsp_yolov5/src/main.cc`（353 行）
> - `src/luckfox_rtsp_yolov5/include/enhanced_decision.h`（162 行）
> - `src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`（170 行）
> - 关联文件：`risk_engine.cc`、`risk_config.h`、`algorithms/{kalman_filter,graph_laplacian,risk_fsm}.h`、`Makefile`、`CMakeLists.txt`、`results/edge_board/EDGE_TEST_REPORT.md`
> 审查维度：内存资源管理（rknn_app_ctx 释放 / RTSP 清理 / 循环 leak）、单应矩阵标定、增强决策接入正确性、线程并发、崩溃恢复。

---

## 0. 结论摘要

代码结构清晰，`enhanced_decision` 模块本身（Kalman 多目标平滑 + 谱特征 + FSM）实现质量较高、无内存泄漏、单线程模型无数据竞争；`main.cc` 的主循环 VI/VENC 帧级资源也做到了“取帧-释放”配对。但存在 **5 个需要优先处理的问题**：

1. **[P0] 增强决策实际未随仓库构建启用**：`USE_ENHANCED_DECISION` 宏在 `Makefile` / `CMakeLists.txt` 中**从未定义**，`main.cc` 的增强决策分支是 `#ifdef` 死代码。默认 `make luckfox` 产物走的是 `#else` 经典 RiskEngine 路径，即使 `enhanced_decision.cc` 被编译链接，运行时也不会被调用。
2. **[P0] 崩溃恢复缺失 / 清理代码不可达**：`while(1)` 无 `break`、无信号处理；`main.cc:328-350` 的 `release_yolov5_model` / `rtsp_del_demo` / `RK_MPI_SYS_Exit` 等全部清理在正常运行时永远执行不到，进程被强杀后 RTSP 554 端口 / RKNN / ISP 硬件状态残留。
3. **[P0] 初始化返回值几乎全部未检查**：`init_yolov5_model`（114）、`init_post_process`（116）、`RK_MPI_MB_CreatePool`（132）、`GetMB`（136）、`vi_chn_init` / `venc_init` / ISP（174-178）均忽略返回值；模型缺失时 `main` 仍打印 “init success”，随后首帧推理崩溃。
4. **[P1] 单应矩阵硬编码**：H 由固定 4 点对写死（`main.cc:88-92`），未从标定文件加载；仓库已有 `ipm_calibrate.py` 但未接入，相机位姿变化需重新编译。且标定四边形顶部在 y=300，高于该线的脚点会映射到地面矩形外，产生无意义地面坐标。
5. **[P1] 板端“真机验证”只覆盖空场景**：`results/edge_board/enhanced_final_20261026.log` 573 条输出全部为 `level=SAFE n=0`，无行人、无 WARNING/DANGER 转换验证。

---

## 一、内存资源管理

### 1.1 rknn_app_ctx
- **配对存在**：`memset`（113）→ `init_yolov5_model`（114）→ `release_yolov5_model`（349）。但释放位于 `while(1)` 之后的不可达死代码，实际只有进程被外部终止时才可能走到（见第五章）。
- **返回值未检查**：`init_yolov5_model` 返回 `int`，`yolov5.cc:39-43` 在 `rknn_init` 失败时返回 `-1`，但 `main.cc:114` 直接忽略。模型路径 `./model/yolov5.rknn` 为相对 CWD 路径，一旦缺失，程序继续执行并在首帧 `inference_yolov5_model` 时访问空 RKNN 上下文崩溃。

### 1.2 RTSP 清理
- `create_rtsp_demo(554)`（167）与 `rtsp_new_session`（168）未判空；`create_rtsp_demo` 失败时对 NULL 调用后续 API 可能导致崩溃。
- `rtsp_del_demo(g_rtsplive)`（344）仅存在于死代码段；循环内推流失败（`g_rtsplive && g_rtsp_session` 判断存在，305）会静默跳过，但不会触发任何清理或重启。

### 1.3 循环内资源（逐帧）
- **无逐帧 leak**：VI `GetChnFrame`（187）/`ReleaseChnFrame`（316）配对；VENC `GetStream`（302）/`ReleaseStream`（320）配对；`stFrame.pstPack` 为循环外 `malloc` 一次（120），非每帧分配。`od_results.count` 在 `postprocess.cc:449` 每帧归零，`results` 数组不会累积越界。**结论：正常运行时主循环无内存泄漏。**
- **结构体未清零**：`stFrame`（119-120）与 `h264_frame`（139-146）仅部分字段初始化（未 `memset`）。`RK_MPI_VENC_GetStream` 失败时（302）仍会无条件调用 `ReleaseStream`（320），此时 `u32PackCount` 等字段为垃圾值，行为未定义。官方样例均为 `memset(&stStream,0,sizeof(stStream))`，此处缺失属健壮性缺陷。
- **每帧堆分配**：`letterbox()`（41-61）每帧新建 640×640×3 `cv::Mat`（约 1.2MB），长期运行在 256MB 设备上会造成堆抖动/碎片；可复用成员 Mat。非泄漏，低优先级。

### 1.4 内存池
- `src_Pool` / `src_Blk` 创建后未判空（132/136）；`ReleaseMB`（329）→ `DestroyPool`（331）顺序正确。同样受死代码问题影响。

---

## 二、单应矩阵标定

- **硬编码，未落地标定流程**：`main.cc:88-92` 用固定点对：
  - src：`(0,480),(720,480),(560,300),(160,300)`（画面底部梯形）
  - dst：`(-3,3),(3,3),(3,-1),(-3,-1)`（地面 6m×4m）
  仓库已提供 `algorithms/ipm_calibrate.py`，但 `main.cc` 未从标定文件/参数加载 H。相机高度/俯仰变化后，地面米制坐标整体失真，风险特征（density/r_gcc/谱）随之失真，且需重新编译。
- **标定区域外脚点无意义**：src 四边形顶部 y=300，即画面高度 62.5% 以上（更远处）的行人脚点经 H 映射后落到地面矩形外（y<-1），仍会进入 `groundPts` 参与建图，产生错误特征。建议对脚点做 ROI 有效性过滤。
- **一致性 OK**：增强路径 `mapToGroundPixel`（72-80）与经典路径 `RiskEngine::mapToGround`（`risk_engine.cc:43-52`）使用同一 H、数学公式一致，两路径互不打架。`w≈0` 时仅保持原值不置 NaN，行为可接受但未定义语义。
- **配置路径不一致**：`main.cc:97` 读取 `./config/default_config.json`（相对 CWD），而仓库实际配置文件在**顶层** `config/default_config.json`（不在 `luckfox_rtsp_yolov5/` 下），且无部署脚本将其拷贝到板端二进制旁。`loadRiskConfigOr` 静默回退默认值（`risk_config.h:139-145`）——用户自定义阈值在板端“静默失效”，难以察觉。

---

## 三、增强决策接入正确性

### 3.1 核心问题：宏未在构建中定义
- `grep -rn "USE_ENHANCED_DECISION"` 全仓库仅命中 `main.cc` 与一条 git commit message；`Makefile` 与 `CMakeLists.txt` 均无 `-DUSE_ENHANCED_DECISION`。
- `make luckfox`（`Makefile:74-88`）与 CMake `BUILD_LUCKFOX_DEMO`（`CMakeLists.txt:73-98`）都把 `enhanced_decision.cc` 编进产物，但 `main.cc` 的增强分支在 `#ifdef` 内（99-103、207-230、283-285），未定义宏时走 `#else`（231-248）经典路径。
- **后果**：从仓库全新构建的板端固件，增强决策是“编进二进制但永不执行”的孤儿代码。真机日志（`enhanced_final_20261026.log`）证明作者在板端**手动**加了宏才跑通，但该宏没有落进仓库构建脚本，**复现性断裂**。修复只需在 `Makefile` / `CMakeLists` 为板端目标统一加 `-DUSE_ENHANCED_DECISION`。

### 3.2 增强路径数据流正确
- 脚点提取（215-220）：框中心 x、底边 y → `mapCoordinates` 还原原图 → `mapToGroundPixel(H)` 地面米 → `processFrame`。与经典路径（234-241）坐标语义一致。
- `crowd = (CrowdLevel)er.level`（223）：`EnhancedResult.level` 为 0/1/2，`CrowdLevel` 枚举同为 0/1/2，强转安全。
- `enhancedCfg.edge_dist_m = riskCfg.edge_dist_m`（101）正确继承配置；`EnhancedDecision` 构造后按帧调用 `processFrame`，状态（tracks/FSM）跨帧保持，符合设计。
- FSM 使用 `steady_clock` 单调时间（`enhanced_decision.cc:25-28`），无系统时间跳变风险。

### 3.3 模块内部隐患
- **空输入清空全部轨迹**（`enhanced_decision.cc:58-62`）：检测器偶发漏检一帧 → 所有人轨迹被整体删除，而非 `miss++` 等待。漏检恢复后目标重新建轨迹，Kalman 平滑连续性被打断，`prevN_`/trend（159-161）可能跳变。建议空输入时保留轨迹并统一 `miss++`，而非清空。
- **关联门限中心未预测**（`enhanced_decision.cc:73-84`）：门限以 `kf.x()/y()`（上一帧校正值）为中心，未先 `predict()`，对快速移动目标门限滞后。此点在 `docs/review/kalman_tracking.md` 已提出，此处不再展开。
- **帧率参数与真机不符**：`EnhancedConfig.frame_dt` 默认 `1/30`（`enhanced_decision.h:85`），而真机实测仅 **6.9 FPS**（`EDGE_TEST_REPORT.md`），Kalman 的 F/Q 矩阵按 33ms 间隔构建、实际 145ms 才更新一次，速度估计系统性偏差。建议按实测帧率配置 `frame_dt`，或按帧间隔动态重建 F/Q。
- **每帧 printf**（`main.cc:228`）：真机 6.9FPS 下仍会持续刷串口、拖慢循环；建议降频（如每秒 1 次）或加打印开关。
- **默认构建的 `#else` 分支中 `featFiedler` 恒为 0**（206 初始化），但该分支 OSD（287）并不使用它，无实际影响。

---

## 四、线程并发

- **无并发问题**：主循环单线程，`enhancedDecision` 仅在该线程调用，`tracks_`/`fsm_` 无共享竞态；头文件亦声明单线程模型（`enhanced_decision.h:114`）。RKNN 推理、RTSP 推流、VI/VENC 均同步串行调用，无跨线程共享对象。
- **无 watchdog**：`RK_MPI_VI_GetChnFrame(...,-1)` 阻塞等待；若 ISP/VI 异常挂起，循环永久阻塞，无超时看门狗、无恢复线程。

---

## 五、崩溃恢复

- **清理代码不可达**：`while(1)`（182）无 `break`、无信号处理器；328-350 行（MB 释放、VI/VENC 销毁、ISP 停止、`rtsp_del_demo`、`RK_MPI_SYS_Exit`、`free(stFrame.pstPack)`、`release_yolov5_model`）在正常运行中**永远不会执行**。进程被 `SIGTERM/SIGKILL` 时跳过全部清理，RKNN 上下文、ISP、RTSP 554 端口残留，需外部看门狗/重启恢复。
- **无错误返回链**：`SAMPLE_COMM_ISP_Init/Run`（155-156）、`vi_chn_init`（174）、`venc_init`（178）均未检查；`RK_MPI_SYS_Init`（159）是唯一检查返回值的初始化。
- **启动副作用**：`system("RkLunch-stop.sh")`（84）返回值未检查；脚本失败不影响继续运行，但若 RkLunch 未停止，可能与后续 VI/VENC 抢占设备。

---

## 六、问题清单

| # | 严重级 | 位置 | 问题 |
|---|---|---|---|
| 1 | P0 | Makefile / CMakeLists.txt / main.cc:99,207 | `USE_ENHANCED_DECISION` 未定义，增强决策默认不生效，构建复现性断裂 |
| 2 | P0 | main.cc:182,328-350 | 无 break/信号处理，全部资源清理为不可达死代码，崩溃后资源残留 |
| 3 | P0 | main.cc:114,116,132,136,174-178 | 初始化/创建返回值未检查，失败后继续运行必崩 |
| 4 | P1 | main.cc:88-92 | 单应矩阵硬编码，未接 ipm_calibrate.py，相机位姿变化需重编译 |
| 5 | P1 | main.cc:97 / config 位置 | 配置文件相对路径与仓库顶层 config 目录不一致，静默回退默认值 |
| 6 | P1 | enhanced_decision.cc:58-62 | 空输入清空全部轨迹，破坏 Kalman 连续性 |
| 7 | P1 | enhanced_decision.h:85 | frame_dt=1/30 与真机 6.9FPS 不符，Kalman 参数失真 |
| 8 | P2 | main.cc:119-123,139-146 | stFrame/h264_frame 未 memset，GetStream 失败时 ReleaseStream 行为未定义 |
| 9 | P2 | main.cc:228 | 每帧 printf 拖慢真机循环 |
| 10 | P2 | results/edge_board | 真机验证日志仅空场景 SAFE(n=0)，缺 WARNING/DANGER 转换验证 |

---

## 七、修复建议（供后续实施，本次未改动）

1. **P0-1**：在 `Makefile` 的 `luckfox` 目标与 `CMakeLists.txt` 的 `BUILD_LUCKFOX_DEMO` 中统一追加 `-DUSE_ENHANCED_DECISION`，使增强决策成为默认板端路径；或删除 `#ifdef` 旁路，直接以增强决策为主、经典 RiskEngine 作为 `--legacy` 可选。
2. **P0-2**：安装 `SIGINT/SIGTERM` 信号处理器，置标志位使 `while(1)` 退出并执行既有清理段；将清理代码提取为 `cleanup()` 函数，保证正常/异常路径都可达。
3. **P0-3**：为 `init_yolov5_model` / `init_post_process` / `MB_CreatePool` / `GetMB` / `vi_chn_init` / `venc_init` / ISP 增加返回值检查，失败即打印并走 `cleanup()` 返回非零。
4. **P1-4**：将 H 从标定文件（或命令行参数）加载，而非硬编码；对标定四边形（y≤300）之外的脚点做 ROI 过滤。
5. **P1-5**：板端部署脚本将 `config/default_config.json` 拷贝到二进制 CWD 下，或把路径改为绝对路径。
6. **P1-6**：`smoothPoints` 空输入改为对既有轨迹 `miss++` 并允许超时删除，而不是整体清空。
7. **P1-7**：按真机实测帧率配置 `frame_dt`（如 1/6.9），或实现自适应 dt。
8. **P2**：`memset` stFrame/h264_frame；`GetStream` 失败时跳过 `ReleaseStream`；降频决策日志；补充带行人的真机 WARNING/DANGER 验证样本。
