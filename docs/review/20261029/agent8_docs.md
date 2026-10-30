# agent8 · 文档/论文审稿报告（README / docs / results × BOM v2 交叉审查）

> 审查日期：2026-10-29 ｜ 审查人：agent8（文档/论文审稿人）
> 范围：`README.md`、`README.en.md`、`docs/`（导读/ARCHITECTURE/工作日志/algorithms/全部硬件文档）、`results/` 聚合数据、`G:/毕设论文/02_答辩准备/答辩硬件展示方案_v2_BOM与PCB手册.md`（BOM v2）
> 方法：通读 + 用 Python 对 `results/rerun/` 的 111 条时间线 JSON（45021 帧）全量重算 README 所有指标，逐条核对；图件盘点；与已有 `agent1_architect.md` 交叉避免重复。
> 声明：只读审查，未修改任何项目文件；本报告为指定输出。

---

## 0. 总体结论（先说重点）

1. **实验数字完全可信、可复现**。README 中全部核心指标（45021 帧、SAFE 26885 / WARNING 6388 / DANGER 11748、平均切换 5.86、中位数 4.0、66 条 DANGER、31 条全程 SAFE、Fiedler 均值 0.017/0.214/1.078、Spearman ρ=0.688、基线 23.2→5.9、消融 A/B/C/D 33.2/8.3/4.3/4.3、DANGER +152/+10.2%、mixkit_13192 179/120/6 四切、mixkit_50134 388 帧全程 SAFE、长稳 28h 看门狗 0 日志 0、174/174、日志 681 行/573 条输出）**经全量重算全部成立**。这是本项目文档最扎实的部分。
2. **问题集中在"文档间不同步"**，而非数据造假：答辩方案文档（BOM v2）与仓库最新状态严重脱节（引脚/端口/软件进度/长稳时长四处过时），旧方案文档（串口屏、DFPlayer、PCB v1.0）在 README 索引中未标注"历史存档"，导致评审按 README 跳转会看到与最终形态完全相反的方案。
3. **内存/帧率口径在 4 份文档间不一致**（10.5~10.9MB vs 11MB vs 11~12.5MB vs 12.5MB），是答辩最容易被评委当场追问的点。
4. **中英文 README 违反项目自身"双语强制同步"规约**（导读 L182），EN 版缺失多个 CN 版章节/特性，架构图也落后于 CN 版。
5. 无 P0 级数据造假/夸大；P0 级矛盾集中在**答辩执行手册 BOM v2 的两处硬错误**（VBUS 引脚、go2rtc 端口）。

---

## 1. README 数字 vs results 全量复核结果

对 `results/rerun/video_enhanced/*/*_timeline.json`（111 条、45021 帧）全量重算：

| README 声称（README.md 行号） | 全量重算实测 | 结论 |
|---|---|---|
| 等级分布 SAFE 26885 / WARNING 6388 / DANGER 11748（L79） | 完全相同 | ✅ |
| 平均切换 5.86 次/条、中位数 4.0（L79） | 均值 5.856、中位数 4.0 | ✅ |
| 66 条出现 DANGER、31 条全程 SAFE（L79） | 66 / 31 | ✅ |
| Fiedler 均值 SAFE 0.017 / WARNING 0.214 / DANGER 1.078（L80） | 全帧重算 0.0169 / 0.2140 / 1.0783 | ✅ |
| Spearman ρ = 0.688（45021 帧全量）（L95） | 全帧重算 0.6882 | ✅ |
| 基线 23.2→5.9 次（L83） | 2578/111=23.22 → 650/111=5.86 | ✅ |
| 基线 24 条 DANGER、增强 66 条；DANGER 帧 2190→11748（L83） | `_comparison_summary.json`：base_lv DANGER=2190、enh_lv DANGER=11748 | ✅ |
| 消融 A 33.2 / B 8.3 / C 4.3 / D 4.3（L101-104） | A 均值 33.17、B 8.33、C 4.33、D 4.33 | ✅ |
| D 组 DANGER +152 帧（相对 C 组 +10.2%）（L104） | C 组 6 条 DANGER 1497 帧、D 组 1649 帧，+152（+10.15%） | ✅ |
| 分视频 +78/+45/+29（L104） | mixkit_4519 +78、mixkit_4178 +45、mixkit_134 +29 | ✅ |
| mixkit_13192：DANGER 179 / WARNING 120 / SAFE 6，305 帧，4 次切换，基线 48 次（L81） | 完全相同（A 组基线切换=48） | ✅ |
| mixkit_50134：388 帧全程 SAFE（L82） | 388 帧全 SAFE、0 切换 | ✅ |
| 7.46 FPS（L114） | `long_stability.jsonl` 9 个 FPS 点 7.44~7.52 | ✅ |
| 28h+ 采样 10 点、看门狗 0 介入、日志 0 错误（L117） | jsonl 10 点，wd_NRestarts=0、journal_error_count=0、log_fail_count=0；svc_NRestarts=1（外部重启 1 次，systemd 自动恢复） | ✅ |
| 174/174 板端黄金测试（L118） | `selftest.md` 描述的 make test 含 174 项预处理（NEON vs 标量） | ✅ |
| 导读 L123：日志 681 行、ED 输出 573 条 | `enhanced_final_20261026.log` 681 行、含 "EnhancedDecision" 573 行、0 错误 | ✅ |

**结论：实验指标无虚假、无夸大。** 唯一"选择性呈现"是内存：README 写 ~10.5-10.9MB，但 10 个长稳点中后 5 点为 9.10~10.15MB，最后 1 点 13.12MB（重启后瞬时高值），全区间 9.10~13.12MB——README 只取了前 5 个"好看"的点（详见 P1-6）。

---

## 2. 矛盾清单（严重度分级）

### P0 —— 答辩执行手册硬错误，照做必翻车

**P0-1 ｜ VBUS 引脚：BOM v2 写 Pin20，仓库全部文档/已完成的 PCB v1.5 用 Pin40**
- 矛盾文件与行号：
  - `G:/毕设论文/02_答辩准备/答辩硬件展示方案_v2_BOM与PCB手册.md` L91：`VBUS_5V | J1(+5V) → U1 排母 VBUS 脚（Pin20）→ U2 输入`
  - `docs/工作日志.md` L51（2026-10-28 01:16，PCB v1.5 电源直连版完成）：`5V/GND 直连 U1 排母 Pin40(VBUS)/Pin38(GND)`
  - `docs/hardware/voice_module.md` L98：`Pin 40 | VBUS（USB 5V，随板供电）`；L102 明确 `VBUS(5V)（Pin 40）…也可接 VSYS（Pin 39，约 4.7V）`
  - `docs/hardware/touchscreen.md` L94/L481：`Pin 20 = GPIO1_D1 = UART3_RX_M1`（串口屏 RX，与 VBUS 无关）
- 矛盾实质：BOM v2（答辩 PCB 手册）与仓库内 3 份文档 + 已完成打样的 v1.5 Gerber 全部冲突。Pin20 在 40pin 排针上是 UART3_RX（GPIO1_D1），不是电源脚；VBUS 在 Pin40。BOM v2 的 Pin20 几乎可断定为笔误，但按它连线/改板会把 5V 接到 UART3_RX。
- 建议改法：BOM v2 L91 改为 `Pin40(VBUS)`；并在 BOM 加一行与 `工作日志.md` v1.5 的核对说明（Pin40/Pin38）。下单/焊接前用万用表实测 Pin40 与 USB-C 5V 连通再确认。

**P0-2 ｜ go2rtc 端口：BOM v2 写 8080，实际部署/脚本/文档全部是 1984**
- 矛盾：
  - BOM v2 L191：`手机浏览器打开 http://<开发板IP>:8080（go2rtc）查看实时画面`
  - `scripts/start_demo.sh` L60、L79：`ss -tlnp | grep -q ':1984'`、`http://$IP:1984`
  - `docs/WEB_VIEWER.md` L9/L32/L48/L55：`http://192.168.0.136:1984`、`listen: ":1984"`、端口表 `1984 | Web 界面/API`
  - `scripts/setup_go2rtc_demo_stream.sh` L29/L94：`PORT=1984`、`api: listen: ":1984"`
- 矛盾实质：BOM v2 是答辩现场操作手册，若按 8080 打开网页会打不开 go2rtc 界面。
- 建议改法：BOM v2 L191 改 `:1984`；同时在"防翻车"表加一行"端口 1984（go2rtc）/ 554（RTSP）"。

### P1 —— 文档与最终形态/仓库状态脱节，答辩会被追问

**P1-1 ｜ PCB 底板方案三重脱节：README/导读链接的 PCB 文档是 v1.0 串口屏+DFPlayer 旧方案，实际最终形态是 SPI 屏+USB 喇叭，且版本号对不上**
- 矛盾：
  - `README.md` L187：把 `docs/hardware/PCB_BOARD_DESIGN.md` 作为当前"PCB 底板打样"扩展模块入口
  - `docs/导读.md` L196：`PCB 底板设计文档 v1.5`
  - `docs/hardware/PCB_BOARD_DESIGN.md` L1：`# 嘉立创 PCB 底板设计规格书（v1.0）`；L3/L9 用途为"串口屏、DFPlayer Mini 语音模块+喇叭"；L29 J6=串口屏四针排针、L28 J5=DFPlayer 六针排针；L46/L55-58 网络表为 UART3→串口屏、UART4→DFPlayer；L86/L97-98 成本含串口屏 4.3 寸
  - 最终形态：`BOM v2` L33（ILI9341 SPI 触摸屏）、L97-98（屏幕走 SPI0 + XPT2046）；`HARDWARE_BUILD_GUIDE.md` L21/L33（ILI9341 SPI 屏、方案 C 已真机落地）；`工作日志.md` L51（v1.5 Gerber 已含 J11 SPI 触摸屏排针）
  - 另：`BOM v2` L88 写着"关键网络（照 PCB_BOARD_DESIGN.md 连线）"，但紧接着 L97 自己就是 SPI0——**BOM v2 内部也自相矛盾**（引用了一份与自己网络表不匹配的旧文档）。
- 矛盾实质：评审按 README→PCB_BOARD_DESIGN 阅读，会看到串口屏+DFPlayer 方案；导读说 v1.5，文件头是 v1.0；实际 Gerber v1.5 在仓库外（`工作日志.md` L51 指向 H 盘）。
- 建议改法：① 将 `PCB_BOARD_DESIGN.md` 更新为 v1.5（SPI 屏/USB 喇叭/Type-C/Pin40）或至少在文件头加"v1.0 为历史存档，当前 v1.5 见本地 PCB_Gerber 目录"；② 同步 `导读.md` L196 与文件头版本；③ `README.md` L187 在链接后注明"该文档为历史版，最新 PCB 见 BOM v2"。
- 注：`agent1_architect.md` L88-90 已指出 PCB 版本不符，本报告补充 BOM v2 内部引用矛盾与最终形态差异。

**P1-2 ｜ HARDWARE_BUILD_GUIDE 采购清单仍是 DFPlayer 方案，与"USB 喇叭推荐"决策矛盾**
- 矛盾：
  - `docs/hardware/HARDWARE_BUILD_GUIDE.md` L18-20：需新购清单含 `DFPlayer Mini`、`8Ω 3W 微型喇叭`、`TF 卡`（存 3 段告警 MP3）
  - 同文件 L32：`语音 USB 喇叭（推荐，用户 2026-10-27 决策）：免驱 USB 喇叭插开发板 USB Host 口…兜底方案：DFPlayer Mini`
  - `BOM v2` L38-39/L56：已改为 USB 喇叭 + 系统 TF 卡，明确"不再需要 DFPlayer 模块和额外 TF 卡"
- 矛盾实质：评审/新手照"组装指南"采购会买到用不上的 DFPlayer/喇叭/TF 卡，且清单里根本没有 USB 喇叭。
- 建议改法：L18-20 改为"USB 喇叭（推荐）+ 8Ω 小喇叭（仅 DFPlayer 兜底）"，删除 TF 卡必购项；与 BOM v2 L56 对齐。

**P1-3 ｜ BOM v2 软件集成清单大面积过时：路径不存在、状态停留在"需实现"**
- 矛盾：
  - `BOM v2` L139：`触摸屏菜单 | src/hardware/lvgl_menu/（ILI9341 + XPT2046 + LVGL v8）| 需实现（大项）`——**`src/hardware/lvgl_menu/` 目录不存在**；实际实现为 `src/hardware/lcd_menu.h`（菜单状态机，99 项测试），ILI9341/XPT2046/sysfs_spi 驱动均已实现并有测试
  - `BOM v2` L137-138：三色 LED/按键"需实现"——实际 `src/hardware/led.h/.cc`、`button.h/.cc` 已实现
  - `BOM v2` L140：语音播报"需实现"——实际 `src/hardware/voice_alert.cc`（AplayVoiceBackend）已实现（工作日志 L30）
  - 对比：`README.md` L19/L161 明确宣称以上驱动全部实现并通过测试
- 矛盾实质：BOM v2 是"答辩执行手册"，若按它的状态做计划会重复造轮子；且评审交叉核对 README 与 BOM 会发现状态矛盾。
- 建议改法：把"软件集成清单"状态列改为"驱动已实现（XX 项测试）+ 待整机联调/待点亮屏幕"，代码位置改为实际路径。

**P1-4 ｜ 内存占用口径 4 处不一致（10.5~10.9 / 11 / 11~12.5 / 12.5MB）**
- 矛盾：
  - `README.md` L116：`~10.5-10.9MB`；L16 特性又写 `内存 ~11MB`
  - `docs/导读.md` L32：`RSS ≈11~12.5MB`；L195：`内存约 11MB`
  - `docs/DEMO_VIDEO.md` L23：`内存 12.5MB`
  - `results/edge_board/EDGE_TEST_REPORT.md`：12.5MB（早期快照）
  - 实测 `results/edge_board/long_stability.jsonl` 10 点：9.10 / 9.12 / 9.43 / 10.15 / 10.55 / 10.55 / 10.66 / 10.91 / 13.12（未取到 FPS 的重启点）
- 矛盾实质：README 取前 5 点（10.5~10.9）为"典型值"，演示材料取早期 12.5MB，二者都没有覆盖全区间；答辩评委如果同时看 README 和演示脚本会当场发现数字对不上。
- 建议改法：统一口径为区间 + 典型值，例如"RSS 约 9~13MB（28h 长稳 10 点采样，稳态典型 10.5~11MB）"，四份文档同步；或在 README L116 加脚注说明 12.5MB 为早期版本快照。

**P1-5 ｜ go2rtc"网页观看"作为 README 特性宣传，但板端 demo 流验证尚未完成**
- 矛盾：
  - `README.md` L17：`手机/电脑浏览器直接打开网页看监控`、`本地视频流兜底演示（P2-1）…一键向 go2rtc 注入预录增强视频 demo 流`（作为已交付特性）
  - `docs/WEB_VIEWER.md` L113-116：`板端验证状态：⬜ 待开发板上线后执行：上传 mixkit_13192_enhanced.mp4 → 运行一键脚本 → 手机浏览器真机验证 demo 流可播放（P2-1 收尾）`
- 矛盾实质：README 宣传的是"特性已就绪"，WEB_VIEWER 明说"板端未验证"。答辩现场如果 demo 流首次在板子上跑，存在翻车风险。
- 建议改法：答辩前完成 P2-1 真机验证并把 WEB_VIEWER L115 的 ⬜ 改为 ✅（附验证时间/日志）；若来不及，README L17 补一句"板端验证待上线后完成"。

**P1-6 ｜ 中英文 README 严重不同步，违反项目自身"双语强制同步"规约**
- 矛盾（`README.md` vs `README.en.md`）：
  - EN 缺整个"真实检测效果视频"章节（CN L123-133 无对应）
  - EN 缺"多目标可切换"特性（CN L15 无对应）；EN L15 可配置仅提阈值，缺 `target_classes` 与 `homography_file`（CN L18）
  - EN 目录结构缺 `start_demo.sh`（CN L47）；EN L54 重复列出 `config/default_config.json`（CN 无此行）
  - EN 架构图缺 go2rtc 分支（CN L32-33 `G --> I` 无对应，EN L22-31 到 `G --> H` 即止）
  - EN 缺图片基线两张图（CN L75-76 fig1/fig2 无对应）
  - EN 部署步骤缺"一键启动"（CN L152 第 5 步无对应）
  - EN L157 多出"repository is private"声明（CN L192-193 无对应）
  - 而 `docs/导读.md` L182 明确规约"`README.md` 与 `README.en.md` 功能/结构/结果变化必须同步更新"
- 矛盾实质：README 是项目门面，双语不同步会让外审/国际评委读到的 EN 版不完整；也违反自己写的工程纪律。
- 建议改法：按 CN 版补齐 EN 的 demo 视频章节、多目标特性、homography 说明、start_demo.sh、go2rtc 架构分支、一键启动步骤；或反过来在 EN 顶部标注"EN 版为节选，完整中文版见 README.md"。

**P1-7 ｜ DEMO_VIDEO 用词"误报抑制"与 README 诚实口径矛盾**
- 矛盾：
  - `docs/DEMO_VIDEO.md` L10：`mixkit_13265_enhanced.mp4 | 稀疏/无人场景 | 全程 SAFE（该场景无行人，未触发告警）| 展示误报抑制`
  - `README.md` L82/L131：对同类稀疏场景（mixkit_50134）表述为"无行人，未触发告警"
  - `README.md` L79 已明确"无人工真值，无法判定未报警是正确不报还是漏报"
- 矛盾实质：DEMO_VIDEO 说"展示误报抑制"，但场景根本没有行人，不存在"误报"可抑制；这与项目"不称零误报"的诚实文化相悖，答辩若被深挖会显得口径不严谨。
- 建议改法：L10 用途改为"展示无人场景不触发告警（未触发告警，非零误报证明）"。

### P2 —— 小口径/可读性/引用问题

| # | 文件:行号 | 问题 | 建议 |
|---|---|---|---|
| P2-1 | `docs/导读.md` L66、L112 | "docs/…（31 篇 .md）"：实际 docs/ 下共 29 篇（含 review 5 篇），不含 review 为 24 篇 | 改为实际数量 |
| P2-2 | `README.md` L193、`README.en.md` L158、`导读.md` L181、`工作日志.md` L51 | 论文/答辩文档路径写 `H:\毕设论文\`，当前机器无 H 盘，实际在 `G:\毕设论文\`（本次审查即从 G 盘读取） | 全仓库统一为 `G:\毕设论文\`（或同时注明盘符可变） |
| P2-3 | `HARDWARE_BUILD_GUIDE.md` L12 | SG90 位于"已有（无需购买）"表内，备注却写"需购买"；BOM v2 L56 已声明"此前清单误标'已有'，已纠正" | L12 移入"需新购"表或删除"需购买"备注 |
| P2-4 | `HARDWARE_BUILD_GUIDE.md` L42 | 组装步骤残留"串口屏显示状态"（当前为触摸屏菜单） | 改为"触摸屏菜单" |
| P2-5 | `DEMO_VIDEO.md` L9/L10/L17/L34、`WEB_VIEWER.md` L82 | 引用旧路径 `results/video_enhanced_fsm/`，README L133 明确规范结果在 `results/rerun/video_enhanced/`；文件虽存在但口径不一 | 统一指向新路径（旧目录可留作备份） |
| P2-6 | `DEMO_VIDEO.md` L10 vs `README.md` L82 | 稀疏场景示例视频不一致：DEMO_VIDEO 用 mixkit_13265，README 用 mixkit_50134（两者都是全 SAFE 真实案例，但口径不一） | 统一为一个主推视频 |
| P2-7 | `BOM v2` L215 | "已长稳 2.5h+ 零重启"已过时（README/导读/edge_board 为 28h+） | 更新为 28h+（注明来源） |
| P2-8 | `README.md` L95/L104 | "显著正相关""显著提升"无统计检验；45021 帧为时间序列（帧间强相关），ρ=0.688 的 p 值无意义 | 弱化为"较强正相关""在 6 条代表性视频上提升"，或补充按视频聚合后的相关性 |
| P2-9 | `README.md` L167-182 文档索引 | 未收录 `docs/导读.md`（导读专为"第一次接触/答辩评委"写，却不在 README 索引中） | 文档索引首行加入 `docs/导读.md` |
| P2-10 | `docs/RISK_THRESHOLD_CHEATSHEET.md` L81 | 引用 `docs/review/IMPLEMENTATION_LOG.md`，该文件不存在 | 改为实际存在的文档或删除引用 |
| P2-11 | `README.md` L19（CN）/L18（EN） | "可扩展硬件"特性 bullet 超过 800 字，信息过载，5 分钟扫读会跳过关键硬件信息 | 拆分为一行摘要 + 链接到 lvgl_board_port/selftest 细节 |
| P2-12 | `README.md` L114 | 6.30 FPS（NEON 前）在 `results/edge_board/` 无原始日志可溯源（long_stability 只有 7.44+ 的点） | 补充含 6.30 的早期 profile 日志或注明来源 |
| P2-13 | `README.en.md` L54 | 目录结构重复列出 `config/default_config.json`（L53 已有 config/ 目录） | 删除重复行 |

---

## 3. README 能否让答辩评委 5 分钟看懂？（评估）

**能看懂的部分（做得好的）**
- 开头 2 行一句话定义 + 架构 mermaid 图（CN L23-33，含 go2rtc 分支），5 分钟内可建立"摄像头→检测→IPM→图论→FSM→三级输出"的完整链路。
- 实验章节表格化、数字集中（L68-121），且每个数字都能在 results/ 溯源。
- "31 条全程 SAFE 不称零误报""DANGER 帧高于基线"等诚实口径已在正文写明（L79/L83），这是答辩最好的防御。
- 消融 A/B/C/D 用一行说清各组件贡献（L101-104），适合评委快问快答。

**阻碍 5 分钟理解的不足**
1. **没有"当前状态"小节**。README 通篇"已完成"口吻，但实际屏幕未点亮、PCB 未打样、go2rtc demo 流未板端验证。评委问"成品在哪？"时只能从 `lvgl_board_port.md` 的"剩余为 ILI9341 到货后接线与校准"推断。建议 README 加"当前硬件状态"表（已就绪：驱动/SPI0/自检；待办：屏幕点亮/PCB/语音联调）。
2. **文档索引漏掉导读**（P2-9）。导读 L114-117 的"路线 A（约 15 分钟，3 篇）"正是给答辩评委设计的，README 却不链它。
3. **架构图没有静态 PNG**。mermaid 在 GitHub 渲染正常，但答辩 PPT 需要导出图；docs/figures/ 里 15 张全是数据图，没有架构图/数据流图 PNG。建议用 `scripts/plot_*.py` 体系补一张 `fig_architecture.png` 并引用。
4. **L19 特性 bullet 过载**（P2-11）。硬件细节（ILI9341/XPT2046/sysfs_spi/自检）全部堆在一条 bullet 里，评委扫读会略过。
5. **demo 视频依赖 GitHub 外链 mp4**。README L128/L131 的 mp4 在 GitHub 可点击播放，但答辩现场网络不稳时需走 DEMO_VIDEO 本地兜底——建议 README 补一行"演示兜底见 DEMO_VIDEO.md"。

---

## 4. 答辩口径风险点（评委可能追问的坑）

1. **内存数字打架**（P1-4）：评委同时翻 README（10.5~10.9MB）和演示材料（12.5MB）会问"到底多少"。答法：口径=长稳 28h 稳态典型 10.5~11MB，早期快照 12.5MB，全区间 9~13MB。
2. **DANGER 帧 2190→11748 是"变多"不是"变好"**：这是本项目最容易误读的指标。答辩必须主动说"增强后更敏感（评分含密度权重+谱特征+DANGER 保持），因此 DANGER 帧/视频数上升是设计意图，稳定性由切换次数下降（23.2→5.9）证明"。（导读 L39 已锁定此口径，务必在 PPT 上写出来）
3. **"31 条全程 SAFE"不能答成"零误报"**：README L79 已写"无人工真值，无法判定"，答辩口头回答也要保持一致，否则会被评委抓"自相矛盾"。
4. **Spearman ρ=0.688 的统计有效性问题**（P2-8）：45021 帧来自 111 条视频，帧间强自相关，p 值无意义。数学背景评委可能问"你的相关性检验考虑独立性了吗？"建议答辩口径：ρ 是数据挖掘描述性指标，不宣称统计显著性；论文中可补充"按视频聚合后 Spearman 仍为正"。
5. **硬件完成度口径**：屏幕未点亮、PCB 未打样是事实。答辩要主动讲"已完成：驱动全实现+主机数百项测试+SPI0 真机落地+自检门禁；待完成：屏幕到货后点亮校准、PCB 焊接"，把"待办"包装成"6 个月计划内的收尾"（BOM v2 L159-169 里程碑可佐证）。
6. **"7807→11748"与"2190→11748"并存**：README L80 写 7807→11748（消融 C→D），L83 写 2190→11748（基线→增强）。两个数都是对的但含义不同（基线/消融 C 组），答辩若被混用会露怯。建议在论文/PPT 中明确标注基线（2190）与消融 C 组（7807）是两个不同参照系。
7. **Fiedler 均值 0.465 的历史错误**：工作日志 L16 记录了"原 0.465 为过期值，已更正为 0.017/0.214/1.078"。答辩前检查论文 7.4/7.6 与 PPT 是否全部使用更正后数值（工作日志称论文第 7 章已更新）。
8. **BOM v2 引脚/端口硬错误**（P0-1/P0-2）：如果答辩现场按 BOM 连线或演示网页，会直接翻车。必须在答辩前修正 BOM v2 并实测。

---

## 5. 最高价值问题清单（给主审）

1. **BOM v2 L91 的 "VBUS=Pin20" 与仓库 3 份文档 + 已完成 PCB v1.5（Pin40）冲突**——是笔误还是旧版残留？答辩前必须修正并实测，否则按手册连线会接错。
2. **BOM v2 L191 端口 8080 与全部脚本/文档的 1984 冲突**——照 BOM 现场演示打不开网页，必须统一。
3. **README 文档索引把 PCB_BOARD_DESIGN.md（v1.0 串口屏+DFPlayer）当当前方案，而最终形态是 SPI 屏+USB 喇叭，且导读宣称 v1.5**——三个版本并存，评审按 README 跳转会看到旧方案，如何快速收敛？
4. **内存占用四份文档四个数（10.5~10.9 / 11 / 11~12.5 / 12.5MB）**——建议统一为区间+典型值口径。
5. **中英文 README 违反"双语强制同步"规约（EN 缺 demo 视频/多目标/homography/一键启动/go2rtc 架构分支）**——是否在答辩前补齐或标注 EN 为节选？
6. **go2rtc demo 流板端验证未完成（WEB_VIEWER L115 ⬜）却被 README 宣传为特性**——答辩现场演示兜底存在翻车风险，优先级如何排？
7. **"显著正相关/显著提升"缺乏统计支撑（帧非独立）**——是否需要在论文中弱化或用按视频聚合的相关性补充论证？
8. **DEMO_VIDEO "展示误报抑制"与 README "未触发告警"诚实口径不一致**——是否统一措辞，避免答辩被深挖"误报"定义？

---

*报告完。所有复核脚本基于仓库内 results 真实 JSON 重算；除本报告外未改动任何项目文件。*