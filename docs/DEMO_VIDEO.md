# 演示视频素材

> 预录的真实增强结果视频，用于**兜底演示**（避免实时画面无人走动时只显示 SAFE）。

## 素材清单

| 视频 | 场景 | 增强结果 | 用途 |
|---|---|---|---|
| `results/video_enhanced_fsm/mixkit_13192_enhanced.mp4` | 密集人群（平均 14.9 人） | **DANGER 179 / WARNING 120 / SAFE 6**，FSM 切换仅 4 次 | 展示 DANGER/WARNING 切换（说服力最强） |
| `results/video_enhanced_fsm/mixkit_13265_enhanced.mp4` | 稀疏/无人场景 | 全程 SAFE（该场景无行人，未触发告警） | 展示误报抑制 |
| `results/video_enhanced/mixkit_134_enhanced.mp4` 等 | 其他场景 | 增强对比 | 备选 |

## 数据真实性

- 视频来自 Mixkit 真实素材，经 ffprobe+HOG 双重校验（111/111 通过）。
- 增强结果由 `scripts/crowd_test_enhanced.py`（Kalman + 谱特征 + FSM）在真实视频上运行生成。
- 时间线 JSON：`results/video_enhanced_fsm/mixkit_13192_timeline.json`（每帧等级/特征，可复现）。

## 演示顺序建议

1. **网页实时观看**：手机/电脑浏览器打开 `http://<开发板IP>:1984`（现场实时画面 + 风险等级）。
2. **预录视频兜底**：若现场镜头前无人走动，播放 `mixkit_13192_enhanced.mp4`（DANGER 切换），强调"密集人群风险检测"。
3. **真机数据**：内存 12.5MB / CPU 42.5% / 6.9 FPS（`docs/WEB_VIEWER.md`、`results/edge_board/EDGE_TEST_REPORT.md`）。
4. **算法讲解**：用 README/ARCHITECTURE 的 Mermaid 图讲 感知→IPM→图论拓扑→FSM；配合 `docs/figures/fig9_fsm_timeline_13192.png`（FSM 决策过程时序图：等级色带/人数/r_GCC/Fiedler 四联图）讲解 FSM 如何把逐帧抖动收敛为稳定等级（305 帧仅 4 次切换）。

## 播放方法

- 电脑：直接用播放器打开视频文件。
- 手机：把 mp4 拷贝到手机相册，现场播放。
- 开发板：可用开发板 VLC/网页播放（若已部署）。

## 位置

- 视频：`results/video_enhanced_fsm/*.mp4`
- 时间线：`results/video_enhanced_fsm/*_timeline.json`