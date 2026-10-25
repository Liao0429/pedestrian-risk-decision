# 系统多 Agent 审查汇总（第 1 轮 · 10 Agent）

> 10 个审查 agent 对系统全部核心模块进行只读审查，报告见 `docs/review/*.md`。
> 本文件为汇总清单，供修复与验收使用。

## 审查范围

| # | 模块 | 报告 |
|---|---|---|
| 1 | 图拉普拉斯谱分析 | `review/graph_laplacian.md` |
| 2 | Kalman 滤波 + 多目标关联 | `review/kalman_tracking.md` |
| 3 | 风险 FSM 状态机 | `review/risk_fsm.md` |
| 4 | ByteTrack 多目标跟踪 | `review/bytetrack.md` |
| 5 | DBSCAN 人群聚类 | `review/dbscan.md` |
| 6 | EMA 平滑 | `review/ema.md` |
| 7 | 风险规则引擎 + 配置 | `review/risk_engine_config.md` |
| 8 | 开发板集成（main.cc + 增强决策） | `review/board_integration.md` |
| 9 | 实验脚本 | `review/scripts.md` |
| 10 | 文档与可复现性 | `review/docs_reproducibility.md` |

## 🔴 P0（必须修）

| # | 问题 | 位置 |
|---|---|---|
| P0-1 | **增强决策默认未启用**：`USE_ENHANCED_DECISION` 宏在构建文件中未定义，main.cc 增强分支为 `#ifdef` 死代码，默认构建不调用增强决策，真机验证为手动加宏 | 构建文件 + main.cc |
| P0-2 | **崩溃恢复缺失**：`while(1)` 无清理/信号处理，强杀后 RTSP/RKNN/ISP 资源残留 | main.cc |
| P0-3 | **初始化返回值未检查**：模型缺失仍打印 success，首帧必崩 | main.cc |
| P0-4 | **脚本 FSM 失效**：`crowd_test_enhanced.py` 用 `SimpleNamespace` 调 `.get()` 抛异常被吞，时序平滑从未生效，视频"稳定等级"实为单帧原始等级 | `scripts/crowd_test_enhanced.py:115` |
| P0-5 | **脚本默认绕过 IPM**：`--scale` 默认 0.01，地面坐标退化为像素×0.01 米 | `scripts/crowd_test_enhanced.py` |

## 🟠 P1（高危，应修）

| # | 问题 | 位置 |
|---|---|---|
| P1-1 | **FSM 保持失效**：`holdElapsed()` 中 `entryMs_<0` 早退短路 DANGER 帧保持（实测 7 帧应 ≥10） | `risk_fsm.h:176` |
| P1-2 | **ByteTrack 阈值语义反转**：两轮关联传 `1.0 - m_matchThresh`，实际门限 IoU≥0.8（应 0.2），轨迹每帧重建 | `bytetrack_tracker.h` |
| P1-3 | **setConfig 回退缺陷**：非法配置回退用 `RiskRule()` 默认（DANGER 级）同时覆盖 warning+danger，回退后 WARNING 永久无法触发 | `risk_engine.cc:23-26` |
| P1-4 | **EMA 两处越界（UB）** + 未按文档接入 EnhancedDecision | `ema_smoother.h` |
| P1-5 | **图拉普拉斯 Jacobi 无收敛检测**、负特征值裁剪与 Python 不一致、输入未校验 | `graph_laplacian.h` |
| P1-6 | **DBSCAN 噪声语义不符**（注释 -1 实际 -2）、eps 不可配置、与图阈值不一致（1.5 vs 2.0）、未接入决策链路 | `crowd_dbscan.h` |
| P1-7 | **Kalman 门限中心陈旧**（未先 predict）、建议 Joseph 协方差形式 | `kalman_filter.h` / `enhanced_decision.cc` |
| P1-8 | **单应矩阵硬编码**：未接 `ipm_calibrate.py`，相机位姿变化需重编译 | `main.cc:88-92` |
| P1-9 | **文档不一致**：README 称 YOLOv5n 而脚本默认 YOLOv8n；INTEGRATION_PLAN 过时；配置键名 `edge_dist` vs `edge_dist_m` | README / docs |

## ✅ 已确认无问题

- 匈牙利算法正确（暴力枚举验证误差 0.0）
- ByteTrack 遮挡二次关联逻辑正确
- Kalman 数值稳定性良好（2000 步无发散）
- FSM 滞回/去抖逻辑正确（与 Python 逐帧一致）
- DBSCAN 空/单点/全噪声无崩溃
- 阈值三端（config/C++/Python）默认值一致
- .gitignore 合理（真机日志白名单正确）

## 修复计划

1. 默认启用增强决策（去掉 #ifdef 死代码或默认定义宏），修复板端初始化/崩溃清理
2. 修复 risk_fsm / bytetrack / risk_engine / ema / graph_laplacian / dbscan 的 C++ bug
3. 修复实验脚本（FSM 调用、IPM 默认、退出码、复现参数）
4. 重跑单元测试 + 重验视频实验（修复 FSM 后）
5. 板端交叉编译验证
6. 更新过时文档（YOLO 版本、INTEGRATION_PLAN、配置键名）
7. 每步提交推送 GitHub