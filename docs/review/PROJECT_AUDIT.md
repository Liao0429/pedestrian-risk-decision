# 项目现状审计（PROJECT_AUDIT）

> 生成：2026-10-26 · 基于 8 路 Agent 独立评审（agent1-8）+ 总负责人核验
> 原则：以真实代码/实验为准；未验证项明确标注。

## 一、项目定位与真实完成度

《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》，目标 Luckfox Pico Max (RV1106, 256MB, 1TOPS NPU)。

**真实完成的主链路（板端 C++，已真机部署运行）**：
摄像头采集 → YOLOv5n(INT8) RKNN 检测 → NMS → person 框 → IPM 地面米坐标 → 每目标 Kalman 平滑 → 图网络特征(n/density/r_gcc/clustering) → RiskFSM 三级分级 → 画框/风险条 → H.264 → RTSP/go2rtc。

**真机证据**：RTSP:554 / go2rtc:1984 正常，进程稳定，约 6-6.5 FPS，内存约 12.5MB，CPU 约 42.5%（调试版数据）；已部署 systemd 看门狗。

**完成度评估**：
- 检测/几何/网络特征/FSM/输出：**已完整集成** ✅
- DBSCAN、ByteTrack、EMA：**有独立实现和单测，但未接入主循环/决策路径** ⚠️（文档宣称与实现不一致）
- 图拉普拉斯**谱特征（Fiedler/谱半径/特征熵）：已计算并显示，但未进入 FSM 评分** ⚠️（核心创新点缺消费方）
- 硬件扩展（云台/语音/触摸屏）：方案文档齐全，**未实机集成** ⚠️
- PC 实验（500图/111视频/消融）：已运行且有真实结果 ✅，但**缺风险真值 GT** ⚠️

## 二、主要问题与证据（跨 Agent 一致确认）

| # | 问题 | 证据 | 严重度 |
|---|---|---|---|
| 1 | 谱特征计算但未进 FSM 评分；消融 C 组与 B 组逐帧相同 | `enhanced_decision.cc` / `risk_fsm.h:44-50` / `results/rerun/_ablation.json` | 高 |
| 2 | 111 条视频无风险 GT，"零误报/40条DANGER"无真值校验 | `results/rerun/README.md`、`agent4_experiment.md` | 高 |
| 3 | 消融基线用 OR 规则，真实引擎用 AND，基线失真 | `ablation_study.py:33` vs `scripts/crowd_test.py` / `risk_engine.cc` | 高 |
| 4 | Python FSM 配置读 `temporal` 键，配置文件是 `fsm` | `risk_fsm.py` vs `config/default_config.json` | 高 |
| 5 | 实验用 YOLOv8n(FP32)，部署 YOLOv5n(INT8 RKNN)，检出率未在部署模型验证 | `scripts/crowd_test*.py` vs 板端 `yolov5.rknn` | 高 |
| 6 | IPM 单应硬编码、未真机标定；区域外脚点无保护 | `main.cc` / `ipm_transform.h` / `config` | 高 |
| 7 | README 数字矛盾（高密度 76.7% vs 79.4%）、"5 real videos"过时、"谱特征提升准确性"无据 | `README.md` / `README.en.md` / `results/rerun/` | 中高 |
| 8 | 板端真实行人从未验证（日志全 SAFE n=0） | `results/edge_board/` | 高 |
| 9 | 无 CI，回归依赖手动 | `.github/workflows` 不存在 | 中高 |
| 10 | C++/Python 平滑语义不一致（幽灵点：漏检时 n 是否保持） | `enhanced_decision.cc::smoothPoints` vs `kalman_tracker.py` | 中 |
| 11 | 黄金数据"参考实现即被测实现"，双实现同时错测不出 | `scripts/generate_golden.py` | 中 |
| 12 | 脚本硬编码 `H:/...` 绝对路径，不可移植 | `ablation_study.py`/`find_sparse_video.py`/`plot_results.py` | 中 |

## 三、已完成的工程健壮性改进（会话期间，commit `41194e2`/`358a36f`/`8a294b8`/`4a4afe7`）

- 主循环 VI/VENC 1s 有限超时 + 看门狗 + SIGTERM 优雅退出
- 取帧失败路径保护、venc_init 返回值检查、RTSP 发送返回值检查
- Kalman frame_dt JSON 可配置；FSM 参数 JSON 可配置 + C++ 滞回校验
- 谱特征收敛状态透出（converged 标志）；单人 r_gcc=0 语义修正
- GitHub 仓库文档收口（论文相关内容移出）

## 四、尚未验证 / 待办（不编造）

- 真实行人端到端 WARNING/DANGER 真机验证（需物理场景）
- 8h/24h 长稳数据（看门狗已就位，未跑满 24h）
- IPM 真机 4 点标定（需打印标定物）
- 非人目标（车辆/动物）阈值重标定
- CI 自动化
