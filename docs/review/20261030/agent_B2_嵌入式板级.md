# B2 嵌入式 Linux 板级专家独立评审报告（R4）

> 轮次：2026-10-30 独立评审（只读，未修改任何源码/板端文件）
> 评审域：`src/luckfox_rtsp_yolov5/` 构建配置、RKNN 调用链、PWM/SPI overlay、NEON 预处理、C++/Python 算法一致性、板端内存/日志/看门狗/systemd
> 审查方式：仓库源码逐行 + 实机 SSH 只读取证（192.168.0.136，板端状态、服务、配置、二进制字符串比对）
> 行号基准：当前工作区 HEAD（`05cf1b1`）

---

## 一、总体结论

**未发现新的 P0 级问题。** 代码层面整体质量高：上轮 P0/P1 大部分已修复（RKNN 返回值检查、`od_results` 清零、看门狗有限超时、FOV 限幅均在仓库代码中落地）；C++ 与 Python 风险 FSM 默认值**逐项一致**，无算法漂移。

本轮新发现 **3 个 P1 级问题**，均与**板端部署/真实硬件**相关，代码审查单独看不出来，必须结合实机状态才能发现：

1. **板端实际运行的是 10-27 构建的旧固件，不含已修复的 RKNN 返回值检查**——长稳数据（`results/edge_board/long_stability.jsonl`）与答辩演示都建立在未打补丁的二进制上，最新修复从未上机验证（P1）。
2. **硬件自检默认 PWM 通道 `-c 0` 与实机/文档（PWM8_M1/PWM9_M1 → pwmchip8/pwmchip9）矛盾**，自检 PWM 项必然 FAIL，会挡住 `start_demo.sh` 演示门禁（P1）。
3. **RTSP 输出红蓝互换**：绘制在 BGR 帧上的红色 DANGER 横幅，经 `cvtColor(BGR2RGB)` + RGB888 编码后变成蓝色（P1，演示画面直接可见）。

另有 6 个 P2 级新问题（systemd 单元未入库、板端配置与仓库漂移、双构建系统链接不一致、自检交叉编译目标缺失、看门狗无熔断、板端 load average 异常偏高）。

---

## 二、问题清单

### P1（必须修）

#### P1-1 板端运行固件陈旧，最新修复未部署、未上机验证【部署/验证】

- **证据**：
  - SSH 实机：`strings /home/pico/daima/luckfox_pico_rtsp_yolov5_demo/luckfox_pico_rtsp_yolov5 | grep -c 'inference_yolov5_model failed'` → **0**；`grep -c 'rknn_run fail'` → 1。即板端二进制**没有** `main.cc:384` 的 `inference_yolov5_model failed!` 分支字符串，说明 RKNN 返回值检查修复未包含在部署固件中。
  - 板端二进制 `md5sum = c85cffb79e9468b626ccff235d8762f2`，文件时间 10-27 03:49；仓库修复提交 `284de8d`（P0×6/P1×16/P2×7 全部修复）时间为 **2026-10-29 21:32**。
  - `results/edge_board/long_stability.jsonl` 全程（10-27 ~ 10-28）采集的也是该旧固件。
- **影响**：答辩/长稳验证跑的是未打补丁的旧代码，`od_results` 清零、RKNN 失败处理、看门狗等修复从未在真机验证；演示时 NPU 异常仍可能崩溃或画垃圾框（旧固件行为）。
- **修复建议**：用当前 HEAD 重新 `make luckfox` 交叉编译 → scp 部署 → 用 `scripts/board_health_snapshot.py` 重新采集 ≥24h 长稳，确认 `log_fail_count=0`、`svc_NRestarts` 稳定；在 `EDGE_TEST_REPORT.md` 记录新二进制的 md5/构建时间。
- **验证状态**：已通过 SSH 实机验证。

#### P1-2 硬件自检默认 PWM 通道为 0，与实机/文档（pwmchip8/pwmchip9）矛盾【硬件集成】

- **证据**：
  - `src/hardware/selftest_main.cc:10-11,43-44`：默认 `-c 0 -p 0`（注释“云台 PAN”）。
  - `docs/hardware/system_integration.md:66-67`：Yaw = `/sys/class/pwm/pwmchip8/pwm0`，Pitch = `pwmchip9/pwm0`；`tracking_servo.md:103-104`：PWM8_M1=Pin7、PWM9_M1=Pin6。
  - SSH 实机：`ls /sys/class/pwm/` 只有 `pwmchip8`、`pwmchip9`，**无 pwmchip0**。
  - 真机自检记录 `docs/hardware/selftest.md:118`：`./hardware_selftest` 默认参数下输出 `PWM=pwmchip0/pwm0` → `[FAIL] PWM_NODE PWM configure(20ms) 失败`。
- **影响**：按 `hardware-selftest.service`（无参数）部署后，开机自检 PWM 项必 FAIL → 不生成 `/tmp/hardware_selftest.pass` → `start_demo.sh` 拒绝进入演示模式（除非 `--force`）；且自检从未真正验证舵机 PWM 通道，硬件故障检测形同虚设。
- **修复建议**：默认值改为 `-c 8 -p 0`；`hardware-selftest.service` 的 `ExecStart` 显式传 `-c 8 -p 0`；同步修正 `selftest.h:25` 示例与 `selftest_main.cc` 头注释；`selftest.md` 更新预期结果。
- **验证状态**：已通过 SSH 实机验证。

#### P1-3 RTSP 输出红蓝互换（DANGER 红色横幅变蓝色）【显示正确性】

- **证据**：
  - `preprocess.cc:214-218`：`yuv420sp_to_bgr` 明确按 **BGR** 顺序写 `vst3_u8(bgr)`；`main.cc:271` 用 `CV_8UC3, data` 构造 `cv::Mat frame`（BGR 语义）。
  - `main.cc:468-488`：所有绘制（绿框、文本、顶部横幅）在 **BGR** 帧上进行。DANGER 横幅颜色 `cv::Scalar(0,0,255)`（BGR 语义 = 红色）。
  - `main.cc:518`：`cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB)` 原位转换。
  - `luckfox_mpi.cc:127`：VENC 声明 `enPixelFormat = RK_FMT_RGB888`，编码器按 RGB 解释缓冲。
  - 结论：BGR 红 `(0,0,255)` 转换后变成 RGB `(0,0,255)`（蓝色）；绿色 `(0,255,0)` 对称不变。**只有红/蓝互换**，绿框不受影响，所以代码审查容易漏掉，但 DANGER 横幅在 RTSP/网页上显示为蓝色。
- **影响**：答辩演示时“红色危险横幅”实际显示为蓝色，风险等级视觉语义错误，评委/用户直接可见。
- **修复建议**：把绘制移到 `cvtColor` 之后，并按 RGB 语义设置颜色（红色 `cv::Scalar(255,0,0)`），或在转换前完成绘制并改用 VENC 的 BGR 输入格式（若 SDK 支持）。建议加一个主机侧像素级回归测试（模拟 BGR 绘制→转换→断言输出色值）。
- **验证状态**：代码逻辑推演（VENC 输出为 RGB），实机 RTSP 截图可验证（板端无新截图，**未做像素级验证**，但逻辑链完整）。

### P2（建议修）

#### P2-1 看门狗关键 systemd 单元与脚本未纳入版本控制【可复现性】

- **证据**：SSH 实机存在 `/etc/systemd/system/pedestrian-risk.service`、`pedestrian-watchdog.service`、`/usr/local/bin/pedestrian-watchdog.sh`；仓库 grep `pedestrian-risk`/`pedestrian-watchdog` 仅命中 `README.md`、`board_health_snapshot.py`、`results/edge_board/README.md`，**没有任何 .service/.sh 源文件入库**（仓库只有 `scripts/hardware-selftest.service`）。
- **影响**：刷机/换板后整个“服务托管 + 看门狗自愈”架构无法从仓库复现，长稳能力丢失；毕业设计源码交付不完整。
- **修复建议**：将两个 service 单元和 `pedestrian-watchdog.sh` 收入 `scripts/`，在 `HARDWARE_BUILD_GUIDE.md` 增加部署步骤。

#### P2-2 板端部署的配置与仓库漂移（缺 `w4` 字段）【配置管理】

- **证据**：SSH 实机 `cat .../config/default_config.json`（1209B，10-26 19:55）中 `fsm` 只有 `w1/w2/w3/n_ref`，**没有 `w4`**；仓库 `config/default_config.json:8` 已含 `"w4": 0.1`（10-29 更新）。板端因 C++ 默认 `w4=0.1`（`risk_fsm.h:61`）才没有功能差异，纯属巧合。
- **影响**：若未来默认值或新字段变更，板端会静默使用旧配置；当前功能无差异，但配置“双端共读同一份”的承诺（`docs/导读.md:53`）在部署层面未落实。
- **修复建议**：`start_demo.sh` 增加部署时同步/校验配置步骤（如 `cmp` 后提示），或让程序启动时打印配置关键字段（`[CONFIG] frame_dt=0.1 w4=0.1 ...`），便于 `board_health_snapshot.py` 核对。

#### P2-3 Makefile 与 CMakeLists 交叉编译链接不一致【构建系统】

- **证据**：`Makefile:179` 链接 `-lrknnmrt -lrtsp_demo -lsample_comm -lopencv_core -lopencv_imgproc`；`CMakeLists.txt:162-164` 链接 `rknnmrt rtsp_demo rkaiq sample_comm opencv_core opencv_imgproc opencv_imgcodecs`。CMake 多了 `rkaiq` 和 `opencv_imgcodecs`。
- **影响**：两套构建在 SDK 上行为不一致，CMake 路径可能因缺少 `libopencv_imgcodecs`/`librkaiq` 而链接失败；且 Makefile 路径缺 `-lrkaiq`，若 SDK 的 `sample_comm` 是静态库则会链接失败。
- **修复建议**：统一为同一链接列表（以实际 SDK 为准）；并在 CI/构建脚本中固定只用一条路径（推荐 CMake）并加真机冒烟测试。

#### P2-4 交叉编译缺少显式 NEON 编译选项【构建系统】

- **证据**：`Makefile:167` 与 `CMakeLists.txt` 均未传 `-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7`。NEON 路径依赖 `__ARM_NEON__`（`preprocess.cc:562`），是否启用完全取决于 `arm-none-linux-gnueabihf-` 工具链默认 fpu（不同版本默认可能是 vfpv3-d16，则 NEON 被静默禁用、性能回退到标量）。
- **影响**：换工具链/换 SDK 后 NEON 加速可能静默失效，`letterbox` 从 ~26ms 退化到标量（每帧多 ~40ms+），FPS 明显下降而无人察觉。
- **修复建议**：Makefile/CMake 显式加 `-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7`；构建后检查二进制是否含 NEON 指令（`arm-linux-objdump -d | grep vst3`）或在日志打印 `[PREPROCESS] neon=on`。

#### P2-5 自检 CLI 无交叉编译目标【构建系统】

- **证据**：`Makefile:142-145` `hardware-selftest` 目标用 `$(CXX)`（默认主机 `g++`）；`docs/hardware/selftest.md:138` 写“交叉编译并部署：`make hardware-selftest` 产物拷到板子”，按文档操作得到的是 x86 二进制，板端无法运行。
- **影响**：部署链断裂；现场按文档操作会得到不可执行文件。
- **修复建议**：增加 `hardware-selftest-arm` 目标（使用 `$(CROSS_COMPILE)g++`），或在 Makefile 中根据 `CROSS_COMPILE` 自动切换；文档同步更新。

#### P2-6 看门狗无重启熔断，可能无限重启循环【可靠性】

- **证据**：`pedestrian-watchdog.sh` 每 15s 检查，日志 30s 未更新即 `systemctl restart pedestrian-risk`，脚本内**没有连续重启计数/熔断**；`pedestrian-risk.service` 设置了 `Restart=always + RestartSec=5 + StartLimitIntervalSec=0`（关闭 systemd 自身重启频率限制）。
- **影响**：若主程序因配置/资源问题启动即秒退，watchdog 与 systemd 会形成 5s+15s 的无限重启循环，日志刷屏、SD 卡寿命损耗，且无任何人工告警。
- **修复建议**：watchdog 增加连续重启计数（如 5 次/10 分钟内停止并 `logger` 告警）；或恢复 `StartLimitIntervalSec`/`StartLimitBurst`。

#### P2-7 板端 load average 长期 ~11（单核）异常偏高【资源/观察项】

- **证据**：`results/edge_board/long_stability.jsonl` 每行 `load average: 11.30 / 11.44 / 11.16 ...`；SSH 实机 `uptime` 仍为 `load average: 11.44, 11.19, 11.12`。主进程 RSS 仅 ~10MB、VSZ 130MB，单核上主循环 ~7.4FPS 至多贡献 load 1，剩余 ~10 的负载来源不明（swap 已用 ~38MB）。
- **影响**：板端整体过载可能引起偶发调度延迟/丢帧；答辩演示时风扇/发热/响应性风险。
- **修复建议**：`ssh` 排查 `top` 占用进程（怀疑系统内其他服务），答辩前关闭非必要进程；`board_health_snapshot.py` 增加 `top` 快照字段。

#### P2-8 上轮遗留未修复项复核（非本轮新发现）

以下为上轮（agent3）已报、本轮在仓库代码中**复核仍存在**的问题，建议一并排期：

| 严重度 | 位置 | 问题 | 复核证据 |
|---|---|---|---|
| P1（遗留） | `sysfs_spi.cc:59-73`、`xpt2046.h:126-133`、`ili9341.h:290-303` | SPI 命令/数据拆两次 `SPI_IOC_MESSAGE(1)`，CS 中间释放；真机触摸/读 ID 有风险 | 代码仍为 `SPI_IOC_MESSAGE(1)` 单消息 + 两次 transfer；板端 SPI0 已打通（工作日志 10-28），但无屏幕真机验证，**未验证** |
| P2 | `main.cc:270,358,543` | `RK_MPI_MB_Handle2VirAddr` 返回值未判空 | 复核仍无判空 |
| P2 | `yolov5.cc:226` | `rknn_run` 无超时，NPU 挂死时主循环阻塞 | 复核仍无超时 |
| P2 | `yolov5.cc:233` | `post_process` 返回值被忽略 | 复核仍被忽略 |
| P2 | `luckfox_mpi.cc:135` | `u32BufSize = width*height*3/2` 与 RGB888（需 3 倍）不匹配 | 复核仍为 `/2`；板端画面正常，**未验证**实际影响 |
| P2 | `luckfox_mpi.cc:115` | `u32BitRate = 10*1024`（10Kbps）且 `u32Gop=1` 全 I 帧，与注释“10Mbps”不符 | 复核仍为 10Kbps；注释（`luckfox_mpi.cc:108`）写“10Mbps”，代码 10Kbps，**未验证**实际码流 |
| P2 | `main.cc:421,465` | 每帧热路径 printf（决策摘要+每框一条） | 复核仍在；`long_stability.jsonl` 一天 `log_enhanced_count` 达 37 万行，日志膨胀 |
| P2 | `risk_config.h:113,120` | `static_cast<int>(asDouble())` 未校验整数 | 已加 0~79 范围校验，整数校验仍无 |

---

## 三、C++ 与 Python 算法一致性专项（risk_fsm.h vs risk_fsm.py）

### 3.1 FSM 默认值逐项对比 ✅ 一致

| 参数 | C++ `src/algorithms/risk_fsm.h:56-77` | Python `src/algorithms/risk_fsm.py:88-112` | 板端 JSON `config/default_config.json` | 结论 |
|---|---|---|---|---|
| w1/w2/w3 | 0.4 / 0.3 / 0.3 | 0.4 / 0.3 / 0.3 | 0.4 / 0.3 / 0.3 | ✅ |
| w4 | 0.1 | 0.1 | 0.1（板端旧配置缺失→走默认 0.1） | ✅（行为一致） |
| n_ref | 20 | 20 | 20 | ✅ |
| min_danger_n | 3 | 3 | —（默认 3） | ✅ |
| up_warning | 0.40 | 0.40 | 0.40 | ✅ |
| up_danger | 0.65 | 0.65 | 0.65 | ✅ |
| down_safe | 0.28 | 0.28 | 0.28 | ✅ |
| down_warning | 0.55 | 0.55 | 0.55 | ✅ |
| confirm_up/down | 3 / 5 | 3 / 5 | 3 / 5 | ✅ |
| danger_hold_frames/ms | 10 / 1000 | 10 / 1000 | 10 / 1000 | ✅ |
| trend_bias | 0.05 | 0.05 | 0.05 | ✅ |

### 3.2 评分/门控/状态机逻辑对比 ✅ 一致

- 评分公式：`w1*norm(n)+w2*density+w3*r_gcc+w4*(fiedler/(fiedler+1))*r_gcc` + `trend_bias*clamp(trend)` —— 两端逐项相同（`risk_fsm.h:143-164` vs `risk_fsm.py:220-248`）。
- 人数门控：`n < min_danger_n` 时 `score = min(score, up_danger - 0.01)` —— 两端一致（`risk_fsm.h:161-163` vs `risk_fsm.py:246-247`）。
- 滞回状态机：SAFE/WARNING/DANGER 转移条件、确认帧、DANGER hold（帧数与毫秒**同时满足才结束**）—— 两端一致（`risk_fsm.h:168-226` vs `risk_fsm.py:250-314`）。C++ 用 `now_ms >= 0` 表示“无时间戳”，与 Python `now_ms is not None` 等价。
- 差异仅剩实现细节（Python `trend` 用 truthiness、C++ 用 `!= 0.0`），数值等价，无行为漂移。

### 3.3 结论与提示

- **黄金测试 174/174 通过**，覆盖 FSM/图特征/EMA 的 C++ vs Python 确定性回归（`Makefile:67-73`），一致性有自动化保障。
- **提示（P2 观察）**：C++ 端 `EnhancedDecision` 的 Kalman 平滑使用 `cfg.frame_dt`（板端 JSON=0.1s，对应 ~7.4FPS 实测）；Python 端 `kalman_tracker.py:50` 默认 `dt=1/30`，二者参考帧率不同，但目前 Python 端没有对应的 `EnhancedDecision` 全链路实现，不存在直接对比漂移。若未来做端到端一致性脚本，需统一 `dt`。

---

## 四、板端实机取证摘要（2026-10-30 03:14，只读）

| 检查项 | 结果 |
|---|---|
| SSH 连通 | 192.168.0.136 可达（pico/luckfox） |
| 主服务 | `pedestrian-risk` active，`pedestrian-watchdog` active，`hardware-selftest` inactive（未启用） |
| 服务文件 | `/etc/systemd/system/pedestrian-risk.service`、`pedestrian-watchdog.service` 存在（**未入库**） |
| 工作目录 | `/home/pico/daima/luckfox_pico_rtsp_yolov5_demo/`，含 `config/default_config.json`（旧版，缺 w4）、大量 `before_*`/`.bak` 旧二进制 |
| PWM | `/sys/class/pwm/` 仅有 `pwmchip8`、`pwmchip9`（npwm=1），**无 pwmchip0** |
| 运行二进制 | `luckfox_pico_rtsp_yolov5` md5 `c85cffb7...`（10-27 03:49），**不含** `inference_yolov5_model failed` 修复字符串 |
| 内存 | 主进程 RSS ~9-13MB / VSZ 130MB；板端 Mem 211MB，可用 ~168MB；swap 已用 ~38MB |
| 负载 | `uptime` load average **11.44**（单核，异常偏高，来源待查） |
| 日志 | `/tmp/enhanced_latest.log` 持续追加 `[EnhancedDecision] level=SAFE ...`，帧率 ~7.4FPS |

---

## 五、未验证项汇总

| 项 | 说明 |
|---|---|
| SPI 事务 CS 语义（P1 遗留） | 板端 SPI0 已打通（`/dev/spidev0.0` + status=1），但无 ILI9341 屏幕实机，触摸/读 ID 的实际影响**未验证** |
| VENC `u32BufSize` / 码率参数 | 板端画面正常（RTSP 可看），但码流参数是否如预期**未验证** |
| 红蓝互换的实机像素级确认 | 逻辑链完整（BGR 绘制 → BGR2RGB → RGB888），但板端当前截图（10-26）非最新固件，**未做像素级验证** |
| 最新固件（含全部修复）在板端的 24h 长稳 | 板端运行的是旧固件，最新代码的长稳表现**未验证**（即 P1-1 的修复方向） |
| load average 11 的根因 | 未排查（只读取证未执行 top） |

---

## 六、修复优先级建议（给下一轮）

1. **立即**：重新交叉编译当前 HEAD 并部署（解决 P1-1）；把 `hardware-selftest` 默认/服务参数改为 `-c 8 -p 0`（解决 P1-2）；修正 BGR/RGB 绘制顺序（解决 P1-3）。
2. **本轮内**：把两个 systemd 单元 + watchdog 脚本入库（P2-1）；同步板端 config（P2-2）；统一 Makefile/CMake 链接与 NEON 编译选项（P2-3/P2-4）；补自检交叉编译目标（P2-5）。
3. **排期**：watchdog 熔断（P2-6）；排查 load average（P2-7）；处理上轮遗留 P1（SPI 事务）与 P2。