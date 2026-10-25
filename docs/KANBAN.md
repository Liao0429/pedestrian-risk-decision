# 毕设项目看板（KANBAN）

> 用户睡前要求（2026-10-26）：自主推进毕设到可验收，多轮优化上传 GitHub，明早验收。
> 本文件是任务看板，每完成一项更新状态。

## 任务状态

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| T0 | 汇总第2轮评测报告（6份）→ 优化清单 | 🔄 进行中 | 已返回 code_quality/demo_readiness，待 fix_recheck/algorithm_config/test_coverage/security_robustness |
| T1 | P0答辩就绪：start_demo.sh + README/架构图补新功能 + 预录DANGER视频 + WIFI_SETUP标注 | ⏳ 待办 | 答辩关键 |
| T2 | P1软件优化：消除重复计算/统一透视变换/VLA改vector/初始化检查/统一枚举 | ⏳ 待办 | 板端性能与健壮性 |
| T3 | P2质量：黄金回归测试/全局状态封装/魔法数字进配置 | ⏳ 待办 | |
| T4 | 全量测试 + 交叉编译 + 部署 + 推送GitHub | ⏳ 待办 | 每轮留痕 |
| T5 | 论文初稿：补网页观看/多目标章节，完善实验/结论（H:\毕设论文） | ⏳ 待办 | |
| T6 | 答辩演示打磨：美观/截图/预录视频/演示脚本 | ⏳ 待办 | |
| T7 | 心跳自检 + 看板更新 + 最终验收报告 | ⏳ 待办 | docs/FINAL_REPORT.md |

## 进度日志

见 `docs/DEVELOPMENT_LOG.md`。

## 关键信息速查

- 项目：H:\PedestrianRiskDecision；GitHub Liao0429/pedestrian-risk-decision（私人）
- 开发板：192.168.0.136（pico/luckfox，daima目录，LD_LIBRARY_PATH=./lib，日志/tmp/enhanced_v3.log，网页 go2rtc :1984）
- 老电脑：192.168.0.68（liao/123456）交叉编译：cd build && make install -j4
- 测试：H:/py311_gpu/Scripts/python.exe -m pytest tests/ -q --no-header --tb=short
- 推送：代理 7890 + gh auth token + x-access-token push
- 论文：H:\毕设论文（本地，不上传）