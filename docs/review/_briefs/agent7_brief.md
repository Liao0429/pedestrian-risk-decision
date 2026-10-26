# Agent 7 测试与质量保障专家 — 任务书

## 角色
独立审查 H:/PedestrianRiskDecision 的测试体系，建立可重复验证机制。

## 必读
- tests/ 全部 8 个 pytest 文件、src/luckfox_rtsp_yolov5/tests/ 4 个 C++ 测试文件、tests/golden/（黄金数据）
- scripts/generate_golden.py（黄金数据生成）、scripts/rerun_*.py、ablation_study.py
- src/luckfox_rtsp_yolov5/Makefile（make test 目标）

## 审查问题
1. 单元/集成/回归测试覆盖情况。
2. golden regression 机制：黄金数据怎么生成、C++/Python 一致性如何保证、边界行为/默认参数/输出规则是否一致。
3. 阈值与配置变化是否有可复现测试记录（test_risk_config、risk_config.h 校验逻辑）。
4. 测试缺口：异常处理、性能回归、长稳、内存泄漏、并发、随机种子、真机相关。
5. 实际运行测试：用 H:/Hermes Portable/data/home/cache/scratch/prd_venv/Scripts/python.exe 在仓库根目录运行 [python, -m, pytest, tests/, -q]，把真实结果写入报告。
6. 提出可执行的低成本改进（如新增针对当前缺陷的回归测试、CI 配置建议）。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent7_qa.md（write_file，中文）
- 最终回复：报告路径 + test_run_result（真实 pytest 输出摘要）+ top_findings

## 环境注意
- terminal 可能挂起；用 read_file/execute_code（subprocess 跑 pytest）
- 不要修改源码
