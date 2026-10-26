# 项目看板 KANBAN

> 状态图例：✅ 完成　🟡 进行中　⬜ 未开始　❌ 已砍/不需

## 主线（答辩验收）

| ID | 任务 | 状态 | 证据 / commit |
|----|------|------|---------------|
| T1 | 轻量化感知 YOLOv5n + INT8（RV1106 NPU） | ✅ | 真机 6.9 FPS / CPU 42.5% / 内存 12.5MB |
| T2 | IPM 逆透视几何校正（统一实现 + 外部标定文件） | ✅ | 3a049e1 |
| T3 | 图论拓扑分析（图拉普拉斯谱 + DBSCAN） | ✅ | 报告 docs/algorithms.md |
| T4 | 时序决策（Kalman/ByteTrack + EMA + FSM） | ✅ | 137 测试全过 |
| T5 | 三级风险分级 SAFE/WARNING/DANGER + 类别阈值 | ✅ | 配置 risk_config.h |
| T6 | JSON 运行时配置 + 失败回退 | ✅ | risk_config 27/27 测试 |
| T7 | 500 张真实图片基线（检出率 82.5%） | ✅ | results/rerun/ |
| T8 | 111 条真实视频增强（45021 帧，切换 4.0 次） | ✅ | results/rerun/ |
| T9 | 消融实验（Kalman/FSM 有效性） | ✅ | ad688e0 + ABLATION_STUDY.md |
| T10 | C++/Python 黄金回归校验 | ✅ | 双向比对 |
| T11 | 单元测试套件 | ✅ | 137 passed |
| T12 | 交叉编译 + 真机部署 | ✅ | build/luckfox_pico_rtsp_yolov5 |
| T13 | 网页观看 go2rtc（:1984）+ RTSP 推流 | ✅ | WEB_VIEWER.md |
| T14 | README / 报告 / 答辩文档 | ✅ | FINAL_REPORT.md / DEMO_SCRIPT.md |
| T15 | GitHub 全量留痕（私人仓库） | ✅ | main = fbe7532（同步） |

## 硬件（已定稿，待采购实测）

| ID | 任务 | 状态 | 说明 |
|----|------|------|------|
| H1 | 双 SG90 云台 + 三色灯 + 按键 | ✅ 方案 | docs/hardware/ |
| H2 | 2.8 寸 ILI9341 触摸屏 LVGL 菜单 | ✅ 方案 | docs/hardware/ |
| H3 | 离线语音 ASR/TTS（sherpa-onnx） | ✅ 方案 | LUCKFOX_VOICE_ASSISTANT_PLAN.md |
| H4 | USB WiFi RTL8723BU 热点接入 | 🟡 待实测 | WIFI_SETUP.md，模块待采购 |
| H5 | PCB 扩展板 | ✅ 方案 | docs/hardware/ |

## 后续 / 非主线

| ID | 任务 | 状态 | 说明 |
|----|------|------|------|
| N1 | RTSP/网页鉴权加固 | ⬜ | 答辩局域网演示足够，后续可做 |
| N2 | 按场景风险语义扩展 | ⬜ | 已有类别差异化阈值基础 |
| N3 | 论文写作 | 🟡 本地 | 初稿在毕设论文/thesis_agent_draft，不上传 |

## 结论
**主线全部完成**，答辩验收就绪。GitHub 与本地一致（fbe7532），137 测试通过，数据全部真实。