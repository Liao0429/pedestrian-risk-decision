# 第 2 轮多 Agent 评测汇总（REVIEW 2 SUMMARY）

> 日期：2026-10-26。对修复后项目（commit fe041e4 之后）进行第 2 轮只读评测，共 6 份报告（docs/review2/）。
> 评审 agent：修复复核 / 代码质量 / 答辩演示就绪 / 算法配置 / 测试覆盖 / 安全健壮性。

## 1. 评测结论

| 报告 | 结论 | 关键项 |
|---|---|---|
| `fix_recheck.md` | 第 1 轮 P0/P1 大部分已修复，但 3 项完全未动、2 项仅部分 | P0-2 崩溃恢复、P0-3 初始化检查、P1-8 单应硬编码未修 |
| `code_quality.md` | 算法层质量高，板端集成层需收敛 | 重复计算、VLA、C 风格全局、双实现漂移风险 |
| `demo_readiness.md` | 答辩就绪度高（8/10），README 需补新功能 | 已补（网页观看/多目标/一键启动），演示素材待整理 |
| `algorithm_config.md` | 多目标 C++ 实现正确，但配置注释有错、脚本未同步 | COCO 注释错（已修）、脚本端未支持多目标 |
| `test_coverage.md` | 修复基本未被测试覆盖，Python risk_fsm 同款 bug 仍在 | risk_fsm.py 帧保持（已修+补测） |
| `security_robustness.md` | 发现 1 个 P0 内存安全、多个 P1 | main.cc sprintf 栈溢出（待修） |

## 2. 高优先级问题与处理

### 已修复（本轮）
- ✅ **COCO 类别注释错误**：default_config.json 原写"狗[17]猫[16]"错误，已改为狗[16]、猫[15]、车辆[2,5,7,3]、动物[14,18,19,20,21]（commit 0b51d00）
- ✅ **Python risk_fsm.py 帧保持 bug**：`_hold_elapsed` 在 `now_ms=None` 时早退导致帧保持失效，已对齐 C++（`state != DANGER` 才早退），新增回归测试 `test_danger_hold_frames_without_timestamp`（14 项全过）
- ✅ **论文移出仓库**：docs/thesis 初稿移至本地 `H:\毕设论文\thesis_agent_draft\`，`docs/thesis/` 已 gitignore（遵守"论文不上传"）

### 待修复（后续迭代）
- 🔴 **P0 安全**：main.cc:265 `char text[16]` + `sprintf` 会因长类名栈溢出 → 改 `char text[32]` + `snprintf` + `memset(text, 0, sizeof text)`（等 Codex P1 完成后应用，避免并发冲突）
- 🟠 **P1 初始化返回码检查**：init_yolov5_model / RK_MPI_MB_CreatePool / vi_dev_init / venc_init 失败应打印并安全退出（Codex P1 任务书中已含）
- 🟠 **P1 崩溃恢复/看门狗**：main.cc `while(1)` 无信号处理、清理代码不可达 → 增加 SIGINT/SIGTERM 处理 + supervisor 看门狗（systemd Restart=always）
- 🟠 **P1 VLA 改 std::vector**：yolov5.cc `input_attrs[io_num.n_input]` C99 VLA → std::vector + 校验（Codex P1 任务书中已含）
- 🟠 **P1 重复计算**：RiskEngine 与 EnhancedDecision 每帧双跑网络特征 → 消除旁路重复计算（Codex P1 任务书中已含）
- 🟡 **P1 脚本端多目标同步**：crowd_test.py / crowd_test_enhanced.py 硬编码 `classes=[0]`，需加 `--classes` 参数读取 target_classes
- 🟡 **P2 测试覆盖**：为 C++ 修复补运行测试（graph_laplacian/ema/bytetrack 的 C++ 单测）、黄金回归数据
- 🟡 **P2 按类别差异化阈值**：车辆/动物与人群阈值语义不同，后续支持按 target_classes 分组设阈值

## 3. 答辩就绪度（8/10 → 目标 10/10）
- 已补：README 特性（网页观看/多目标）、架构图（target_classes 过滤）、开发板一键启动 `scripts/start_demo.sh`、WIFI_SETUP 实测状态标注、演示视频素材说明 `docs/DEMO_VIDEO.md`
- 待办：预录 DANGER 演示视频已在 `results/video_enhanced_fsm/`，答辩前确认 WiFi 模块实测

## 4. 相关提交
- `0b51d00`：COCO 注释修正 + risk_fsm.py 修复 + 6 份评测报告 + 演示素材 + 论文移出
- `ea39d12`：P0 答辩文档（start_demo.sh / README / 架构图 / WIFI_SETUP / KANBAN / 开发日志）