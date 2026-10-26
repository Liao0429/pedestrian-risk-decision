# Agent 5 C++ 与嵌入式部署专家 — 任务书

## 角色
独立审查 H:/PedestrianRiskDecision 板端 C++ 工程。目标板 Luckfox Pico Max RV1106（256MB/1TOPS NPU/单核 A7）。本机无 Luckfox SDK，无法真实交叉编译——未验证项必须明确标注。

## 必读
- src/luckfox_rtsp_yolov5/CMakeLists.txt、Makefile、src/main.cc（初始化序列/主循环/看门狗/超时/清理）、luckfox_mpi.cc、yolov5.cc、postprocess.cc、risk_engine.cc、enhanced_decision.cc
- include/risk_config.h、risk_engine.h、enhanced_decision.h、ipm_transform.h
- src/algorithms/*.h（header-only 算法）
- scripts/start_demo.sh、config/default_config.json
- docs/review/BASELINE_RESULTS.md（真机 6FPS、-O2 无提升、日志样例）

## 审查问题
1. C++ 主程序集成度：RKNN 推理、VI 取帧、VENC 编码、RTSP 推流、风险决策是否真正串通；算法模块是否进入实时主循环并输出叠加/日志。
2. 交叉编译/SDK/OpenCV/运行库/部署脚本问题。
3. 资源与性能：单线程串行、每帧堆分配、Jacobi O(n^3)、vector<vector<bool>> 邻接矩阵在 256MB 上的影响。
4. 稳定性：检查 main.cc 是否仍存在 -1 无限阻塞、返回值忽略、无重连、资源泄漏、异常退出（之前批次已加：VI 取帧超时 1s、连续失败 30 次退出、venc 返回值检查、frame_dt JSON 配置、cleanup lambda）。
5. 配置可维护性：硬编码阈值、IPM 默认 H（srcPts/dstPts）、homography_file 外部标定。
6. '主机 make test 通过 ≠ 板端可用'的差距；给出无需开发板即可执行的验证方式清单。

## 交付
- 报告保存到 H:/PedestrianRiskDecision/docs/review/agent5_embedded.md（write_file，中文）
- 最终回复：报告路径 + top_findings（severity/issue/evidence）

## 环境注意
- terminal 可能挂起；用 read_file/execute_code
- 不要修改源码
