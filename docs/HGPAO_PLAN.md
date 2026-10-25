# HGPAO 毕业设计自主攻关与验收计划

> Hermes Graduation-Project Autonomous Ops（HGPAO）
> 项目：《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》

## 一、多 Agent 角色分工

| 角色 | 承担者 | 职责 |
|---|---|---|
| 首席架构师 | Hermes（主编排） | 需求分析、架构设计、任务拆解、进度跟踪 |
| 开发者 / Codex 专家 | Codex CLI | 算法实现、代码补全、单元测试生成 |
| 云端/测试专家 | 老电脑 Ubuntu + 开发板 | 交叉编译、部署、真机测试、日志采集 |
| 数据审计员 | Hermes 数据校验脚本 | 核对真实数据/日志，拦截伪造指标 |
| 学术论文写手 | Hermes（后续） | 撰写毕业论文（本地保存，不上传 GitHub） |
| 主验收官 | Hermes 终审 + 用户确认 | 代码/数据/文档验收，签发 PASS/REJECT |

## 二、数据流水线（三方对比，保证零造假）

| 阶段 | 说明 | 状态 |
|---|---|---|
| 原始数据集 | COCO 2017 图片 500 张 + Mixkit 视频 111 条（真实公开数据） | ✅ 已获取 |
| 处理/测试后 | 基线测试：YOLO 检测 + 复杂网络规则判断 | ✅ 图片基线完成（检出率 82.5%） |
| 算法增强后 | 增强版：+谱特征 + Kalman/ByteTrack + FSM 时序决策 | ✅ 视频增强完成（5 条视频） |
| 日志凭证 | results/、dataset/_work/ 日志、tests 输出 | ✅ 已保存并推送 |

## 三、当前进度看板

### ✅ 已完成
- [x] 真实数据集（500 图 + 111 视频，双重校验）
- [x] 基础算法：图拉普拉斯 / Kalman / FSM（测试全过）
- [x] 升级算法：ByteTrack / DBSCAN / IPM标定 / EMA（测试全过）
- [x] 图片基线实验（检出率 82.5%）
- [x] 视频增强实验（5 条真实视频）
- [x] 性能对比报告
- [x] 开发板 C++ 核心（YOLO+RiskEngine）真机运行
- [x] GitHub 全程留痕（私人仓库）
- [x] EnhancedDecision 增强决策模块（谱特征+Kalman+FSM）交叉编译成功，开发板真机运行（日志见 results/edge_board/）

### 🔄 进行中 / 待办
- [x] **P0：增强算法 C++ 板端移植**（EnhancedDecision 模块交叉编译 → 开发板真机运行，输出增强决策日志）
- [x] P1：真机增强版实测（增强版运行中，RTSP 正常，EnhancedDecision 每帧输出；帧率/内存量化待补）
- [x] P1：真机日志已保存（results/edge_board/enhanced_final_20261026.log，573 条增强决策输出）
- [x] P1：真机截图 + 量化测试完成（见 results/edge_board/EDGE_TEST_REPORT.md：内存12.5MB/CPU42.5%/RTSP6.9FPS/4张真实截图）
- [ ] P2：毕业论文撰写（本地保存）
- [ ] P2：硬件扩展实施（舵机/语音/大屏/开关，需用户采购硬件）
- [ ] P2：PCB 打样（嘉立创，需用户按规格书下单）

## 四、开发路线图（P0 优先）

### 任务 T1：增强算法 C++ 集成 ✅
- 输入：`src/algorithms/*.h`（graph_laplacian/kalman_filter/risk_fsm/bytetrack/dbscan/ema）
- 目标：把谱特征 + FSM 集成进 `src/luckfox_rtsp_yolov5/` 工程（main.cc/risk_engine.cc）
- 执行：Codex 生成 EnhancedDecision 模块 → 老电脑交叉编译 → 开发板部署 ✅
- 验收：板子日志出现 `[EnhancedDecision]` 输出（已实现），RTSP 正常 ✅

### 任务 T2：真机实测
- 记录：帧率、CPU/内存、等级切换次数、检测人数
- 凭证：板端日志 + 截图（保存 results/board_test/）

### 任务 T3：论文撰写
- 章节：绪论/相关技术/系统设计/算法实现/实验/总结
- 图表：使用真实实验结果
- 保存：本地 docs/thesis/（不上传 GitHub）

## 五、验收标准（主验收官）

1. 所有实验指标来自真实运行日志（无硬编码/伪造）
2. 所有单元测试通过（tests/ 全绿）
3. GitHub 每次改动有 commit 说明
4. 开发板真机可运行增强版
5. 论文基于真实数据，图表可溯源

## 六、GitHub 留痕策略

- 每个功能/修复/实验/文档 → 单独 commit + push
- commit message 说明改动内容
- 私人仓库，毕设完成后由用户手动公开