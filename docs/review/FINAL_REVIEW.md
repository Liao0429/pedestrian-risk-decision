# 最终评审（FINAL_REVIEW）

> 生成：2026-10-26 · 阶段 B 完成 / 阶段 C 待启动

## 一、本阶段完成情况

**阶段 A（盘点与基线）** ✅
- 创建独立分支 `review-opt`；基线：Python 137 passed、C++ golden 104/104、risk_config 58/58。
- 真机基线：调试版 6 FPS、-O2 版 6 FPS（证明瓶颈不在编译优化）。
- 交付：`SKILLS_AND_TOOLS.md`、`BASELINE_RESULTS.md`。

**阶段 B（多 Agent 独立评审）** ✅（含交叉质疑与裁决）
- 8 路 Agent：7 路完成独立报告，嵌入式 Agent 被中断（已由总负责人补写 `agent5_embedded.md` 并标注）。
- 交付：`PROJECT_AUDIT.md`、`MULTI_AGENT_REVIEW.md`、`IMPROVEMENT_ROADMAP.md`、`THESIS_INNOVATION.md`、`DEFENSE_PREPARATION.md`、`EXPERIMENT_PLAN.md`。

## 二、最重要的发现（按杀伤力排序）

1. **核心创新点（谱特征）当前无消费方**：计算了但未进 FSM，消融 C 组=B 组（多 Agent 独立确认）。→ 必须收敛论文表述或做接入实验。
2. **111 视频无风险 GT**：一切"零误报/准确率"类表述不可信。→ 已删表述，需补标注。
3. **消融基线 OR 与真实 AND 不一致 + Python 配置键 temporal/fsm 错位**：论文定量结论被这两个实现缺陷污染。→ P0 修复。
4. **实验/部署模型不一致（YOLOv8 vs YOLOv5n INT8）+ IPM 未真机标定**：实验结果外推性弱。→ 需真机验证与标定。
5. **README 数字矛盾 + 夸大表述**：答辩高风险。→ 立即修正。

## 三、剩余风险（如实）

- **真机行人验证未完成**：WARNING/DANGER 端到端未在目标硬件实测（需物理场景）。
- **8h/24h 长稳未跑**：看门狗已就位，数据未采集。
- **GT 标注未执行**：需要人工投入（图片 2h + 视频 12-18h）。
- **谱特征接入 FSM 实验未做**：P1，需在 review-opt 分支实现并消融对比。
- **CI 未建立**：P1。

## 四、建议的论文表述（收敛定位）

> 建议将论文定位为：**面向 RV1106 的边缘端"检测-几何-图论-时序"轻量化行人风险决策系统的设计与实现**。
> - 创新点表述：① 面向 1TOPS NPU 的端到端轻量决策流水线；② 图结构指标（密度/连通分量）驱动风险分级，区分分散/聚集；③ 时序 FSM 决策在低帧率下的稳定性工程。
> - 删除/修正：删除"谱特征提升准确性""零误报""DBSCAN/ByteTrack 已集成""IPM 已标定"等无据表述；统一数字口径。

## 五、下一阶段（阶段 C）计划

在 `review-opt` 分支实施（每项配测试 + 记录 IMPLEMENTATION_LOG）：
1. P0-1：消融基线 OR→AND 修正。
2. P0-2：Python 配置键 temporal→fsm 修复。
3. P0-3：README/论文表述收敛 + 数字统一。
4. P1-1：谱特征接入 FSM 实验（含归一化拉普拉斯 spike）。
5. P1-3：新增消融一致性测试 + 配置加载测试。
6. P1-5：黄金独立参考断言。
7. P1-4：GitHub Actions CI。
8. P2-5：修复 RkLunch-stop.sh 启动噪音。

每项完成后跑 `pytest tests/` + 老电脑 `make test`，通过才继续。


---

## 六、阶段 C 完成情况（2026-10-27 更新）

### 已完成并验证
- **P0 数据可信度修复闭环**：基线 OR→AND + 111 条全量重跑 + `_comparison_summary.json`/图表/README/实验摘要全部重生成；
  高密度 79.4% 统一、mixkit_13192 基线 48 次、31 条表述修正。
- **CI 修复**：cpp job 改为真实 `make test`（原引用不存在的文件）。
- **代码缺陷修复**：NMS 跨类别抑制（classIds[j]）、颜色通道（BGR→RGB 两处）、画框/决策过滤一致（main.cc）。
- **场景判别实验**：新增脚本+结果+测试，证明同人数不同分布可产生不同风险等级。
- **测试**：150 passed（新增 test_baseline_consistency / test_comparison_consistency / test_scenario_discrimination）。

### 待办（如实）
- C++ 改动**未在板端重编译/真机验证**（NMS、颜色、画框）——需老电脑交叉编译 + 真机冒烟（放红/蓝色卡）。
- CI 修复未推送运行验证（未授权推送）。
- 谱特征真正接入 FSM 的消融实验未做（P1）。
- 图片 500 张 GT 标注、IPM 真机标定、主循环打点、8h/24h 长稳未做。
