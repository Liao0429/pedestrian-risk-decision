# 第 1 轮审查修复复核报告（只读）

> 复核方式：只读检查源码，未修改任何文件。
> 依据：`docs/review/REVIEW_SUMMARY.md` 的 P0/P1 清单 + 10 份模块审查报告。
> 范围：对照清单逐项读 `main.cc`、`risk_fsm.h`、`bytetrack_tracker.h`、`risk_engine.cc`、`ema_smoother.h`、`graph_laplacian.h`、`crowd_dbscan.h`、`kalman_filter.h`、`enhanced_decision.h/.cc`、`risk_config.h`、`scripts/crowd_test_enhanced.py`、`crowd_test.py`、`README`、`INTEGRATION_PLAN.md`、`config/default_config.json`。
> 验证：仓库 `pytest` 133 项测试全部通过（3.67s）。

## 结论摘要

第 1 轮修复（commit `fe041e4` 及其前后相关提交）**成功解决了大部分 P0/P1 核心 C++ bug**，但 **3 项 P0/P1 完全未动**（P0-2 崩溃恢复、P0-3 初始化返回值检查、P1-8 单应硬编码），**2 项仅部分修复**（P1-4 EMA 未接入决策、P1-6 DBSCAN 未接入决策且阈值仍不一致），**文档项 P1-9 只修了配置键名、YOLO 版本与 INTEGRATION_PLAN 仍未澄清**。未发现修复引入的新回归。

---

## P0 复核

| # | 问题 | 状态 | 证据 |
|---|---|---|---|
| P0-1 | 增强决策默认未启用 | ✅ 已修复 | `main.cc:100-103` 移除 `#ifdef USE_ENHANCED_DECISION`，`EnhancedDecision` 无条件构造；`:208-229` 每帧调用 `processFrame` 作为默认路径；`Makefile`/`CMakeLists.txt` 注释同步。经典 `RiskEngine` 降为旁路对比。 |
| P0-2 | 崩溃恢复缺失 | ❌ 未修复 | `main.cc:182` 仍是 `while(1)`，无 `break`、无信号处理（`grep signal/SIGINT/SIGTERM/atexit/cleanup` 在 `src/` 全仓库为 0 命中）；`:319-343` 的 MB/VI/VENC/ISP/RTSP/RKNN 清理代码仍为**不可达死代码**。进程被强杀后 554 端口/RKNN/ISP 资源依旧残留。 |
| P0-3 | 初始化返回值未检查 | ❌ 未修复 | `main.cc:114` `init_yolov5_model(...)` 返回值仍被忽略，`:115` 无条件打印 `init rknn model success!`；`:116` `init_post_process` 及 `RK_MPI_MB_CreatePool`/`GetMB`/`vi_chn_init`/`venc_init`/ISP 返回值均未检查。模型缺失时仍会在首帧推理崩溃。 |
| P0-4 | 脚本 FSM 失效 | ✅ 已修复 | `crowd_test_enhanced.py:120` 改为向 `fsm.update()` 传 **dict**（`{"n":...,"density":...,"r_gcc":...}`），不再用 `SimpleNamespace.get()`；`:118` 正确计算 `now_ms`；异常路径打印告警回退单帧（`:124-127`）。 |
| P0-5 | 脚本默认绕过 IPM | ✅ 已修复 | `crowd_test_enhanced.py:158` `--scale` 默认改为 `None`；`:78-80` 无 H 且无 scale 时自动用 `get_homography(width,height)` 真实 IPM，不再退化为 `像素×0.01`。 |

## P1 复核

| # | 问题 | 状态 | 证据 |
|---|---|---|---|
| P1-1 | FSM 保持失效 | ✅ 已修复 | `risk_fsm.h:175-186` `holdElapsed` 由 `if (entryMs_ < 0) return true;` 改为 `if (level_ != RISK_DANGER) return true;`；帧保持与毫秒保持（`now_ms<0` 时只按帧）均生效，7 帧短路问题消除。 |
| P1-2 | ByteTrack 阈值语义反转 | ✅ 已修复 | `bytetrack_tracker.h:519` 与 `:548` 两轮关联均直接传 `m_matchThresh`（不再 `1.0 - m_matchThresh`）；`associate` 注释明确 `1-IoU <= thresh` 即 `IoU >= 1-thresh`；Python 版 `bytetrack_tracker.py:526,549` 同步统一。 |
| P1-3 | setConfig 回退缺陷 | ✅ 已修复 | `risk_engine.cc:25-27` 非法配置回退改用完整 `pedestrian::RiskConfig()`（warning=5/0.28/0.55、danger=10/0.55/0.80），不再用 `RiskRule()` 默认覆盖 warning，WARNING 等级可正常触发。 |
| P1-4 | EMA 两处越界 + 未接入 | ⚠️ 部分修复 | **越界已修**：`ema_smoother.h:62-67`、`:81-86` `setAlpha` 维度变化时重置状态；`:116-132` 标量接口对 `dim>1` 安全处理（先 `m_s.assign(m_dim,0)` 再更新第 0 维）；维度不匹配不再破坏状态（`:146-159`）。**但 EMA 仍未接入**：`enhanced_decision.h` 只 include graph_laplacian/kalman/risk_fsm，`enhanced_decision.cc` 全程无 `EmaSmoother` 引用，文档承诺的“FSM 前 EMA 层”依旧未落地。 |
| P1-5 | 图拉普拉斯 Jacobi | ✅ 已修复 | `graph_laplacian.h` 增加收敛检测（`:162-208` max_sweeps=100/tol=1e-12，不收敛返回 false）；`:213-215` 负特征值统一截断为 0（与 Python `np.maximum` 一致）；`:81-103`、`:123-130` 增加 edgeDist/坐标/建边输入校验。 |
| P1-6 | DBSCAN 语义/阈值/接入 | ⚠️ 部分修复 | 噪声语义已修（`:107-108` NOISE=-1、UNCLASSIFIED=-2 内部哨兵）；eps 可配置并校验（`:81-84`）。**但**：默认 `eps` 仍为 1.5（`crowd_dbscan.h:47`），与图阈值 `edge_dist_m=2.0` 的不一致未消除；`crowd_dbscan.h` 未被 `enhanced_decision.*` 引用，**仍未接入决策链路**。 |
| P1-7 | Kalman 门限中心陈旧 | ✅ 已修复 | `enhanced_decision.cc:69-71` 关联前先对所有轨迹 `kf.predict()`，门限中心用预测位置；`:107-108` 匹配后只 `correct()` 避免二次预测。Joseph 协方差形式仅为“建议”，未实现（非必须）。 |
| P1-8 | 单应矩阵硬编码 | ❌ 未修复 | `main.cc:88-93` 仍用固定四点 `cv::getPerspectiveTransform(srcPts,dstPts)` 写死 H；全工程无加载 `ipm_calibrate.py` 标定文件/命令行参数的逻辑。相机位姿变化仍需重编译。 |
| P1-9 | 文档不一致 | ⚠️ 部分修复 | 配置键名已统一为 `edge_dist_m`（`config/default_config.json`、`risk_config.h:87`、`crowd_test.py`/`crowd_test_enhanced.py` 均一致）。**但**：README 仍以 YOLOv5n 作为系统检测模型（`README.md:10,22,32`），PC 实验脚本默认 `yolov8n.pt` 的区分未明确说明；`INTEGRATION_PLAN.md` 仍标注“方案设计（不落实现有代码）”且称 `src/algorithms/` 为空、main 为单线程，明显过时。 |

---

## 复核发现的遗漏（第 1 轮修复未覆盖）

1. **P0-2 / P0-3 / P1-8 完全未处理**：均属于 `board_integration.md` 报告的原始 P0/P1，`fe041e4` 提交信息列出的“8 项 C++ bug”未包含这三项，`main.cc` 相关代码零改动。
2. **板端集成报告中的其他 P1 也未纳入**：
   - `enhanced_decision.cc:58-62`：空输入仍整体清空全部轨迹（破坏 Kalman 连续性），未改为 `miss++` 等待。
   - `EnhancedConfig.frame_dt` 仍默认 `1/30`，与真机实测 6.9 FPS 不符（`enhanced_decision.h:85`）。
   - `main.cc:97` 读取 `./config/default_config.json` 相对路径，与仓库顶层 `config/` 目录不一致，板端部署无拷贝脚本，用户阈值可能静默回退默认。
3. **P1-4 / P1-6 的“接入决策链路”是文档级承诺**：要么按 `ALGORITHM_UPGRADE.md` 将 `EmaSmoother` 插入 FSM 前、将 `CrowdDBSCAN` 作为特征接入，要么修订文档明确不接入，当前处于“头文件已实现但无调用方”状态，且无 C++ 运行时测试覆盖。
4. **C++ 头文件修复缺少真实编译/运行测试**：`tests/test_ipm_ema.py` 对 C++ 头文件仅做文本默认值比对，EMA/DBSCAN/图拉普拉斯/ByteTrack 的 C++ 修复依赖 Python 对照测试间接保障，未在板端工具链重新编译验证（P1-5 头文件注释亦自述需板端重验）。

## 未发现新回归

- 全部 133 项测试通过。
- 阈值键名三端（config/C++/Python）一致，`pytest` 中对 `edge_dist_m` 的一致性断言通过（`test_integration_pipeline.py:311`）。
- ByteTrack 阈值修正后 Python/C++ 行为一致；EMA 维度变更路径不再越界；DBSCAN 噪声标签语义与结构体注释一致。