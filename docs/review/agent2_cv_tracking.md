# Agent2 计算机视觉 / 目标跟踪专项审查报告

范围：`src/algorithms/kalman_filter.h`、`kalman_tracker.py`、`bytetrack_tracker.h`、`src/luckfox_rtsp_yolov5/src/enhanced_decision.cc`（smoothPoints）、`scripts/crowd_test_enhanced.py`，并对照 `main.cc`/`postprocess.cc`/`risk_config.h`/`config/default_config.json`。只读审查，未修改任何文件。

## 问题清单

### 1. Python 与 C++ 平滑输出语义不一致（“幽灵点”）
- 【证据】`crowd_test_enhanced.py:101-102` 取 `tracker.update()` 返回的全部活跃轨迹；`kalman_tracker.py:414-416` 的 `states()` 包含未匹配（仅外推）轨迹；而 `enhanced_decision.cc:126-137` 未匹配轨迹不输出、`:58-62` 空输入直接清空轨迹。
- 【严重度】高
- 【影响】PC 端漏检帧仍输出外推幽灵点，n/density/r_gcc 虚高，人群散去后风险等级延迟回落；C++ 端 n 恒等于当帧检测数，两端结果不可直接对比。
- 【建议】统一语义：Python 在特征计算前过滤 `misses>0` 的轨迹，并加 golden 回归测试锁定两端一致。

### 2. 固定 dt 与真实帧率不匹配
- 【证据】`crowd_test_enhanced.py:86` 用视频 FPS 定 dt；`config/default_config.json` 设 `frame_dt=0.1`（10FPS），`main.cc:126` 传入板端；`kalman_filter.h:250-273` 的 F/Q 按固定 dt 构建。
- 【严重度】高
- 【影响】板端帧间隔抖动，固定 dt 使漏检外推位移与速度估计失真；PC 用 1/30s、板端 0.1s，滤波器动态特性不同，PC 结论难外推。
- 【建议】按真实时间戳变步长更新（每次重算 F/Q），Python 端改读 config.frame_dt。

### 3. 贪心最近邻关联在密集场景易 ID 互换，ByteTrack 未接入
- 【证据】`enhanced_decision.cc:80-92` 距离贪心关联、门限 2m；`kalman_tracker.py:341-383` 同构；`docs/ARCHITECTURE.md` 将 `bytetrack_tracker.h` 标为“升级”，但两处流水线均未使用。
- 【严重度】中高
- 【影响】行人间距<2m 交叉时轨迹被错误关联，脚点被拉向他人，产生位置跳变与虚假边。
- 【建议】改用 bytetrack 的 IoU 关联+低分二次关联，再取轨迹框底边中点做 IPM。

### 4. match_by_id 关联路径无门控
- 【证据】`kalman_tracker.py:329-339` 按 ID 直接 `correct`，无距离门限；最近邻路径 `:369` 有 `gate_dist` 检查。
- 【严重度】中
- 【影响】上游 ID 错误/互换时，任意距离观测直接注入状态，速度估计瞬间爆表。
- 【建议】所有关联路径统一加新息门限，超门限的 ID 匹配转为 miss。

### 5. 板端脚点精度：整数框 + letterbox 反算截断
- 【证据】`main.cc:349-354` 脚点取 box 底边中点再 `mapToGround`；`main.cc:76-82` `mapCoordinates` 用 `(int)` 截断；`postprocess.cc:467-470` 输出整数框并 clamp。
- 【严重度】中高
- 【影响】小目标（高<30px）脚点误差达数像素，IPM 在标定梯形上边缘（y≈300）附近将误差放大数倍，超出 R=0.5m 假设；框底中心在坐姿/遮挡时并非真实脚点。
- 【建议】postprocess 保留浮点框、mapCoordinates 浮点化；对标定区外的脚点截断或丢弃。

### 6. 观测噪声固定，不随距离/目标尺寸自适应
- 【证据】`kalman_filter.h:271-272` R=diag(0.5²)；`enhanced_decision.h:86-90` 默认 `kalman_measurement_noise=0.5`。
- 【严重度】中
- 【影响】远处小目标脚点地面误差>1m 仍被过度信任，平滑不足，抖动直接进入图特征。
- 【建议】R 按目标框高度/IPM 尺度自适应（可参考 ByteTrack 按 h 缩放噪声的做法）。

### 7. 新轨迹首帧即输出，无确认机制
- 【证据】`enhanced_decision.cc:114-124` 未匹配点立即建轨迹并原样输出；`kalman_tracker.py:385-400` 同样首帧即输出。
- 【严重度】中
- 【影响】低照度下单帧误检直接成为图节点，n 虚增、density 跳变。
- 【建议】引入 `min_hits` 确认机制，未确认轨迹不参与特征计算。

### 8. 低照度/小目标：模型与帧率双重不匹配，漏检容错过短
- 【证据】PC 用 YOLOv8n FP32（`crowd_test_enhanced.py:171`），板端 YOLOv5n INT8 RKNN（`postprocess.cc:408-418`）；`enhanced_decision.h:89` `kalman_max_miss=5`（10FPS 仅 0.5s）；`enhanced_decision.cc:58-62` 空帧清空轨迹。
- 【严重度】中高
- 【影响】低照度/密集遮挡下 INT8 漏检频繁，0.5s 即断轨并重建 ID，平滑失效；空帧清空加剧 ID 碎片化；PC 实验结果不代表板端真实表现。
- 【建议】max_miss 提升至 15~30 帧、空帧不立即清空；PC 用 INT8 模型/10FPS 复现对比；低照度加预处理（如 CLAHE/红外补光）。

## 总体结论

Kalman 滤波核心数学实现（Python/C++ 双端）正确且一致，但多目标关联与工程集成存在上述 8 个问题。最优先处理 #1（幽灵点语义分歧）、#2（变步长）与 #3（接入 ByteTrack），其次是 #5/#8（板端脚点精度与低照度鲁棒性），否则板端与 PC 实验的风险等级结论不可互相印证。
