# 答辩演示就绪度评估报告

> 评估范围：README.md、docs/ARCHITECTURE.md、docs/WEB_VIEWER.md、docs/WIFI_SETUP.md、docs/hardware/、results/edge_board/、src/luckfox_rtsp_yolov5（target_classes 多目标切换）
> 评估方式：只读代码/文档/证据核验。日期 2026-10-26。

## 总体结论

**演示就绪度：高（8/10）。** 真机证据、硬件方案、网页观看、现场无线方案均已具备且互相衔接，足以支撑一场直观的现场演示。主要扣分点集中在**主文档（README/架构图）未同步两个新功能**：网页观看和多目标切换没有进入 README 特性列表、架构图与文档索引，老师只看 README 会看不到这些加分项；且论文尚未引用这两项。

## 逐项核验结果

| 项 | 就绪情况 | 证据 |
|---|---|---|
| README | ✅ 完整（中英双语/架构图/数据集/实验/部署/文档索引） | README.md 128 行 |
| 架构图 | ✅ Mermaid 分层图+数据流+部署拓扑 | docs/ARCHITECTURE.md |
| 真机证据 | ✅ 内存12.5MB/CPU42.5%/拉流6.9FPS/4张真实抓帧/573条增强决策日志 | results/edge_board/EDGE_TEST_REPORT.md、screenshots/、enhanced_final_20261026.log、thesis/figures |
| 网页观看 | ✅ 访问地址/go2rtc部署/端口/验证/复现齐全 | docs/WEB_VIEWER.md（76行） |
| 现场无线 | ✅ USB WiFi 自动连手机热点、屏幕显示IP、零命令行 | docs/WIFI_SETUP.md（75行） |
| 硬件方案 | ✅ 9份文档+云台/接线3D图（SVG+PNG），论文已引用 | docs/hardware/、thesis/figures/fig_hardware_* |
| 多目标切换 | ✅ 代码落地完整（std::find 过滤 + JSON 数组校验 + 默认[0]=person） | main.cc:213,235、risk_config.h:53/94-112、default_config.json `target_classes:[0]` |

## 关键缺口与建议

1. **README 未体现网页观看与多目标切换**：README 特性列表、架构图、文档索引（第 105-115 行表格）均未提及 `docs/WEB_VIEWER.md` 和 `target_classes`。架构图仍写"仅取 person 类 (COCO id=0)"（ARCHITECTURE.md:20-21），与多目标能力矛盾。建议：README 特性加一条"网页免安装观看（浏览器直接看监控）"和"多目标类别可切换（人/车辆/动物）"，文档索引表补 WEB_VIEWER.md、WIFI_SETUP.md；架构图把"仅取 person"改为"按 target_classes 过滤（默认 person）"。

2. **论文尚未引用新功能**：`docs/thesis/毕业论文.md`、`最终验收报告.md`、`主验收官终审报告.md` 中 grep 不到"网页观看/多目标切换/go2rtc"相关内容。若答辩演示主打网页观看，建议在论文补充一节"网页监控终端"与"多目标类别可配置"，并把 fig_board_live_frame.png 等真机截图正式入论文。

3. **网页观看的现场网络依赖需标注实测状态**：WEB_VIEWER 默认地址 192.168.0.136 是有线网；现场无网线时依赖 WIFI_SETUP 的 USB WiFi 模块（RTL8723BU，约20-30元）。该模块是否已采购并实测过？文档目前是"方案"口吻，未标注"已实测"。建议在 WIFI_SETUP.md 顶部明确标注**哪些步骤已实测、哪些为待购待测**，避免现场翻车。

4. **真机截图均为无人/稀疏场景**：edge_board 日志全部 `n=0 level=SAFE`，4 张截图是真实画面但无行人。答辩若现场展示实时画面而镜头前无人走动，只会显示 SAFE，说服力弱。建议：准备一段**有人走动/聚集的现场演示**，或预录含 WARNING/DANGER 切换的短视频（可用 mixkit_13192 密集人群视频经开发板/PC 增强路径出片）作为兜底，演示时先播预录、再切实时。

5. **演示"一键启动"脚本缺失**：README 部署段是手动命令（交叉编译→scp→sudo运行→再起 go2rtc）。答辩现场若需快速拉起整套（RTSP源+go2rtc+显示IP），建议补一个开发板上的 `start_demo.sh`（同时拉起检测程序与 go2rtc，打印访问地址），把 WIFI_SETUP 的"零命令行"承诺真正落到脚本层。

## 演示建议顺序（面向老师）

1. **现场网页观看**（手机/电脑浏览器打开 http://<IP>:1984，实时监控画面 + 风险等级条）——最直观。
2. **真机数据**：内存 12.5MB、CPU 42.5%、6.9FPS，强调"边缘端轻量化"。
3. **算法流水线**：用 README/ARCHITECTURE 的 Mermaid 图讲 感知→IPM→图论拓扑→时序FSM。
4. **视频实验**：播 mixkit_13192 增强结果（FSM 生效，等级切换仅4次）与 mixkit_13265（零误报）。
5. **硬件扩展**：云台/语音/触屏/PCB 3D 图与方案，体现可扩展性。