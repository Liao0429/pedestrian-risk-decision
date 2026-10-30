# C2 板级代码安全 交叉互评报告

> 角色：交叉互评专家 C2（板级代码安全）
> 日期：2026-10-30
> 对象：B6《代码安全与质量评审》 vs B2《嵌入式 Linux 板级专家独立评审（R4）》
> 方法：读取 CONTEXT.md 与两份报告；抽查 `postprocess.cc`、`main.cc`、`yolov5.cc`、`luckfox_mpi.cc`、`preprocess.cc`、`selftest_main.cc`、`Makefile`、`CMakeLists.txt`、`src/hardware/selftest.h`、`docs/hardware/*`、`docs/WEB_VIEWER.md`、`scripts/board_health_snapshot.py`、`config/default_config.json`、`results/edge_board/long_stability.jsonl`；`git log -L` 追溯 NMS 函数历史；`git ls-files` 核查 service 单元入库情况。
> 行号基准：当前工作区 HEAD `05cf1b1`（2026-10-30 01:45）。

---

## 一、总体结论

两份报告互补性强：**B6 强于代码静态安全（资源生命周期、内存/指针、失败路径），B2 强于实机部署状态（固件版本、硬件通道、板端配置）**。二者均正确判定**无 P0**。

但交叉验证发现两个必须向下一轮澄清的重要偏差：

1. **B6 P1-1（NMS 类别过滤下标）在当前 HEAD 已修复**——B6 引用的 `postprocess.cc:138/148` 代码状态是 `284de8d`（10-29 21:32）之前的旧版本；`284de8d` 已将该问题彻底修复，当前 HEAD 为 `classIds[order[i]]` / `classIds[m]`。B6 此条为**过时报**，应关闭，不得再作为待办缺陷。
2. **B2 P1-3（RTSP 红蓝互换）逻辑链有误，很可能误报**——`cv::cvtColor(BGR2RGB)` 是**真实数据变换**（逐像素交换 B/R 通道），不是仅改变语义标记。BGR 帧上红色 `(0,0,255)` 经转换后内存为 `(255,0,0)`，VENC 按 `RK_FMT_RGB888` 读取仍是红色。当前代码链路颜色正确。

真正需要下一轮优先处理的是 **B2 P1-1（板端固件陈旧未部署）** 与 **B2 P1-2（自检 PWM 默认通道 0）** 两个实机问题，以及 B6 报告的 **P1-2~P1-5 四个仍存在于当前 HEAD 的 C++ 板端缺陷**。

---

## 二、B6 报告逐条交叉验证

### B6 P1-1：NMS 类别过滤用错下标 —— ❌ 不成立（已修复，过时报）

| 项 | 内容 |
|---|---|
| B6 证据 | `postprocess.cc:138` `classIds[i]`、`:148` `classIds[j]` |
| 当前 HEAD 实际代码 | `postprocess.cc:141` `if (order[i] == -1 \|\| classIds[order[i]] != filterId)`；`:150` `if (m == -1 \|\| classIds[m] != filterId)` |
| git 历史 | `284de8d`（2026-10-29 21:32，"P0×6/P1×16/P2×7 全部修复实施完成"）diff 明确将外层 `classIds[i]`→`classIds[order[i]]`、内层 `classIds[j]`→`classIds[m]`，并更新注释"类别过滤必须用原始索引 classIds[order[i]]" |
| 判定 | **交叉确认：B6 引用的是 284de8d 之前的代码状态（a92e4a5 部分修复后的中间态：外层仍 `classIds[i]`、内层错用 `classIds[j]`），与当前 HEAD 矛盾**。修复方向与 B6 建议完全一致（`classIds[order[i]]`/`classIds[m]`），说明该缺陷确曾存在且已按正确方案落地。B6 报告日期 10-30，却基于旧代码审查，属评审基准错误。该条**关闭**，无需再修；建议补充"person 与 vehicle 重叠"回归用例以固化修复。 |

### B6 P1-2：退出路径对 SDK 内部指针 free() —— ✅ 交叉确认（代码存在；SDK 行为为静态推断）

- 当前 HEAD 确认：`main.cc:155` `memset(&stFrame,0,...)`、`:156` `stFrame.pstPack = malloc(sizeof(VENC_PACK_S))`、`:170` `frame_pack_ready = (stFrame.pstPack != NULL)`、`:188` `if (frame_pack_ready) free(stFrame.pstPack)`、`:538` `RK_MPI_VENC_GetStream(0, &stFrame, ...)`。
- 判断：rockit 的 `VENC_GetStream` 会填充 `pstStream->pstPack` 指向编码通道内部缓冲（Luckfox 官方示例从不预分配该指针），因此 `:156` 的 malloc 在首次 GetStream 后被覆写（一次性泄漏），`:188` 的 `free` 释放 SDK 内部指针（无效 free → 堆损坏/ABRT 风险）。**该行为属 rockit 实现细节，B6 已标注"静态分析结论，需板端 kill -TERM 验证"**。
- 判定：**交叉确认（代码层成立，SDK 行为层未板端实测）**。修复建议（删除 malloc 与 free 分支）低风险且必然正确，应执行。

### B6 P1-3：VIDEO_FRAME_INFO_S 部分初始化 —— ✅ 交叉确认

- 当前 HEAD 确认：`main.cc:262-271` 声明 `VIDEO_FRAME_INFO_S h264_frame;` 后仅赋值 `u32Width/u32Height/u32VirWidth/u32VirHeight/enPixelFormat/u32FrameFlag/pMbBlk` 7 个字段，**无 memset**；`main.cc:521` 每帧传给 `RK_MPI_VENC_SendFrame`。C++ 局部结构体部分初始化，其余字段为栈上垃圾值，SDK 读取即 UB。
- 判定：**交叉确认（代码层成立）**。实际影响依赖 SDK 是否读取未初始化字段（B6 已标注板端实测正常、SDK 升级有风险）。修复（memset）零风险，应执行。

### B6 P1-4：RKNN 零拷贝内存/属性 malloc 返回值未检查 —— ✅ 交叉确认

- 当前 HEAD 确认：`yolov5.cc:119`、`:130` `rknn_create_mem` 返回值未判空；`:153`、`:155` `malloc` 后立即 `memcpy`（OOM 即写 NULL）；下游解引用 `main.cc:376` `input_mems[0]->virt_addr`、`postprocess.cc:447/454/459` `_outputs[i]->virt_addr/.buf` 均未判空。
- 判定：**交叉确认（代码层成立）**。OOM 场景概率低但后果为 SIGSEGV。应判空并统一失败处理。

### B6 P1-5：RK_MPI_MB_Handle2VirAddr 未判空 —— ✅ 交叉确认（与 B2 P2-8 为同一问题，分级冲突）

- 当前 HEAD 确认：`main.cc:270`（cv::Mat 包装 `data`）、`:358`（`vi_data` → `yuv420sp_to_bgr`/`yuv420sp_to_rgb_letterbox`）、`:543`（`pData` → `rtsp_tx_video`）三处均未判空。
- 判定：**交叉确认**。B6 定级 P1（崩溃风险），B2 在 P2-8 遗留复核中定级 P2（实机正常路径不触发）。二者是同一问题，分级冲突详见第三节。从安全视角按 P1 处理（加判空成本极低），可接受。

### B6 P1-6 / P1-7 / P1-8：安全外围（go2rtc 无认证 / SSH 默认凭据 / 文档明文凭据）—— ✅ 证据抽查确认

- `docs/WEB_VIEWER.md:31-33` `api: listen: ":1984"`、`:47-49` `curl http://127.0.0.1:1984/api/streams` 验证无鉴权 —— 确认。
- `scripts/board_health_snapshot.py:38-41` `HOST/USER/PASS` 默认值 `192.168.0.136/pico/luckfox`、`:176` `paramiko.AutoAddPolicy()` —— 确认。
- `CONTEXT.md:13` 明文 SSH 凭据 `192.168.0.63（liao/123456）` —— 确认。
- 判定：**交叉确认（证据均可在仓库复现）**。属部署/文档外围，B2 未涉及（视角差异，见遗漏分析）。

### B6 P2 抽查

- P2-3 `loadLabelName` 恒返回 0：确认（`postprocess.cc:112-117` 忽略 `readLines` 返回值）。✅
- P2-5 硬编码三输出尺度：确认（`postprocess.cc:438` `for (int i = 0; i < 3; i++)`，未用 `io_num.n_output`）。✅
- P2-2 初始化失败路径未 `rknn_destroy`：确认（`yolov5.cc:64-67/87-90/105-108/123-126/132-135` 直接 `return -1`）。✅
- P2-9 注释/调试残留、P2-1 `system()`、P2-7 `voice_alert.cc` shell 拼接、P2-8 SIGPIPE：与 B6 描述一致，未发现矛盾。

---

## 三、B2 报告逐条交叉验证

### B2 P1-1：板端固件陈旧，最新修复未部署 —— ✅ 交叉确认（实机侧无法直接复核，仓库侧证据链完整）

- 仓库侧可复核证据：
  - `main.cc:384` 确实存在修复字符串 `inference_yolov5_model failed!`（B2 用 `strings 板端二进制 | grep -c` 该字符串为 0 来判定旧固件，方法合理）。
  - `results/edge_board/long_stability.jsonl` 首行 `svc_ExecMainStartTimestamp: Tue 2026-10-27 03:49:24 CST`，与 B2 所述板端二进制时间 10-27 03:49 吻合；该日志全程为旧固件采集，与 B2 描述一致。
  - 修复提交 `284de8d` 时间 2026-10-29 21:32，晚于板端二进制 2 天，B2 的"部署滞后"结论时间线自洽。
- 判定：**交叉确认**。实机 SSH 取证（md5、strings）无法由本机复核，但仓库侧证据全部支持。这是当前项目**最需要立即行动**的一条：答辩演示与长稳数据均建立在未打补丁固件上。

### B2 P1-2：自检默认 PWM 通道 -c 0 与实机矛盾 —— ✅ 交叉确认

- 代码：`selftest_main.cc:10-11`（头注释 `-c 0 PWM chip（云台 PAN）`）、`:43-44`（`pwmChip = 0; pwmChan = 0;`）——确认。
- 文档：`docs/hardware/system_integration.md:66-67` 明确 `Yaw → /sys/class/pwm/pwmchip8/pwm0`、`Pitch → pwmchip9/pwm0`；`docs/hardware/tracking_servo.md:103-104` `PWM8_M1=Pin7`、`PWM9_M1=Pin6`——确认。
- 实机自检记录：`docs/hardware/selftest.md:118` 记录默认参数输出 `PWM=pwmchip0/pwm0` → `[FAIL] PWM_NODE PWM configure(20ms) 失败`——确认。
- 实机 `/sys/class/pwm` 仅有 `pwmchip8/pwmchip9`（B2 SSH 取证）本机无法复核，但代码+文档+既有自检记录三方一致支持。
- 判定：**交叉确认**。默认通道应改为 `-c 8 -p 0`，并同步 `hardware-selftest.service` 参数与 `src/hardware/selftest.h` 示例。

### B2 P1-3：RTSP 输出红蓝互换 —— ⚠️ 存疑（很可能误报）

- B2 证据链：`preprocess.cc:214-218` BGR 写入（确认）→ `main.cc:271` CV_8UC3 BGR Mat（确认）→ `main.cc:468-488` BGR 帧上绘制（确认）→ `main.cc:518` `cvtColor(BGR2RGB)`（确认存在）→ `luckfox_mpi.cc:127` / `main.cc:267` VENC `RK_FMT_RGB888`（确认）。
- **关键分歧**：B2 认为"BGR 红 `(0,0,255)` 转换后变成 RGB `(0,0,255)`（蓝色）"。这是对 OpenCV 的误解——`cv::cvtColor(src,dst,COLOR_BGR2RGB)` 是**逐像素数据变换**（交换 R/B 通道值），而非仅改变解释语义。
  - 实际链路：BGR 帧上红色横幅写入内存 `[0,0,255]`（B=0,G=0,R=255）→ cvtColor 交换 → 内存 `[255,0,0]`（R=255,G=0,B=0）→ VENC 按 RGB888 读 `[255,0,0]` = 红色 → H.264/RTSP 显示**红色**。颜色正确。
  - 绿色 `(0,255,0)` 在 BGR/RGB 下对称不变，故"只有红蓝互换、绿框不受影响"的直觉恰好解释了为何若没有 cvtColor 才会出问题；而 cvtColor 已存在。
- B2 自己也标注"未做像素级验证"。`main.cc:515-517` 注释表明作者已针对该问题加入 cvtColor 修复。
- 判定：**存疑（逻辑链有误，大概率误报）**。不建议按 P1 阻塞；建议降为 **P2 验证项**：在主机侧加一个像素级回归测试（BGR 绘制红色 → cvtColor → 断言输出 `(255,0,0)`），一锤定音后关闭。

### B2 P2 系列交叉验证

| 条目 | 验证 | 结论 |
|---|---|---|
| P2-1 systemd 单元未入库 | `git ls-files` 仅 `scripts/hardware-selftest.service`，无 `pedestrian-risk.service`/`pedestrian-watchdog.service`/`pedestrian-watchdog.sh` | ✅ 交叉确认 |
| P2-2 板端配置缺 w4 | 仓库 `config/default_config.json:8` 含 `"w4": 0.1`；板端 JSON 缺 w4 为 SSH 取证，本机无法复核 | ✅ 仓库侧确认，实机侧逻辑自洽 |
| P2-3 Makefile/CMake 链接不一致 | `Makefile:179` 链 `-lrknnmrt -lrtsp_demo -lsample_comm -lopencv_core -lopencv_imgproc`；`CMakeLists.txt:162-164` 多 `rkaiq`、`opencv_imgcodecs` | ✅ 交叉确认 |
| P2-4 无显式 NEON 编译选项 | `Makefile` luckfox 目标仅 `-std=c++11 -O2`，无 `-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7` | ✅ 交叉确认 |
| P2-5 自检无交叉编译目标 | `Makefile:142-145` `hardware-selftest` 用 `$(CXX)`（主机 g++） | ✅ 交叉确认 |
| P2-6 看门狗无熔断 | 脚本在板端未入库，本机无法复核；逻辑合理 | ⚠️ 存疑（无法复核，逻辑自洽） |
| P2-7 load average ~11 | `long_stability.jsonl` 多行 `load average: 11.30/11.44/11.20`，RSS ~10MB、swap 38MB | ✅ 交叉确认 |
| P2-8 遗留 SPI 事务 | 未抽查 `sysfs_spi.cc` 细节 | ⚠️ 未复核（B2 自标未验证） |
| P2-8 遗留 `luckfox_mpi.cc:135` u32BufSize=3/2 | 确认 `u32BufSize = width * height * 3 / 2`（YUV420SP 大小）而像素格式为 RGB888（需 3 倍） | ✅ 交叉确认 |
| P2-8 遗留码率 10Kbps vs 注释 10Mbps | 确认 `luckfox_mpi.cc:115` `u32BitRate = 10 * 1024`，注释 `:111` 写 10Mbps | ✅ 交叉确认 |
| P2-8 遗留热路径 printf | 确认 `main.cc:465-466` 每框 printf、`:421` 每帧决策摘要 | ✅ 交叉确认（与 B6 P2-9 一致） |
| P2-8 遗留 `risk_config.h` 整数校验 | 未抽查 | ⚠️ 未复核 |

---

## 四、一致点 / 冲突点 / 遗漏

### 4.1 一致点

1. **无 P0**：两份报告均判定正常运行路径无答辩阻断级缺陷。
2. **RKNN 返回值检查修复已落地**：B6 确认 `yolov5.cc` 已 `memset(od_results)` + 检查 `rknn_run` 返回值；B2 确认上轮 P0/P1 大部分修复在仓库代码中可见。
3. **Handle2VirAddr 未判空**：B6 P1-5 与 B2 P2-8 复核指向同一代码（`main.cc:270/358/543`），结论一致。
4. **热路径 printf**：B6 P2-9 与 B2 P2-8 均指出 `main.cc:465` 附近每帧 printf，结论一致。
5. **C++/Python 算法一致性**：B2 做了 FSM 默认值逐项对比（一致）；B6 认可 `risk_fsm.h` 与 Python 端逐项对齐。两者互相印证。

### 4.2 冲突点

| # | 冲突 | 说明 | C2 裁定 |
|---|---|---|---|
| 1 | **B6 P1-1 vs 当前 HEAD** | B6 报 NMS 下标错误，当前代码已修复（`284de8d`） | **B6 过时报**，问题已关闭；B6 需更新报告或标注评审基准 |
| 2 | **B2 P1-3 红蓝互换** | B2 认为 cvtColor 后红色变蓝；实际 cvtColor 交换数据，颜色正确 | **B2 大概率误报**，降级为 P2 像素验证项 |
| 3 | **Handle2VirAddr 分级** | B6 定 P1（崩溃风险），B2 定 P2（实机不触发） | 合并为同一问题，按 **P1 处理**（判空成本极低，安全视角从严） |

### 4.3 遗漏

**B6 未覆盖（B2 独有，且 B6 声称深走查过 main.cc 与 6 个 hardware 文件）：**
- 板端固件陈旧未部署（B2 P1-1）——B6 完全未做实机/部署状态核查。
- 自检 PWM 默认通道错误（B2 P1-2）——纯代码可查（`selftest_main.cc:43-44`），但需要硬件通道知识才能定性，B6 未发现。
- systemd 单元未入库、板端配置漂移、构建系统不一致、NEON 选项、自检交叉编译目标、看门狗熔断、load average（B2 P2-1~P2-7）。
- `luckfox_mpi.cc` 的 `u32BufSize=3/2` 与码率 10Kbps 两个遗留项——B6 深走查 luckfox_mpi.cc 却未报告。

**B2 未覆盖（B6 独有）：**
- 退出路径 free SDK 指针（B6 P1-2）、VIDEO_FRAME_INFO_S 部分初始化（B6 P1-3）、rknn_create_mem 未判空（B6 P1-4）——B2 的 P2-8 遗留复核表覆盖了 rknn_run 超时/post_process 返回值，却漏了同文件的 rknn_create_mem 判空。
- go2rtc 无认证、SSH 默认凭据、文档明文凭据（B6 P1-6/7/8）——B2 视角未覆盖部署安全。
- B6 的多数 P2 代码安全项（system()、RKNN 上下文泄漏、loadLabelName、快排、硬编码输出尺度、SIGPIPE、voice_alert shell 拼接、Python 端问题）。

**共同盲区：** 两份报告均未对"RTSP 颜色正确性"做像素级验证（B2 提出但未做，B6 未涉及），导致红蓝互换争议只能靠推理；建议下一轮用主机侧像素回归测试闭环。

---

## 五、合并问题清单（含修复优先级）

> 状态说明：✅=交叉确认；⚠️=存疑/未验证；❌=已修复/误报（无需处理）

| # | 优先级 | 问题 | 来源 | 状态 | 位置 | 修复动作 |
|---|---|---|---|---|---|---|
| 1 | **P0 紧急** | 无 | — | ✅ 双方一致 | — | — |
| 2 | **P1-最高** | 板端固件陈旧，修复未部署、长稳数据基于旧固件 | B2-P1-1 | ✅ 交叉确认 | 板端二进制 vs HEAD | 重新 `make luckfox` 交叉编译 → 部署 → 重新采集 ≥24h 长稳 → 记录新 md5 |
| 3 | **P1-高** | 退出路径 free SDK 内部指针（无效 free/堆破坏） | B6-P1-2 | ✅ 交叉确认（SDK 行为待板端验证） | `main.cc:156,170,188` | 删除 malloc 与 free 分支，`frame_pack_ready` 不再基于 pstPack |
| 4 | **P1-高** | 自检 PWM 默认 `-c 0 -p 0`，实机为 pwmchip8/9，自检必 FAIL 挡演示门禁 | B2-P1-2 | ✅ 交叉确认 | `selftest_main.cc:10-11,43-44`、`selftest.h`、service 单元 | 默认改 `-c 8 -p 0`，service ExecStart 显式传参，同步文档 |
| 5 | **P1-中高** | VIDEO_FRAME_INFO_S 部分初始化（UB） | B6-P1-3 | ✅ 交叉确认 | `main.cc:262-271` | `memset(&h264_frame,0,...)` 后再赋值 |
| 6 | **P1-中高** | rknn_create_mem / malloc 未判空（OOM 空指针解引用） | B6-P1-4 | ✅ 交叉确认 | `yolov5.cc:119,130,153-156` | 判空 + 统一失败处理；`:153/155` 改 calloc/vector |
| 7 | **P1-中** | Handle2VirAddr 三处未判空（SIGSEGV） | B6-P1-5 = B2-P2-8 | ✅ 交叉确认（分级冲突：B6=P1/B2=P2） | `main.cc:270,358,543` | 三处判空，失败跳过本帧并走看门狗计数 |
| 8 | **P1-中** | go2rtc 监控 Web 无认证（局域网可看/API 滥用） | B6-P1-6 | ✅ 证据确认 | `docs/WEB_VIEWER.md:31-33` | `api: {listen:"127.0.0.1:1984", auth:...}` + 文档安全段 |
| 9 | **P1-中** | SSH 运维脚本默认凭据 + AutoAddPolicy | B6-P1-7 | ✅ 证据确认 | `scripts/board_health_snapshot.py:38-41,176` | 默认密码删除改必填；主机密钥校验 |
| 10 | **P1-低** | 评审文档明文 SSH 凭据 | B6-P1-8 | ✅ 证据确认 | `CONTEXT.md:13` | 替换占位符 + grep 防复发 |
| 11 | **P2** | RTSP 红蓝互换（**很可能误报**） | B2-P1-3 | ⚠️ 存疑（建议降 P2） | `main.cc:518` + `luckfox_mpi.cc:127` | 加主机侧像素回归测试断言红色输出；通过后关闭 |
| 12 | **P2** | systemd 单元/看门狗脚本未入库 | B2-P2-1 | ✅ 交叉确认 | `git ls-files` 仅 hardware-selftest.service | 两个 service + watchdog.sh 收入 `scripts/` |
| 13 | **P2** | 板端配置缺 w4（部署漂移） | B2-P2-2 | ✅ 仓库侧确认 | `config/default_config.json:8` vs 板端 | 部署时同步/校验配置 |
| 14 | **P2** | Makefile/CMake 链接不一致 | B2-P2-3 | ✅ 交叉确认 | `Makefile:179` vs `CMakeLists.txt:162-164` | 统一链接列表，固定单一构建路径 |
| 15 | **P2** | 缺显式 NEON 编译选项 | B2-P2-4 | ✅ 交叉确认 | `Makefile` luckfox 目标 | 加 `-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7` |
| 16 | **P2** | 自检 CLI 无交叉编译目标 | B2-P2-5 | ✅ 交叉确认 | `Makefile:142-145` | 加 `hardware-selftest-arm` 目标 |
| 17 | **P2** | 看门狗无重启熔断 | B2-P2-6 | ⚠️ 无法复核（脚本未入库） | 板端 `pedestrian-watchdog.sh` | 加连续重启计数/熔断 |
| 18 | **P2** | 板端 load average ~11 异常 | B2-P2-7 | ✅ 交叉确认 | `long_stability.jsonl` | 排查 top 占用进程 |
| 19 | **P2** | SPI 事务 CS 中途释放（遗留 P1） | B2-P2-8 | ⚠️ 未复核（B2 自标未验证） | `sysfs_spi.cc:59-73` 等 | 真机验证或改单消息事务 |
| 20 | **P2** | VENC u32BufSize=3/2 与 RGB888 不匹配 | B2-P2-8 | ✅ 交叉确认 | `luckfox_mpi.cc:135` | 按 RGB888 改 3 倍（或确认 SDK 容忍） |
| 21 | **P2** | VENC 码率 10Kbps 与注释 10Mbps 不符 | B2-P2-8 | ✅ 交叉确认 | `luckfox_mpi.cc:115` | 按实际需求修正 |
| 22 | **P2** | rknn_run 无超时 / post_process 返回值被忽略 | B2-P2-8 | ⚠️ 未逐条复核，B2 已复核 | `yolov5.cc:226,233` | 加超时/检查返回值 |
| 23 | **P2** | 每帧热路径 printf 刷屏 | B6-P2-9 = B2-P2-8 | ✅ 交叉确认 | `main.cc:421,465` | 节流/移入调试开关 |
| 24 | **P2** | system() 不检查返回值 | B6-P2-1 | ✅ 未发现矛盾 | `main.cc:108` | 改 execl + 检查 |
| 25 | **P2** | 初始化失败路径泄漏 RKNN 上下文 | B6-P2-2 | ✅ 交叉确认 | `yolov5.cc:64-135` | 失败分支 rknn_destroy |
| 26 | **P2** | loadLabelName 恒返回 0 | B6-P2-3 | ✅ 交叉确认 | `postprocess.cc:112-117` | 检查 readLines 返回值 |
| 27 | **P2** | 快排最坏情况 O(n²)/深递归 | B6-P2-4 | ✅ 未发现矛盾 | `postprocess.cc:176-207` | 三数取中/非递归 |
| 28 | **P2** | 硬编码 3 输出尺度 | B6-P2-5 | ✅ 交叉确认 | `postprocess.cc:438` | 用 `io_num.n_output` |
| 29 | **P2** | 配置回退静默 / shell 拼接 / SIGPIPE / 注释残留 | B6-P2-6/7/8/9 | ✅ 抽查一致 | 多处 | 见 B6 报告 |
| 30 | **P2** | Python 端脚本问题（静默吞异常、README 表述等） | B6-P2-10~14 | ✅ 未发现矛盾 | `scripts/`、`README.md` | 见 B6 报告 |

---

## 六、给下一轮的建议

1. **更新 B6 报告**：P1-1 基于旧代码，需标注已修复或重新基于 HEAD 出具；避免缺陷清单误导排期。
2. **立即执行**：重新交叉编译当前 HEAD 部署到板端（B2 P1-1），替换 10-27 旧固件；部署后重采 24h 长稳。
3. **像素级闭环红蓝争议**：主机侧写一个 BGR 绘制 → `cvtColor(BGR2RGB)` → 断言像素值的回归测试，直接判定 B2 P1-3 是否成立，避免继续靠推理。
4. **一次性修复板端 5 个内存/初始化缺陷**（合并清单 #3/#5/#6/#7）：均为低风险高收益改动，建议下一轮随固件部署一起完成并复测。
5. **构建链统一**（#14/#15/#16）：固定 CMake 路径 + NEON 选项 + 自检交叉编译目标，避免演示现场用文档命令得到 x86 产物。
6. **部署资产入库**（#12/#13）：systemd 单元、watchdog 脚本、板端配置同步机制纳入版本控制。

---

## 七、验证痕迹

- `git log --oneline -8 -- postprocess.cc`：`284de8d`（全部修复）、`a92e4a5`（部分修复）、`efeb52b`（骨架）。
- `git log -L 131,173:postprocess.cc`：`284de8d` diff 显示 `classIds[i]→classIds[order[i]]`、`classIds[j]→classIds[m]`。
- `git ls-files | grep -iE 'pedestrian-(risk|watchdog)|watchdog\.sh|\.service'`：仅 `scripts/hardware-selftest.service`。
- `sed` 抽查：`Makefile:142-145,179`、`CMakeLists.txt:162-164`、`selftest_main.cc:10-11,43-44`、`luckfox_mpi.cc:111-135`、`preprocess.cc:214-219`、`config/default_config.json:8`、`WEB_VIEWER.md:31-49`、`board_health_snapshot.py:38-41,176`。
- `results/edge_board/long_stability.jsonl`：load average 11.2~11.4、svc 启动时间 10-27 03:49、RSS ~10MB。

*报告结束。*