# 优化前基线（BASELINE_RESULTS）

> 生成：2026-10-26 · 阶段 A：项目盘点与基线验证
> 用途：记录优化开始前（main 分支 `358a36f`）的真实测试结果、真机性能与已知待办，作为后续改进的对照基线。所有数据来自真实运行，未编造。

## 一、仓库状态

- 分支：`main`（commit `358a36f`，工作区干净）
- 本次优化工作分支：`review-opt`（已创建，不直接破坏 main）
- 远程：GitHub Liao0429/pedestrian-risk-decision（私人）

## 二、单元 / 回归测试基线

### 本机 Python（`python -m pytest tests/`）
- 结果：**137 passed**（耗时 ~1.2s）
- 覆盖：graph_laplacian、kalman_tracker、risk_fsm、crowd_dbscan、bytetrack_tracker、golden_regression、integration_pipeline、ipm_ema 等

### 老电脑 C++（`cd src/luckfox_rtsp_yolov5 && make test`，交叉工具链主机测试）
| 测试目标 | 结果 |
|---|---|
| `test_enhanced_decision` | ✅ 26/26 |
| `test_minijson` | ✅ 32/32 |
| `test_risk_config`（含 FSM 滞回校验、frame_dt 校验） | ✅ 58/58 |
| `test_golden_regression`（C++/Python 双实现一致性） | ✅ **104/104** |

## 三、真机性能基线（开发板 192.168.0.136，Luckfox Pico Max / RV1106）

### 调试版（默认 `-g` 无优化，2026-10-26 实测）
- 实测帧率：**6 FPS**（30 秒内 197 帧日志）
- 程序：`luckfox_pico_rtsp_yolov5`（6154484B，含取帧失败保护/venc_init 检查/frame_dt 配置等最近修复）
- RTSP :554 / go2rtc :1984 正常监听
- 增强决策日志：`[EnhancedDecision] level=SAFE n=0 rho=0.00 r_gcc=0.00 fiedler=0.000`（当前摄像头前无人）

### `-O2` 优化版（手动交叉编译，2026-10-26 实测）
- 产物：`/tmp/luckfox_o2`（5816284B，比调试版小 ~5.5%）
- 实测帧率：**6 FPS**（30 秒内 195 帧）
- 结论：在"摄像头前无人 + 单线程串行"场景下，`-O2` 暂未显著提升帧率——**帧率瓶颈不在编译优化，而在主循环串行与 VI/推理/编码耗时**（待多 Agent 进一步定位）。

## 四、已确认的缺陷 / 待办（评审 agent 前期发现，详细分析见 MULTI_AGENT_REVIEW）

| 类别 | 问题 | 严重度 |
|---|---|---|
| 错误处理 | `venc_init` 曾无条件返回成功 | 已修复（`358a36f`） |
| 错误处理 | 取帧失败时仍编码陈旧帧/错误释放 | 已修复（`358a36f`） |
| 时序 | Kalman `frame_dt=1/30` 与真实 6.9FPS 不符 | 已修复（`358a36f`，frame_dt JSON 可配置） |
| 数值 | Jacobi 不收敛曾静默归零 | 已修复（`8a294b8`，converged 透出） |
| 配置 | FSM 参数曾硬编码不可配 | 已修复（`8a294b8`） |
| 语义 | 单人 `r_gcc=1.0` 不合理 | 已修复（`4a4afe7`，n=1 → r_gcc=0.0） |
| 主循环 | 多处 `-1` 无限阻塞、无看门狗 | **待处理** |
| RTSP | 发送/事件返回值忽略、无重连 | **待处理** |
| 性能 | 单线程串行、Jacobi O(n³)、每帧堆分配 | **待分析** |
| 实验 | 无人工 GT、误报/漏报率无法计算 | **待处理（需标注方案）** |
| 实验 | 消融仅 6 条视频、谱特征切换次数零贡献 | **待处理** |
| IPM | 默认 H 未真机标定、广角畸变未校正 | **待处理** |
| 长稳 | 无 8h/24h 长稳记录、无看门狗 | **待处理** |

## 五、本次优化工作的验收标准（总体）

1. 每项改进不破坏现有 137 项 Python 测试与 104 项黄金回归。
2. 改进均记录"改动前 vs 改动后"对比（IMPLEMENTATION_LOG）。
3. 涉及核心算法/阈值/IPM 的改动，保留原版本（main 分支 + 分支对比），可回退。
4. 未验证项（真机行人、长稳等）明确标注状态，不编造结果。
5. 全部过程文档写入 `docs/review/`。