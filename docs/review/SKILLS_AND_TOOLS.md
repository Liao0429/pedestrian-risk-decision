# 技能与工具盘点（SKILLS_AND_TOOLS）

> 生成：2026-10-26 · 阶段 A：项目盘点与基线验证
> 目的：列出本项目中已检查、已实际调用、以及不可用的技能与工具。所有条目均如实记录；未实际调用的注明"未调用"，不可用/受限的注明原因。

## 一、项目相关技能清单（按用途分类）

### 1. 代码审查与质量
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `code-security-audit` | 代码安全审计、机器可读 findings | 中（板端 C++ 有系统调用/内存管理） | 未调用（后续可） |
| `requesting-code-review` | 提交前代码评审门禁 | 中 | 未调用 |
| `codebase-inspection` | 统计代码规模/语言/结构 | 中（盘点仓库） | 未调用 |
| `grill-me` | 对抗式方案审查 | 高（方案裁决前） | 未调用 |
| `triage` | 问题/PR 分诊 | 低 | 未调用 |
| `dogfood` | 探索式 QA 找 bug | 高（找实现缺陷） | 未调用 |

### 2. 学术研究 / 文献
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `arxiv` | arXiv 检索 + Semantic Scholar 引文/推荐 + BibTeX | **高**（补充文献、核对参考文献） | 已调用（能力确认） |
| `web_search` / `web_extract` | 通用检索 / 网页提取 | 高（查官方文档、论文） | 可用 |
| `grounded-citations` | 引用 grounding（可溯源） | 高（论文引用） | 未调用 |
| `parallel-cli` | Agent 原生深度搜索 | 高 | 未调用 |
| `scrapling` | 网页抓取（反爬） | 低 | 未调用 |

### 3. 嵌入式开发 / 部署
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `luckfox-pico-rv1106` | Luckfox RV1106 交叉编译/RKNN/部署 | **最高**（本项目目标平台） | **已实际调用**（老电脑交叉编译、真机部署） |
| `old-pc-home-server` | 老电脑当服务器 | 中（老电脑 Ubuntu 作编译机） | 已实际使用（SSH/交叉编译） |
| `docker-management` | 容器 | 低 | 未调用 |

### 4. 算法验证 / 测试
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `tdd` / `test-driven-development` | 测试驱动开发 | 高（算法改动配测试） | 已通过现有 pytest 体系验证 |
| `systematic-debugging` | 根因定位 | 高 | 已实际使用（定位 CMake/flags 问题） |
| `spike` | 一次性实验验证想法 | 高（候选优化先 spike） | 未调用 |
| `subagent-driven-development` | 子 agent 分阶段执行+评审 | 高（多 Agent 团队） | 将使用（delegate_task） |
| `plan-execute` | 计划-执行-重规划 | 高（本任务总流程） | 已使用（阶段推进） |
| `reflexion` | 失败后自省 | 中 | 未调用 |

### 5. 文档 / 绘图 / 写作
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `architecture-diagram` | dark 主题系统架构图（HTML/SVG） | **高**（论文系统架构图） | **已实际调用**（生成 `H:\毕设论文\系统总体架构图.html/png`） |
| `excalidraw` / `concept-diagrams` | 手绘/概念图 | 中 | 未调用 |
| `academic-project-delivery` | 毕设实验报告/消融/仓库卫生 | **最高**（本项目核心） | **已实际调用**（消融、文献、仓库清理） |
| `stop-slop` | 清除 AI 写作痕迹 | 高（论文/文档去 AI 味） | 未调用（后续论文润色可用） |
| `docx` / `pdf` | Word/PDF 读写 | 高（论文文档处理） | 未调用 |
| `guizang-ppt-skill` | 网页 PPT | 中（答辩演示可选） | 未调用 |

### 6. Git / 版本管理
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `github` | gh CLI：PR/issue/repo | 高 | 已实际使用（gh auth token、推送） |
| `open-source-revamp` | 开源项目二次开发 | 中 | 未调用 |

### 7. 多 Agent / 编排
| 技能 | 解决的问题 | 本项目适用性 | 调用情况 |
|---|---|---|---|
| `delegate_task`（工具） | 创建真正独立子 Agent | **最高**（本任务多 Agent 团队） | **已实际调用**（3 路评审 agent 已运行） |
| `multi-agent-coding-collab` | Codex/Claude/Hermes 协同 | 高 | 未调用 |
| `dynamic-workflow` | 计划内扇出、对抗验证 | 高 | 未调用 |

## 二、工具能力

| 工具 | 用途 | 可用性 | 说明 |
|---|---|---|---|
| `execute_code` / `terminal` | Python/Shell 执行、编译、git | ✅ | 已大量使用 |
| `web_search` / `web_extract` | 文献/资料检索 | ✅ | 可用 |
| `vision_analyze` | 图像分析 | ⚠️ **受限** | 当前模型 u2-flash 不支持 image_url（调用返回 400），无法自动看图；改用 Edge headless 截图+人工/其他模型核对 |
| `browser_exec`（local） | 驱动本地真实浏览器 | ⚠️ **受限** | Edge 正在运行且 profile 锁定，不能强制关闭用户浏览器；改用 Chrome/Edge headless 截图 |
| `delegate_task` | 创建子 Agent | ✅ | 可创建真正独立子 Agent（后台并行、可 steer/stop） |
| MCP 服务 | 无项目相关 MCP | — | 未发现与本项目相关的 MCP |

## 三、不可用 / 受限 / 未验证项（如实说明）

1. **真实摄像头前无行人**：开发板（192.168.0.136）在线，但摄像头前当前无人，无法在真机直接验证"有行人时的 WARNING/DANGER 端到端输出"。
2. **vision_analyze 不支持当前模型**：无法用 AI 直接检查架构图渲染质量；已用 Edge headless 真实渲染截图（166KB）作为可检查产物。
3. **browser_exec local 受 Edge profile 锁定**：改用独立 profile 的 headless 截图，不影响用户浏览器。
4. **GPU 1080Ti 本机**：可用于大规模数据实验，但当前未做新的 GPU 实验（已有历史结果）。
5. **无人工 GT 标注**：111 条视频无逐帧风险真值，无法直接计算精度/召回/误报率（详见实验文档）。
6. **第三方"Nature/Paper 系"技能**：来源不明，未安装；其核心能力由现有技能（arxiv/architecture-diagram/stop-slop 等）覆盖。

## 四、本任务将实际使用的核心技能组合

- **多 Agent 评审**：`delegate_task`（8 个专家角色独立评审 + 交叉质疑）
- **代码审查**：`codebase-inspection` + `grill-me` + `dogfood` 思路
- **算法验证**：`tdd`（每项改进配测试）+ 现有 pytest/make test 回归
- **嵌入式验证**：`luckfox-pico-rv1106`（老电脑交叉编译 + 真机部署）
- **文档交付**：`academic-project-delivery` + `architecture-diagram` + `stop-slop`

---

## 五、本轮实际调用更新（2026-10-27 第三批）

| 技能/工具 | 实际用途 | 结果 |
|---|---|---|
| `delegate_task`（工具） | **真实创建 8 路独立评审 Agent + 3 路交叉质疑 Agent**（均同步运行、独立读码、独立写报告） | agent1-8 报告 + _cross/crossA/B/C 报告 |
| `read_file` / `search_files` | 读取源码/文档 | 正常（初期 sandbox 短暂不可用，已恢复） |
| `execute_code`（Python） | 文件浏览、subprocess 跑 pytest/GPU 重跑、JSON 重算、括号平衡检查 | 150 测试通过；基线重跑完成 |
| `web_search` / `web_extract` | 文献检索（RESEARCH_AND_INNOVATION） | 见 RESEARCH 文档 |
| `terminal` | ⚠️ **本会话大部分时间不可用**（bash 挂起），子 Agent 的 terminal 可用 | 已用 execute_code 替代 |
| `vision_analyze` | 子 Agent 用于核验截图（agent8/crossC） | 部分可用（个别调用 400） |
| `py311_gpu`（H:/py311_gpu） | GPU 重跑 111 视频基线（torch 2.5.1+cu121 / GTX 1080Ti） | 需清空 PYTHONPATH（Hermes 内核注入 cp314 numpy 会污染） |
| `patch` / `write_file` | 实施代码与文档修改 | 正常 |

### 本任务使用的核心技能组合（最终）
- 多 Agent：`delegate_task`（8 独立 + 3 交叉仲裁）→ 汇总 `MULTI_AGENT_REVIEW.md`
- 算法验证：`tdd`（每项改进配测试，新增 3 个测试文件）
- 数据实验：GPU 全量重跑（公平基线）、场景判别实验
- 嵌入式：代码级修复（NMS/颜色/画框），交叉编译/真机标记为**待验证**
- 文档：`academic-project-delivery` 思路（增量更新，不覆盖旧成果）
