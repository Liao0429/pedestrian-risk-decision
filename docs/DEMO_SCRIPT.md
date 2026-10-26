# 答辩现场演示脚本（DEMO SCRIPT）

> 面向老师的完整现场演示讲稿：每步「说什么 → 做什么 → 预期现象 → 兜底」。
> 配套素材：`docs/DEMO_VIDEO.md`（预录视频）、`scripts/start_demo.sh`（一键启动）、`results/edge_board/`（真机证据）。
> 目标时长：**8~10 分钟**。本脚本所有引用文件均已核实存在。

---

## 0. 演示前准备清单（出门前逐项打勾）

| 项 | 说明 | 核对 |
|---|---|---|
| 开发板 Luckfox Pico Max | 已部署最新版（含增强决策 + 网页观看） | ☐ |
| 摄像头 | 接到开发板 MIPI/USB 接口 | ☐ |
| USB WiFi 模块（RTL8723BU） | 已插上，自动连手机热点（`docs/WIFI_SETUP.md`） | ☐ |
| 手机热点 | 已开，SSID/密码与 WIFI_SETUP 配置一致 | ☐ |
| 移动电源 | 给开发板独立供电（现场可能无插座） | ☐ |
| 预录视频 | `results/video_enhanced_fsm/mixkit_13192_enhanced.mp4`（DANGER）与 `mixkit_13265_enhanced.mp4`（零误报）拷到电脑/手机 | ☐ |
| 浏览器 | 手机或电脑能打开 `http://<板子IP>:1984` | ☐ |
| 演示文档 | README.md / docs/ARCHITECTURE.md / docs/DEMO_VIDEO.md 打开备用 | ☐ |

---

## 1. 开场（30 秒）：一句话定位

**旁白**："我做的是一套跑在 256MB 内存边缘开发板上的实时行人感知与风险分级系统，用于人群聚集预警。"

**操作**：展示开发板实物（小、便宜、功耗低）。

---

## 2. 网页实时观看（2 分钟）—— 最直观，先给结论

**操作**：
1. `sudo bash scripts/start_demo.sh`（开发板一键启动，自动拉起 RTSP + go2rtc + 打印 IP）。
2. 手机/电脑浏览器打开终端打印的地址 `http://<IP>:1984`。

**旁白**："现在大家可以用自己手机扫这个地址，浏览器直接看到监控画面，不需要装任何 App。右上角是实时风险等级 SAFE / WARNING / DANGER。"

**预期现象**：画面实时刷新，风险等级条随人群密度变化（镜头前有人走动时从 SAFE 升到 WARNING）。

**兜底**：若现场镜头前无人走动导致一直 SAFE，**立即切换**到第 3 步预录视频，说"实时画面受现场环境限制，我准备了一段密集人群的增强结果"。

---

## 3. 预录视频兜底（1.5 分钟）—— 展示核心能力 DANGER/WARNING 切换

**操作**：播放 `mixkit_13192_enhanced.mp4`（密集人群，平均 14.9 人）。

**旁白**："这是真实密集人群视频经过系统增强的输出。可以看到风险在 DANGER 和 WARNING 之间切换——用了 FSM 有限状态机做滞回防抖，305 帧里等级切换只有 4 次，不闪跳，误报被有效抑制。另一段稀疏场景 281 帧全程 SAFE、零误报。"

**预期现象**：画面叠加框 + 右上角风险等级在 DANGER/WARNING 间切换，且切换次数很少。

**数据支撑**（可随口报）：密集人群 DANGER 179 / WARNING 120 / SAFE 6，FSM 切换仅 4 次；稀疏场景零误报。

---

## 4. 真机性能数据（1 分钟）—— 强调"边缘端轻量化"

**操作**：翻到 `results/edge_board/EDGE_TEST_REPORT.md` 或 README 实验段。

**旁白**："这整套检测加风险决策，在开发板上内存占用仅 12.5MB、CPU 42.5%、RTSP 实时拉流 6.9 FPS。板子成本低、功耗小，适合大规模布设在商场、景区、地铁口做人群聚集预警。"

**数据**：内存 **12.5MB** / CPU **42.5%** / RTSP **6.9 FPS** / 573 条真实增强决策日志 / 4 张真实抓帧（`results/edge_board/screenshots/`）。

---

## 5. 算法流水线讲解（3 分钟）—— 用 Mermaid 图讲透

**操作**：打开 README/`docs/ARCHITECTURE.md` 的架构图，按流水线讲。

**旁白**（四段式）：
1. **感知**：YOLOv5n + INT8 量化，跑在板子 NPU 上，支持多目标类别切换（人/车辆/动物），按 `target_classes` 配置过滤。
2. **几何校正**：IPM 逆透视映射，把像素脚点投影到地面米制坐标，消除透视畸变——这一步是"距离关系"可靠的基础。
3. **图论拓扑**：建邻接矩阵 + 图拉普拉斯谱分析（Fiedler 值/谱半径/代数连通度）+ DBSCAN 人群聚类，判断人群是否聚集。
4. **时序决策**：Kalman/ByteTrack 跟踪 + EMA 平滑 + FSM 滞回防抖，输出三级风险，避免逐帧规则判定导致的闪跳。

**加分点**（可扩展处顺口提）：检测阈值可按类别差异化配置（`class_conf_thresholds`，车辆要够清晰才算、人群低一点也认），现场可演示调参。

---

## 6. 硬件扩展方案（1.5 分钟）—— 体现可扩展性

**操作**：展示 `docs/hardware/` 的 3D 图（云台 `gimbal_3d.png`、接线 `wiring_3d.png`）。

**旁白**："当前是单摄像头固定视角，我规划了可扩展硬件：双舵机云台做主动追踪、彩色触屏菜单、USB 语音（离线 ASR/TTS，危险时语音提醒）、三色灯报警、移动电源独立供电，还设计了扩展 PCB。论文里有完整方案。"

---

## 7. 收尾（30 秒）：一句话总结 + 数据可信

**旁白**："总结一下：这是一套在低成本边缘端完整跑通的感知—几何—图论—时序决策系统，数据全部来自真实实验（500 张 COCO 图、111 条真实视频、开发板真机），代码已开源在 GitHub，可复现。谢谢老师。"

---

## 故障预案

| 故障 | 现象 | 处理 |
|---|---|---|
| 网页打不开 | go2rtc 未起/网络断 | 先看 start_demo.sh 打印的 IP；确认手机与板子同一热点；改有线网直连 |
| 一直 SAFE | 镜头前无人 | 立即播预录视频兜底（第 3 步），别让画面空着 |
| 开发板不启动 | 无画面 | 检查供电/移动电源，重启；口头讲算法 + 放预录视频 + 展示抓帧 |
| 现场无网线 | WiFi 未连上 | 用 `docs/WIFI_SETUP.md` 手机热点方案；仍失败则全程用预录视频 + 真机抓帧 |
| 时间不够 | — | 砍掉第 6 步硬件（可一句带过），保 2/3/5 核心环节 |

---

## 引用文件核对（已核实存在）

- ✅ `scripts/start_demo.sh`
- ✅ `results/video_enhanced_fsm/mixkit_13192_enhanced.mp4` + `_timeline.json`
- ✅ `results/video_enhanced_fsm/mixkit_13265_enhanced.mp4` + `_timeline.json`
- ✅ `results/edge_board/screenshots/board_live_frame05/25/45/58.png`
- ✅ `docs/ARCHITECTURE.md`、`docs/DEMO_VIDEO.md`、`docs/WEB_VIEWER.md`、`docs/WIFI_SETUP.md`
- ✅ `docs/hardware/`（gimbal_3d.png / wiring_3d.png 等）