# Agent5 · C++ 与嵌入式部署专家 · 审查报告

> 生成：2026-10-26 · 审查分支：`review-opt` @ `9472a5a`（工作区干净，仅两个未跟踪 docs 文件）
> 审查范围：`src/luckfox_rtsp_yolov5/`（CMakeLists.txt / Makefile / src/*.cc / include/*.h）、`src/algorithms/*.h`、`scripts/start_demo.sh`、`config/default_config.json`、`.github/workflows/ci.yml`、`docs/review/BASELINE_RESULTS.md`、`docs/review/IMPLEMENTATION_LOG.md`、`docs/ARCHITECTURE.md`、`README.md`
> 方法：全量源码静态审查 + git 历史核对 + 已有真机记录（BASELINE_RESULTS / IMPLEMENTATION_LOG）交叉验证。
> **未验证项声明**：本机无 Luckfox SDK，无法真实交叉编译/真机复测。所有"板端行为"结论基于源码静态分析与既有真机记录，凡需真机确认的发现已明确标注"待真机验证"。未修改任何源码。

---

## 一、C++ 主程序集成度（审查问题 1）

**结论：链路已真正串通。** `main.cc` 单线程主循环完整串联：

```
VI 取帧(YUV420SP, 1s 超时) → cvtColor 转 BGR → letterbox 640×640
→ RKNN YOLOv5 推理 → NMS 后处理 → target_classes 过滤(person)
→ 脚点 mapCoordinates + IPM 单应(mapToGround) → EnhancedDecision(Kalman+谱特征+FSM)
→ 画框 + CROWD 风险条叠加 → VENC H.264 → RTSP 推流
```

证据：
- 主循环：`main.cc:311-509`，`RK_MPI_VI_GetChnFrame`(:316) → `inference_yolov5_model`(:331) → `enhancedDecision.processFrame`(:356) → `RK_MPI_VENC_SendFrame`(:451) → `rtsp_tx_video`(:469)。
- 增强决策进入实时主循环并输出日志 `[EnhancedDecision] level=... n=... rho=...`（`main.cc:362-363`）与画面叠加（`main.cc:412-423`）。
- 画框/日志循环已按 `target_classes` 过滤（`main.cc:390-393`，commit `9472a5a` 已生效，磁盘内容已核实），与决策人数一致。

**集成度方面的剩余问题：**
1. **决策结果无外部告警通道**：风险等级只体现在视频叠加和 stdout 日志（`main.cc:362-363, 412-423`），无 GPIO/串口/MQTT/HTTP 输出。若无人观看 RTSP，WARNING/DANGER 不产生任何可触达的告警。对"风险决策系统"定位是功能缺口（Medium）。
2. **画框过滤与决策过滤不完全一致**：决策路径额外按 `class_conf_thresholds` 精筛（`main.cc:346-348`），画框循环只按 `target_classes`（`main.cc:390-391`）。若配置了差异化置信度（如 `{"0": 0.5}`），低于阈值的检测框仍会画出但未计入决策人数，画面与日志人数仍可能不一致（Medium/Low）。
3. **经典 RiskEngine 路径默认不运行**：`enable_risk_engine_fallback=false`（`default_config.json:19`），`main.cc:369` 整段旁路不执行；`risk_engine.cc` 仍编译进产物，增加二进制体积，功能冗余（Low）。

---

## 二、交叉编译 / SDK / 部署（审查问题 2）

### 2.1 仓库无法独立交叉编译 Luckfox 目标 —— 【高】

- 仓库内**不存在** SDK 头文件：全仓库检索无 `yolov5.h`、`sample_comm.h`、`rtsp_demo.h`、`rknn_api.h`（`yolov5.cc:21` include `"yolov5.h"`，`luckfox_mpi.cc:19` include `"sample_comm.h"`，`main.cc:17` include `"rtsp_demo.h"`）。
- `CMakeLists.txt:83-111` 与 `Makefile:83-97` 的 luckfox 目标假设 `LUCKFOX_SDK_ROOT/include`、`/3rdparty`、`/lib` 布局且库名 `rknnmrt rtsp_demo rkaiq opencv_core opencv_imgproc opencv_imgcodecs` 直接可链；但实际构建从未走这两个入口——`IMPLEMENTATION_LOG.md` 记录的交叉编译是在 SDK 的 example 构建系统里完成的（`cd ~/luckfox/luckfox_pico_rkmpi_example-kernel-5.10.160/build && make install -j4`）。
- `README.md:131` 声称 `make luckfox LUCKFOX_SDK_ROOT=<SDK路径>` 即可交叉编译，与事实不符：该目标缺少 `yolov5.h` 等头文件来源，且 `-I$(OPENCV_ROOT)/include/opencv4` 在 `OPENCV_ROOT` 为空时变成无意义的 `-I/include/opencv4`。
- **结论**：仓库自带的 CMake/Makefile 交叉编译入口是"从未验证的模板"，可复现性差。实际可复现路径是手工把工程放进 SDK example（无文档化步骤，只有 IMPLEMENTATION_LOG 零散记录）。

### 2.2 板端运行库依赖外部 OpenCV / rtsp_demo / rkaiq

- 板端需要 `libopencv_core/imgproc/imgcodecs`、`librtsp_demo`、`librkaiq` 等运行库；`start_demo.sh:27` 依赖 `LD_LIBRARY_PATH=./lib`，即产物目录旁需手工放置 `lib/`。仓库无这些库的获取/打包脚本。

### 2.3 CI 的 C++ job 实际是 no-op —— 【中/高】

- `.github/workflows/ci.yml:35-39` 编译命令引用 `tests/test_graph_laplacian.cc` 与 `../src/algorithms/graph_laplacian.cpp`，**两个文件均不存在**（仓库只有 `tests/test_graph_laplacian.py` 与 `src/algorithms/graph_laplacian.h`）。
- 且命令尾部 `| head -5` + `|| true` 会吞掉 g++ 报错并使管道提前 SIGPIPE，CI 永远"绿"但什么都没测。**必须修复**（改为编译 `tests/test_golden_regression.cc` + enhanced_decision 等真实存在的目标，并去掉 `|| true`）。

### 2.4 部署脚本不可移植

- `scripts/start_demo.sh` 硬编码 `/home/pico/daima/luckfox_pico_rtsp_yolov5_demo`、`/home/pico/go2rtc`、`go2rtc.yaml`（:9-12），go2rtc 二进制与配置不在仓库内，脚本是"现场专用"，换板子即失效。
- 端口探测用 `ss -tlnp`（:31,45），buildroot/busybox 环境通常无 `ss`（iproute2），会静默失败并空等 40s/30s。建议改 `netstat -ltn` 或 `nc -z` 探测。
- systemd 服务文件（`pedestrian-risk.service` / `pedestrian-watchdog.service`）只在 IMPLEMENTATION_LOG 提及，仓库内无文件，部署不可复现。

---

## 三、资源与性能（审查问题 3）

### 3.1 6FPS 瓶颈分析：单线程串行 + OpenCV 软件操作，而非 Jacobi

按帧耗时的静态估算（A7 1.2GHz 单核）：
- `cvtColor` 720×480 YUV420SP→BGR（`main.cc:325`）全帧软件转换：~30-60ms；
- `resize` 720×480→720×480 in-place（`main.cc:326`）：~20-30ms；
- `letterbox` 再次 `resize` 540×360 + 新建 640×640 Mat + `copyTo`（`main.cc:54-74,329`）：~20ms；
- `memcpy(data, frame.data, 720*480*3)`（`main.cc:448`）：源与目标同一块内存（`main.cc:234-235` frame 由 data 构造），**纯浪费 ~1MB 读写/帧**（Low，可直接删除）；
- `rknn_run` + NMS 后处理：~10-40ms；
- 每检测框 `cv::putText`（`main.cc:409-411`）：软件字体渲染，~5-20ms；
- VENC 编码/取流/RTSP 推流：~20-50ms。

合计 ~150-200ms → 与真机实测 6FPS（BASELINE_RESULTS 三）吻合。**-O2 不提升帧率的根因在此**：瓶颈是软件像素处理与串行等待，不是编译优化。

**建议（按收益排序）**：
1. 用 RGA/VPSS 硬件做 YUV→RGB 与缩放（Luckfox 官方有 RGA 示例），可省去 cvtColor+resize+letterbox 大部分耗时；
2. 复用 letterbox 输出缓冲（static/成员 Mat），消除每帧 1.2MB 堆分配；
3. 删除 `main.cc:448` 冗余 memcpy；
4. 画框改用 RGA 叠加或降低 putText 频率；
5. 降低编码分辨率（如 640×360）或降低 VI 输入分辨率，减少全帧软件扫描与 VENC 负载。

### 3.2 每帧堆分配与 256MB 内存

- 每帧堆分配点：`letterbox` 新建 640×640×3 ≈ 1.2MB（`main.cc:69`）；`post_process` 中 `filterBoxes/objProbs/classId/indexArray`（`postprocess.cc:388-434`）；`GraphLaplacian::computeFeatures` 中 `adj/L`（`graph_laplacian.h:238-241`）；`groundPts` 等 vector。
- 单帧峰值额外 ~2-3MB，对 256MB 总量不是致命问题，但**长时间运行（8h+）反复 malloc/free 1.2MB 块会产生堆碎片**，存在长期内存增长/分配失败风险。建议复用 letterbox 缓冲（Medium）。
- `risk_engine.cc:51-52` 的 `vector<vector<bool>>` 邻接矩阵（位压缩、访问慢）与 `vector<vector<float>>` 距离矩阵每帧分配，但**该路径默认不运行**（`enable_risk_engine_fallback=false`），实际影响可忽略（Low）。
- Jacobi O(n³)：对实际行人数 n<30（30³=2.7e4 浮点），每帧一次完全不是瓶颈；`graph_laplacian.h` 的 `vector<vector<double>>` 分配开销反而大于计算。**不要为 Jacobi 优化投入资源**。

---

## 四、稳定性（审查问题 4）

**已确认的既有修复（真机记录支持）**：
- VI/VENC 有限超时 1s（`main.cc:36-38`）、连续取帧失败 30 次主动退出交 systemd 重启（`main.cc:433-439`）；
- `venc_init`/`SendFrame`/`GetStream`/`ReleaseStream`/`rtsp_tx_video`/`rtsp_do_event` 返回值均检查（`luckfox_mpi.cc:122-134`、`main.cc:451-505`）；
- 取帧失败时跳过编码/推流/释放，避免陈旧帧与错误释放（`main.cc:441-507` 结构）；
- `cleanup` lambda 按就绪标志安全释放（`main.cc:166-177`）；
- `signal(SIGINT/SIGTERM)` + `g_stop` 优雅退出（`main.cc:86-92`）。

**仍存在的问题**：

| 问题 | 严重度 | 证据 |
|---|---|---|
| `system("RkLunch-stop.sh")` 每次启动调用：板子无该脚本时向 stderr 打印噪音；经 shell fork/exec，启动路径不可控 | Low | `main.cc:92` |
| RTSP 无重连：`rtsp_tx_video<0` 仅计数告警（60 次汇总后重置），不主动重建会话/重连；客户端异常后依赖 rtsp_demo 内部状态 | Medium | `main.cc:469-487` |
| 信号响应最坏延迟 ~3s：主循环内三个 1s 超时调用串行，SIGTERM 后最坏需等 3 次超时返回 | Low | `main.cc:316,451,463` |
| 成功帧日志每帧打印（检测框 + EnhancedDecision），长期运行日志无限增长（`/tmp/demo_rtsp.log`） | Low | `main.cc:362-363,398-399` |
| VENC 输入颜色格式疑似不匹配（见 M1），若成立则 RTSP 画面红蓝翻转，属功能缺陷 | Medium，待真机验证 | 见下 |

**疑似颜色格式不匹配（重点，待真机验证）**：
- `main.cc:325` 用 `cv::COLOR_YUV420sp2BGR` 得到 **BGR** 数据；
- `main.cc:234-235` `cv::Mat frame(..., CV_8UC3, data)` 数据布局为 BGR；
- 但 `main.cc:231` 与 `luckfox_mpi.cc:111` 均声明 `RK_FMT_RGB888`，VENC 按 RGB 字节序解释输入。
- **推论**：若 Rockchip 的 `RK_FMT_RGB888` 与 OpenCV BGR 字节序不同，RTSP 画面红蓝通道互换。此问题可能源自 Luckfox 官方示例原样，但本仓库代码确有此不一致。
- **处置**：真机放一张红/蓝色卡确认画面颜色；若异常，将 `cvtColor` 改为 `COLOR_YUV420sp2RGB`（或 VENC 声明 `RK_FMT_BGR888` 若 SDK 支持）即可。

---

## 五、配置可维护性（审查问题 5）

| 配置项 | 现状 | 严重度 |
|---|---|---|
| 检测置信度 `BOX_THRESH` / `NMS_THRESH` | 硬编码在 SDK `yolov5.h`（`yolov5.cc:196-197` 引用），**仓库无此头文件，无法审计/调参**，JSON 无对应字段 | **高** |
| IPM 默认单应 | `main.cc:96-100` 硬编码 srcPts/dstPts（画面底部梯形→6m×4m），`homography_file` 可覆盖但默认值为空（`default_config.json:20`），**未经真机标定**（BASELINE_RESULTS 已承认） | **高** |
| IPM 梯形区域外脚点 | `ipm_transform.h:33-42` `mapToGround` 仅做 w≠0 归一化，**无 ROI 检查**；脚点在梯形外（y<300）时外推产生不可信地面坐标，污染图结构 | Medium |
| `frame_dt` | 已 JSON 可配，默认 0.1s（≈10FPS）贴近实测 6.9FPS | 已修复 |
| FSM 参数 | 已 JSON 可配 + 语义校验（`risk_config.h:209-274`） | 已修复 |
| `model_path` / 分辨率 | `./model/yolov5.rknn`、720×480 硬编码 | Low |

---

## 六、"主机 make test 通过 ≠ 板端可用"差距与无板验证清单（审查问题 6）

### 6.1 差距

- `make test` 只覆盖算法层：`test_enhanced_decision`（Kalman/谱特征/FSM 逻辑）、`test_minijson`、`test_risk_config`、`test_golden_regression`（C++/Python 黄金回归）。**不覆盖**：`main.cc` 集成逻辑、`luckfox_mpi.cc`、`yolov5.cc/postprocess.cc`（依赖 SDK 头）、`ipm_transform.h`、RKNN/VI/VENC/RTSP 驱动、以及**交叉编译本身**。
- `risk_engine.cc` 仅当主机装有 OpenCV 才被编译进测试（`CMakeLists.txt:50-66`、`Makefile:46-50`）；CI 的 Ubuntu runner 无 OpenCV，**CI 上 risk_engine.cc 完全不被编译**。
- 因此主机全绿只能证明算法头文件正确，**板端集成层（驱动调用、内存布局、字节序、性能）零验证**。

### 6.2 无需开发板即可执行的验证方式清单

| # | 验证项 | 方法 | 当前状态 |
|---|---|---|---|
| 1 | 算法主机回归 | `cd src/luckfox_rtsp_yolov5 && make test` | ✅ 已做（220/220 断言 + golden 104） |
| 2 | IPM 逻辑主机测试 | 写 `test_ipm_transform.cc`（需 OpenCV core），验证 4 点映射/退化矩阵/文件加载 | ❌ 缺失，可补 |
| 3 | 集成层语法编译 | 提供 SDK 头文件 stub（`yolov5.h`/`rtsp_demo.h`/`sample_comm.h`/`rknn_api.h` 最小声明），对 `main.cc`/`luckfox_mpi.cc`/`yolov5.cc`/`postprocess.cc` 执行 `g++ -fsyntax-only` | ❌ 可补，成本低收益高 |
| 4 | 后处理离线验证 | 用 Python 端 RKNN 工具或真机录制一次输出 tensor 存为二进制，主机定义 `RV1106_1103` 宏跑 `post_process` 与 Python NMS 结果比对 | ❌ 可补（需一次真机采样） |
| 5 | ARM 指令级验证 | `arm-none-linux-gnueabihf-g++` 静态编译算法测试，用 QEMU user-mode 运行，验证 ARM 浮点/字节序下结果与 x86 一致 | ❌ 可补 |
| 6 | 静态分析 | `cppcheck --enable=warning,performance` 扫 `luckfox_rtsp_yolov5/src`（需 stub 头） | ❌ 可补 |
| 7 | 日志/帧率冒烟 | 解析真机 `/tmp/demo_rtsp.log` 断言 `[EnhancedDecision]` 格式、失败计数、帧间隔 | 部分（真机日志已采集） |
| 8 | 内存长稳监测脚本 | 板端 `free`/`/proc/meminfo` 采样脚本（仓库外） | ❌ 可补 |
| 9 | 颜色/字节序确认 | 真机放红蓝色卡，确认 RTSP 画面颜色 | ⏳ 仅真机可做 |
| 10 | RKNN/VI/VENC/RTSP 端到端 | 真实摄像头 + 真实行人场景 | ⏳ 仅真机可做（当前未验证） |

---

## 七、Top Findings 汇总

| # | Severity | Issue | Evidence |
|---|---|---|---|
| 1 | **High** | 仓库无法独立交叉编译：SDK 头文件（yolov5.h/sample_comm.h/rtsp_demo.h/rknn_api.h）全部缺失，CMake/Makefile 的 luckfox 目标从未实际使用（实际走 SDK example 构建），README 声称可复现不实 | `CMakeLists.txt:83-111`；`Makefile:83-97`；全仓库 find 无 4 个头文件；`IMPLEMENTATION_LOG.md` 二.2；`README.md:131` |
| 2 | **High** | IPM 默认单应硬编码、未真机标定，梯形区域外脚点无保护外推，米制坐标不可信 | `main.cc:96-100`；`ipm_transform.h:33-42`；`default_config.json:20`；BASELINE_RESULTS 四 |
| 3 | **High** | 检测置信度阈值硬编码在仓库外的 SDK `yolov5.h`，无法审计/配置 | `yolov5.cc:196-197`；仓库无 `yolov5.h`；`default_config.json` 无阈值字段 |
| 4 | Medium | VENC 输入 BGR 数据声明为 `RK_FMT_RGB888`，RTSP 画面疑似红蓝翻转（待真机验证） | `main.cc:325`（COLOR_YUV420sp2BGR）；`main.cc:231`；`luckfox_mpi.cc:111` |
| 5 | Medium | CI C++ job 引用不存在的 `tests/test_graph_laplacian.cc` 与 `graph_laplacian.cpp`，`\|\| true` 吞错，CI 实际不测 C++ | `.github/workflows/ci.yml:37-39`；`ls tests/` 与 `src/algorithms/` 无对应 .cc/.cpp |
| 6 | Medium | 画框/日志循环仅按 target_classes 过滤，未按 class_conf_thresholds 精筛，配置差异化阈值时画面与决策人数不一致 | `main.cc:346-348` vs `main.cc:390-391` |
| 7 | Medium | 每帧 1.2MB letterbox Mat 堆分配 + postprocess/图矩阵 vector 反复分配，256MB 长稳运行有碎片风险 | `main.cc:69`；`postprocess.cc:388-434`；`graph_laplacian.h:238-241` |
| 8 | Medium | 风险决策无外部告警输出（仅视频叠加+日志），无人观看时告警不可达 | `main.cc:362-363,412-423`；全仓库无 GPIO/串口/网络告警代码 |
| 9 | Medium | RTSP 无重连逻辑，推流失败仅计数告警 | `main.cc:469-487` |
| 10 | Medium | 板端真实行人场景从未验证（日志全 SAFE n=0），WARNING/DANGER 端到端未在目标硬件确认 | BASELINE_RESULTS 三；IMPLEMENTATION_LOG 四 |
| 11 | Medium | start_demo.sh 依赖 /home/pico 固定路径与仓库外 go2rtc 二进制/配置；`ss` 在 buildroot 可能缺失；systemd 服务文件不在仓库 | `start_demo.sh:9-12,31,45` |
| 12 | Low | `memcpy(data, frame.data, 720*480*3)` 源=目标，每帧白拷 1MB | `main.cc:234-235` vs `main.cc:448` |
| 13 | Low | `system("RkLunch-stop.sh")` 在无脚本板子产生噪音且 fork shell | `main.cc:92` |
| 14 | Low | `risk_engine.cc` 默认不运行仍编译进产物；`vector<vector<bool>>` 影响实际为零 | `default_config.json:19`；`main.cc:369`；`risk_engine.cc:51-52` |
| 15 | Low | 成功帧日志每帧打印，长期运行日志无限增长 | `main.cc:362-363,398-399` |

---

## 八、给总负责人的建议（按收益/成本排序）

1. **修 CI C++ job**（10 分钟）：改为编译真实存在的 `test_golden_regression.cc` + `test_enhanced_decision.cc`，去掉 `|| true`，让 CI 真正执行 C++ 回归。
2. **真机颜色确认**（5 分钟）：红蓝色卡入镜，确认/排除 M1，一劳永逸。
3. **IPM 真机标定**（1 小时）：用 `ipm_calibrate.py` 标定并写入 `homography_file`，同时给 `mapToGround` 加梯形 ROI 保护。
4. **删除冗余 memcpy + 复用 letterbox 缓冲**（30 分钟）：低风险减负。
5. **主循环耗时打点**：VI/cvtColor/RKNN/谱特征/VENC 各段加时间戳，用真机日志定位 6FPS 的 166ms 分布，验证 RGA 优化收益。
6. **补 SDK 头文件 stub + `-fsyntax-only` 检查**：让集成层在 CI 上至少可编译。
7. **板端真实行人场景验证**：答辩前必须完成 WARNING/DANGER 端到端真机证据。
