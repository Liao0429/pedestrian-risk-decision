# Agent 8 答辩与产品演示审查报告

> 审查角色：本科毕业答辩评委 + 现场演示人员
> 审查对象：H:/PedestrianRiskDecision
> 审查日期：2026-10-26
> 审查方式：通读 README/DEMO_VIDEO/WEB_VIEWER/BASELINE_RESULTS/EDGE_TEST_REPORT、docs/hardware/ 全部 10 个 md 方案文件；对 docs/figures/、results/demo/、results/edge_board/、results/rerun/ 中的图表、视频、截图、日志做了程序化核验（ffprobe 元数据、逐帧哈希/像素差、状态条颜色分析、日志行数统计）。

---

## 一、结论摘要（评委视角）

**总体判断：实验数据扎实、真机证据初步成链、预录视频可用，但"可视化证据"目前只做到 60 分——核心算法创新点（IPM 几何校正、图论网络、谱特征、时序稳定性）几乎没有一张能让评委秒懂的图。现场演示最致命的缺口是：真机日志全是 `n=0 SAFE`，没有"真实行人入镜触发 WARNING/DANGER"的真机画面证据。**

按答辩优先级排序的改进：
1. **补齐 3 张图**（风险曲线图、IPM 地面坐标俯视图、网络拓扑图）——这是"算法原理可视化"的最小必要集，半天~1 天工作量。
2. **录制一条真机 DANGER 触发视频**（平板/手机播放人群视频放在摄像头前）——这是现场演示说服力最强的证据。
3. **补 2 张截图**（手机浏览器打开 go2rtc 网页画面、SSH 里 `top/ss/ps` 资源监控）——补完"真机 + 网页观看"证据链。
4. **硬件扩展做减法**：OLED/串口屏/语音告警值得投入，PCB 打样、LVGL 触摸屏、语音识别大方案立即停止。

---

## 二、现有可视化资产盘点（已逐项核验）

### 2.1 已有且真实可用的资产

| 资产 | 位置 | 核验结果 | 答辩用途 |
|---|---|---|---|
| 图片基线等级分布图 | docs/figures/fig1_level_dist.png（900×675） | matplotlib 柱状图，数据源 results/rerun/images500_baseline/results.json 真实存在 | PPT 可用 |
| 图片检出率图 | docs/figures/fig2_detect_rate.png | 同上 | PPT 可用 |
| 增强前后对比图（3 联） | docs/figures/fig3_before_after.png（2100×675） | 数据源 _comparison_summary.json：平均切换 5147→444，DANGER 视频 102→40，全 SAFE 9→31 | **最有说服力的图之一** |
| 视频增强等级分布图 | docs/figures/fig4_video_level.png | 数据源 _aggregate.json：SAFE 25669/WARNING 11545/DANGER 7807 | PPT 可用 |
| 消融实验图 | docs/figures/fig_ablation.png（2250×1200） | 6 视频 × 4 组合柱状图（脚本 ablation_study.py 可复现） | PPT 可用，但见风险点 |
| 增强效果视频 ×2 | results/demo/*.mp4（另有 rerun/video_enhanced/ 下 111 条） | ffprobe 确认有效 MP4，640×360@~24fps；**逐帧分析确认增强视频顶部有随风险状态变色的状态条**（绿→红→黄随 FSM 切换），poster 图与视频帧像素差 MAE 1.0~3.8，确为同源帧 | 现场兜底播放 |
| 视频封面图 ×4 | docs/figures/demo_*.png | 与对应增强视频内容一致（非原片帧，是增强后帧） | README 链接用 |
| 真机 RTSP 截图 ×4 | results/edge_board/screenshots/board_live_*.png（720×480） | 唯一色 302+、顶部 64px 为绿色 SAFE 状态条，确认真实抓帧且带叠加层 | 真机证据 |
| 真机运行日志 | results/edge_board/enhanced_final_20261026.log（681 行，573 条 EnhancedDecision 输出） | 全部 `level=SAFE n=0`；日志首行有 `RkLunch-stop.sh: not found` 噪音 | 真机证据 |
| 真机性能指标 | docs/review/BASELINE_RESULTS.md + results/edge_board/EDGE_TEST_REPORT.md | RSS 12.5MB、CPU 42.5%、RTSP 拉流 6.9 FPS、端口 554/1984 监听 | 答辩数据页 |
| 检测框可视化图 | results/images500_baseline/vis/*.jpg（500 张） | 程序化分析确认含红/黄/绿检测框叠加色 | **未被 README 引用，可精选 3~5 张进 PPT** |
| 时间线 JSON | results/video_enhanced_fsm/*_timeline.json、results/rerun/video_enhanced/*/ 下 | 每帧 level/n/density/r_gcc/fiedler 齐全 | **尚未画成图，见缺口** |

### 2.2 可视化缺口（答辩硬伤）

1. **没有风险-时间曲线图**。timeline JSON 里每帧的 level/n/rho/fiedler 都齐了，但仓库里没有任何一条曲线图。评委看不到"FSM 滞回稳定"的直接视觉证据。
2. **没有 IPM 地面坐标俯视图（BEV）**。IPM 是论文核心创新点之一，但没有任何"像素脚点→地面米制坐标"的鸟瞰图。评委无法直观理解"几何校正消除透视畸变"。
3. **没有网络拓扑图**。邻接矩阵/图拉普拉斯/Fiedler 值是核心卖点，但仓库里没有任何"行人为节点、距离<2m 为边"的图。
4. **没有网页观看截图**。WEB_VIEWER.md 写得很完整，但没有任何一张"手机/浏览器打开 :1984 看到实时画面"的截图。
5. **没有资源监控截图**。12.5MB/42.5%/6.9FPS 只有文字记录，没有 `top`/`free`/`ss` 的实拍截图。
6. **没有真机 DANGER 触发视频**。真机日志全部是 `n=0 SAFE`，缺一条"真人入镜→WARNING/DANGER"的真机画面证据（DEFENSE_PREPARATION 已把此列为第一优先，但仓库中尚无成品）。

---

## 三、问题 1：如何直观呈现行人检测、聚集分析、风险状态变化与时序稳定效果？

### 现状评估

| 维度 | 现有手段 | 够不够 | 缺口 |
|---|---|---|---|
| 行人检测 | 500 张 vis/*.jpg + 检出率柱状图 | 够（但 vis 图没被引用） | 精选 3~5 张进 PPT |
| 聚集分析 | 只有等级分布统计图 | **不够** | 缺"人群密度/网络"的直观图 |
| 风险状态变化 | 增强视频顶栏颜色变化（已验证真实存在） | 基本够 | 视频里无文字标注，评委可能看不懂顶栏含义 |
| 时序稳定效果 | fig3 切换次数对比 + 消融图 | **不够** | 缺"一条视频内 level 随时间变化"的曲线图 |

### 建议（低成本补齐）

1. **风险曲线图（最高优先）**：用 `results/video_enhanced_fsm/mixkit_13192_timeline.json` 画双轴曲线：x=帧号，左轴 level（SAFE/WARNING/DANGER 阶梯线），右轴 n/density；再叠一张 FSM 切换点标记。一张图同时讲清"聚集程度上升→风险升级→FSM 滞回不抖动"。
2. **图片精选**：从 `results/images500_baseline/vis/` 选 3 张（1 人 SAFE / 6~8 人 WARNING / 14+ 人 DANGER），PPT 里并排放，配合 fig1/fig2 讲检测与分级。
3. **视频加字幕**：若时间允许，用 ffmpeg 给 demo 视频加"当前等级"文字烧录一版，现场播放时评委不用猜。

---

## 四、问题 2：哪些可视化真正有助于理解算法原理？

按"帮助评委理解算法"的性价比排序：

1. **IPM 地面坐标叠加图（BEV 俯视图）——必做，性价比最高**
   - 左图：原图像素坐标下的行人脚点（透视畸变，近大远小）。
   - 右图：同一帧经单应 H 映射到地面米制坐标的俯视图（x/y 轴标米），画出 2m 邻接边。
   - 一图讲清"为什么需要 IPM""透视畸变如何被消除""图论边的物理含义"。
   - 实现：`crowd_test_enhanced.py` 已输出脚点，用 `cv2.getPerspectiveTransform` 的 H 映射即可，半天工作量。

2. **网络拓扑图（图论）——与 BEV 合并做，必做**
   - 在 BEV 俯视图上叠加：节点=行人、边=距离<2m、聚类=DBSCAN 圈、连通分量着色。
   - 配合一行文字："Fiedler 值=代数连通度，衡量人群紧密程度"。
   - 这张图是"复杂网络拓扑分析"卖点的唯一视觉证明。

3. **风险曲线图（时序）——必做**
   - 见问题 1。讲 FSM 滞回、EMA 平滑、切换次数下降。

4. **谱特征曲线（可选）**
   - timeline JSON 里有 fiedler/spectral_radius/eig_entropy，可画一条 fiedler 随帧变化曲线，叠在风险曲线上，说明"谱特征作为图连通性指标参与显示与计算"。
   - 注意答辩口径：消融数据中 C(+谱特征)==B(+Kalman)，谱特征对切换次数零贡献，**不要主动强调谱特征"降低切换次数"**，只讲"作为连通性指标参与显示/计算"。

5. **系统架构图（已有）**
   - README 的 Mermaid 流水线图（感知→IPM→图论→FSM）本身够用，建议答辩 PPT 里转成静态图并高亮各模块。

---

## 五、问题 3：如何证明程序运行在目标嵌入式设备？

现有证据链已初步成形，但缺"可视化闭环"。建议答辩现场按以下顺序展示：

| 步骤 | 动作 | 证据 |
|---|---|---|
| 1 | SSH 到开发板，`ps aux \| grep luckfox` | 进程在跑（真机） |
| 2 | `ss -tlnp \| grep -E '554\|1984'` | RTSP + go2rtc 端口监听 |
| 3 | `tail -5 /tmp/enhanced_latest.log` | EnhancedDecision 实时滚动 |
| 4 | `top -bn1 \| head` | CPU/内存实时数字 |
| 5 | 手机浏览器打开 `http://192.168.0.136:1984` | 实时画面 + 风险等级（需提前截图存档） |
| 6 | 播放预录的真机 DANGER 视频（需录制） | "真实行人触发风险"的闭环证据 |

**仓库里已有的静态证据**：
- `results/edge_board/screenshots/board_live_*.png`（4 张真实 RTSP 抓帧，顶部绿色 SAFE 状态条）
- `results/edge_board/enhanced_final_20261026.log`（573 条增强决策输出）
- `EDGE_TEST_REPORT.md`（12.5MB / 42.5% / 6.9FPS）

**还缺**：
- 手机浏览器打开网页的截图（补一张即可）。
- `top`/`free` 实拍截图（补一张即可）。
- 真机有人触发 DANGER 的截图或短视频（**最重要**）。

---

## 六、问题 4：现场演示故障预案

| 故障 | 预案 |
|---|---|
| **RTSP 失败** | ① `ss -tlnp` 确认 554 端口；② 重启程序 `sudo LD_LIBRARY_PATH=./lib ./luckfox_pico_rtsp_yolov5`；③ 若 RTSP 一直起不来，立即切预录视频 `results/video_enhanced_fsm/mixkit_13192_enhanced.mp4` 兜底；④ 也可改 go2rtc.yaml 注入本地文件流 |
| **网络不通** | ① 网线直连开发板与笔记本（静态 IP 192.168.0.136 已配）；② 开发板开手机热点；③ 最坏情况：全程用预录视频 + 离线 PPT，不讲网页观看 |
| **电源** | ① 演示前确认充电宝/适配器满电，备双电源；② 舵机/外设务必独立 5V 供电且共地，否则拉低电压会重启；③ 演示前 30 分钟上电预热，避免现场首次上电异常 |
| **无人经过** | ① 用平板/手机播放人群密集视频或图片放在摄像头前（DEFENSE_PREPARATION 方案，成本几元，效果最强）；② 请现场观众走到镜头前；③ 直接切预录视频 |
| **误检/漏检** | ① 用光照正常、无玻璃反光的场景；② 调低/调高置信度阈值（config/class_conf_thresholds 可配）；③ 口径：演示场景聚焦"聚集风险"，不承诺绝对零误报 |
| **板子过热** | ① 加散热片/小风扇（system_integration.md §6.2 有方案）；② 控制演示时长 <10 分钟；③ 连续运行后摸板子温度，过热先断电冷却再演示；④ 注意长时间运行无看门狗（BASELINE_RESULTS 已列为待处理），演示前重启一次程序 |
| **投影** | ① 提前测试 HDMI 转接/分辨率，PPT 用 16:9；② 图表用高对比深色；③ 视频提前拷到演示电脑并用本地播放器打开（不要现场依赖网络流）；④ 投影仪声音外放测试 |
| **页面打不开** | ① 确认 go2rtc 进程在（`ps aux \| grep go2rtc`）；② 确认 :1984 监听；③ 手机与开发板同网段；④ 兜底：直接用 VLC 打开 `rtsp://192.168.0.136:554/live/0` |

---

## 七、问题 5：值得做 vs 应停止投入的扩展

### 值得做（低成本、高答辩增量）

| 扩展 | 成本 | 答辩增量 | 建议 |
|---|---|---|---|
| 真机 DANGER 触发视频录制 | 1~2 小时 | ★★★★★（现场最缺的证据） | **立即做** |
| 风险曲线图 + IPM/BEV 图 + 网络图 | 1~2 天 | ★★★★★（核心创新可视化） | **立即做** |
| 手机网页观看截图 + 资源监控截图 | 0.5 小时 | ★★★★ | **立即做** |
| OLED/串口屏实拍（显示等级/人数/FPS/IP） | ¥20~100 + 半天 | ★★★（"硬件交互"直观） | 若已采购就做，未采购优先做上面三项 |
| 语音告警（DFPlayer/JQ8900） | ¥15~30 + 半天 | ★★★（DANGER 语音播报震撼现场） | 值得做，但要先保证主流程稳定 |

### 应停止投入（对答辩增量低或风险高）

| 扩展 | 理由 |
|---|---|
| **PCB 底板打样** | ¥145~240 + 3~5 天打样周期，答辩前大概率用不上；评委不关心 PCB 布线。**停止** |
| **LVGL 彩色触摸屏 + 语音识别（sherpa-onnx）** | 交叉编译 armv7 风险高、耗时长，SPI 屏帧率低，答辩前做不完。**停止** |
| **双舵机云台追踪** | 机械装配 + 独立供电 + PWM 设备树，风险高；若还没调通，不要为答辩冒险。**已调通则展示，未调通则停止** |
| **7 寸/5 寸大屏** | 边际收益低。**停止** |
| **8h/24h 长稳测试** | 答辩前做不满；改为做 1~2 小时连续运行记录即可。**降级** |
| **语音大模型（LUCKFOX_VOICE_ASSISTANT_PLAN）** | 纯展望，答辩一句话带过即可。**停止** |

**原则**：答辩前 3 天只做"能进 PPT/能现场演"的事；一切涉及新硬件、新工具链、长周期打样的事全部砍掉。

---

## 八、问题 6：答辩现场演示脚本建议（8 分钟版）

> 目标：前 3 分钟抓住评委（现场触发风险），中间 3 分钟讲算法，最后 2 分钟给真机证据。

| 时间 | 环节 | 内容 | 素材 |
|---|---|---|---|
| 0:00–1:30 | 需求与挑战 | 边缘端（RV1106，1TOPS NPU，256MB）、实时行人感知 + 聚集风险分级的难点 | PPT 架构图 |
| 1:30–3:00 | **现场真机演示** | 手机/浏览器打开 go2rtc 网页 → 平板播放人群密集视频放镜头前 → 观察等级 SAFE→WARNING→DANGER 跳变 + 顶栏变色 | 真机 + 平板 |
| 3:00–4:00 | **预录视频兜底** | 播放 `mixkit_13192_enhanced.mp4`：DANGER 179/WARNING 120/SAFE 6、切换仅 4 次（基线 46 次） | 预录视频 |
| 4:00–5:30 | 算法原理 | IPM 地面坐标（BEV 图）→ 图论网络（节点/边/聚类）→ 谱特征 → FSM 滞回（风险曲线图） | **需补齐的 3 张图** |
| 5:30–7:00 | 实验数据 | 500 图检出率 82.5%、111 视频 45021 帧、平均切换 46.4→4.0、消融 | fig1-4 + fig_ablation |
| 7:00–7:40 | **真机证据** | SSH 现场敲 `ps/ss/top/tail`，展示 12.5MB / 42.5% / 6.9FPS + 截图 | 真机 + 截图 |
| 7:40–8:00 | 总结与局限 | 创新点一句话（端到端"检测-几何-图论-时序"轻量决策流水线）；主动说局限（无 GT 只报稳定性、IPM 默认 H 待真机标定、长稳待做） | PPT |

### 备用方案
- 真机全挂 → 直接走 3:00 的预录视频 + 4:00 的算法图 + 5:30 的数据，仍能撑满 8 分钟。
- 投影挂 → 用评委电脑/自己笔记本本地播放视频 + 打印版图表。
- 现场无网络 → 网线直连开发板，或纯离线 PPT + 预录视频。

### 评委高危追问应答口径（来自 DEFENSE_PREPARATION，已核对）
- "谱特征起什么作用？" → 作为图连通性指标参与计算与显示；FSM 评分用 n/density/r_gcc。**不要宣称谱特征降低了切换次数**（消融数据显示贡献为零）。
- "PC 用 YOLOv8，板端 YOLOv5n INT8，能代表吗？" → 承认口径差异，已做真机部署验证，量化掉点待补测。
- "IPM 标定了吗？" → 默认单应可配置，真机 4 点标定流程已提供（ipm_calibrate.py），答辩前应完成。
- "切换次数下降是否把真实危险也压掉了？" → 承认需要 GT 才能判误报/漏报；FSM 参数可配置（confirm_up/down_warning 等）平衡灵敏度。
- "为什么 6FPS？" → 单线程串行 + 板端算力约束；帧率瓶颈在 VI/推理/编码串行链路，与编译优化无关（-O2 实测仍 6FPS）。

---

## 九、Top Findings 汇总

| Severity | Issue | Evidence |
|---|---|---|
| **HIGH** | 真机日志全部为 `n=0 SAFE`，仓库中没有任何"真实行人入镜触发 WARNING/DANGER"的真机画面证据；现场演示无法证明端到端风险触发生效 | results/edge_board/enhanced_final_20261026.log（573 条全为 `level=SAFE n=0`）；DEFENSE_PREPARATION 将"现场制造 DANGER 录制"列为第一优先但未完成 |
| **HIGH** | 核心创新点 IPM 几何校正没有任何可视化（无地面坐标俯视图/BEV），评委无法直观理解"像素→米制坐标" | docs/figures/ 仅有柱状图与视频封面，无任何 BEV/坐标投影图；SYSTEM_PRINCIPLE.md §2.2 仅文字描述 |
| **HIGH** | 图论网络/谱特征（邻接矩阵、Fiedler 值、DBSCAN）无任何可视化图，论文最大卖点缺视觉证明 | 全仓库图片清单中无网络拓扑图；docs/figures/ 无相关图 |
| **HIGH** | 时序稳定性只有统计柱状图，没有"风险-时间曲线"；timeline JSON 数据齐全但未画图，FSM 滞回效果无法直观展示 | results/video_enhanced_fsm/*_timeline.json 含每帧 level/n/rho/fiedler；scripts/ 中无 timeline 绘图脚本 |
| **MEDIUM** | 网页观看（go2rtc）与资源监控只有文字说明，无手机浏览器截图、无 top/ss 实拍图，真机证据链缺可视化闭环 | docs/WEB_VIEWER.md 无截图；EDGE_TEST_REPORT.md 仅文字指标 |
| **MEDIUM** | 消融图中谱特征贡献为零（C 组合 == B 组合，切换次数完全相同），若主动宣称"谱特征提升稳定性"会被评委当场戳穿 | results/rerun/_ablation.json：mixkit_134/13192/3428/4519/4178/50134 的 B 与 C 切换次数逐条相同 |
| **MEDIUM** | 帧率口径不一致：README/BASELINE 写 6 FPS，EDGE_TEST_REPORT 写 RTSP 拉流 6.9 FPS；6FPS 与 6.9FPS 并存易被追问 | BASELINE_RESULTS.md §三（6 FPS）；EDGE_TEST_REPORT.md §2（6.9 FPS，注明为客户端拉流口径） |
| **MEDIUM** | 稀疏场景演示素材文档不一致：README 用 mixkit_50134 作"稀疏/无人"示例，DEMO_VIDEO.md 用 mixkit_13265，现场易拿错文件 | README.md L99-100 vs docs/DEMO_VIDEO.md §素材清单 |
| **LOW** | 真机日志首行有 `RkLunch-stop.sh: not found` 噪音，演示时 tail 日志会露出不专业画面 | results/edge_board/enhanced_final_20261026.log 第 1 行 |
| **LOW** | 500 张检测可视化图（results/images500_baseline/vis/*.jpg）已存在但 README 未引用，答辩素材浪费 | results/images500_baseline/vis/ 下 500 张含红/黄/绿框叠加图 |
| **LOW** | 增强视频顶栏状态条无文字/图例说明，评委看视频时可能不理解颜色含义 | 程序化分析确认 demo 视频顶部有色条随 FSM 切换（红/黄/绿），但 DEMO_VIDEO.md 未说明 |

---

## 十、给总负责人的三天行动清单

1. 【今天】用平板/手机播放 `dataset/videos50/mixkit_13192.mp4` 放在开发板摄像头前，录一段真机画面（含 RTSP 推流 + 网页），保存到 `results/edge_board/`。
2. 【今天】用 timeline JSON 画风险曲线图（matplotlib 双轴），存 `docs/figures/fig5_risk_timeline.png`。
3. 【明天】用 `crowd_test_enhanced.py` 的脚点 + 默认单应 H 画 IPM 地面坐标俯视图（叠加 2m 邻接边 + DBSCAN 聚类），存 `docs/figures/fig6_ipm_bev.png` 与 `fig7_network_graph.png`。
4. 【明天】补两张截图：手机浏览器打开 `http://192.168.0.136:1984` 的画面、SSH 里 `top`/`ss` 输出。
5. 【后天】更新 README 引用新图；统一 6 FPS/6.9 FPS 口径；清理日志噪音；按本报告第八节彩排 8 分钟演示。
6. 【停止】PCB 打样、LVGL 触摸屏、sherpa-onnx 语音识别、7 寸大屏、8h 长稳——全部砍掉。