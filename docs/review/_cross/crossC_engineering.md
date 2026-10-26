# 交叉质疑 C：工程修复与演示投入仲裁 — 工程安全仲裁报告

> 工程安全仲裁员 · 2026-10-27
> 依据：agent1/2/5/8 四报告 + 对 postprocess.cc、main.cc、luckfox_mpi.cc、Makefile、ci.yml、default_config.json、graph_laplacian.h、tests/、git 历史、板端截图逐像素分析的独立核验。

## 1. NMS 跨类别修复：方向正确，可合入；需补测试 + 板端复验

- 核验：postprocess.cc:128 已改 `classIds[j] != filterId`（工作区未提交）。旧条件在外层已保证 `classIds[i]==filterId` 时恒假，退化为跨类别混合 NMS——确认真实 bug，修复方向与 ultralytics per-class NMS 一致。
- 影响：人+车/自行车重叠场景会保留更多 person 框，检测人数/风险等级可能上升；纯行人不变；历史 n=0 日志不受影响。
- 板端复验：**需要**——重编产物后做"行人+自行车同框"冒烟。
- 合入判定：**可以合入**（一行改动、逻辑无争议）。但 postprocess.cc 依赖缺失的 `yolov5.h`，主机编译不了、CI 测不到；须补 stub 头或把 nms 抽 header-only 加回归测试，否则修复无保护。

## 2. VENC 颜色：静态看是真实 bug，5 分钟色卡定论

- 核验：main.cc:323-325 用 `COLOR_YUV420sp2BGR` 把 **BGR** 写入 VENC 输入缓冲；main.cc:231 与 luckfox_mpi.cc:111 声明 `RK_FMT_RGB888`，字节序不一致 → 红蓝互换风险。写法源自 Luckfox 官方示例，但官方示例可能自带此问题。
- 附加影响：DANGER 红条 `(0,0,255)` 会显示为蓝、WARNING 黄条变青，破坏演示语义。
- 低成本验证：板端放红/蓝色卡看 RTSP 5 分钟。现有 4 张板端截图全为灰场景+绿条（绿条红蓝互换后仍绿，无法证伪）且近乎相同，建议重抓。
- 修复：首选 VENC 改 `RK_FMT_BGR888`（若 SDK 支持），不动推理链；若改 cvtColor 为 RGB，RKNN 输入通道也变，须验证检测不掉点。

## 3. 画框/决策过滤不一致：已部分修复，残留低危

- 核验：9472a5a 已给画框加 target_classes 过滤（main.cc:390-393），agent1"全部画框"已过时；画框仍未应用 `class_conf_thresholds`（决策路径 main.cc:346-348 有）。配置差异化阈值时框数>决策人数，**真实**。
- 默认 `class_conf_thresholds={}`，不配置无差异。修复约 1 行（画框复用同一精筛条件）。低优先，答辩前顺手修。

## 4. CI 修复：方向正确，预判 Ubuntu CI 通过

- 核验：ci.yml 已改 `cd src/luckfox_rtsp_yolov5 && make test`，替换原 no-op（不存在文件+`|| true`）。
- Makefile OPENCV 条件：CI 无 OpenCV → `pkg-config` 空 → `RISK_CONFIG_SRC` 空 → test-risk-config 只编 test_risk_config.cc；该测试用 `__has_include` 守卫，无 OpenCV 时 SKIP 相关断言，可编译链接。其余 3 目标（enhanced/minijson/golden）无 OpenCV 依赖，golden JSON 存在。
- 预判：**通过**（本机无 g++ 未实跑，静态核验）。注意：CI 上 risk_engine.cc 仍不编译、OpenCV 断言 SKIP 属已知妥协，建议 CI 加可选 `libopencv-dev` 覆盖。

## 5. 性能瓶颈：Jacobi 在密集 50 人时也不构成反例

- 核验 graph_laplacian.h:155-218：每次迭代一次旋转，上限 `max_sweeps=100`，每轮 O(n²) 找最大非对角元。n=50 上限≈100×1225 次比较，亚毫秒~毫秒级，远小于 rknn_run 的 80-140ms。
- 附注：100 次上限可能使大 n 谱特征不收敛而静默回退 0；但谱特征未进 FSM（agent1 P0-1），不影响决策。
- **"先打点再优化"正确**：先加分段计时，再上 RGA/降分辨率/降频/流水线。不要为 Jacobi 投入。

## 6. agent8 演示减法：合理，采纳

- 砍 PCB/LVGL/语音大方案：同意，答辩前 3 天只做能进 PPT/现场演的事。
- 保留：真机 DANGER 触发视频（第一优先）、风险曲线/IPM BEV/网络拓扑 3 图、网页与资源监控 2 截图、OLED/语音告警（可选）。
- 工程侧补充：`voice_alert.h` 声明无实现（agent1 P1-6），不做语音就从 README 删除或标注"方案/未集成"。

## 仲裁结论（Top 5）

1. **NMS 修复正确可合入**，但须补主机回归测试（stub 头）+ 板端重编译"人+车同框"冒烟；修复会改变混合场景检测数/风险等级。
2. **VENC 颜色疑为真实 bug**（BGR 数据声明 RK_FMT_RGB888，DANGER 红条会显示为蓝）；色卡 5 分钟定论，修复首选 `RK_FMT_BGR888`。
3. **画框/决策过滤残留不一致真实但低危**，默认配置无差异，一行修复。
4. **CI make test 预判通过**：4 个测试目标无 OpenCV 硬依赖，无 OpenCV 时 risk_config 相关用例 SKIP 属预期。
5. **Jacobi 非瓶颈（50 人亦非反例）**；优化顺序=打点→RGA/降分辨率/降频；演示投入采纳 agent8 减法，真机 DANGER 视频第一优先。