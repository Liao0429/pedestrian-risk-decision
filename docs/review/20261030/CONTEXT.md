# 项目评审上下文（第二轮 B 团队）

> 供 B1~B8 独立评审子代理使用。2026-10-30 生成。

## 项目概况
- **项目**：`G:/PedestrianRiskDecision` 毕设《行人风险决策系统》
- **硬件**：Luckfox Pico Max（RV1106）+ SC3336 摄像头 + RTSP + YOLOv5n 检测 → Kalman 平滑 → 图拉普拉斯谱特征 + 风险 FSM → 等级输出；配套 100×80mm PCB 底板 + 2.8" ILI9341 SPI 触摸屏 + 双 SG90 云台 + USB 喇叭/麦克风 + RTL8723BU WiFi + 3D 打印外壳
- **语言**：Python 原型（PC 实验）+ C++11 header-only（板端）双实现
- **当前分支**：`review-opt-20261029`（本地评审分支，不推送远程）；最新提交 `05cf1b1`
- **Python 环境**：`G:/py311_gpu/Scripts/python.exe`（含 numpy/pytest/trimesh 等）
- **跑测试**：`cd /g/PedestrianRiskDecision && env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" -m pytest tests -q`
- **C++ 主机测试**：`cd src/luckfox_rtsp_yolov5 && make test`（需 g++，本机 G:/DevTools/mingw 或 WSL/远程）
- **远程环境**：老电脑 Ubuntu 192.168.0.63（SSH 用户/口令见本地记忆，**评审文档禁止写明文凭据**）；开发板 192.168.0.136

## 代码结构
- `src/algorithms/`：graph_laplacian.py（谱特征）、risk_fsm.py（FSM）、C++ 对应头文件（risk_fsm.h/graph_laplacian.h/risk_engine.h）
- `src/luckfox_rtsp_yolov5/`：板端工程（yolov5.cc、main.cc、CMakeLists、Makefile、include/）
- `src/hardware/`：pan_tilt.h（舵机限幅 60~120°）、led.h、button.h、voice_alert.cc、ili9341.h、lvgl_menu.h 等
- `scripts/`：crowd_test.py / crowd_test_enhanced.py / spectral_fsm_experiment.py / reproduce_consistency.py / generate_pcb.py（PCB）等
- `tests/`：190 passed / 4 skipped（pytest）
- `docs/`：架构、算法、硬件、导读、评审报告
- `results/rerun/`：111 条视频时间线 JSON + 聚合结果（`_aggregate.json`、`_comparison_summary.json`、`_spectral_fsm_experiment.json`、`_consistency.json`）
- `results/edge_board/`：板端真机日志/长稳数据
- PCB/Gerber/3D 外壳在仓库外：`G:/毕设论文/02_答辩准备/PCB_Gerber/`（generate_pcb.py、design.json、*.gbr、嘉立创下单说明.md）与 `G:/毕设论文/02_答辩准备/3D_外壳/`（generate_enclosure_v2.py、enclosure_v2_*.stl、verify_alignment.py、装配验证报告.md）

## 关键口径（已确认，不要重复质疑）
- 111 条真实视频 45021 帧：SAFE 26885 / WARNING 6388 / DANGER 11748；66 条出现 DANGER，31 条全程 SAFE
- 生产公式：`score += w4·fiedler/(fiedler+1)·r_gcc`（w4=0.1）；M1/M2 是实验公式（无 r_gcc 门控）
- 一致率：M0(w4=0) = 89.6737%（40372/45021，可复现）；生产配置回放 = 91.3~91.6%（可复现，见 `results/rerun/_consistency.json`）；98.21% 来自旧评审报告 cross_R10 但当前代码不可复现——README 已按此标注
- Fiedler 均值随等级单调上升：SAFE 0.017 / WARNING 0.214 / DANGER 1.078
- Spearman ρ=0.688（全量，描述性）；按视频聚合 ρ 均值 0.2273 / 中位 0.1959（帧非独立，不宣称显著）
- 图片数据集：images500（500 张 COCO，1~19 人，检出率 82.5%）；videos50（111 条 Mixkit）
- 板端：黄金测试 174/174，FPS 7.46（NEON 预处理 136→33ms），RKNN 模型 yolov5n
- PCB：Gerber v1.6（98 焊盘/19 走线/14 过孔），跳线区/输出排针 2.54mm，SPI：MOSI=Pin15/MISO=Pin16/CS=Pin12/SCLK=Pin14，PWM8_M1=Pin7/PWM9_M1=Pin6/PWM10_M1=Pin5/PWM11_IR_M1=Pin4
- 3D 外壳：5 件分体（底座/背板/面板/云台/盖板），53×79 窗口，10/10 零干涉，螺柱局部 z=5 装配 +30
- 历史数据：7807 是谱特征接入前生产快照（git a92e4a5 存档），M0 离线重算 7906

## 本轮目标
1. 用**与第一轮不同的 8 个专业视角**（B1~B8）对项目做独立评审，重点找**新问题**（而非重复已知项）
2. 每份报告：总体结论 + 问题清单（P0/P1/P2）+ 每条问题给**证据**（文件:行号或命令输出）+ 修复建议
3. 报告中文，保存到 `G:/PedestrianRiskDecision/docs/review/20261030/agent_B{n}_{角色}.md`
4. **必须真实检查**：跑测试/读代码/查数据，禁止凭印象编造；无法验证的标注"未验证"

## 第一轮已完成的修复（不要重复报告为问题）
- P0×8：舵机 PWM 引脚、BOM 位号、语音方案、评分倒挂门控（min_danger_n=3+去重）、RKNN 返回值、STL 几何、F3 螺柱/穿线孔坐标、折边孔
- P1×17：构建、FOV 限幅（60~120°）、连接矩阵、FFC、PWM overlay、引脚表、数据口径、中文注释、SPI 映射
- P2×10+：配置键名、Gerber v1.6 间距、扩展方案废弃标记、PWM10/11 物理脚等
- 7 项低危（2026-10-30 清理）：门控/去重直接单测、yolov5.cc 死标签、一致率存档+README 口径、成本表引用 BOM v3、导读提醒
- 若发现上述领域仍有**实质性新问题**（非重复），可报告，但要说明"修复后仍存在"并给证据