# 毕设项目看板（KANBAN）

> 用户睡前要求（2026-10-26）：自主推进毕设到可验收，多轮优化上传 GitHub，明早验收。
> 本文件是任务看板，每完成一项更新状态。

## 任务状态

| ID | 任务 | 状态 | 备注 |
|---|---|---|---|
| T0 | 汇总第2轮评测报告（6份）→ 优化清单 | ✅ 完成 | docs/review2/ 6份 + REVIEW2_SUMMARY |
| T1 | P0答辩就绪：start_demo.sh + README/架构图补新功能 + 演示视频 + WIFI_SETUP标注 | ✅ 完成 | 已推送 ea39d12；待USB WiFi模块实测 |
| T2 | P1软件优化：重复计算/VLA/初始化检查/枚举统一/IPM统一 | ✅ 完成 | Codex完成，推送 3b44cf9；CreatePool重试 90df382 |
| T3 | P2质量：黄金回归测试/全局状态封装/魔法数字进配置 | ✅ 完成 | 黄金回归框架落地（C++ 104/104 过）；顺带修复 minijson 布尔解析 bug |
| T4 | 全量测试 + 交叉编译 + 部署 + 推送GitHub | ✅ 完成 | 134测试过，交叉编译过，新版已部署开发板 |
| T5 | 论文初稿推进：补网页观看/多目标章节 | ✅ 完成 | 本地论文已补 6.6多目标+6.7网页监控，目录同步 |
| T6 | 答辩演示打磨：美观/截图/预录视频/演示脚本 | ✅ 完成 | DEMO_SCRIPT.md 讲稿+故障预案落地，DEMO_VIDEO.md/截图/视频已核实 |
| T7 | 心跳cron自检 + 看板更新 + 最终验收报告 | ✅ 完成 | docs/FINAL_REPORT.md 已生成 |
| T8 | 按类别差异化置信度阈值（车辆/动物语义不同） | ✅ 完成 | 心跳第5轮落地，commit 见日志；C++ 27/27 + Python 137 过 |

## 进度日志

见 `docs/DEVELOPMENT_LOG.md`。

## 关键信息速查

- 项目：H:\PedestrianRiskDecision；GitHub Liao0429/pedestrian-risk-decision（私人）
- 开发板：192.168.0.136（pico/luckfox，daima目录，LD_LIBRARY_PATH=./lib，日志/tmp/enhanced_v3.log，网页 go2rtc :1984）
- 老电脑：192.168.0.68（liao/123456）交叉编译：cd build && make install -j4
- 测试：H:/py311_gpu/Scripts/python.exe -m pytest tests/ -q --no-header --tb=short
- 推送：代理 7890 + gh auth token + x-access-token push
- 论文：H:\毕设论文（本地，不上传）