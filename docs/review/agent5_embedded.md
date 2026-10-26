# Agent5 · C++ 与嵌入式部署专家 · 审查报告（总负责人补写）

> 说明：原 Agent5（嵌入式专家）在并行评审中被中断（deleg_1b366aba task-4 status=interrupted），本报告由总负责人基于 Agent1（架构师）、Agent2（CV/跟踪）、Agent8（演示）报告的嵌入式相关内容，以及 `IMPLEMENTATION_LOG.md` 与真机验证记录**补写**，并标注每条的来源。未进行新的仓库改动。

## 一、真实集成状态（板端）

**已真正接入 main.cc 单线程主循环的**（Agent1 确认）：
- VI 取帧 → RKNN YOLOv5n(INT8) 推理 → NMS → person 框 → IPM 地面米坐标 → 每目标 Kalman 平滑 → 图拉普拉斯网络特征(n/density/r_gcc/clustering) → RiskFSM 分级 → 画框/风险条 → H.264 → RTSP。
- 已真机部署（开发板 192.168.0.136）：运行稳定、RTSP :554 / go2rtc :1984 正常。

**已完成的工程健壮性修复**（IMPLEMENTATION_LOG，commit `41194e2`/`358a36f`）：
- VI/VENC 从 `-1` 无限阻塞改为 **1s 有限超时**；连续失败 30 次主动退出，由 systemd 看门狗重启。
- `venc_init` 返回值检查（不再吞错）。
- `rtsp_tx_video` 返回值检查（成功返回帧长、失败 `<0`），失败计数告警。
- 取帧失败时跳过编码/推流/释放（不再编码陈旧帧、不再错误释放）。
- Kalman `frame_dt` 支持 JSON 配置（`default_config.json` 默认 0.1s）。
- 板端部署 `pedestrian-risk.service`（Restart=always）+ `pedestrian-watchdog.service`（日志新鲜度看门狗）。

## 二、仍存在的嵌入式问题（按严重度）

1. **【高】板端真实行人从未验证**（Agent1/2/8）：摄像头前无人，日志全 `SAFE n=0`；WARNING/DANGER 端到端未在目标硬件实测。→ 需现场放人物照片/人群入镜验证。
2. **【高】IPM 单应硬编码、未真机标定**（Agent2/6）：`main.cc` 默认梯形仅覆盖 y=300..480，`homography_file` 为空；区域外脚点无保护外推（`ipm_transform.h` 仅做 w≠0 归一化）。→ 需用 `ipm_calibrate.py` 做真实 4 点标定并写入配置。
3. **【中】单线程串行主循环 + 每帧 Jacobi O(n³)**（Agent1/2）：6 FPS 的结构性瓶颈；建议先做主循环耗时打点，再做检测降频/独立决策线程。
4. **【中】检测阈值不可见**（Agent2）：`BOX_THRESH`/`NMS_THRESH` 来自外部 SDK 的 `yolov5.h`，仓库中无该头文件，无法独立编译/调参。
5. **【中】C++/Python 平滑语义不一致**（Agent2）：C++ `smoothPoints` 漏检时目标不出现在输出（n 立即掉）；Python `KalmanTracker.states()` 返回全部存活轨迹（n 保持）。
6. **【低】启动日志噪音**（Agent8）：`RkLunch-stop.sh not found`（板子无该脚本），一行可修复。
7. **【中】无长稳记录**：看门狗已就位，但 8h/24h 长稳数据未采集。

## 三、建议（按收益/成本排序）

1. **主循环耗时打点**（VI / RKNN / cvtColor / 谱特征 / VENC 各段时间戳）→ 定位 6 FPS 的 166ms 分布。成本低，收益高。
2. **真机 IPM 标定**（打印棋盘格/地面 4 点，写入 `homography_file`）→ 让"米制坐标"可信。
3. **现场制造真实行人**（人物照片/人群入镜）→ 完成 WARNING/DANGER 端到端真机验证，补最关键证据。
4. **检测降频 + 决策线程解耦**（检测 6FPS、决策/推流 15FPS 等）→ 提升帧率或降低 CPU。
5. **修复 `RkLunch-stop.sh` 噪音**（先检查脚本存在性再调用）。

> 来源标注：Agent1 报告 §1-§5；Agent2 报告 §1-§8；Agent8 报告 §一/§二；IMPLEMENTATION_LOG.md。
