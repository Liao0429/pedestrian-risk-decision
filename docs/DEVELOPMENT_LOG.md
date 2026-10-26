# 开发日志（DEVELOPMENT_LOG）

> 本文件是心跳 cron 的进度日志：每轮追加「时间 + 做了什么 + 验证结果 + commit」。
> 详细技术文档见 `docs/review/IMPLEMENTATION_LOG.md`（按阶段），本文只留索引与最新状态。

## 进度时间线（摘要）

| 时间 | 事项 | 结果 / commit |
|---|---|---|
| 2026-10-26 | 多 Agent 全面评审（8 路 + 交叉评审） | 结论沉淀于 `docs/review/`（PROJECT_AUDIT / IMPROVEMENT_ROADMAP / THESIS_INNOVATION / DEFENSE_PREPARATION） |
| 2026-10-26~27 | P0 全部：消融基线 OR→AND、配置键 temporal→fsm、README/论文表述收敛、谱特征定位收敛 | 111 条基线重跑；`_comparison_summary.json` 重生成；pytest 全绿 |
| 2026-10-27 | P1-1 谱特征接入 FSM 实验 | **裁决：不采纳**（M1 过度敏感 / M2 仅整体阈值偏移），详见 IMPLEMENTATION_LOG 阶段七 |
| 2026-10-27 | P1-4 CI 建立、P1-5 黄金独立参考、P1-3 一致性测试 | GitHub Actions 全绿；158 pytest + C++ make test 通过 |
| 2026-10-27 | P1-6 主循环耗时打点 | 真机 `[PROFILE]`：瓶颈=CPU 预处理 + NPU 推理串行 |
| 2026-10-27 | **P1 新增：NEON 预处理优化** | CPU 预处理 136→33ms（-76%），6.30→7.46 FPS（+18%），NPU 91ms 成唯一瓶颈；板端黄金 174/174；commits `795a2ec` `cf72a67` `5743c16` `fd47944` `4e19cbe` |
| 2026-10-27 04:10~ | **P1-8 长稳采集启动** | `scripts/board_health_snapshot.py` + `results/edge_board/long_stability.jsonl`；cron 每 4h 自动采集 |
| 2026-10-27 05:05 | P1-8 快照 t4 | 运行 1h16m，零重启零错误，RSS 稳定 10.9MB，FPS 7.44；commit `6476e0b` |
| 2026-10-27 05:10 | P2-6 风险阈值速查页 | `docs/RISK_THRESHOLD_CHEATSHEET.md`（基于 `config/default_config.json` 真实值） |

## 当前主线

- **P1-8 长稳 8h/24h**：板端运行 NEON 优化版，cron `ffdce17a10c9` 每 4h 采集健康快照。目标：至少 8h 零重启、零 `rknn_run fail`、内存无泄漏。数据文件 `results/edge_board/long_stability.jsonl`。

## 待人工任务（无法自主完成）

- **P1-2**：500 张图片风险等级人工标注（约 2h，产出 `risk_gt.csv`，Kappa>0.6）。
- **P1-7**：真机 IPM 4 点标定（需打印标定物，产出 `homography_file`）。
- 冒烟测试：红蓝卡 / 人车同框（需现场）。