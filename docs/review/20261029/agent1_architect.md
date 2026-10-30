# A1 系统架构师 · 独立评审报告（2026-10-29 第二轮）

> 角色：系统架构师（A1）｜审查域：整体架构、模块边界、配置一致性、文档与代码同步、构建系统自洽性、docs/hardware 与 BOM v2 矛盾
> 分支：`review-opt-20261029`｜方式：只读审查，未修改任何代码
> 验证动作：通读 README.md / README.en.md / docs/ARCHITECTURE.md / docs/导读.md / config/default_config.json / src 全树 / CMakeLists.txt / Makefile / main.cc / risk_config.h / risk_fsm.{h,py} / docs/hardware/* 全部硬件文档 / G:/毕设论文/02_答辩准备/答辩硬件展示方案_v2_BOM与PCB手册.md；运行 `pytest tests/test_ablation_consistency.py tests/test_baseline_consistency.py tests/test_comparison_consistency.py tests/test_integration_pipeline.py tests/test_go2rtc_demo_config.py -q` → **33 passed, 4 skipped**（Windows 上 4 项 shell 测试跳过，CI 覆盖）。
> 说明：上一轮已闭环的问题（消融 OR→AND、配置键 temporal→fsm、谱特征 w4 接入 C++/Python FSM、CI 门禁、黄金独立参考）本次不再重复列出，仅在其仍与文档表述有关处顺带确认。

---

## 一、总体架构评价

**总体结论：分层清晰、配置单源、双端（C++/Python）实现与黄金回归锁定的做法在同类毕设中属优秀水平；主要问题集中在“文档化构建路径不可用”与“硬件文档（含外部 BOM v2）相互矛盾”两类，均不涉及算法正确性。**

架构分层（自底向上）：

```
src/algorithms/*.h       纯 C++ 头文件算法库（图拉普拉斯/Kalman/FSM/DBSCAN/ByteTrack/EMA/ipm_transform/minijson）
        ▲ 被 enhanced_decision.cc / risk_config.h 引用
src/luckfox_rtsp_yolov5  板端应用（main.cc + RKNN/RTSP/OpenCV，单线程主循环 + EnhancedDecision 主决策路径）
src/hardware/*           外设驱动（gpio/led/button/servo/pan_tilt/voice_alert/lcd_menu/ili9341/xpt2046/sysfs_spi）
        ▲ 仅被 tests/ 与 selftest_main.cc 消费（无板端运行时消费者，见问题 Q-9）
scripts/, tests/         Python 实验链（基线/增强/消融/谱特征/对比）与 CI 一致性测试
config/default_config.json  双端运行时配置唯一来源
```

优点（确认项）：
1. **配置键三端一致**：`default_config.json` 的 `fsm.*`（w1~w4、n_ref、up_warning、up_danger、down_safe、down_warning、confirm_up、confirm_down、danger_hold_frames、danger_hold_ms、trend_bias）、`danger/warning.n|rho|gcc`、`edge_dist_m`、`frame_dt`、`homography_file`、`class_conf_thresholds`、`target_classes`、`enable_risk_engine_fallback` 在 `risk_config.h`（C++）与 `risk_fsm.py`（Python）中均有真实读取与校验消费方，无“配置了没人读”的键。`temporal` 旧键已彻底清除。
2. **README 宣称的算法特性有代码消费方**：谱特征 w4=0.1 在 C++ `risk_fsm.h`（score += w4*fiedler/(fiedler+1)*r_gcc，行 147-150）与 Python `risk_fsm.py`（行 88）双端落地；黄金测试 174 项、一致性测试锁数字均属实。
3. **依赖方向单向**：algorithms ← luckfox ← hardware，hardware 各驱动仅依赖 `gpio.h`，无环。
4. **可测性设计好**：硬件驱动全部通过内存替身（Fake GPIO/SPI）主机测试，CI 覆盖。
5. **主机测试路径（`make test` + pytest）自洽**，33 项 Python 一致性测试通过。

问题集中在三处：① 两条文档化交叉编译路径（Makefile、CMake）均存在缺陷；② `docs/hardware/` 与外部 BOM v2 存在架构级矛盾；③ 若干“文档措辞超前于代码”的过度宣称（DBSCAN 进架构图、双并行路径、一键切换、英文 README 不同步）。

---

## 二、模块清单与消费关系

| 模块/文件 | 类型 | 消费方 | 状态 |
|---|---|---|---|
| `src/algorithms/graph_laplacian.h` / `kalman_filter.h` / `risk_fsm.h` | 纯头文件算法库 | enhanced_decision.cc（主路径） | ✅ 生产接入 |
| `src/algorithms/minijson.h` | 纯头文件 JSON | risk_config.h | ✅ 生产接入 |
| `src/algorithms/ipm_transform.h` | 纯头文件 | main.cc（mapToGround） | ✅ 生产接入 |
| `src/algorithms/ipm_calibrate.py` | 标定工具 | 文档指引（生成 homography_file） | ✅ 工具链 |
| `src/algorithms/crowd_dbscan.h` / `bytetrack.h` / `ema_smoother.h` | 备选扩展 | 仅 tests/（黄金/单测） | ⚠️ 无运行时消费者，文档表述需收敛（Q-5） |
| `src/luckfox_rtsp_yolov5/main.cc` | 板端入口 | 无（产品本体） | ✅ |
| `src/luckfox_rtsp_yolov5/risk_engine.cc` | 兜底决策 | main.cc 仅 `enable_risk_engine_fallback=true` 时构建（默认 false） | ⚠️ 默认不启用（文档表述需收敛，Q-5） |
| `src/hardware/*`（led/button/servo/pan_tilt/voice_alert/lcd_menu/ili9341/xpt2046/sysfs_spi） | 外设驱动 | 仅 tests/* 与 `selftest_main.cc`（hardware-selftest） | ⚠️ 无板端运行消费者（Q-9） |
| `scripts/start_demo.sh` / `hardware_selftest.sh` / `go2rtc_demo_stream.py` | 部署脚本 | 板端演示、CI（config 一致性测试） | ✅ |
| `scripts/crowd_test*.py` / `rerun_*` / `ablation_*` / `spectral_ablation_*` | 实验链 | CI 一致性测试锁定 | ✅ |
| `config/default_config.json` | 配置单源 | C++（main.cc/risk_config.h）与 Python（risk_fsm.py/crowd_test*.py） | ✅ |

---

## 三、问题清单

### P1（优先改进，建议本轮回合内修复）

**Q-1 ｜ Makefile `luckfox` 目标缺少工程头文件路径，README 宣称的板端构建命令不可用**
- 文件：`src/luckfox_rtsp_yolov5/Makefile:165-178`（luckfox 目标第 168-170 行仅 `-I$(LUCKFOX_SDK_ROOT)/include -I$(LUCKFOX_SDK_ROOT)/3rdparty -I$(OPENCV_ROOT)/include/opencv4`）；`README.md:165`（“交叉编译完整工程：`make luckfox LUCKFOX_SDK_ROOT=<SDK路径>`”）
- 问题：`main.cc` 引用了 `"risk_engine.h"`、`"risk_config.h"`、`"enhanced_decision.h"`、`"preprocess.h"`、`"ipm_transform.h"`（均在 `include/`），而 Makefile 的 luckfox 目标没有 `-Iinclude -I../algorithms`，按 README 命令直接执行会在编译阶段失败。
- 影响：按 README 部署文档无法产出板端二进制；实际成功路径（将源码放入 Luckfox SDK example 工程构建）未在 README 中写明，用户只能自己排查。
- 修复建议：luckfox 目标补 `-Iinclude -I../algorithms`（与 Makefile 测试目标 `INC` 一致）；README 同时补充“若在 SDK example 方式构建需怎样放置头文件”的说明。

**Q-2 ｜ CMake `BUILD_LUCKFOX_DEMO` 目标漏编 `preprocess.cc`，交叉编译链接必失败**
- 文件：`src/luckfox_rtsp_yolov5/CMakeLists.txt:103-109`（demo 目标源列表缺 `preprocess.cc`）；`CMakeLists.txt:11-12` 文档化该交叉编译路径；对比 `Makefile:171`（luckfox 目标已包含 `$(SRC_DIR)/preprocess.cc`）
- 问题：`main.cc` 调用的 `letterbox_prepare` / `yuv420sp_to_bgr` / `yuv420sp_to_rgb_letterbox` / `letterbox_params_free` 均定义在 `preprocess.cc`（非 inline），CMake demo 目标不含该源文件，且整个 CMakeLists 无任何 `preprocess` 引用，链接阶段必然 undefined reference。
- 影响：CMake 文档化交叉编译路径（`cmake -DBUILD_LUCKFOX_DEMO=ON`）不可用；与 Makefile 形成“两条路径互为不一致”的状态。
- 修复建议：demo 目标源列表加入 `${PROJ_SRC_DIR}/preprocess.cc`，并在 CMake 中补 `test_preprocess`（NEON 对照）以便 ctest 覆盖（见 Q-8）。

**Q-3 ｜ `docs/hardware/` 与 BOM v2 存在架构级矛盾（引脚、显示方案、软件状态、端口）**
- 文件：
  - `docs/hardware/PCB_BOARD_DESIGN.md`（v1.0）：外设为 **DFPlayer Mini 语音模块 + 串口屏（USART HMI）**，J6=串口屏四针排针；
  - `G:/毕设论文/02_答辩准备/答辩硬件展示方案_v2_BOM与PCB手册.md` 行 6（“依据 docs/hardware/ 下全部既有方案合并去重”）、行 78-79（J5=DFPlayer 六针排针、J6=触摸屏排针 SPI+触摸）、行 95-96（PWM_PAN=Pin5 PWM8_M1、PWM_TILT=Pin4 PWM9_M1）、行 126（“DFPlayer 插 J5，喇叭接 J7”）、行 56（注②“不再需要 DFPlayer 模块和额外 TF 卡”）、行 136-141（软件集成表）、行 191（go2rtc 端口 8080）；
  - `docs/hardware/system_integration.md` 行 7/28/44-46/70-73（OLED SSD1306 + JQ8900 语音 + 4 线程架构）、行 139-153 引脚表（行 143-144：舵机 Yaw=Pin54 PWM10_1、Pitch=Pin55 PWM11_1；行 147-148：OLED=I2C2 Pin72/73；行 151-152：语音=UART4 Pin77/78）。
- 问题（四层矛盾）：
  1. **屏幕/语音方案冲突**：PCB_BOARD_DESIGN.md 基于串口屏+DFPlayer，而 BOM v2 基于 ILI9341 SPI 触摸屏 + USB 喇叭；BOM v2 却要求“照 PCB_BOARD_DESIGN.md 连线”，照做会接错屏幕。
  2. **舵机引脚两套**：system_integration.md（Pin54/55，PWM10_1/11_1）与 PCB_BOARD_DESIGN.md/BOM v2（Pin4/5，PWM8_M1/9_M1）互斥，至少一套错误。
  3. **BOM v2 自相矛盾**：注②说不再需要 DFPlayer，但 J5 仍布 DFPlayer 排针、装配步骤仍要求“DFPlayer 插 J5”；“喇叭接 J7”但 J7 未在 J 列表定义。
  4. **软件集成表过时**：BOM v2 行 136-141 将 `led.h/.cc`、`button.h/.cc`、`voice_alert.cc` 标为“需实现”（实际均已实现并主机测试 118 项），并引用不存在的路径 `servo.h/.cc`（实际只有 `servo.h`）、`src/hardware/lvgl_menu/`（实际为 `lcd_menu.h`）。
  5. **端口错误**：BOM v2 行 191 写 `:8080`，实际 `scripts/start_demo.sh:60,79` 与 `docs/WEB_VIEWER.md:9,55` 均为 **1984**。
- 影响：答辩/硬件组装按文档无法正确复现；交叉评审若以 BOM v2 为准会与代码事实冲突。
- 修复建议：① 将 PCB_BOARD_DESIGN.md 升级为 v2（ILI9341 SPI + XPT2046 + LED/按键 + USB 喇叭，引脚与 BOM v2 对齐）；② system_integration.md 顶部标注“历史方案/已废弃”，或按当前硬件重写；③ 修正 BOM v2 内部 J5/J7、软件状态表与端口号。

### P2（可选，建议本轮或近期修复）

**Q-4 ｜ 导读声称的 PCB 文档版本与文件实际版本不符**
- 文件：`docs/导读.md:196`（“PCB 底板设计文档 v1.5”）vs `docs/hardware/PCB_BOARD_DESIGN.md:1`（“嘉立创 PCB 底板设计规格书（v1.0）”）
- 影响：文档索引失实，评审引用时版本号对不上。
- 修复建议：随 Q-3 升级 PCB 文档至 v1.5/v2 后同步导读；或改导读为 v1.0。

**Q-5 ｜ ARCHITECTURE.md 把 DBSCAN/RiskEngine 描述为运行时必选路径，与代码不符**
- 文件：`docs/ARCHITECTURE.md:8,22,50,83`（DBSCAN 进入系统描述与架构图、模块表）；`docs/ARCHITECTURE.md:31,70`（“两条并行决策路径”：riskEngine 与 enhancedDecision）；`docs/ARCHITECTURE.md:109`（配置表写 `edge_dist`，实际键为 `edge_dist_m`）
- 问题：main.cc 主循环只走 EnhancedDecision；RiskEngine 仅在 `enable_risk_engine_fallback=true`（默认 false）时构建；DBSCAN/ByteTrack/EMA 仅有测试消费方（导读已诚实标注“备选扩展”，但 ARCHITECTURE.md 仍画进架构图）。
- 影响：阅读架构图的读者会误认为 DBSCAN 已接入、双路径常驻；`edge_dist` 键名与 `default_config.json:2` 的 `edge_dist_m` 不一致，按文档改配置会改错键。
- 修复建议：架构图/正文为 DBSCAN、ByteTrack、EMA 标注“备选·仅测试接入”；明确 RiskEngine 为“默认关闭的兜底路径”；配置表键名改为 `edge_dist_m`。

**Q-6 ｜ README “一键切换目标类别”对板端属过度宣称**
- 文件：`README.md:15`（“通过 `target_classes` 配置…一键切换危险目标聚集报警”）
- 问题：板端 `main.cc` 不使用 `argc/argv`（无命令行参数解析、无运行时切换入口），`target_classes` 仅在启动时从 JSON 加载，现场只能改文件重启。“一键切换”仅存在于 PC 实验工具（`scripts/crowd_test.py --classes`）。
- 影响：答辩演示时若按 README 措辞现场切换会被卡住。
- 修复建议：README 改为“修改 `target_classes` 后重启生效”；或给 main.cc 增加命令行参数覆盖（可选增强）。

**Q-7 ｜ README.en.md 特性清单与中文 README 不同步**
- 文件：`README.md:15,18`（多目标可切换、homography_file/IPM 标定）vs `README.en.md:15`（仅 “rule thresholds loaded from JSON”）
- 影响：违反导读中“README 双语强制同步”的工程纪律，英文读者看不到完整能力。
- 修复建议：同步 target_classes / class_conf_thresholds / homography_file 到英文版。

**Q-8 ｜ “等价 CMake 方式”不成立：ctest 覆盖远小于 `make test`**
- 文件：`README.md:158-159`（“等价 CMake 方式：cmake … ctest”）；`CMakeLists.txt:34-87`（仅 5 个测试 target：test_enhanced_decision / test_minijson / test_risk_config / test_hardware_modules / test_golden_regression）
- 问题：`make test` 覆盖 10 组（含 test-preprocess NEON 对照、test-selftest、test-lcd-menu、test-ili9341、test-xpt2046、test-sysfs-spi），ctest 全部缺失。
- 影响：用 CMake 的开发者得不到 README 承诺的同等覆盖，硬件驱动/预处理测试会被悄悄跳过。
- 修复建议：CMake 补齐全部 test_* target（含 preprocess），或把 README 措辞改为“CMake 方式覆盖核心算法测试子集”。

**Q-9 ｜ 硬件驱动模块无板端运行时消费方（除自检）**
- 文件：`src/hardware/*`（led/button/servo/pan_tilt/voice_alert/lcd_menu/ili9341/xpt2046/sysfs_spi）；`src/luckfox_rtsp_yolov5/src/main.cc`（未包含任何 hardware 头文件）；`src/hardware/selftest_main.cc`（唯一板端可执行产物）
- 问题：全部外设驱动只有主机测试与开机自检两个消费方；风险等级输出（SAFE/WARNING/DANGER）没有任何代码把它接到 LED 颜色/语音播报/菜单显示上。
- 影响：硬件“驱动就绪”但无“产品集成路径”，答辩现场即使接好外设也没有可运行程序驱动它们。
- 修复建议（架构层，可纳入 roadmap）：增加 `DEMO_MODE` 编译开关或独立 demo 二进制，将 EnhancedDecision 的 `CrowdLevel` 映射到 LED 三色、语音 aplay、触摸屏菜单；或至少在 roadmap 中显式标注“驱动已就绪，集成待硬件到货”。

---

## 四、结论

1. **代码架构本身健康**：配置三端一致、算法双端一致、依赖单向、测试覆盖真实，CI 门禁绿。上一轮遗留的 OR/AND、temporal、w4 未接入等问题均已闭环。
2. **构建系统两条文档化路径均有缺陷**（Q-1/Q-2），属“README/构建文档与代码不同步”的典型，建议本轮优先修复。
3. **硬件文档与 BOM v2 的冲突是架构层最重问题**（Q-3），涉及引脚、显示/语音方案、软件状态、端口，需文档侧统一口径。
4. **无 P0 问题**；P1 共 3 项，P2 共 6 项。

> 审查依据全部为只读操作（读文件、grep、pytest 只读测试）。未运行 C++ 主机测试（本机无 g++，CI 覆盖）；Windows 下 4 项 shell 测试跳过属平台限制，非缺陷。