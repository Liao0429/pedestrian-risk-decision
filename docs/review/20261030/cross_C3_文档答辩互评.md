# 交叉互评报告（C3：文档口径与答辩一致性）

> 评审人：C3（交叉互评专家 · 文档口径与答辩一致性）
> 日期：2026-10-30
> 对象：`agent_B5_演示答辩.md`、`agent_B1_算法统计.md`、`agent_B8_可复现性.md`
> 方法：逐一抽查三份报告引用的关键证据（README.md / README.en.md / BOM v3 手册 / lvgl_board_port.md / selftest.md / 导读.md / WEB_VIEWER.md / docs/algorithms.md / risk_fsm.py / crowd_test.py / graph_laplacian.py / enhanced_decision.cc / reproduce_consistency.py / requirements.txt / dataset 脚本 / .gitignore / _aggregate.json / _consistency.json / _spectral_fsm_experiment.json / summary.json / 嘉立创下单说明.md / 装配验证报告.md 等），并**独立运行** `scripts/reproduce_consistency.py`、`pytest tests -q` 与自写回放脚本复算 B1 的统计数字。
> 环境：B5 声称「terminal 不可用（G: 盘命令超时）」；C3 在相同机器上 terminal、pytest、复现脚本**全部成功运行**，B5 的「运行态未验证」项已由本次交叉评审补齐。

---

## 一、总体结论

三份报告**总体可靠，核心 P0 主张全部经得起独立复算**。B1 的 P0-1/P0-2（时间线早于门控修复、README 生产口径失真）被 C3 独立重算**精确命中**（4041 帧 n<3 DANGER、48 条视频、M0=7801/PROD=8452、ρ=0.6882 等），是本次评审证据链最硬的问题；B5 的硬件状态冲突（README 特性表 vs BOM v3 §六）事实成立但需修正两处细节（行号 L16→L19、定性「声称已实现」→「未标注未完成状态」）；B8 的四个 P1 可复现性缺口全部属实，且 C3 实测其复现输出与 B1 完全一致（89.6737%/91.2974%/90.5911%/91.6350%，190 passed/4 skipped）。

**三份报告的最大冲突点**：B8 评价「README 数字口径做得非常好」，而 B1 给出 P0 级相反证据（README 头部统计建立在过期时间线上、生产口径失真）。两者并不矛盾——B8 只验证了「一致率数字可复现」，没有审查「等级分布/DANGER 帧数/切换次数/Fiedler 统计」这些头部数字的当前代码语义；B8 的 README 评价需限定范围。

---

## 二、三份报告核心主张逐条交叉验证

### 2.1 B5（答辩模拟）——9 条 P0/P1 + 1 条 P2 级许可

| 编号 | B5 主张 | B5 证据 | C3 独立核验 | 评级 |
|---|---|---|---|---|
| P0-1 硬件展示闭环未完成（屏/语音/云台/LED 仍「需实现」） | BOM v3 §六 L203-211；lvgl_board_port L3-6；导读 L200-202 | ✅ 逐条属实：BOM v3 L205-209 确为「需接入主循环/需实现（大项）」；lvgl_board_port L3-6 自述「ILI9341 尚未采购」；导读 L200-202 待办一致；selftest 两轮真机 0 PASS/6 FAIL（SPI 从「未找到节点」进步到「LCD ID 不匹配 0x9341」） | **交叉确认** |
| P0-2 README 特性表与答辩方案状态冲突 | README L16 | ⚠️ 事实成立，**行号偏差**：相关特性在 **README L19**（L16 是「真机性能」条目）；且 **README.en.md L19 有完全相同的特性表述**（B5 未覆盖英文版）。定性需修正：README 措辞是「可扩展硬件」+ 驱动测试数量（123/98/40 项），并未直接宣称「整机已闭环」，真正问题是没有像 README L12 对 DBSCAN 那样标注「**未接入主循环**」，易被评委误读为已实现 | **交叉确认**（细节修正） |
| P0-3 外设采购/焊接/联调时间风险 | BOM v3 L91-111、L145-150 | ✅ BOM 必购 16 项（L91 标题「约 ¥170~300」与 L111「约 ¥175~310」并存，B5 引 L111 值正确）；焊接 2~3 小时属实 | **交叉确认** |
| P1-1 一致率三口径复杂度（89.67/98.21/91.3~91.6） | README L80；cross_R10 L34 | ✅ README L80 口径说明③与 `_consistency.json` 一致；98.2097% 不可复现已存档。C3 实测四模式与 B1/B8 完全一致 | **交叉确认** |
| P1-2 Spearman 机制解释内部不一致（README「同源构造性相关」vs cross_R10「共线」） | README L95；cross_R10 L51-53 | ✅ README L95 确为「同源构造性相关」措辞；B1 实测 fiedler>0 子集 ρ=0.3503、全量 0.6882 由组间差异驱动，支持 B5 的担忧 | **交叉确认** |
| P1-3 7.46 FPS 实时性话术 | README L114-119 | ✅ L114-119 数字属实（7.46 FPS、136→33ms、10.5-10.9MB、28h+）。属建议性 P1，非事实错误 | **交叉确认** |
| P1-4 无人工 GT 验证局限 | README L79；rerun README L39-41 | ✅ 两处原文均见「无人工真值/无 GT，不称零误报」 | **交叉确认** |
| P1-5 阈值标定无 ROC/网格寻优 | CHEATSHEET L51-57 | ✅ 原文为抽样标定（DANGER≈0.67~0.68→up_danger=0.65），无统计寻优 | **交叉确认** |
| P1-6 无 SOTA 定量对比表 | 定性 | ✅ 全库检索 README/docs 无 CSRNet/DeepSORT/密度估计/mAP 对比章节（仅 review 报告自身命中） | **交叉确认** |
| P1-7 PCB/外壳无实物验证 | 嘉立创下单说明；装配验证报告 L55 | ✅ 下单说明仅有 Gerber/下单步骤；装配验证报告「10 对零件全部无干涉」但无打印实物记录 | **交叉确认** |
| P1-8 go2rtc demo 流未真机验证 | WEB_VIEWER L113-116 | ✅ 原文「⬜ 待开发板上线后执行」 | **交叉确认** |
| P1-9（B5 自归 P2）数据许可缺失 | 仓库未见许可 | ⚠️ **表述不精确**：`dataset/README.md` L24 已有 **COCO CC BY 4.0** 说明；`dataset/videos50/README.md` L9 有 Mixkit「免注册、免费使用授权」一句。但根 README L64 未链接这些说明、Mixkit 无许可条款原文/链接——核心建议（根 README 补许可入口）仍有效 | **交叉确认**（需修正表述） |

### 2.2 B1（算法统计）——2 条 P0 + 3 条 P1 + 6 条 P2

| 编号 | B1 主张 | B1 证据 | C3 独立核验 | 评级 |
|---|---|---|---|---|
| **P0-1** 核心时间线早于门控修复，34.4% DANGER 帧不可复现 | timeline mtime vs commit 284de8d；n<3 帧统计；回放分布 | ✅✅ **C3 独立重算全部命中**：存档 DANGER=11748，n<3=**4041**、涉及 **48 条视频**、n 桶 {0:173, 1:1075, 2:2793}（与 B1 逐位一致）；commit 284de8d=2026-10-29 21:32，timeline mtime=2026-10-28 15:16（早于门控修复）；当前 PROD fps 回放 SAFE 28214/WARNING 8355/DANGER **8452**、DANGER 视频 **45**、切换均值 **5.12**、n<3 现 DANGER **1203**（B1 的 1203 一致；与存档 n<3 重叠 C3 算 1176 vs B1 的 1163，±13 帧，属回放模式/滞回路径微差，非实质冲突） | **交叉确认**（强） |
| **P0-2** 「7807→11748」归因错误，把门控差异算成谱特征贡献 | README L80 | ✅✅ C3 实测 M0(fps) DANGER=**7801**、PROD(fps)=**8452**，谱特征净贡献 **651 帧（+8.3%）**，与 B1 完全一致；README L80 原文确为「7807 → 11748（谱特征提升高风险识别）」 | **交叉确认**（强） |
| P1-1 docs/algorithms.md 缺 w4 与 min_danger_n | grep 无命中；risk_fsm.py 含 | ✅ grep `w4\|min_danger` 于 docs/algorithms.md **零命中**；risk_fsm.py L27/88/92/236-240 含 w4、L94-95/244-248 含门控 | **交叉确认** |
| P1-2 Python 原型 n/density/r_gcc 未去重（板端已去重） | crowd_test.py 无去重；graph_laplacian.py L152-158 去重；enhanced_decision.cc 用 computeFeatures | ✅ crowd_test.py::compute_features（L157）无去重逻辑，crowd_test_enhanced.py 亦无；graph_laplacian.py L152-158 有 P0-4 去重；enhanced_decision.cc L148-166 走 graph_.computeFeatures()（去重后）——Python/板端输入不一致成立 | **交叉确认** |
| P1-3 Spearman 稳健性数字未披露 | 全量 0.6882；80 条有效均值 0.3154/中位 0.2849；含 31 条 0 均值 0.2273；fiedler>0 子集（10814 帧）ρ=0.3503 | ✅✅ C3 实测：全量 **0.6882**、80 条有效均值 **0.3154**/中位 **0.2819**（B1 0.2849，差 0.003）、含 31 条 0 均值 **0.2273**、fiedler>0（**10814 帧**）ρ=**0.3503**——几乎逐位一致 | **交叉确认** |
| P2-1 Fiedler 均值单调但中位数反转 | _aggregate.json 130-154 | ✅ SAFE mean 0.0174/median 0.0/std 0.1841；WARNING mean 0.2143/median 0.0（positive 仅 2250/6388）；DANGER mean 1.0779/median 0.814——与 B1 一致 | **交叉确认** |
| P2-2 M2 降级帧 fiedler 并不低 | _spectral_fsm_experiment.json | ✅ M2 仍 DANGER median 0.6901；被降级（原 D）median 0.817——与 B1 一致 | **交叉确认** |
| P2-3 检出率只报召回无 precision | summary.json | ✅ gt 3089/det 2548/82.49；worst_over_detected 存在；README L71 只给 82.5% | **交叉确认** |
| P2-4 M0 重算对 now_ms 模式敏感、best_mode 向存档对齐 | spectral_fsm_experiment.py L178-191 | ✅ 代码确在 frame/ms33 中选与存档一致率更高的模式（L180-191） | **交叉确认** |
| P2-5 高密度桶小样本 | summary.json 14-19 桶 | ✅ images=12、gt=175、det=139、79.43% | **交叉确认** |
| P2-6 门控只约束进入不约束保持 | risk_fsm.py L244-248；回放 1203 帧 | ✅ 代码仅封顶 score（up_danger-0.01），降级靠 down_warning=0.55；C3 实测回放 n<3 DANGER=1203 | **交叉确认** |

### 2.3 B8（可复现性）——4 条 P1 + 5 条 P2

| 编号 | B8 主张 | B8 证据 | C3 独立核验 | 评级 |
|---|---|---|---|---|
| P1-1 reproduce_consistency.py 全新 clone 除零 | :88 除零；main 无空数据防护；test_spectral 有 skipif | ✅ 代码 :88 `agree/total*100.0` 无 total=0 防护；main 不检查空时间线；tests/test_spectral_fsm_experiment.py L30-32/96-97 确有 skipif 防护而本脚本没有。补充：全新 clone 若 `video_enhanced` 目录不存在会先抛 FileNotFoundError，存在但为空才 ZeroDivisionError——**两种情形都是崩溃**，B8 结论成立 | **交叉确认** |
| P1-2 README 缺环境安装/数据获取步骤 | README 无 pip/install/requirements/pytest/环境；部署方法直接给命令；pytest 命令仅在导读 L141 | ✅ C3 通读 README 全文 193 行，确认无上述字样；导读 L141 确有 `python -m pytest tests/ -q` | **交叉确认** |
| P1-3 数据集脚本硬编码本机路径与代理 | download_mixkit.sh L4/L21；download_50_videos.py L11/L13；check_people.py L6 | ✅ 逐行核实：IDFILE=`/h/PedestrianRiskDecision/mixkit_good_ids_lf.txt`（文件不在仓库）、curl `-x http://127.0.0.1:7890`；OUT=`H:/...`、PROXY；vdir=`H:/...` | **交叉确认** |
| P1-4 requirements 注释声称锁上限实际只有下限 | requirements.txt L4 注释 vs L5-8 | ✅ L4「主版本锁上限」与 L5-8 全部 `>=` 无上限并存 | **交叉确认** |
| P2-1 脚本 docstring 残留本机路径 | generate_golden.py:22；test_golden_regression.py:11 | ✅ 两处均为 `H:/py311_gpu/Scripts/python.exe ...` | **交叉确认** |
| P2-2 README/导读/工作日志 H: 盘符残留 | README L193、en L159、导读 L182、工作日志 L7/22/51 | ✅ README L193、README.en L159、导读 L182、工作日志 L7/L51 均核实 | **交叉确认** |
| P2-3 CMake 与 Makefile 测试目标不一致 | CMakeLists 10 个 add_test；Makefile test-sysfs-spi | ✅ grep CMakeLists `add_test` 计数=10；Makefile L27/38/133 有 test-sysfs-spi（Linux 才纳入） | **交叉确认** |
| P2-4 CI 未覆盖复现脚本 | ci.yml 三 job 均不跑 reproduce | ✅ ci.yml 仅 python-tests/cpp-tests/cmake-tests，无 reproduce_consistency | **交叉确认** |
| P2-5 代理/内网 IP 未标示例 | WEB_VIEWER L21；ARCHITECTURE L95 | ✅ WEB_VIEWER L21 硬编码 `https_proxy=http://127.0.0.1:7890` | **交叉确认** |

---

## 三、一致点

1. **一致率数字三方一致（B1 ∩ B8 ∩ C3）**：`reproduce_consistency.py` 输出 M0_ms33=89.6737%、PROD_ms33=91.2974%、PROD_frame=90.5911%、PROD_fps=91.6350%，B1、B8 报告与 C3 实测**逐位相同**；pytest 均 190 passed/4 skipped（B8 6.74s、C3 6.54s）。→ **89.67% 与 91.3~91.6% 的可复现性交叉确认**。
2. **98.21% 不可复现已标注**：B5（Q3）、B1（附件）、B8（2.1）三方一致认可 README L80 对 98.21% 的标注属实，`_consistency.json` 存档 98.2097% 无法复现。
3. **Spearman 0.688 表述问题方向一致**：B5 Q4 与 B1 P1-3 都认为 README L95 的「同源构造性相关」解释站不住，应改用「共线/组间差异 + 帧非独立 + 按视频聚合 0.23」表述——B1 用数据（ρ=0.3503）支持了 B5 的答辩话术担忧。
4. **检出率口径（召回）**：B5 Q2（文档能回答，导读 L38 提醒「不是识别精度」）与 B1 P2-3（README 未注明 precision）互补，都指向「82.5% 是 det/gt 召回口径」这一事实。
5. **硬件驱动「已实现」与「未集成」的分层认知**：B5 P0-1 与 B8 引用的 cpptest.log（hardware 118/lcd_menu 99/ili9341 123/xpt2046 98/selftest 80 全过）不冲突——驱动层已实现并主机测试，外设未采购/未接入主循环。

## 四、冲突点

1. **B8 对 README 口径的高度评价 vs B1 的 P0（最重要冲突）**：B8 结论「README 实验数字口径做得非常好（89.67 可复现 / 98.21 标注、生产公式与 M1/M2 区分）」，而 B1 P0-1/P0-2 证明 README 头部统计（11748/66 条/5.86/0.688/7807→11748）建立于**门控修复前的过期时间线**，生产口径失真。**调和**：B8 验证的是「一致率数字可复现」成立；B1 质疑的是「等级分布/DANGER 帧数/切换次数」这些头部统计的当前语义，同样成立。B8 的「口径做得好」结论应限定为「一致率与差异原因标注做得好」，且 B8 遗漏了对时间线时效性的审查（见第五节）。
2. **B5 对 README 特性表的定性偏重**：B5 称 README「把语音告警/双舵机云台/触摸屏菜单列为已实现特性」；实际 README L19 措辞是「可扩展硬件」并给出驱动与测试证据，未宣称「整机闭环」，但**没有**像 DBSCAN 那样标注「未接入主循环」。冲突实质（易误读、与 BOM v3 §六状态不一致）成立，定性建议改为「未标注集成未完成状态」。
3. **B5 Q1 许可缺失表述过度**：B5 称「仓库内未见 Mixkit/COCO 具体许可证」；实际 dataset/README.md 已有 COCO CC BY 4.0，videos50/README.md 有 Mixkit 一句授权。缺失的是**根 README 入口与 Mixkit 条款细节**，不是「仓库完全没有」。
4. **B5 环境声明 vs B1/B8 实测**：B5 声明「terminal 不可用（G: 盘命令超时）」，B1/B8/C3 均成功运行。这是 B5 子代理的环境问题，不是项目问题；B5 的「运行态未验证」已由 C3 补验，其静态证据全部属实。

## 五、遗漏点

### 5.1 B5 漏了什么（对照 B8 的路径/许可问题）

- **漏：B8 的路径问题只抓了一半**。B5 P2-7 抓到了 `嘉立创下单说明.md` 的 v1 标题 + H: 路径，但**漏了** `README.md:193`、`README.en.md:159`、`导读.md:182`、`工作日志.md:7,51` 的 H: 盘符残留（B8 P2-2），也**漏了** dataset 下载/校验脚本硬编码本机路径与代理（B8 P1-3）——后者才是真正阻断第三方复现的路径问题。
- **漏：README 缺环境安装/数据获取步骤**（B8 P1-2）——答辩文档口径下不显眼，但属于「文档能否支撑从零复现」的硬缺口。
- **漏：reproduce_consistency.py 全新 clone 崩溃**（B8 P1-1）——答辩现场若评委要求当场复现一致率，脚本在干净环境会崩。
- **漏：README.en.md L19 存在同样的硬件特性表述问题**——B5 只引中文 README，未检查双语同步（导读 L183 规约「README 双语强制同步」）。

### 5.2 B1 漏了什么

- **漏：README L104「显著提升高风险判定灵敏度」措辞未单列**。B1 验证了消融 D 组 +152 帧可复现，其「未验证项 3」也承认未做显著性检验，但没有把 README 的「**显著**提升」作为文档问题列入清单——仓库内 README/algorithms.md/SYSTEM_PRINCIPLE.md 均无任何显著性检验/ p 值记录，「显著」一词无统计支撑。建议在 B1 清单补一条 P2。
- **漏：可复现性工程入口与其 P0-1 修复建议的耦合**。B1 建议「用 crowd_test_enhanced.py 重新生成 111 条时间线再重算聚合」，但未提重跑产物会被 `.gitignore` 排除、第三方 clone 后仍无法复现（需与 B8 P1-1 的空数据防护一起解决，否则重跑后 `_consistency.json` 会再次成为「本地可复现、clone 不可复现」状态）。

### 5.3 B8 漏了什么

- **漏：数据许可问题**（B5 Q1/P1-9）。B8 审查工程化时未检查数据集许可条款缺失——第三方若想合法复现数据链路，许可说明是必要组成。
- **漏：时间线时效性（B1 P0-1）**。B8 实测一致率可复现，但没有检查时间线文件生成时间早于门控修复、README 头部统计（11748 等）对应当前代码语义失效——这是其「README 口径非常好」结论需要打折的原因。
- **漏：硬件文档状态冲突**（B5 P0-2）。B8 引用 README 特性（如 L19 硬件）时未留意与 BOM v3 §六「需实现」的矛盾。

### 5.4 三份报告共同遗漏

- README.en.md 的同步问题（英文版同样存在硬件特性表述 + H: 盘符残留），三份报告均只查中文 README。
- 根 README 未链接 `dataset/README.md`（COCO 许可其实已在其中）——修复许可问题只需加链接，无需新写许可章节。
- B5 引「BOM 价格 ¥175~310」正确（L111），但 BOM v3 L91 标题写「约 ¥170~300」，同文档两处价格区间并存，B5 未指出。

---

## 六、合并文档修正清单

> 按 P0 → P1 → P2 排序；「来源」为提出报告的评审，状态 = C3 交叉验证结论。

### P0（必须修）

| # | 修正项 | 文件:位置 | 来源 | C3 状态 |
|---|---|---|---|---|
| 1 | 用当前代码（含门控）重新生成 111 条时间线，重算 `_aggregate.json`、`_spectral_fsm_experiment.json`、`_ablation.json`、`_consistency.json` 与图 4-9；对外口径废弃 11748/66 条/5.86 | results/rerun/*、README L79-83 | B1 | **交叉确认**（C3 独立复算命中全部数字） |
| 2 | README 删除/改写「7807 → 11748（谱特征提升高风险识别）」归因，改为门控后实测谱项净贡献约 +8%（M0 7801→PROD 8452）并注明版本差异 | README L80 | B1 | **交叉确认** |
| 3 | README 特性表（中文 L19 + 英文 L19）为语音告警/双舵机云台/三色 LED/触摸屏菜单补「驱动已就绪、主机测试通过，**未接入主循环/待外设到位**」状态标注，与 BOM v3 §六对齐；或答辩前完成接入并同步 BOM v3 状态 | README.md L19、README.en.md L19、BOM v3 §六 L203-211 | B5 | **交叉确认**（行号由 L16 修正为 L19；建议扩展到英文版） |
| 4 | 立即下单 BOM 必购 16 项并排期焊接/联调，答辩前完成屏幕点亮/触摸校准/云台与语音接入主循环 | BOM v3 §三/§四 | B5 | **交叉确认** |

### P1（重要）

| # | 修正项 | 文件:位置 | 来源 | C3 状态 |
|---|---|---|---|---|
| 1 | docs/algorithms.md 9.2 公式与 9.4 配置表补 w4=0.1 与 min_danger_n=3（含封顶语义） | docs/algorithms.md（当前 grep 零命中） | B1 | **交叉确认** |
| 2 | Python 原型 `compute_features` 入口补完全重合点去重（对齐 graph_laplacian.py L152-158），保证 Python/板端 FSM 输入一致 | scripts/crowd_test.py、crowd_test_enhanced.py | B1 | **交叉确认** |
| 3 | README L95 的 Spearman 表述改为「组间差异驱动 + 按视频聚合 0.23 + fiedler>0 子集 ρ≈0.35」；答辩话术同步 | README L95、导读 L43 | B5 + B1 | **交叉确认** |
| 4 | README 增「环境安装」小节：`pip install -r requirements.txt`、Python 版本、`pytest tests/ -q`、数据集已在库/重下方式；链接导读 §6 | README「部署方法」前 | B8 | **交叉确认** |
| 5 | reproduce_consistency.py 空时间线防护（明确指引 + 非零退出），CI 加合成数据冒烟 job | scripts/reproduce_consistency.py、.github/workflows/ci.yml | B8 | **交叉确认** |
| 6 | dataset 下载/校验脚本去本机化：IDFILE 入库存相对路径、代理改 `${PROXY:-}` 环境变量、`vdir/OUT` 基于 `__file__` 推导；或将脚本标注「历史采集，数据已入库，仅供溯源」 | dataset/videos50/download_mixkit.sh、dataset/_work/download_50_videos.py、dataset/videos50/check_people.py | B8 | **交叉确认** |
| 7 | requirements.txt 注释与实际一致：给上限（如 `ultralytics>=8.2,<9`、`numpy>=1.24,<3`）或生成 lockfile | requirements.txt L4-8 | B8 | **交叉确认** |
| 8 | 准备「1TOPS 约束 + 秒级响应 + 320×320 为下一步」帧率话术，建议补帧率-分辨率权衡实测 | 答辩材料 | B5 | **交叉确认**（建议性） |
| 9 | 准备无 GT 验证话术（灵敏度/稳定性维度，不声称准确率） | 答辩材料 | B5 | **交叉确认**（建议性） |
| 10 | 论文补 SOTA 定位/定量对比（或明确「不做人群计数，做风险分级」） | 论文/README | B5 | **交叉确认** |
| 11 | PCB 打样与外壳打印实物验证（DRC/干涉报告仅作设计验证） | — | B5 | **交叉确认** |
| 12 | go2rtc demo 流真机验证（P2-1 收尾） | WEB_VIEWER L113-116 | B5 | **交叉确认** |

### P2（建议）

| # | 修正项 | 来源 | C3 状态 |
|---|---|---|---|
| 1 | README L104「显著提升」改为「提升」（未做显著性检验），或补检验 | C3 补充 | **交叉确认**（新增） |
| 2 | 根 README 数据段链接 dataset/README.md（COCO CC BY 4.0 已有）与 videos50/README.md，补 Mixkit 许可条款链接 | B5/B8 合并 | **交叉确认**（表述修正） |
| 3 | 清除 H: 盘符残留：README L193、README.en L159、导读 L182、工作日志 L7/51、嘉立创下单说明 L1-3（顺带改 v1→v1.6 标题）、generate_golden.py L22、test_golden_regression.py L11 | B8 + B5 | **交叉确认** |
| 4 | CMakeLists 注册 test_sysfs_spi（Linux 条件）使 ctest 与 make test 对齐 | B8 | **交叉确认** |
| 5 | 代理/内网 IP 标注「本机示例，需替换」（WEB_VIEWER L21、ARCHITECTURE L95、dataset/videos50/README L147） | B8 | **交叉确认** |
| 6 | 统一 BOM v3 价格区间（L91「约 ¥170~300」 vs L111「约 ¥175~310」） | C3 补充 | **交叉确认**（新增） |
| 7 | Fiedler 表述改「均值随等级升高（分布右偏，SAFE/WARNING 中位数为 0）」 | B1 | **交叉确认** |
| 8 | M2 实验如实表述「未观察到对低 fiedler 帧特异性」或重设计 | B1 | **交叉确认** |
| 9 | README 检出率处补「召回口径（det/gt），未含 precision/过检率」 | B1 | **交叉确认** |
| 10 | 高密度桶（12 图/175 GT）注明样本量，不做桶间显著性对比 | B1 | **交叉确认** |
| 11 | 实验脚本统一生产 fps 模式，报告给出模式波动区间 | B1 | **交叉确认** |
| 12 | risk_fsm.py 文档补「门控仅禁止 n<3 进入 DANGER，进入后滞回仍可保持」 | B1 | **交叉确认** |
| 13 | 制作现场应急卡（--force 路径、外设排查、aplay 卡号） | B5 | **交叉确认** |

---

## 七、附：C3 本次实测输出

```
$ env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" scripts/reproduce_consistency.py
时间线：111 条，总帧数 45021
  M0_ms33: 40372/45021 = 89.6737%
  PROD_ms33: 41103/45021 = 91.2974%
  PROD_frame: 40785/45021 = 90.5911%
  PROD_fps: 41255/45021 = 91.6350%

$ env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" -m pytest tests -q
190 passed, 4 skipped in 6.54s

# C3 独立审计脚本（111 条存档时间线 + 当前代码 fps 回放 + Spearman）
存档 DANGER=11748；DANGER(n<3)=4041，48 条视频，{n=0:173, n=1:1075, n=2:2793}
M0(fps)：SAFE 28249 / WARNING 8971 / DANGER 7801，DANGER 视频 40
PROD(fps)：SAFE 28214 / WARNING 8355 / DANGER 8452，DANGER 视频 45，切换均值 5.12（中位 4），n<3 现 DANGER 1203
Spearman：全量 0.6882；80 条有效均值 0.3154/中位 0.2819；含 31 条 0 均值 0.2273；fiedler>0（10814 帧）ρ=0.3503
```

*报告完。C3 未修改任何项目文件；全部引用行号均经本次抽查核实，与三份报告原引行号的偏差已在上文注明。*