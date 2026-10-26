# 数据集 / 实验设计 / 指标口径 只读审查报告（Agent4）

> 审查对象：数据集来源与标注（`dataset/`）、实验脚本（`scripts/crowd_test.py`、`crowd_test_enhanced.py`、`ablation_study.py`、`rerun_all_videos.py`、`rerun_video_baseline.py`、`find_sparse_video.py`、`plot_results.py`）、实验结果（`results/`）、README/文档结论（`README.md`、`README.en.md`、`results/rerun/README.md`、`results/rerun/_ablation.json`、`_comparison_summary.json`、`results/rerun/video_enhanced/_aggregate.json` 等）。
> 审查方式：只读 + 对 `results.json`/`_aggregate.json`/`_ablation.json` 做数值复核（用 Python 重新计算检出率、高密度率、等级分布、消融切换次数）。未修改任何文件、未运行任何实验脚本。
> 日期：2026-10-26

---

## 一、结论摘要（先给结论）

1. **视频数据没有任何逐帧风险真值（GT）**，111 条视频全部只有“系统自身输出”。因此 README 中的 **“零误报”“切换稳定”“风险语义准确”等结论没有外部对照**，既不能证明“对”，也不能证明“错”——只能说明“系统内部一致”。**在补人工标注之前，不得报告准确率/召回率/误报率/漏报率**。
2. **图片只有“人数”GT，没有“风险等级”GT**。人数标注来自 COCO YOLO 标签（`coco2017labels.zip`），只验证“检测到几个人”，**不能作为风险真值**。风险等级（SAFE/WARNING/DANGER）是系统输出，无 GT 对照。
3. **检出率 82.5% 口径不严谨**：它是 `检测总人数 / GT 总人数` 的计数比，**没有 IoU 框匹配、没有精度、没有逐目标召回**；且检测器 YOLOv8n 在 COCO 上训练，评估集 500 张中有 50 张来自 **train2017（训练集本身）**，其余 val2017 也在训练分布内，**82.5% 不是独立测试**。
4. **视频“前后对比”基线不公平**：基线（`rerun_video_baseline.py`）用 **OR 规则**（`n≥10 或 rho≥0.55 或 gcc≥0.8` 即 DANGER），增强路径用 **FSM 综合评分 + 滞回**（另一套决策函数）。二者同时改变了“决策函数”和“时序平滑”，“切换次数↓91.4%”**混入了规则变更的贡献**，不是纯增强收益。
5. **消融实验 C 组（+Kalman+谱特征）与 B 组（+Kalman）输出完全相同**（`_ablation.json` 六条视频逐条验证：切换次数、等级分布逐帧一致）。README 宣称“谱特征提升风险语义准确性”**无任何实验支撑**；且消融基线 A 也用 OR 规则，与真实引擎不一致。
6. **帧级统计存在样本非独立问题**：45021 帧来自 111 条视频，相邻帧强相关，按帧聚合的等级分布/百分比有效样本量远小于表观值；应按“逐视频聚合后再平均”报告，并给出按视频重采样的置信区间。
7. **低照度漏检只“核对”未“量化”**：`find_sparse_video.py` 用**同一检测器**去验证“视频是否真稀疏”，属循环论证；31 条全程 SAFE 视频中到底几条是暗光漏检、几条是真空旷，没有落盘统计。
8. **报告数字自相矛盾**：中文 README 高密度检出率 **76.7%（9 张）**，英文 README **79.4%**；用 `results/rerun/images500_baseline/results.json` 重算为 **79.43%（12 张，GT≥14）**。英文 README 仍写“5 real videos”（中文已更新为 111 条）。`results/experiment_summary.json` 仍是旧版（仅 5 条视频）。

---

## 二、数据来源 / 标注 / 质量 / 可重复性

| 维度 | 现状 | 评价 |
|---|---|---|
| 图片来源 | COCO 2017 官方 CDN，450 张 val2017 + 50 张 train2017 | 真实公开数据，来源可查，许可 CC BY 4.0（图片版权归原摄影者，仅研究用途） |
| 图片人数 GT | `coco2017labels.zip`（Ultralytics 将 COCO 标注转 YOLO 格式，class 0=person），`dataset/_work/select_person_images.py` 统计 class 0 框数 | **人数 GT 可信**（计数级），可用于“检测到几个人”的召回评估 |
| 图片风险 GT | **无**。仅按人数分桶（分散 200 / 局部聚集 150 / 较拥挤 138 / 拥挤 12），`dataset/README.md` 把桶名直接映射为 SAFE/WARNING/DANGER | **人数桶 ≠ 风险真值**（见问题 2） |
| 视频来源 | Mixkit 免版权素材，`download_mixkit.sh` 从 `assets.mixkit.co/videos/<id>/<id>-360.mp4` 下载，111 条 | 真实素材，来源可查 |
| 视频 GT | **无逐帧人数、无逐帧风险等级** | **无任何风险/聚集真值** |
| 视频质量校验 | ffprobe + HOG 双重校验（README 声称 111/111 通过） | 仅“能打开、有人”，非内容级 GT |
| 可重复性 | 下载/挑选/校验脚本齐全（`_work/*.py`、`download_mixkit.sh`） | 数据获取可复现 |
| 可重复性缺陷 | `ablation_study.py:17-18`、`find_sparse_video.py:8-9`、`plot_results.py` 仍硬编码 `H:/PedestrianRiskDecision` 绝对路径；`ablation_study.py` 未设随机种子 | 换机器不能直接跑（Git 历史声称已清理但实际未清干净） |

---

## 三、具体问题清单（按严重度排序）

### 【P1-高】视频无逐帧风险真值，一切“准确/误报/漏报/稳定”结论无外部对照

- **证据路径**：
  - `dataset/videos50/` 仅含 mp4 + 下载脚本，无任何 JSON/CSV 标注；`dataset/README.md` 无视频 GT 说明。
  - `results/rerun/video_enhanced/_aggregate.json`、`results/rerun/_comparison_summary.json` 只含系统输出等级分布/切换次数，无 GT。
  - README.md“实验结论”中的“**31 条未触发风险报警（含少量低照度漏检）**”“零误报”“40 条出现 DANGER”均基于系统自身输出，无真值校验。
- **严重度**：高。这是论文最核心的度量缺口。
- **影响**：无法回答“系统把真实的危险场景漏报了几次、把安全场景误报了几次”；无法计算召回率/误报率/漏报率/预警延迟；所有“风险语义准确性”“零误报”类表述目前只是系统自说自话。
- **建议**：见“四、低成本人工标注方案”与“五、可复现实验设计”。**在补 GT 前，README/论文删除“准确率”“零误报”“误报/漏报”等字眼**，改用“系统等级分布”“切换次数”等自洽描述，并明确“未做真值评估”。

---

### 【P2-高】图片只有人数 GT，没有风险等级 GT；人数桶不能当作风险真值

- **证据路径**：
  - `dataset/images500/README.md`：标注为“ground-truth 人数”，按人数分桶（分散 1-3 / 局部聚集 4-9 / 较拥挤 10-13 / 拥挤 14-19）。
  - `dataset/README.md`（旧 30 图）直接写“分散(SAFE) / 局部聚集(WARNING) / 拥挤(DANGER)”——把**人数桶等同风险等级**。
  - `results/rerun/images500_baseline/results.json`：风险等级由 `evaluate_level(n,density,r_gcc)` 输出，无 GT 对照。
- **严重度**：高。
- **影响**：人数与风险不是一一对应——3 个人紧贴在一起也可能 WARNING，14 个人分散在整幅图可能是 SAFE。用人数桶当风险 GT 会给出错误“准确率”；当前报告只报“等级分布”（SAFE 414/WARNING 61/DANGER 25），没报“风险准确率”，这点是对的，但**必须防止后续把人数桶当 GT 去算风险准确率**。
- **建议**：为 500 张图片补**风险等级人工标注**（每张图标 SAFE/WARNING/DANGER），与检测无关、只看图中行人空间聚集程度；人数桶仅用于分层抽样展示，不作为真值。

---

### 【P3-高】“检出率 82.5%”口径不严谨 + 评估集含训练集（COCO 泄漏）

- **证据路径**：
  - `scripts/plot_results.py`：`overall = sum_det / sum_gt`，`gt` 取自文件名 `_<人数>` 后缀；`results/rerun/images500_baseline/results.json` 复核：sum_det=2548 / sum_gt=3089 = **82.49%**。
  - 复核发现：500 张中 88 张**过检**（检测数>GT）、226 张**漏检**（检测数<GT）、186 张相等。计数比会互相抵消，不能反映定位质量。
  - `dataset/_work/download_500_images.py`：`train_items = load_train_dense(train_csv, TARGET_TRAIN)` → **50 张来自 COCO train2017**（YOLOv8n 的训练集）；YOLOv8n 本身在 COCO 上训练。
  - 无 IoU 匹配代码：`crowd_test.py` 只累加检测框数量，不与 GT 框做匹配。
- **严重度**：高。
- **影响**：82.5% 既不是“逐目标召回”，也不是“独立测试准确率”，而是“COCO 训练分布上的计数比”。审稿人若追问 mAP/IoU/独立测试集，当前口径撑不住；且 50 张训练集图片会系统性抬高检出率。
- **建议**：
  1. 明确指标名称为“**计数召回率（count-based recall）**”，并在 README 注明“未做 IoU 框匹配、评估集与训练集同分布，含 50 张 train2017”。
  2. 若论文需要严谨检测指标：用 **COCO val2017 全量或独立采集的标注图片**做 mAP@0.5（需框级 GT），或用**该检测器在未见过的场景（如 Mixkit 抽帧）**上人工标框评估。
  3. 把 50 张 train2017 从“检出率评估”中剔除，或单独报告 train/val 两个子集。

---

### 【P4-高】视频“前后对比”基线不公平：基线 OR 规则 ≠ 增强 FSM 评分（混入决策函数变更）

- **证据路径**：
  - `scripts/rerun_video_baseline.py:34-41` `rule_level`：`if n >= d["n"] or rho >= d["rho"] or gcc >= d["gcc"]: return "DANGER"`（**OR**）。
  - `scripts/crowd_test.py` `evaluate_level`：`if n >= dn and (density >= drho or r_gcc >= dgcc)`（**AND**）。
  - `src/algorithms/risk_fsm.py`：增强路径用 **`score = w1*norm(n)+w2*density+w3*r_gcc` + 滞回阈值**，是第三套决策函数。
  - `results/rerun/_comparison_summary.json`：base_sw=5147 → enh_sw=444，宣称“切换次数↓91.4%”。
- **严重度**：高。
- **影响**：基线用 OR 规则（人数达标即 DANGER），天然切换更频繁、DANGER 帧更多（base_lv DANGER 21969 vs enh 7807）。因此“切换↓91.4%”“出现 DANGER 视频数↓（102→40）”“零误报↑3.4 倍”**部分是规则口径差异造成的，不是纯时序增强收益**，结论不可比。
- **建议**：**统一基线决策函数**。基线应使用与增强完全相同的决策函数（FSM 的 score + 滞回，只是去掉/保留时序层），或至少让基线也用 `evaluate_level`（AND 规则），只改变“是否加 Kalman/FSM”。即：公平对照应是“同一规则引擎 + 有无时序平滑”，而不是“两套不同规则”。

---

### 【P5-高】消融 C 组“+谱特征”零贡献，README“谱特征提升风险语义准确性”无支撑

- **证据路径**：
  - `results/rerun/_ablation.json`：6 条视频中 **B(+Kalman) 与 C(+Kalman+谱特征) 的切换次数、等级分布逐帧完全相同**（如 mixkit_134 都是 switches=16、SAFE195/WARN23/DANGER382；mixkit_4178 都是 45、SAFE14/WARN102/DANGER182）。
  - `scripts/crowd_test_enhanced.py:110-122`：谱特征计算后只写入 `feat` 字典用于显示，`fsm.update({"n":n,"density":density,"r_gcc":r_gcc})` **不包含任何谱特征**。
  - README.md：“**谱特征提升风险语义准确性**”无对应实验。
- **严重度**：高。
- **影响**：这是论文核心“创新点”的定量证据。当前消融矩阵证明谱特征**对输出零影响**，README 表述与数据矛盾；答辩/审稿一旦复跑即暴露。
- **建议**：二选一——(a) 让谱特征真正进入 FSM score（需先解决 agent3 的 fiedler 跨规模不可比问题）并重跑消融；(b) 若谱特征不参与决策，删除 C 组、删除“提升风险语义准确性”表述，如实写“谱特征当前仅作为可观测特征未进入决策”。

---

### 【P6-中】帧级统计样本非独立（相邻帧强相关），等效样本量远小于 45021

- **证据路径**：
  - `results/rerun/video_enhanced/_aggregate.json`：45021 帧来自 111 条视频，最长 1515 帧、最短 117 帧；top10 视频占 21.2% 帧。
  - 所有“等级分布百分比”“SAFE 25669/WARNING 11545/DANGER 7807”都是**按帧计数**。
  - 相邻帧在同一场景、同一相机下高度自相关；若按帧算“准确率”或做显著性检验，样本不独立，方差被严重低估。
- **严重度**：中。
- **影响**：帧级百分比看起来样本巨大，实则等效独立样本 ≈ 视频条数级。若论文报告“85% 帧为 SAFE”并据此推断，统计上不严谨。
- **建议**：所有视频级指标改为“**逐视频聚合后再平均**”（video-level mean），并报告**按视频重采样的 bootstrap 置信区间**；帧级分布仅作图示，不用于统计推断。

---

### 【P7-中】低照度漏检“核对”是循环论证，且未量化

- **证据路径**：
  - `scripts/find_sparse_video.py`：对 31 条全程 SAFE 视频采样 15 帧，用 **同一个 YOLO 检测器** 算 `avg_detected`，`avg_detected<0.5 且亮度>80` → “✓真稀疏”。
  - 若检测器在暗光下漏检，`avg_detected` 自然偏低，于是“确认”视频是空的——**用被验证对象自身去验证自己**。
  - 该脚本只打印到 stdout，未把 31 条视频的亮度/检测数落盘，**低照度漏检没有量化结论**（无“N 条视频暗光、漏检率 X%”）。
  - Git 历史显示演示视频已从 `mixkit_13265`（夜店暗光，亮度 8，漏检）替换为 `mixkit_50134`（亮度 128）——属**事后挑选数据/演示素材**，虽诚实记录，但“零误报”演示带有挑选性。
- **严重度**：中。
- **影响**：“31 条零误报”中可能混有“暗光漏检导致的假 SAFE”，数量未知。论文若写“零误报”会被质疑。
- **建议**：对 31 条全程 SAFE 视频，用**人工或第二检测器（如 YOLOv8x / RT-DETR / 人工抽帧）**复核，输出“N 条真空旷、M 条有行人但被漏检”；把低照度漏检率量化成数字写进报告；演示视频标注“经人工确认真稀疏”。

---

### 【P8-中】报告数字自相矛盾（中文 vs 英文 vs 实测）

- **证据路径**：
  - `README.md`：高密度（≥14 人）检出率 **76.7%**；`README.en.md`：**79.4%**。
  - 用 `results/rerun/images500_baseline/results.json` 重算：GT≥14 的图有 **12 张**，sum_det=139 / sum_gt=175 = **79.43%**。中文 README 的“76.7%（9 张）”与数据不符（12 张）。
  - `README.en.md` 视频章节仍写“**5 real videos**”，中文已更新为 111 条。
  - `results/experiment_summary.json` 仍是最早的 5 条视频版本（timelines 只有 5 条），与 `results/rerun/` 111 条不一致。
- **严重度**：中。
- **影响**：审稿人/老师对两个版本数字不一致会直接质疑数据真实性；旧 JSON 与新 README 并存造成口径混乱。
- **建议**：统一为最新重算值（高密度 79.4%/12 张），删除或标注旧 `experiment_summary.json`；英文 README 同步为 111 条视频版本；全仓库检索所有数字并统一。

---

### 【P9-中】可重复性缺陷：硬编码绝对路径 + Python 端 FSM 配置键与 JSON 不一致

- **证据路径**：
  - `scripts/ablation_study.py:17-18`：`sys.path.insert(0, r"H:/PedestrianRiskDecision/scripts"); os.chdir(r"H:/PedestrianRiskDecision")`。
  - `scripts/find_sparse_video.py:8-9`：同样硬编码 `H:/PedestrianRiskDecision`。
  - `scripts/plot_results.py`：`PROJECT = r"H:/PedestrianRiskDecision"`。
  - `scripts/crowd_test_enhanced.py:87`、`scripts/ablation_study.py:46`：`cfg.get("temporal", {})`；`config/default_config.json` 键名是 `"fsm"`。当前 RiskFSMConfig 默认值与 JSON `fsm` 块**恰好完全一致**，所以未暴露；但用户改 JSON 后 Python 端不生效，C++ 端才生效。
  - Git 历史 `82912f8` 声称“清理硬编码绝对路径”，实际未清干净。
- **严重度**：中。
- **影响**：换机器/换目录无法直接复现消融与零误报核验；调参实验（改 `default_config.json`）在 Python 端不生效，论文复现脚本与板端行为不一致。
- **建议**：用 `Path(__file__).resolve().parent.parent` 替换绝对路径；把 `cfg.get("temporal", {})` 改为 `cfg.get("fsm", {})`；重跑并确认输出不变（黄金回归已锁默认值，改动应只影响读取键名）。

---

### 【P10-中】缺对照 / 缺敏感性分析；板端无真实行人验证，PC 结论不能外推

- **证据路径**：
  - 所有风险阈值（`edge_dist=2.0m`、`n=10/5`、`rho=0.55/0.28`、`gcc=0.8/0.55`、FSM 权重/滞回）**均为手工设定，无阈值敏感性实验、无验证集选参**。
  - `results/edge_board/EDGE_TEST_REPORT.md`：板端日志全部是 `level=SAFE n=0`（**摄像头前无人**），从未在真实行人/人群前验证；README 也说板端 6.9FPS 是“拉流实测”。
  - 无不同检测器（YOLOv8n vs v8s/v8m vs RT-DETR）、无不同 IPM 标定、无不同帧率的对照。
- **严重度**：中。
- **影响**：阈值对结论影响大（OR/AND 规则一改，切换次数天差地别）；板端“增强决策”在真实行人上从未验证，PC 端 82.5%/切换次数等结论不能代表板端表现。
- **建议**：对 `edge_dist`、`n/rho/gcc`、FSM 参数各做 ±20% 扫描，报告等级分布/切换次数的变化范围；板端在真实行人/人群前抓帧 + 记录日志 ≥5 分钟，验证 n>0 时 FSM 是否输出 WARNING/DANGER。

---

## 四、低成本人工标注方案（补真值，不编造准确率）

### 4.1 标注目标
只标**风险等级**（SAFE / WARNING / DANGER 三分类），不标框。判定规则写死给标注员：
- **SAFE**：画面中行人彼此分散（最近间距 ≥2m 或各走各的），无聚集。
- **WARNING**：出现 5 人及以上局部聚集（间距 <2m 的小团体），或多人朝同一密集区域移动。
- **DANGER**：10 人及以上明显聚集/拥挤（几乎贴在一起），或人群密度显著且连通成片。
> 说明：用系统规则口径给标注员作参考，但**标注员只看画面、不看系统输出**，避免循环。

### 4.2 图片（500 张）
- 工具：Label Studio（免费开源）或共享 CSV 投票表；每图附缩略图。
- 工作量：500 张 × 每张 <10 秒 ≈ **1.5–2 小时/人**。
- 流程：2 人独立标注，Kappa 一致性；冲突（约 10–20%）由第 3 人仲裁。输出 `dataset/images500/risk_gt.csv`（`image_id,label_A,label_B,final`）。

### 4.3 视频（111 条）
- **不要逐帧标**（45021 帧不现实）。采用**场景片段采样**：
  - 用 `ffmpeg`/`cv2` 每 2 秒抽 1 帧（或每视频均匀抽 20–40 帧），得到约 **2500–4000 帧**，标注员只标这些关键帧的风险等级。
  - 关键帧之间用**线性/多数投票**插值成逐帧标签（只用于片段级指标，不用于帧级“准确率”）。
  - 额外标注“事件片段”：对出现明显聚集→疏散变化的视频，标注**风险起始帧/结束帧**（用于预警延迟）。
- 工作量：约 3000 帧 × 8 秒/帧 ≈ **6–8 小时/人**，双人 + 仲裁总计约 **12–18 小时**。
- 输出：`dataset/videos50/risk_annotations/` 下每视频 `*_risk_gt.json`（`{frame: label}` 或 `{segments:[{start,end,label}]}`）。

### 4.4 标注质量
- 计算标注员间 Cohen's Kappa；报告 Kappa ≥0.6 才用于评估。
- 公开标注协议（标注指南）随数据集发布，保证可复现。

---

## 五、可复现实验设计（评估指标 + 实验矩阵）

### 5.1 核心指标（全部在“逐视频/逐图”粒度上计算，再聚合）
设 GT 等级为 `y`，系统输出为 `ŷ`，三分类混淆矩阵 `M[3][3]`：

| 指标 | 定义 |
|---|---|
| **风险等级混淆矩阵** | 3×3：行=GT，列=输出；报告归一化百分比矩阵 |
| **加权/macro F1** | 对 SAFE/WARNING/DANGER 分别算 F1 再平均（解决类别不均衡） |
| **每等级召回率** | `M[i][i] / Σ_j M[i][j]`（如“真 DANGER 被检出为 DANGER 的比例”） |
| **误报率（假阳性）** | GT=SAFE 却输出 WARNING/DANGER 的帧/片段比例（可分级：把 SAFE 报成 DANGER 是严重误报） |
| **漏报率（假阴性）** | GT=DANGER 却输出 SAFE/WARNING 的帧/片段比例（危险漏报） |
| **预警延迟** | 从 GT 风险片段起始帧到系统首次输出目标等级的帧数/秒数（如“进入 DANGER 后 X 秒才报”） |
| **切换次数** | 仅作为稳定性描述，**不作为正确性指标**；且只在 GT 片段边界内统计 |

### 5.2 基线公平性修正（必改）
- 基线、消融 A 组、增强路径**统一使用同一决策函数**（建议统一为 `evaluate_level`，AND 规则，与 C++ `risk_engine.cc` 一致）。
- 公平对照矩阵：
  - A1：`evaluate_level` 逐帧（无 Kalman/FSM）＝“算法前基线”
  - A2：`evaluate_level` + Kalman（坐标平滑，决策函数不变）
  - A3：`evaluate_level` + Kalman + FSM（FSM 只做状态滞回，score 仍基于 n/density/r_gcc）
  - A4：完整增强（若谱特征进决策）
  - 这样每一列的差异**只由一个组件引起**，消融才有效。

### 5.3 消融实验修正
- **C 组**：要么让谱特征真正进入 score（先归一化 fiedler，见 agent3 方案 A/B），要么删除 C 组并如实说明。
- 样本量：6 条视频太少，至少扩到 20–30 条（覆盖空旷/稀疏/聚集/拥挤/暗光 5 类，每类 ≥4 条）。
- 每条视频在 4 组配置下跑，报告每组的**平均切换次数 + bootstrap 95% CI**，而非只报均值。

### 5.4 样本独立性与统计
- 所有百分比、均值**按视频/图片聚合后再平均**（video/image-level macro）。
- 报告**按视频重采样（bootstrap 1000 次）的 95% CI**，避免相邻帧伪样本。
- 图片评估：分桶（1-3/4-9/10-13/14-19 人）分层报告，避免 1 人图（113 张）主导。

### 5.5 复现性
- 固定随机种子；`requirements.txt` 锁定 ultralytics/opencv 版本。
- 实验脚本统一用相对路径；`results/rerun/` 每个产物记录 git commit、脚本参数、运行时间。
- 发布标注文件与评估脚本（`scripts/evaluate_risk.py`），一键输出混淆矩阵/召回/误报/漏报/延迟。

---

## 六、README / 论文需立即修正的表述

| 原文 | 问题 | 建议改法 |
|---|---|---|
| “检出率 82.5%” | 计数比、含训练集、无 IoU | “计数召回率 82.5%（count-based recall，未做框级匹配；含 50 张 train2017，属训练分布）” |
| “零误报 31/111 条” | 无 GT，循环验证 | “31 条视频全程输出 SAFE（未经真值验证，待人工复核）” |
| “切换次数↓91.4%” | 基线规则不同 | 先统一基线再报；或注明“基线为 OR 规则，不可与增强直接比” |
| “谱特征提升风险语义准确性” | 无实验支撑 | 删除，或改为“谱特征作为可观测特征记录（未进入决策）” |
| 高密度检出率 76.7%/79.4% | 两版本矛盾、实测 79.4%/12 张 | 统一为 79.4%（12 张） |
| “5 real videos”（英文） | 过时 | 改为 111 条 |

---

## 七、总结（一句话）

**数据是真实且来源可查的，但“风险”这一核心真值在图片和视频上都不存在；当前所有“准确/误报/漏报/稳定”结论都是系统内部自洽，缺少外部对照。** 最小可行修正：①补风险等级人工标注（图片 500 张约 2 小时、视频抽帧约 12–18 小时）；②统一基线决策函数；③让谱特征进决策或删除其结论；④帧级指标改为逐视频聚合 + 置信区间；⑤统一 README 数字。**在补真值之前，不报告任何准确率/误报率/漏报率。**