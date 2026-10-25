# 开发进度日志（DEVELOPMENT LOG）

> 每完成一项优化，在此追加记录（含 commit hash），GitHub 留痕。

## 2026-10-26 睡前自主推进（用户验收）

### 第 2 轮评测启动
- 派发 6 个评测 agent（docs/review2/）：fix_recheck / code_quality / demo_readiness / algorithm_config / test_coverage / security_robustness
- 已返回 2 份：code_quality.md、demo_readiness.md
  - code_quality：P1 消除 RiskEngine/EnhancedDecision 重复计算、VLA 改 vector、初始化错误检查；P2 黄金回归测试、统一枚举、全局状态封装
  - demo_readiness：README/架构图未同步网页观看+多目标、需 start_demo.sh 一键启动、WIFI_SETUP 需标注实测状态、需准备有人/聚集演示素材

### 已完成
- 建立文档看板 docs/KANBAN.md
- 创建心跳任务（hermes cron 9cb3159e30dc，每 60 分钟自主推进）
- 编写开发板一键启动脚本 scripts/start_demo.sh（答辩现场拉起 RTSP + go2rtc + 打印访问地址）
- 待办：README/架构图补新功能、WIFI_SETUP 标注、预录演示视频、P1 软件优化、论文补新章节

### 待后续提交
- scripts/start_demo.sh、docs/KANBAN.md、docs/DEVELOPMENT_LOG.md

## 后续推进（心跳第 1 轮）
- 6 份评测报告全部返回（docs/review2/）：fix_recheck / algorithm_config / test_coverage / security_robustness / code_quality / demo_readiness
- 已修复：
  - default_config.json COCO 类别注释修正（狗=16、猫=15，车辆[2,5,7,3]，动物[14,18,19,20,21]）
  - Python risk_fsm.py 帧保持 bug（now_ms=None 时对齐 C++），新增回归测试（14 项全过）
  - 论文初稿移出仓库 → H:\毕设论文\thesis_agent_draft\（docs/thesis 已 gitignore，不上传）
- 提交：ea39d12（P0答辩文档）、0b51d00（修复+6份评测报告+演示素材+论文移出）
- 进行中：Codex 后台执行 P1 C++ 优化（VLA/初始化检查/重复计算/枚举统一，proc_ed9f0c5f55c5）
- 待办：main.cc sprintf 栈溢出修复（P0安全，等 Codex 完成后改）、论文补网页观看/多目标章节、预录演示视频整理