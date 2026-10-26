# Agent 2 计算机视觉与目标跟踪专家 — 任务书

## 角色
独立审查 H:/PedestrianRiskDecision 的检测与跟踪实现。目标板 RV1106（1TOPS NPU，YOLOv5n INT8，板端实测 6-6.9FPS）。

## 必读
- src/luckfox_rtsp_yolov5/src/yolov5.cc、postprocess.cc（检测后处理/脚点提取）、main.cc（letterbox 720x480→640x640，scale/leftPadding/topPadding）
- src/luckfox_rtsp_yolov5/include/ipm_transform.h、src/algorithms/ipm_calibrate.py（IPM 地面坐标）
- src/algorithms/kalman_filter.h、kalman_tracker.py、bytetrack_tracker.py/.h
- src/luckfox_rtsp_yolov5/src/enhanced_decision.cc（smoothPoints：每目标 Kalman+最近邻贪心关联；参数 kalman_gate_m/process_noise/measurement_noise/max_miss/frame_dt 来自 config/default_config.json）
- scripts/crowd_test.py、crowd_test_enhanced.py（Python 实验）
- dataset/README.md、results/experiment_summary.json（检出率 82.5%/高密度 76.7%）

## 审查问题
1. YOLOv5n INT8 在板端的实际推理链路（rknn 初始化/推理/后处理）与漏检、误检、遮挡、小目标、低照度场景的潜在缺陷。
2. Kalman 平滑实现质量：单目标 4 态模型套多目标的合理性、最近邻贪心关联的局限（交叉/遮挡）、门限与噪声参数。
3. ByteTrack 实现是否完整、是否有调用方、与 Kalman 是否重复。
4. 检测框脚点提取方式（用框底边中点？）与 IPM 映射的误差来源（默认 H 矩阵、广角畸变）。
5. Python 实验与 C++ 板端部署的差异：输入分辨率、letterbox 坐标还原、置信度阈值、帧率（Python 可能 >10FPS，板端 6FPS）、检测类别。
6. 真正瓶颈判断：不要默认换检测模型。给出证据。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent2_vision_tracking.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code 读文件
- 不要修改源码
