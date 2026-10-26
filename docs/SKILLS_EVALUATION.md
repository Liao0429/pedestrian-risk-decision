# 科研 AI 技能评估报告（针对毕业论文）

> 评估日期：2026-10-26。目的：分析用户提供的 8 类科研 AI 技能的真实来源、功能，评估对毕设《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》的实际价值，筛选真正有用的技能。
> 方法：GitHub 检索确认来源 + 实际克隆读取 6 个核心技能源码（deep-research / fireworks-tech-graph / guizang-ppt / humanizer / scientific-agent / stop-slop）。

## 价值评级
- ★★★ 强烈推荐（对毕设论文/答辩有直接高价值，轻量可装）
- ★★☆ 有用（特定场景有价值，或较重需选择性用）
- ★☆☆ 不推荐（对毕设不适用/冗余）

---

## 1. 文献检索与综述
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| Deep-Research-skills | Weizhena/deep-research-skills | 两阶段深度研究：大纲生成+深度调查，human-in-the-loop | ★★☆ | 论文综述有用，但需配 web-search agent + pyyaml，较重 |
| last30days-skill | kellyoconor/last30days-skills 等 | 检索近 30 天公开讨论提炼线索 | ★☆☆ | 适合追踪前沿，毕设偏工程落地，需求弱 |
| academic-research-skills | 社区多个实现 | 连接检索与修改流程 | ★★☆ | 可辅助综述，但依赖检索后端 |

**结论**：Deep-Research 可用作论文综述/技术调研辅助；其余可选。

## 2. 实验设计与测试方法
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| experiment-design-skill | rampstackco/claude-skills | 研究问题→实验方案 | ★★☆ | 毕设实验已设计完成，主要用于论文"实验设计"章节叙述 |
| lab-protocol-writer | 社区 | 实验步骤协议 | ★☆☆ | 毕设实验步骤已在文档中 |
| data-analysis-skill | 社区 | 数据处理 | ★★☆ | 可用于实验数据整理 |
| uncertainty-analysis | 社区 | 测量误差评估 | ★★☆ | 毕设真机测量（FPS/CPU/内存）可做误差分析，论文需要 |

**结论**：毕设实验已完成，此类主要用于论文叙述；uncertainty-analysis 对真机数据误差分析有价值。

## 3. SCI 论文写作与审稿
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| stop-slop | hardikpandya/stop-slop | 清除 AI 写作模式（filler/公式化/被动语态/破折号） | ★★★ | 论文去 AI 味，已读源码质量高，轻量直接可用 |
| humanizer | blader/humanizer | 重写 AI 味文本使其像人写（基于 Wikipedia Signs of AI writing） | ★★★ | 论文去 AI 味，已读源码质量高，与 stop-slop 互补 |
| 论文/代码审查（Claude Skills） | anthropics/skills 等 | 论文与代码审查、模拟审稿 | ★★☆ | 模拟审稿对论文质量有帮助 |

**结论**：**stop-slop + humanizer 强烈推荐**，论文初稿必备。

## 4. 科研数据分析与统计
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| scientific-agent-skills | K-Dense-AI/scientific-agent-skills | 177 个科学计算技能+100+数据库，arXiv 支撑 | ★★☆ | 功能强大但体积大（177 技能），可按需抽取统计检验技能 |
| AI-research-SKILLs | 社区 | 建模与评估 | ★☆☆ | 与毕设技术栈重叠 |
| duckdb-skills | duckdb/duckdb-skills | DuckDB 本地数据分析 | ★☆☆ | 毕设数据是图像/视频，非 SQL 表格，不适用 |

**结论**：毕设实验数据用 Python 处理即可；scientific-agent 可选择性用其统计检验子技能。

## 5. 科研绘图与可视化
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| fireworks-tech-graph | yizhiyanhua-ai/fireworks-tech-graph | 精确 SVG 技术图解（架构/流程/云图），导出 PNG/HTML/GIF | ★★★ | 已读源码，适合画系统架构图/数据流图，论文+答辩都需要 |
| ui-ux-pro-max-skill | 社区 | 排版配色参考 | ★☆☆ | 毕设图较简单，需求弱 |
| Document-illustrator-skill | 社区 | 文档配示意图 | ★★☆ | 论文配图有帮助 |

**结论**：**fireworks-tech-graph 推荐**（技术架构图）。

## 6. 学术 PPT
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| guizang-ppt-skill | op7418/guizang-ppt-skill | 单文件 HTML 网页 PPT（杂志风/瑞士风，WebGL 背景，演讲者视图，讲稿备注） | ★★★ | 已读源码，答辩 PPT 直接可用，风格专业 |
| PPT Master / Banana Slides | 社区 | PPT 模板/大纲生成 | ★★☆ | 可选 |

**结论**：**guizang-ppt-skill 强烈推荐**（答辩 PPT 制作）。

## 7. Word/PDF/LaTeX
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| MarkItDown | microsoft/markitdown | 任意文件→Markdown（docx/pdf/xlsx 等） | ★★★ | 微软官方，把论文/文献转 md 供 AI 读取，高价值 |
| MinerU | opendatalab/MinerU | 高质量 PDF 内容提取（版面/公式） | ★★★ | 文献 PDF 提取，论文引用文献必备 |
| Pandoc | pandoc | 文档格式互转 | ★★☆ | 论文排版（md→docx/pdf） |
| LaTeX-OCR | 社区 | 图中公式→LaTeX | ★★☆ | 论文数学公式 |
| Zotero Better BibTeX | 社区 | 文献引用管理 | ★★☆ | 参考文献管理（可后续配） |

**结论**：**MarkItDown + MinerU 推荐**（文献/文档处理）；Pandoc 可选。

## 8. 科研编程与 AI 自动化
| 技能 | 来源 | 功能 | 评级 | 理由 |
|---|---|---|---|---|
| superpowers | obra/superpowers | Claude Code 超能力（规划/执行/验证） | ★★★ | 编程自动化，对毕设代码开发/重构有帮助；Hermes 已有部分等价能力 |
| ponytail / caveman / qmd-skill | 社区 | 代码简化/Agent 上下文/md 检索 | ★☆☆ | 与本系统现有能力重叠 |

**结论**：superpowers 可装（与现有 plan-execute 互补）；其余低价值。

---

## 最终推荐安装清单（高价值、轻量）
| 优先级 | 技能 | 用途 |
|---|---|---|
| ★ 必装 | stop-slop | 论文去 AI 味 |
| ★ 必装 | humanizer | 论文去 AI 味（拟人化） |
| ★ 必装 | guizang-ppt-skill | 答辩网页 PPT |
| ★ 必装 | fireworks-tech-graph | 系统架构图/流程图（论文+答辩） |
| ★ 推荐 | MarkItDown | 文档转 Markdown（文献/论文处理） |
| ★ 推荐 | MinerU | 文献 PDF 提取 |
| ☆ 可选 | Deep-Research-skills | 论文综述/技术调研 |
| ☆ 可选 | superpowers | 编程自动化增强 |
| ☆ 可选 | uncertainty-analysis | 真机测量误差分析 |

## 不推荐（对毕设不适用）
- duckdb-skills（数据非表格）、last30days（偏新闻追踪）、ui-ux-pro-max（图简单）、lab-protocol-writer（实验已定）、ponytail/caveman/qmd（与现有能力重叠）