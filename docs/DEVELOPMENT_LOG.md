# 开发进度日志（DEVELOPMENT LOG）

> 每完成一项优化，在此追加记录（含 commit hash），GitHub 留痕。

## 2026-10-26 睡前自主推进（用户验收）

### 第 2 轮评测启动
- 派发 6 个评测 agent（docs/review2/）：fix_recheck / code_quality / demo_readiness / algorithm_config / test_coverage / security_robustness
- 已返回 2 份：code_quality.md、demo_readiness.md
  - code_quality：P1 消除 RiskEngine/EnhancedDecision 重复计算、VLA 改 vector、初始化错误检查；P2 黄金回归测试、统一枚举、全局状态封装
  - demo_readiness：README/架构图未同步网页观看+多目标、需 start_demo.sh 一键启动、WIFI_SETUP 需标注实测状态、需准备有人/聚集演示素材

### 已完成
- 建立文档看板 docs/KANBAN.md
- 创建心跳任务（hermes cron 9cb3159e30dc，每 60 分钟自主推进）
- 编写开发板一键启动脚本 scripts/start_demo.sh（答辩现场拉起 RTSP + go2rtc + 打印访问地址）
- 待办：README/架构图补新功能、WIFI_SETUP 标注、预录演示视频、P1 软件优化、论文补新章节

### 待后续提交
- scripts/start_demo.sh、docs/KANBAN.md、docs/DEVELOPMENT_LOG.md

## 后续推进（心跳第 1 轮）
- 6 份评测报告全部返回（docs/review2/）：fix_recheck / algorithm_config / test_coverage / security_robustness / code_quality / demo_readiness
- 已修复：
  - default_config.json COCO 类别注释修正（狗=16、猫=15，车辆[2,5,7,3]，动物[14,18,19,20,21]）
  - Python risk_fsm.py 帧保持 bug（now_ms=None 时对齐 C++），新增回归测试（14 项全过）
  - 论文初稿移出仓库 → H:\毕设论文\thesis_agent_draft\（docs/thesis 已 gitignore，不上传）
- 提交：ea39d12（P0答辩文档）、0b51d00（修复+6份评测报告+演示素材+论文移出）
- 进行中：Codex 后台执行 P1 C++ 优化（VLA/初始化检查/重复计算/枚举统一，proc_ed9f0c5f55c5）
- 待办：main.cc sprintf 栈溢出修复（P0安全，等 Codex 完成后改）、论文补网页观看/多目标章节、预录演示视频整理

## 后续推进（心跳第 2 轮）
- ✅ 修复 P0 安全：main.cc sprintf 栈溢出（text[16]→[32]+snprintf，info 也改 snprintf）
- ✅ 多目标脚本同步：crowd_test.py / crowd_test_enhanced.py 加 `--classes` 参数（逗号分隔 COCO id），mock 检测器签名同步，134 测试全过（commit 2a53577）
- ✅ Codex P1 C++ 优化完成并推送（commit 3b44cf9）：消除 RiskEngine/EnhancedDecision 重复计算（config 加 enable_risk_engine_fallback:false 默认关）、统一 IPM 变换（新增 ipm_transform.h）、VLA 改 std::vector+校验、关键初始化错误检查、统一 RiskLevel 枚举；修复 yolov5.cc vector 传参 .data()；134 测试全过 + 交叉编译通过
- ⏳ 下一步：把最新版部署到开发板真机验证；论文 agent 后台补充网页观看/多目标章节

## 后续推进（心跳第 3 轮，响应 review2 安全报告）
- ✅ 处理第 2 批后台评测（algorithm_config/test_coverage/security_robustness）高优项：COCO注释（已修）、risk_fsm（已修）、sprintf栈溢出（已修）、初始化检查（已修）、脚本多目标（已修）均在前面 commit
- ✅ 新增 main.cc 信号处理（SIGINT/SIGTERM → g_stop → while(!g_stop) 优雅 cleanup 退出，配合 systemd Restart=always 自愈）；memset(text,sizeof text) 修正；commit a13371d
- ✅ 信号处理版已部署开发板真机运行（RTSP 554 + 网页 1984 + 增强决策正常）
- ⏳ 待办：T3 黄金回归测试（Python/C++ 一致性）、按类别差异化阈值、最终验收报告 docs/FINAL_REPORT.md

## 论文初稿完善（agent 后台完成）
- ✅ 本地论文 `H:\毕设论文\thesis_agent_draft\毕业论文.md` 新增 **6.6 多目标类别可配置** + **6.7 网页监控终端（go2rtc）** 两节，目录/功能需求表（F10/F11）/工作总结同步更新
- 论文基于真实源码/文档/提交（c043879/ea39d12/0b51d00），未编造；仍只在本地，不上传 GitHub
- 看板 T5 标记完成；下一步由心跳 cron 推进 T3 黄金回归测试 / T6 演示美观打磨

## 后续推进（心跳第 4 轮，落地 T3 黄金回归测试）
- ✅ **建立 C++/Python 双实现黄金回归框架**（响应 code_quality/test_coverage 报告）：
  - 生成器 `scripts/generate_golden.py`：用已验证的 Python 实现生成固定输入/期望输出 JSON 到 `tests/golden/`（graph_laplacian 6 用例 + ema_smoother 3 用例 + risk_fsm 3 用例）
  - Python 侧回归 `tests/test_golden_regression.py`：读取同一批 JSON 重跑 Python 实现，3 测试全过
  - C++ 侧主机测试 `src/luckfox_rtsp_yolov5/tests/test_golden_regression.cc`：用 minijson 读取同一批 JSON，在旧电脑（192.168.0.68）g++ 编译运行 **104/104 断言全过**
  - 已接入 Makefile（`make test-golden`）与 CMakeLists（ctest 新增 test_golden_regression，显式传 golden 绝对路径）
- 🐛 **顺带发现并修复 minijson 布尔解析 bug**：`parseLiteral()` 忽略 value 参数恒返回 `true`，导致 JSON 里 `false` 被解析为 `true`。影响 `config/default_config.json` 的 `enable_risk_engine_fallback: false` —— 会被读成 true，**板端会静默启用 RiskEngine 重复计算旁路**（抵消 3b44cf9 的 P1 优化）。已修复并新增 `test_minijson.cc::test_boolean_false`（32/32 过，含回归用例）。
- ✅ 全量 Python 测试 137 passed；C++ 黄金回归 + minijson 均在旧电脑编译运行通过
- 📌 待办：T6 答辩演示打磨（美观/预录视频最终确认）、最终验收报告 docs/FINAL_REPORT.md 已存在，本次提交后更新

## 后续推进（心跳第 5 轮，落地按类别差异化置信度阈值）

- ✅ **实现「按类别差异化置信度阈值」**（响应 review2 algorithm_config 建议，车辆/动物语义不同）：
  - `config/default_config.json` 新增可选字段 `class_conf_thresholds`（COCO id -> 最低置信度 0~1，空对象默认不区分）
  - `risk_config.h` 解析该映射并校验（键 0~79 整数、值 [0,1]、对象类型），非法配置抛异常
  - `main.cc` 增强决策循环：目标类别过滤后，若该类配置了更高阈值且当前置信度不足则精筛跳过（不送入空间网络），未配置类别行为不变
  - `test_risk_config.cc` 新增 `test_class_conf_thresholds()`：正常读取/补零键/非法键/越界/非法值/非法类型 共 6 组断言
- ✅ **验证**：
  - Python 全量测试 **137 passed**
  - C++ 主机测试（旧电脑 192.168.0.68 `make test`）：enhanced_decision 26/26、minijson 28/28、risk_config **27/27**（含新增差异化阈值用例）全过
  - 交叉编译器（arm-none-linux-gnueabihf-g++）对 main.cc 做 `-fsyntax-only`：本次修改无新增错误；剩余 `rknn_app_context_t.input_mems` 报错为旧电脑 SDK include 环境差异（该行是既有代码，真实交叉编译以 SDK example CMake 构建为准，待真机部署最终验证）
- 📌 待办：T6 答辩演示打磨（美观/预录视频最终确认）、单应矩阵外部标定文件加载、USB WiFi 采购实测

## 后续推进（心跳第 6 轮，收口 T6 答辩演示打磨）

- ✅ **新增 `docs/DEMO_SCRIPT.md` 现场答辩演示脚本**（响应 review2 demo_readiness 建议，落地为可直接照做的讲稿）：
  - **演示前检查清单**：开发板/摄像头/USB WiFi/手机热点/移动电源/预录视频/浏览器，逐项打勾
  - **8-10 分钟分步流程**：开场定位 → 网页实时观看 → 预录视频兜底（DANGER/WARNING 切换）→ 真机性能数据 → 算法流水线讲解（感知→IPM→图论→FSM）→ 硬件扩展方案 → 收尾
  - **每环节含旁白话术 + 操作步骤 + 预期现象 + 数据支撑**（12.5MB/42.5%/6.9FPS、DANGER 179/WARNING 120/SAFE 6、零误报）
  - **故障预案表**：网页打不开 / 一直 SAFE / 开发板不启动 / 现场无网线 / 时间不够，各给处理方案
  - **引用文件逐一核实存在**：`start_demo.sh`、两段增强视频+timeline、4 张真机抓帧、架构图/硬件 3D 图
- ✅ 同步更新：README 文档索引补 `DEMO_SCRIPT.md`；KANBAN **T6 标记完成**；FINAL_REPORT 完成度 **96% → 97%**、答辩准备段补现场讲稿
- ✅ 看板全部任务（T0-T8）现已 **全部完成**，无进行中项

## 后续推进（心跳第 7 轮，收口 review2 P1-8 单应矩阵硬编码）

- ✅ **落地「单应矩阵外部标定文件加载」**（响应 review2 `fix_recheck.md` 中唯一未修的 P1-8 高优项：main.cc 硬编码 srcPts/dstPts 四点，相机位姿变化需重编译）：
  - `include/ipm_transform.h` 新增 `loadHomographyFromFile(path, H)` 与 `loadHomographyOr(path, fallback)`：
    - 读取文本 3x3 单应（空格/逗号分隔，与 Python `src/algorithms/ipm_calibrate.py` 的 `save_homography(..., .txt/.csv)` 输出兼容）
    - 校验恰好 9 个数值、拒绝分母行全 0 的退化透视、文件缺失返回 false（不抛异常，便于回退）
    - 返回 `cv::Mat(3,3,CV_64F)`，天然满足 security_robustness 对 `setHomography` 的类型要求
  - `include/risk_config.h`：`RiskConfig` 新增可选字段 `homography_file`（空串=用默认），`loadRiskConfig` 校验必须是字符串
  - `src/main.cc`：配置了 `homography_file` 时优先加载外部标定并打日志，加载失败回退默认硬编码单应并告警；删除原先重复的 `loadRiskConfigOr` 调用
  - `config/default_config.json` 新增 `"homography_file": ""` 及注释；`README.md` / `KANBAN.md` / `FINAL_REPORT.md` 同步更新
- ✅ **测试**：`tests/test_risk_config.cc` 新增 `test_homography_file_field`（缺失默认空串/正常读取/非法类型抛异常）与 `test_load_homography_file`（合法空格/逗号 CSV/文件缺失/数值不足/退化矩阵/loadHomographyOr 回退）
- ✅ **验证（全真实）**：
  - Python 全量 **137 passed**
  - 旧电脑（192.168.0.68）主机 g++ 编译运行 test_risk_config：**30/30 通过**（OpenCV 缺失时加载函数走 SKIP，配置字段测试真实运行）
  - Luckfox SDK 交叉编译 `luckfox_pico_rtsp_yolov5`：**编译链接成功**（无新增错误）
  - 交叉编译独立板端测试 `homography_load_test`，传开发板 192.168.0.136 真实运行：`load ok=1 type=6(CV_64F)`、`map(0,480)->(173.469391,1081.632690)` 与手工齐次计算完全一致（w=1.96, x=340/1.96=173.47, y=2120/1.96=1081.63）；6 个数值/退化矩阵/文件缺失均正确返回 false
- 📌 看板新增 **T9 完成**：至此 review2 全部高优项（P0/P1 及 P1-8）均已落地，剩余仅硬件采购实测与 RTSP/网页鉴权等非主线项
