# 视频数据集说明

真实视频数据，用于测试时序算法（Kalman 滤波追踪、FSM 风险决策状态机）和多帧性能。

## 文件清单

| 文件 | 来源 | 大小 | 内容 | 用途 |
|---|---|---|---|---|
| `people_counting.mp4` | Ultralytics Assets (MIT) | 0.36MB | 真实场景人员计数视频，含行人走动 | 行人检测 + 时序追踪 + 人群拥挤度 |
| `traffic_anpr.mp4` | Ultralytics Assets (MIT) | 10MB | 真实交通/道路视频（含车辆，部分行人） | 补充场景（街道） |

## 下载来源
- 原始地址：https://github.com/ultralytics/assets/releases/download/v0.0.0/People-counting-compressed.mp4
- 原始地址：https://github.com/ultralytics/assets/releases/download/v0.0.0/anpr-demo-video.mp4
- 许可：Ultralytics Assets，供测试/学习使用。

## 使用
- 将 `crowd_test.py` 扩展为支持 `--input *.mp4` 视频模式（逐帧处理 + 时序状态）
- 集成 Kalman 滤波（轨迹平滑）、图拉普拉斯谱特征、FSM 风险决策后，在视频上跑实验
- 统计：平均帧率(FPS)、等级序列、状态切换次数、检出人数随时间变化

## 备注
- 如需更多行人/人群视频，可补充 MOT17 数据集（需注册）或 Pexels/Pixabay 免版权视频。
- 视频为真实拍摄数据，非合成。下载时间：2026-10-25。