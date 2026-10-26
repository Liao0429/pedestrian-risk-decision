# 项目最终报告

> 项目：基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计
> 平台：Luckfox Pico Max（RV1106，1 TOPS NPU，256MB）
> 仓库：https://github.com/Liao0429/pedestrian-risk-decision（私人）
> 数据：全部真实实验（COCO 2017 图片、Mixkit 视频、开发板真机），无编造。

## 1. 系统概述

在边缘端（RV1106 NPU）实现实时目标检测、聚集分析与三级风险预警（SAFE / WARNING / DANGER）。
技术流水线：**轻量化感知（YOLOv5n + INT8）→ 几何校正（IPM）→ 图论拓扑分析（图拉普拉斯谱 + DBSCAN）→ 时序决策（Kalman/ByteTrack + FSM）**，输出实时风险并 RTSP 推流，支持浏览器网页观看。

## 2. 软件系统完成性

- **多目标检测**：`target_classes` 配置检测类别（人/车辆/狗/猫/动物），一键切换危险目标聚集报警
- **几何校正**：IPM 逆透视映射统一实现（`ipm_transform.h`），支持外部标定文件加载（`homography_file`），相机位姿变化免重编译
- **图论拓扑**：邻接矩阵 + 图拉普拉斯谱（Fiedler 值/谱半径/代数连通度）+ DBSCAN 聚类
- **时序决策**：Kalman/ByteTrack 跟踪 + EMA 平滑 + FSM 滞回防抖（C++/Python 行为一致）
- **风险分级**：SAFE / WARNING / DANGER，按类别差异化置信度阈值（`class_conf_thresholds`）
- **可配置**：JSON 运行时加载阈值/目标类别/标定文件，失败回退默认
- **健壮性**：信号处理优雅退出、关键初始化错误检查、内存安全修复（snprintf）、CreatePool 重试

## 3. 硬件系统方案（已定稿）

- 双舵机云台（SG90×2）、三色灯、按键、2.8 寸触摸屏（LVGL 动画菜单）
- USB 麦克风 + 喇叭（sherpa-onnx 离线语音 ASR/TTS，危险语音提醒）
- USB WiFi（RTL8723BU）连手机热点，屏幕显示 IP，现场零命令行
- 移动电源 + DC 电源模块独立供电；立体接线图/云台结构图齐全
- 方案文档：`docs/hardware/`（9 份）+ 语音方案 + PCB 设计

## 4. 性能与优化（已落地）

- 消除 RiskEngine 与增强决策的重复特征计算（默认关闭旁路）
- VLA 改 std::vector + 校验；关键初始化错误检查；统一 RiskLevel 枚举
- 统一 IPM 变换（消除三份重复实现）；外部单应标定文件加载
- C++/Python 双实现黄金回归校验（生成黄金数据，双向比对）
- 修复 minijson 布尔解析缺陷（false 被读成 true 的问题）
- 单元测试 **137 项全过**；C++ 主机测试（enhanced 26/26、minijson 28/28、risk_config 27/27）
- 交叉编译通过；真机部署验证通过

## 5. 真实实验数据

### 图片基线（500 张 COCO 真实含行人图）
| 指标 | 结果 |
|---|---|
| 处理图片 | 500 / 500（0 失败） |
| ground-truth 总人数 | 3089 |
| 检测总人数 | 2548 |
| **总体检出率** | **82.5%** |
| 高密度（≥14人）图检出率 | 76.7% |
| 等级分布 | SAFE 414 / WARNING 61 / DANGER 25 |

### 视频增强（111 条真实 Mixkit 视频，45021 帧）
| 指标 | 结果 |
|---|---|
| 等级分布 | SAFE 25669 / WARNING 11545 / DANGER 7807 |
| 平均每条等级切换 | 4.00 次（FSM 滞回稳定） |
| 出现 DANGER 视频 | 40 条 |
| 全程 SAFE（零误报） | 31 / 111 条 |

明细与时间线：`results/rerun/`（`_aggregate.json`、`_summary.json`、`_timeline.json`）。

### 真机指标
内存 **12.5MB**、CPU **42.5%**、RTSP **6.9 FPS**、573 条增强决策日志、4 张真实抓帧（`results/edge_board/`）。

## 6. 答辩准备

- 一键启动：`scripts/start_demo.sh`（拉起 RTSP + 网页 + 打印 IP）
- 网页观看：浏览器 `http://<板子IP>:1984`（go2rtc，免安装）
- 现场讲稿：`docs/DEMO_SCRIPT.md`（分步流程 + 故障预案）
- 演示视频：`results/video_enhanced_fsm/`（密集人群 DANGER 切换）
- 无线方案：`docs/WIFI_SETUP.md`（USB WiFi 自动连热点，屏幕显示 IP）

## 7. 论文

- 初稿位于本地 `毕设论文/thesis_agent_draft/毕业论文.md`（八章结构，含摘要/目录/实验）
- 含多目标类别可配置、网页监控终端等章节；真实图表在 `thesis_agent_draft/figures/`
- 论文仅本地保存，不上传仓库

## 8. 已知限制 / 后续

- 按类别差异化阈值已支持；按场景的风险语义可进一步扩展
- USB WiFi 模块待采购实测（方案已定）
- RTSP/网页鉴权可后续加固（当前面向答辩局域网演示）

## 9. 验收指引

1. **开发板**：192.168.0.136（RTSP rtsp://192.168.0.136:554/live/0，网页 http://192.168.0.136:1984）
2. **GitHub**：全部 commit 留痕，可查看每一版改动
3. **数据**：`results/rerun/`（图片基线 + 视频增强全量真实数据）、`results/edge_board/`（真机）

## 10. 结论

系统软件完成度高、数据全部真实、答辩就绪。剩余为硬件采购实测与鉴权加固等非主线项，可后续推进。
