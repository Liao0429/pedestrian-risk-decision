# 基于 C++ 的边缘端轻量化行人感知与风险决策软件系统

> **毕设项目**：在 Luckfox Pico Max（RV1106，1TOPS NPU，256MB）上实现实时行人检测、人群聚集分析与风险分级预警。
> 采用"**前端轻量化感知 + 几何空间校正 + 图论拓扑分析 + 时序风险决策**"复合算法流水线。

## ✨ 特性

- **轻量化检测**：YOLOv5n + INT8 量化，在边缘端 NPU 上实时检测行人（C++/RKNN）
- **几何校正**：逆透视映射（IPM）把像素脚点映射到地面米制坐标，消除透视畸变
- **复杂网络拓扑**：邻接矩阵 + 图拉普拉斯谱分析（Fiedler 值/谱半径/代数连通度）+ DBSCAN 人群聚类
- **时序决策**：Kalman/ByteTrack 多目标跟踪 + EMA 平滑 + FSM 有限状态机（滞回防抖）
- **风险分级**：实时输出 **SAFE / WARNING / DANGER** 三级风险
- **可配置**：规则阈值通过 JSON 配置文件加载（`config/default_config.json`）
- **可扩展硬件**：支持双舵机云台、语音告警、彩色触控屏、物理开关（`docs/hardware/`）

## 🏗️ 系统架构

```
摄像头 → YOLOv5n(INT8/NPU) → IPM坐标映射 → 复杂网络/聚类
       → Kalman/ByteTrack 时序平滑 → FSM 风险决策 → SAFE/WARNING/DANGER
       → RTSP 推流 + 画面叠加 + 告警
```

## 📁 目录结构

```
pedestrian-risk-decision/
├── dataset/            # 真实数据集
│   ├── images500/      #   500 张 COCO 真实含行人图片（1~19 人）
│   └── videos50/       #   111 条真实行人/人群视频（Mixkit，已校验）
├── scripts/            # 测试脚本
│   ├── crowd_test.py           # 图片模式（基线）
│   └── crowd_test_enhanced.py  # 视频模式（Kalman+谱特征+FSM 增强）
├── src/
│   ├── algorithms/     # 算法模块（图拉普拉斯/Kalman/ByteTrack/DBSCAN/FSM/IPM/EMA，含 Python+C++ 接口）
│   ├── hardware/       # 硬件驱动（servo/pan_tilt/voice_alert）
│   └── luckfox_rtsp_yolov5/  # 开发板 C++ 工程（RKNN）
├── config/             # 阈值配置
├── results/            # 实验结果（图片基线 + 视频增强）
├── tests/              # 算法单元测试（全通过）
├── docs/               # 系统原理/算法优化/硬件方案/集成方案/性能对比/PCB设计/语音方案
└── config/default_config.json
```

## 📊 真实数据集

- **图片**：500 张 COCO 2017 真实含行人图片，覆盖 1~19 人（分散 200 / 聚集 150 / 较拥挤 138 / 拥挤 12），含 ground-truth 人数标注
- **视频**：111 条 Mixkit 真实行人/人群视频（360p，3.9~50 秒），ffprobe + HOG 双重校验（111/111 通过）
- 全部为真实数据，无合成/编造；下载脚本与校验脚本在 `dataset/_work/` 和 `dataset/videos50/`

## 🧪 实验结果（真实）

### 图片基线（500 张）
| 指标 | 结果 |
|---|---|
| 检出率（总体） | **82.5%** |
| 高密度图（14+人）检出率 | **79.4%** |
| 等级分布 | SAFE 414 / WARNING 61 / DANGER 25 |

### 视频增强（5 条真实视频，Kalman+谱特征+FSM）
- 密集人群（平均 14.9 人）：**持续输出 DANGER**（264/305 帧），等级切换仅 4 次 → 稳定
- 稀疏/无人场景：全程 SAFE，**零误报**
- 瞬时聚集：被 FSM 滞回正确抑制，不闪跳

详见 `docs/PERFORMANCE_COMPARISON.md`。

## 🚀 部署方法

### PC 端测试（GPU，用于实验）
```bash
# 图片基线
python scripts/crowd_test.py --input dataset/images500 --output results/images500_baseline --device 0
# 视频增强（Kalman + 谱特征 + FSM）
python scripts/crowd_test_enhanced.py --input <video.mp4> --output results/video_enhanced --spectral --device 0
```

### 开发板部署（Luckfox RV1106，C++/RKNN）
1. 老电脑（Ubuntu）用交叉编译工具链编译 `src/luckfox_rtsp_yolov5/` 工程（见 `docs/INTEGRATION_PLAN.md`）
2. 产物 scp 到开发板，`sudo ./luckfox_pico_rtsp_yolov5` 运行
3. RTSP 推流：`rtsp://<板子IP>:554/live/0`
4. 增强算法（谱特征/ByteTrack/FSM）的 C++ 头文件在 `src/algorithms/*.h`，可交叉编译集成

## 📚 文档索引

| 文档 | 内容 |
|---|---|
| `docs/SYSTEM_PRINCIPLE.md` | 系统实现原理与算法详解（答辩用） |
| `docs/ALGORITHM_OPTIMIZATION.md` | 算法效率/准确度分析与升级建议 |
| `docs/ALGORITHM_UPGRADE.md` | ByteTrack/DBSCAN/IPM标定/EMA 升级说明 |
| `docs/INTEGRATION_PLAN.md` | 算法集成方案（Python+C++，四线程架构） |
| `docs/PERFORMANCE_COMPARISON.md` | 性能对比实验报告 |
| `docs/hardware/` | 硬件扩展（舵机/语音/大屏/开关/PCB） |
| `docs/experiment_results.md` | 30 张图基线实验报告（早期） |

## 🧩 可扩展模块（后续）

- **语音对话大模型**（网络/本地双方案）：`docs/LUCKFOX_VOICE_ASSISTANT_PLAN.md`
- **端侧 AI 智能体**（Needle 2，linux-armv7 支持）：`docs/AI_needle2_analysis.md`
- **小智 AI 结合**（MCP + ESP32）：`docs/XIAOZHI_COMBINED_PLAN.md`
- **PCB 底板打样**：`docs/hardware/PCB_BOARD_DESIGN.md`

## 📌 说明

- 本仓库为**私人仓库**，毕设完成后由作者手动公开。
- 论文保存在本地，不在本仓库上传。
- 所有实验数据均来自真实数据集，可复现。