# 2026-10-30 Round3 多 Agent 交叉审计报告

> 报告范围：仓库源码 + 真机（Luckfox Pico Max / RV1106）+ 构建机 + PCB Gerber/3D 外壳（只读）
> 组织方式：1 名 Lead + 5 名专业 Agent 并行，写 scope 互斥，Lead 独立复跑全部测试与真机测量
> 详细过程报告留在本地 `docs/review/20261030_round3/`（按仓库隐私规则不入库），本文件是入库的技术结论汇总

---

## 1. 环境与真机

| 角色 | 地址/路径 | 说明 |
|---|---|---|
| 开发板 | `192.168.0.136` pico（口令走 BOARD_PASS 环境变量） | armv7l 5.10.160，211MB RAM，systemd 托管 `pedestrian-risk.service` + 看门狗 + go2rtc |
| 构建机 | `192.168.0.63` liao | Ubuntu 22.04，12 核，`~/board_sdk`/`~/luckfox_sdk` 完整 SDK + arm-linux-gnueabihf 交叉链 |
| 仓库 | `G:\PedestrianRiskDecision` | exFAT（注意：node/js 的原子写/硬链接式新建会失败，用 `fs.writeFileSync` 或 base64 落盘） |
| Host 工具链 | `G:\DevTools\mingw\w64devkit\bin` | g++ 16.2 / make（Windows 主机单测） |
| Host Python | `X:\py311_gpu\Scripts\python.exe` | 3.11.17 + numpy 2.4.6 + pytest 9.1.1 + cv2 5.0.0 + paramiko 5.1.0 |

---

## 2. Lead 独立复测结果（全部在真机/磁盘上复跑）

| 项 | 结果 | 复现命令 |
|---|---|---|
| Python 测试 | **313 passed / 4 skipped** | `python -m pytest tests/ -q` |
| C++ 主机单测 | 40/32/58/104/174/118/80/99/123/98，**全部 0 失败** | `cd src/luckfox_rtsp_yolov5 && mingw32-make test` |
| 板端 RTSP 帧率 | **7.63 FPS**（30s 抓包 229 帧） | `ffmpeg -rtsp_transport tcp -i rtsp://192.168.0.136:554/live/0 -c copy -t 30 x.mkv` + `ffprobe -count_frames` |
| 板端 `[PERF]` 日志 | `fps=7.52~7.74`（与抓包一致） | `tail /tmp/enhanced_latest.log`（注意：ptys 下 stdout 被 line buffer 到 8 帧才 flush） |
| 服务状态 | `NRestarts=0`、观察期无新 core、RTSP :554 正常、RSS 13.8MiB | `systemctl show pedestrian-risk -p NRestarts` |
| 修复前现场 | **38 个 core dump（546MB）**、根分区 80%、进程长期 D 状态、load > 11 | `ls core.*`、`top` |

**帧率瓶颈结论**：NPU 推理 `infer=91.5ms`（71%），预处理 `prep=31.0ms`（24%），其余合计 <8%。
"谱特征 Jacobi 每帧是瓶颈"这一先验**被推翻**（`decide` 仅 0.02ms / 0.03%）。

---

## 3. 板端 C++ 流水线（P0/P1 修复）

| # | 问题 | 证据 | 修复 |
|---|---|---|---|
| 1 | VENC 只 malloc 1 个 pack，只发 `pstPack[0]` | 旧 `stFrame` 单包槽；`u32PackCount>1` 越界写 → core / 画面撕裂 | `stPacks[8]` + 全 pack 发送 |
| 2 | 每帧每框 `fprintf(stderr)/printf` | 管道写满即阻塞 → **"卡死"直接诱因** | `BOARD_VERBOSE_DETECT` 编译开关 + 1Hz `[PERF]` |
| 3 | `RK_MPI_*` `timeout=-1` + 返回值不检查 | VI/VENC 三处 | VI 200ms / VENC 100ms + 连续失败 50 次退出交 systemd |
| 4 | `conf_threshold` 无解析分支（调参空操作） | 全仓库唯一引用是 JSON 自身 | `include/detect_thresholds.h` + risk_config 解析 + 运行时传入 |
| 5 | `BOX/NMS_THRESH` 来自 SDK 且两套 SDK 漂移 | board_sdk 0.35/0.45 vs luckfox_sdk 0.25/0.45 | 阈值入库为显式常量 |
| 6 | 干净仓库板端**编不过** | `yolov5.cc` 2 参 vs SDK 3 参；Makefile 缺 `-I .../rknn`、缺 `-DRV1106_1103`、链接库不全 | Makefile/CMake 按 board_sdk 实测 flags 重写 |
| 7 | `bytetrack_tracker.h` 编译不过且从未被编译 | `xyahToBox(std::array<double,4>&)` 收到 8 维 `kf.mean()` | 形参改 8 维 + parity 契约测试 |
| 8 | NV21 别名当 NV12、BGR 字节喂 RGB 模型 | `COLOR_YUV420sp2BGR` + 直写 RKNN 输入 | 单遍零拷贝 `yuv420sp_to_rgb_letterbox` |
| 9 | 同地址自 `memcpy` 冒充缓存同步 | frame 就包在 data 之上 | 删除 + 补 `RK_MPI_SYS_MmzFlushCache` |
| 10 | 每帧 `stat()` 配置热重载 | 旧实现每帧一次 | 每 300 帧一次 |

结论：预处理改造对帧率**中性**（31.2ms 新 vs 31.0ms 旧），收益在正确性与去掉 1.2MB memcpy + 1MB 自拷贝、二进制小 0.8MB。

---

### 3.1 T6 后续发现：颜色通道回归（task-1 修复引入，T6 定位并修掉）

- 现场：抓帧逐像素读数发现 **整帧 R/B 互换**（画黄显青、画红显蓝），ROI 与 OSD 叠加层同样被颠倒。
- 根因：帧缓冲是 OpenCV **BGR**，而 VENC 配的是 `RK_FMT_RGB888`。旧二进制没暴露，是因为旧链路有**两个互相抵消的 swap**（`COLOR_YUV420sp2BGR` 把 NV21 当 NV12 + VENC 格式不匹配）；task-1 修掉 NV21 别名错误后，只剩 VENC 这一个 swap。
- 修复：`luckfox_mpi.cc` `RK_FMT_RGB888 → RK_FMT_BGR888`（`board_sdk/include/rk_comm_video.h:156` 确认该枚举存在）。
- 状态：**已修 + 交叉编译通过（md5 `d19f88f2c921d05307c1e62d05178288`），但最后一次上板终验未完成**（见 §8 板卡不稳定）。

> 这条是"修复引入新问题"的典型，也是本次交叉验证最有价值的一次互查：单看代码或单看旧画面都发现不了，只有**修复后重新抓帧读数**才暴露。

---

### 3.2 「板子老是卡死」的完整机制链（Round 2 新增，板端实证）

| 环节 | 证据 |
|---|---|
| 1 旧代码在 VENC 取流路径崩 | 38 个 core 全部落在 2026-10-30 10:37~11:08；core.1235 gdb：SIGSEGV / SEGV_MAPERR / si_addr=0x0，指令 str r0,[r8] 且 r8=0，函数 RKVencChannel::getStreamInfo+136（旧代码只分配 1 个 pack 槽、只发 pstPack[0]） |
| 2 崩溃进入重启风暴 | 崩溃时**整栈重建**（VI/ISP/VENC/RKNN）；Restart=on-failure + RestartSec=5 |
| 3 30s 内第 6 次启动被 systemd 拒绝 | 构建机受控复刻：Start request repeated too quickly，NRestarts 冻结、ActiveState=failed、is-active rc=3 → **演示永久停摆** |
| 4 整机被拖到 sshd 都握不上手 | 2026-10-31 00:05:32 后 **91 分钟 journal 零写入**（journalctl --list-boots 原文：boot -2 止于 00:05:32，下一条到 01:36:34）；02:05 再次同型：**ping 通、554/1984/8554 端口开、TCP 22 可连但 12s 不出 SSH banner** |
| 5 现场旁证 | 掉线时 ps 仍能看到应用在 D 状态占 32% CPU、systemd --user 占 66%；板子 211MB 内存、**单核** CPU、load average 11；go2rtc 仍在跑而 pedestrian-risk 已停止 —— 与"用户态饥饿"形态一致 |

**结论**：所谓"卡死"不是单一 bug，而是「VENC 路径崩溃 → 重启风暴 → StartLimit 永久放弃 → 整机用户态饥饿（sshd 无法完成握手）」这条链。修复分两半：
- **代码侧**：VENC 多 pack 全发 + 有限超时 + 去掉无界打印（见 §3）→ 观察期内无新 core；
- **部署侧**：Restart=always + StartLimitIntervalSec=0 + 崩溃时不要全栈重建 + 重启次数告警（见 §5.2）。

---

## 4. 算法层（C++/Python 双实现一致性）

| # | 问题 | 影响 | 修复 |
|---|---|---|---|
| 1 | `ipm_calibrate.py` 像素标定点共线时 cv2 静默返回退化单应 | 全部脚点映射到 (0,0) → 去重后只剩 1 人 → **永远不报警** | 新增像素共线 + 非退化（NaN/Inf/全零）校验 |
| 2 | DBSCAN 噪声标签 Python `-2` vs 板端 `-1` | 下游漏判；且测试里的"C++ 复刻"副本也写 -2，把分歧掩盖一轮 | 统一 `NOISE=-1 / UNCLASSIFIED=-2` |
| 3 | 并列簇顺序不确定 | C++ 与 Python 结果不可比 | 双方补 tie-break（size 降序 + 簇号升序） |
| 4 | 脚点去重与**全部历史点**比较 | (0,1.5cm,3cm) 三人被链式合并为 1 人 → 低估人数 | 只与已保留点比较 |
| 5 | EMA 时间戳量纲 Python 秒 / C++ int64 | 板端若按秒配 tau，alpha≈1 → **EMA 完全失效** | 契约注释 + 类型说明（板端尚未接线 EMA） |
| 6 | Kalman `correct()` 每帧重分配 `np.eye(4)`/`np.linalg.inv` | 离线原型热点 | 缓存常量 + 2×2 显式求逆（奇异→零矩阵，不再抛异常打断整帧） |
| 7 | 去重循环纯 Python 双重循环 | cProfile 最大热点 | 平方距离向量化（语义等价） |
| 8 | `graph_laplacian`/FSM 注释与实现不符 | 误导维护者 | 更正注释并由测试钉死真实行为 |

离线原型总耗时 **1.461s → 0.963s（-34%）**；`compute_laplacian_features` tottime -87%，Kalman cumtime -47%。
新增 `tests/cpp_parity/parity_harness.cc` + `KNOWN_DIVERGENCES` 双向契约：新分歧→红，分歧消失→红（提示删条目）。

---

## 5. 硬件 / PCB / 外壳 vs 软件（真机实测 + Gerber 解析）

三层同时不匹配：**代码常量 ↔ 实物**、**PCB ↔ 软件期望**、**外壳 ↔ PCB**。

| 信号 | 代码/文档写的 | 真机实测正确值 | 证据 |
|---|---|---|---|
| PAN PWM | `pwmchip0/pwm0` | **`pwmchip8/pwm0`**（PWM8_M1, GPIO1_C4, Pin7） | `/sys/class/pwm` 只有 chip8/9，各自 npwm=1 |
| TILT PWM | `pwmchip0/pwm1` | **`pwmchip9/pwm0`** | 每 chip 仅 1 通道 → pwm1 结构上不存在 |
| 按键 | GPIO1_C3(51) / `-b 92` | **GPIO2_A3 = 67** | pin51 = `spi0m0-miso`；92 = GPIO2_D4 |
| 触摸 SPI CS | `/dev/spidev0.1` | **不存在**（SPI1 disabled，SPI0 仅 cs0）→ 必须 GPIO 手工片选 | `/sys/bus/spi/devices` |
| 屏背光 / T_IRQ | LcdPins::backlight / SysfsTouchPin | **J11 只有 8 脚，无背光脚、无 T_CS/T_IRQ** | Gerber J11 |
| 语音 | `plughw:1,0` | **只有 card0 = rv-acodec**；USB 是 **gadget 模式** | `/proc/asound/cards`、`/sys/class/udc` |
| 屏幕自检 LCD ID | 命令含 "9341" | compatible = `rockchip,spidev` → 该项永远 FAIL | `/proc/device-tree` |
| 串口屏/OLED | `/dev/ttyS3`、`/dev/i2c-2` | **都不存在** | 实测 |

PCB（Gerber v1.7）：
- **P0 `5V_SERVO` 是孤立网络** —— 与 `5V` 之间无任何走线/跳线，舵机轨**没有电源**（文档却写"板上已接好"）。
- **P0 J11 缺背光 / 触摸片选 / 触摸中断**。
- P1 无覆铜（`add_fill` 定义但从未调用）、J10A 丝印步进 5.08mm 而焊盘 2.54mm（标注错位）、U1 排母焊盘在底层、无 DC-DC/3.3V 轨。
- 功耗：现状典型 0.5~1.0A / 峰值 ≈2.3A；若按文档加 USB Hub+WiFi+喇叭+麦 → ≈3.1A **超适配器**。

外壳 V2：4×φ7 柱位与 PCB 孔位 **90×70mm 完全吻合**；但 4 按键孔 / 3 LED 孔与 PCB 前边缘**差 13~14mm**（PCB 上也没有按键/LED 焊盘），**J8 告警开关在外壳上无开孔**。
SoC 实测 **66.6~67.2°C**（PLA 封闭腔无风扇）。

代码侧已修 13 个 `src/hardware/` 文件（PWM 芯片/通道、按键 GPIO、触摸 GPIO 手工片选、`SpiBus::beginCs/endCs` 片选会话、ILI9341 `MADCTL 0x28`、初始化 SWRESET/SLPOUT 延时、真实读 `0xD3`、语音 device 回退）。
主机单测 118/80/99/123/98 全绿；构建机 `test_sysfs_spi` 40/0；ARM 交叉编译 0 警告。

---

### 5.1 真机实跑自检（T7，ARM 交叉编译产物在板上 sudo 实跑）

```
=== hardware selftest ===
GPIO loopback=56 button=67 PWM=pwmchip8/pwm0
[PASS] GPIO_LOOPBACK  GPIO 输出写读回 OK
[PASS] PWM_NODE       PWM 节点导出/配置/写 duty OK
[FAIL] SPI_LCD_ID     LCD ID 不匹配 0x9341
[FAIL] USB_AUDIO      未检测到 USB 声卡
[FAIL] WIFI_LINK      wlan0 不存在或未就绪
[PASS] BUTTON_LEVEL   按键电平读取 OK（当前 LOW）
=== result: 3 PASS / 3 FAIL ===  rc=1
```

3 个 FAIL 均为**预期**（无屏 / 无 USB 声卡 / WiFi 未连）；**3 个 PASS 恰好覆盖本轮修好的两处引脚错误**（PWM 必须是 `pwmchip8/pwm0`、按键必须是 `GPIO2_A3=67`）→ 修复在真机闭环。
另有独立探针 8/8：PWM 回读一致、`/dev/spidev0.0` mode0/1MHz 打开、手工片选 CS 回读（会话中=0 / 会话后=1）、`Ili9341::begin()=1`、运行时 `MADCTL=0x28`。
实跑又暴露并修掉 3 个只在真机出现的缺陷：`SysfsLcdPins` 构造不配方向（导致 `begin()` 返回 0、**屏幕点不亮**）、自检残留 `gpio56/gpio67`、非 root 下 USB_AUDIO 假 FAIL。

**USB host 模式判定**：`/proc/device-tree/usbdrd/usb@ffb00000/dr_mode = "peripheral"`，`xhci-hcd` 无设备绑定，`/sys/bus/usb/devices` 为空，唯一 USB 口被 gadget（rndis/adb/uvc/uac1/uac2）占用 → **USB 喇叭方案在当前镜像不可落地**（要动 DTB + 拆 gadget，会丢 usb0 网络与 adb）。
可行替代：**板载 card 0: rvacodec 实测可播放**（`sudo aplay -D plughw:0,0 silence.wav` rc=0），`voice_alert` 已回退 `plughw:1,0 → default`。唯一剩余未知：ACODEC 输出引脚是否引出/有功放。

---

### 6.2 NPU 算力利用率归因（T8，板端受控实测）

问题：infer=91.5ms/帧 占整帧 71%，而 YOLOv5n INT8 在 1 TOPS NPU 上理论远低于此。

| 观测项 | 实测 | 含义 |
|---|---|---|
| submit_avg | **88.01 ms**（p10 87.87 / p50 87.94 / max 89.11） | rknn_run 一提交即阻塞 88ms |
| wait_avg | **0.11 ms** | rknn_wait 几乎不耗时 |
| RKNN_QUERY_PERF_RUN | ret=-1（runtime v0.9.2 不支持） | 拿不到"纯 NPU 计算"计时，**没有用估算冒充实测** |
| 每帧 C++ 侧可归因开销 | 约 3.5ms（同步 + 后处理） | 微优化空间仅约 4% |

**结论**：91.6ms 属于**驱动/NPU 提交路径本身**，不是我们的 mem_sync、不是 post_process、也不是调度等待 → **C++ 侧没有优化空间**。
并且 RKNN_FLAG_ASYNC / non_block 在该驱动上**不产生真异步**（提交即阻塞）→ "CPU/NPU 流水线重叠"这条路在 RV1106 + 该驱动上**不可行**。
剩余可行方向只有：推理降频、更小输入（需重标定）、换模型/换驱动。

**方法论更正**：早期观察到的"帧率周期性下探到 5.4~6.9"经受控复测（看门狗停用、无其它 SSH/取证会话）消失（min=7.13 / p10=7.45），归因于**外部 CPU 争抢**，不是 go2rtc 背压、不是 config reload、也不是内部长尾。

---

### 5.2 板卡服务加固（T10）

| 缺陷 | 证据 | 修复 |
|---|---|---|
| Restart=on-failure + StartLimitIntervalSec=30/Burst=5 → 30s 内第 6 次启动被拒，服务**永久停摆** | 构建机 6 组受控实验（复刻出 Start request repeated too quickly、NRestarts 冻结、窗口过后才恢复）；**板端实证**：02:02:11 崩溃循环触发同一条消息 | Restart=always + StartLimitIntervalSec=0 |
| 看门狗脚本用 is-active 判活，而该命令在 auto-restart 窗口同样返回 3 → 每 15~16s 盲目 restart（反而喂满限流窗口）；从不 reset-failed、不看返回码 | 板端 journal（2026-10-27 01:20:23→01:26:57 共 27 条，间隔 15/16s；出处与适用范围见本地报告 §3.1） | 改为 timer 驱动的 oneshot：failed 才介入并先 reset-failed；**inactive 默认不动手**；/run/pedestrian-risk.hold 抑制文件 |
| 部署窗口缺陷：stop → 上传 5MB(40~60s) → mv → start 期间服务 inactive，旧看门狗会把**旧二进制**拉起，随后被 mv 覆盖 → "旧进程 + 新文件" | board-perf 现场复盘 + 板端 NRestarts 异常 | 部署脚本强制：**停看门狗 + 置 hold → 上传 → md5 校验 → 安装 → 验证 active → 才放开看门狗**；FAIL 分支保留 hold |
| MemoryMax=200M 在 211MB 板上约 95% RAM，既不保护系统又把服务变成唯一 OOM 受害者 | 现场形态：go2rtc 还在、pedestrian-risk 没了、sshd 出不了 banner | MemoryMax=160M + MemoryHigh=128M + OOMPolicy=continue |
| LimitCORE 未在早期生效 → 38 个 core 占约 477MiB、根分区 80% | du 实测（表观 3.52GiB，稀疏实占 477MiB） | unit 显式 LimitCORE=0 + scripts/board_maintenance.sh（清 core/轮转日志/vacuum journal，清前先导出证据） |

附带修复：board_maintenance.sh 会在 vacuum 前**先导出 journal**（10-30/10-31 的行为证据只存在于 journal 里），避免自己毁掉证据。

---

## 6. 参考视频（同类型作品）逆向分析

素材：`G:\毕设论文\学校参考材料\784859453-1-30080.m4s`（只读，未入库）。
H.264 High@L5.0 1920×1080@30fps、43.233s、1297 帧、2.29Mbps，B 站 DASH 分片，内容是多场景拼接剪辑（≈5s/段）。

观察到：左上绿字 OSD `Num of Person / Dangerous Person / Assembly`（部分片段多一行 Crowding）；**全片无 FPS/耗时/NPU 显示**（所以"NPU 利用率拉满"无法从素材证实）；ROI 是纯红轴对齐矩形、无画框 UI，"进入即告警"=整框变红+标签红底+计数 +1；蓝框 `person 0.xx` + 脚底绿点；**全片无跟踪 ID**（无法证实 ByteTrack）。

它踩的 5 个坑 vs 本项目：
| 坑 | 本项目 |
|---|---|
| NPU 量化精度损失 | **同踩**：仓库无量化/校准脚本、无 FP32-vs-INT8 精度对比（模型为预构建 `yolov5n_external.rknn`） |
| OpenCV 去重参数错误 | **部分同踩**：2cm 容差无实验依据（顺序贪心、非顺序无关） |
| 硬件资源调度不合理 | **同踩**：单线程串行大循环、VENC 输入 RGB888（1.5× 带宽）、GOP=1+10Mbps、无 VPSS/RGA |
| 跟踪器重复计数 | **部分同踩**：本位不用 ByteTrack（未接线），计数=当帧检测数，无轨迹级去重 |
| 硬件解码画面错位 | 根因已定位并修复（VENC 多 pack 越界 + 伪 memcpy），见 §3 |

---

### 6.1 对齐参考视频的三项改进（T6）

| 项 | 实现 | 状态 |
|---|---|---|
| ROI 折线颜色 | 由硬编码改为读 `riskCfg.roi.color`（此前 `risk_config.h` 解析了却没用） | 已上板抓帧验证渲染正确 |
| OSD `Dangerous Person:N` | 帧内中心点落入 ROI 的目标计数（参考作品同款第三行） | 已上板抓帧验证 |
| `Clusters: K` + `Assembly: xx%` | 接入 `crowd_dbscan.h` 簇计数，与占比并列显示 | 已上板抓帧验证 |

颜色真源裁决：**以仓库 `config/default_config.json` 为准**（`roi_zone.color=[0,0,255]` = BGR 红，与参考视频一致）；板上历史配置是 `[0,255,255]`（黄），需随部署同步覆盖。

---

## 7. 数据完整性

`results/rerun/_aggregate.json` 旧值无法从当前 111 条逐帧 timeline 复现（206 处差异），已由 `scripts/aggregate_timelines.py` 重算并覆盖（保留 provenance 注释）：

- 等级分布 **SAFE 28214 / WARNING 8355 / DANGER 8452**（相加 = 45021 帧 ✓）
- 等级切换 **568 次**（均值 5.12，中位 4.0）
- **45 条**出现 DANGER；**31 条**全程 SAFE
- Fiedler 均值 SAFE 0.021 / WARNING 0.567 / DANGER 1.084；Spearman ρ = **0.692**（45021 帧）
- 基线侧与磁盘证据完全一致：2578 次切换（均值 23.22）、DANGER 2190 帧、24 条 DANGER、63 条全程 SAFE

`.gitignore` 调整：逐帧 timeline JSON 不再被忽略（聚合数字的唯一可复现证据，必须入库）；`tools/*.html` 解禁（ROI 画框页是交付物）。

---

### 7.1 算法层新增：轨迹级计数去重（T11/T12）

参考视频第 4 个坑是"跟踪器重复计数"。本项目原状：板端没接 ByteTrack，人数 = 当帧检测框数。

新增 src/algorithms/track_counter.{h,py}（C++11 header-only + Python 镜像，逐值 parity）：轨迹身份级去重 + ROI 进出事件（滞回 + 最短驻留）+ re-id 合并 + tentative 不计 + 滑行框不计。

| 合成序列场景 | legacy（当帧框数） | spatial（仅 2cm 几何去重） | states（只数已确认轨迹） | **tracked（本模块）** |
|---|---|---|---|---|
| 单人 3 框 / 20 帧 | 60 | 60 | 60 | **20（-66.7%）** |
| 双人各 3 框 / 30 帧 | 180 | 180 | 180 | **60（-66.7%）** |
| 丢失 12 帧后 ID 切换 / 30 帧 | 18 | 39 | 40 | **16（-60%）** |
| 重复框在 ROI 内 / 12 帧 | 24 | 24 | 24 | **12（-50%）** |
| min_danger_n=3 误开门帧 | 30/30 | 30/30 | — | **0/30** |

**关键实测**：spatial（仅靠现有 2cm 脚点去重）与 legacy **完全相同** → 6cm 量级的脚点偏移下，2cm 几何去重**完全无效**，必须用身份级判据。这一条直接解释了"伸一只手判出两个人"。

---

## 8. 明确未验证 / 仍在进行

- 画面撕裂是否彻底消失：需长时抓帧对比。
- BGR→RGB 修正与 `conf=0.35` 对识别精度的增益：无人工 GT，未量化。
- ~~core dump 根因未确认~~ → **2026-10-31 已定位**：用 `gdb-multiarch` + 带调试符号的 `core.1235`（102MB 完整 core，mtime 10-30 11:35，早于本轮修复）解出：
  `SIGSEGV / si_code=1 (SEGV_MAPERR) / si_addr=0x0`，崩溃指令 `str r0, [r8]` 且 `r8=0x0` → **对 NULL 的写**；
  崩溃函数 `RKVencChannel::getStreamInfo(rkVENC_STREAM_S*, RTVideoPacket*) + 136`，调用链 `RKVencChannel::getStream()` ← 我们代码；12 个线程中仅 VENC 线程崩，其余停在 futex 等待。
  → 崩溃点与本轮 P0 修复的 VENC 路径（旧代码只分配 1 个 pack 槽 / 只发 `pstPack[0]` / 不检查 `SendFrame` 返回值）**完全对应**；core 时间集中在 10-30 上午，修复后观察期无新 core。
- 码率 / GOP（`10*1024` + `GOP=1`，vendor 值）未做真机码流测量。
- 帧率进一步提升的三项（推理降频、CPU/NPU 流水线重叠、输入 640→416）为**推断，未实现**。
- 硬件自检的真机实跑、USB host 模式能否开启：进行中。
- 排针↔GPIO 物理通断、ILI9341 模块是否自带 3.3V 稳压：需万用表/实物确认。

---

- **板卡在 2026-10-31 00:00 起 SSH/RTSP 握手不可用**：ping 通、22/554/1984 端口开着、go2rtc API 仍有响应，但 sshd 拿不到 banner（Windows 与构建机两个独立主机都失败）。现场指标：load > 11、可用内存 ~18MB（总 211MB）、根分区 80% 满、**38 个 core dump 共 546MB**。
  → T6 的终版二进制（含 R/B 修复）与仓库 config 的**最后一次部署 + 终验被阻塞**，等板恢复后一条命令即可完成（步骤见本地 `docs/review/20261030_round3/ref_video_alignment.md` §7）。
  → 这本身是可用性缺陷：建议清掉 core dump/日志、给 `pedestrian-risk.service` 加 core 限制与日志上限，并复核 `MemoryMax=200M` 在 211MB 板上的合理性。

---

### 8.1 Round 2 新增未验证/待办

- **T6 板端终验已完成**（ROI 红 / Dangerous Person 红 / Clusters+Assembly 黄 逐像素读数；fps 7.74~7.85）。蓝框未取到样本（抓帧时画面无人）→ 蓝框颜色仍未直接验证（通道已由三处独立读数间接证明）。
- **T8 的 dec2（推理降频）变体数据作废**：该变体启动即 SIGABRT（`std::length_error: vector::reserve`），根因是 main.cc 里 `od_results` 为栈上未初始化结构体，而降频在首帧跳过推理 → `od_results.count` 是栈垃圾 → reserve(垃圾)。**已修**（显式清零 + 每帧钳制 count ∈ [0, OBJ_NUMB_MAX_SIZE] + 降频首帧强制推理），但**修复版（dec2c）未上板复测**。
  泛化风险：任何"跳过推理"的路径（降频 / 推理失败 / 初始化失败 / 未来开关）都会踩到未初始化的 `od_results` → 清零必须放在声明处或每帧入口，不能依赖 post_process。
- **T12 板端 A/B 未做**（track_counter 已接入 main.cc，编译开关 BOARD_TRACK_COUNT，host 侧 15/15 通过；板端重复计数改善幅度未实测）。
- **板卡在 2026-10-31 02:05 再次失联**（与 00:05 同型：ping 通、22 可连无 banner、554/1984/8554 端口开），需用户再次断电重启；T10 的加固（`board_recover.sh` / `board_maintenance.sh`）**尚未在板上执行**。
- 崩溃-重启风暴"拖垮整机"目前是**强相关**（现场时间顺序 + 同型症状），不是受控实验证明的因果，报告里按"现场证据链 + 可复现线索"表述。
- 板上 root 直登已不可用（root 直登认证失败），现用 pico + sudo；`board_recover.sh` 已适配。

---

## 9. 复现命令

```powershell
# Python 测试
cd G:\PedestrianRiskDecision; $env:PATH='X:\py311_gpu\Scripts;'+$env:PATH; python -m pytest tests/ -q

# C++ 主机单测
cd G:\PedestrianRiskDecision\src\luckfox_rtsp_yolov5
$env:PATH='G:\DevTools\mingw\w64devkit\bin;'+$env:PATH; mingw32-make test

# 聚合数字复算并核对
python scripts/aggregate_timelines.py --compare   # 与 _aggregate.json 比对
python scripts/aggregate_timelines.py --check     # 不可复现即非零退出

# 板端帧率
ffmpeg -rtsp_transport tcp -i rtsp://192.168.0.136:554/live/0 -c copy -t 30 x.mkv
ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of default=nw=1 x.mkv
```

---

## 11. Round 3：CI 修复 与 凭据泄漏整改（2026-10-31 凌晨）

### 11.1 CI 从红转绿（此前 2 次 push 都是 failure）

| # | 根因 | 证据 | 修复 |
|---|---|---|---|
| 1 | 单元测试读取了 **被 gitignore 的报告** `docs/review/20261030_round3/board_service_hardening.md`，CI 检出后文件不存在 | CI 日志：`FileNotFoundError` in `test_report_contains_key_conclusions` 等 3 项 | 报告改放**被跟踪**的 `docs/audit/`，测试改读该路径 |
| 2 | 新增的凭据守卫自己抓出报告里残留的口令示例行 | CI 日志：`[口令管道喂给 sudo -S] ... echo <password> | sudo -S` | 报告内口令示例全部改占位符 |
| 3 | 部署脚本/unit 在导出时被 Windows 的 `core.autocrlf=true` 转成 CRLF（库里 blob 本来是 LF） | `test_artifacts_are_lf_only` 在纯净导出上 6 项失败 | 新增 `.gitattributes`（`* text=auto eol=lf` + 显式类型 + 二进制标记） |

**验证方式（本次新增的纪律）**：不再只在本机跑 pytest，而是 `git archive HEAD` → 解到空目录 → 跑 pytest = **精确复刻 CI**。
这一招第一次跑就抓出了上面第 3 条（本机工作区看不出，因为 Windows 工作区行为与 Linux 检出不同）。最终 428 passed / 6 skipped（纯净导出），GitHub CI **success**（run 41112671865）。

### 11.2 凭据泄漏整改（安全，最高优先）

本仓库是**公开仓库**，而此前两轮把真实口令写进了被跟踪文件并推到 GitHub：

| 位置 | 内容 | 处置 |
|---|---|---|
| `scripts/board_recover.sh` | `BOARD_PASS` 默认值 | 去掉默认值；必须由 `BOARD_PASS` 或 `BOARD_PASS_FILE` 注入，缺失时明确报错退出 |
| `scripts/board_health_snapshot.py` | `os.environ.get("BOARD_PASS", "<字面量>")` | 去掉默认值并报错提示 |
| `docs/` 多处 | `用户/口令` 简写、`echo <口令> \| sudo -S` 示例 | 全部改占位符 |
| `docs/review/**`（**41 个文件**） | 评审/裁决类文档（按 AGENTS.md 本应只存本地） | `git rm --cached` 取消跟踪，**文件保留在本地磁盘** |
| `.claude/tasks/build-deploy.md` | Agent 工作区里的部署记录（含口令） | 取消跟踪 + `.gitignore` 增 ` .claude/` |

新增 `tests/test_no_committed_credentials.py`：**上下文感知**的凭据守卫（裸 token 黑名单会误报，因为 `luckfox` 同时是产品名）：只在 `PASS=`、`sshpass -p`、`用户/口令` 简写、`sudo -S` 喂入、环境变量默认值这些"口令位置"比对 SHA-256（不存明文），并自带两条元测试 —— 一条证明它能抓出历史泄漏提交，一条证明产品名单独出现不算泄漏。

> ⚠️ **仍需人工处置**：口令仍在 git 历史里。公开仓库下最有效的做法是**轮换板卡与构建机口令**；若要彻底清除历史需 `git filter-repo` 重写全部提交（会改变所有 SHA，影响既有 baseline tag/备份约定）。本仓库无法代劳这两件事。

---

## 12. 板端最终验证结果（2026-10-31 14:13 断电重启后，全部真机实测）

板子 ISP 曾因 D 态进程锁死（`RKVencChannel` 崩溃风暴遗留）而无法软件复位，物理断电重启后恢复。以下为恢复后的完整验收。

### 12.1 T6 画面验证（逐像素读数，非肉眼）

| 项 | 实测 | 判定 |
|---|---|---|
| 二进制 | `d19f88f2`（后升级为 `7333c4bf` control2） | ✓ |
| config | `5c79a22a`（与仓库逐字节一致） | ✓ |
| ROI 框颜色 | `(B0,G0,R224)` = **红** | ✓（此前 RK_FMT_BGR888 修复生效） |
| OSD `Dangerous Person` | 红色 | ✓ |
| OSD `Clusters/Assembly` | 黄色 | ✓ |
| OSD 一/二行 | 绿色 | ✓ |
| 全帧红像素 | 3620（无人帧）/ 7552（含人帧） | ✓ |
| 蓝框（person 检测框） | 多次抓帧画面内无 person 框，**未取到样本** | 未验证（通道已由 3 处独立读数间接证明） |

### 12.2 T8 NPU 归因（受控 66/68 窗口，`estab_rtsp=0` 全程无外部拉流）

| 变体 | fps | submit_avg | wait_avg | 结论 |
|---|---|---|---|---|
| perf2c（`7129540b`） | 7.67 | **88.30 ms** | 0.11 ms | 88ms 属驱动/NPU 提交路径，C++ 无空间 |
| dec2c（`aa57ec99`，降频修复版） | **11.86**（+54%） | — | — | 降频是当前唯一立竿见影的提速手段 |

- `RKNN_QUERY_PERF_RUN` 仍 `ret=-1`（runtime v0.9.2 不支持），**没有用估算冒充实测**。
- `non_block` 不产生真异步 → 「CPU/NPU 流水线重叠」在 RV1106 + 该驱动上**不可行**（负面结论）。
- dec2c 有效推理 46.2ms/帧、每 2 帧推理 1 次、prep 31.9ms/帧不变。

### 12.3 T12 轨迹计数集成开销（A/B）

| 变体 | fps | cpu | infer/窗口 | 结论 |
|---|---|---|---|---|
| control2（track_count 开，`7333c4bf`） | 7.69 | 33.4% | 731.3 ms | **集成零开销** |
| legacy2（track_count 关，`7e1e5c65`） | 7.69 | 33.4% | 731.0 ms | 基线 |

- 人数去重/门控语义的**板端**表现未验证（需真人进 ROI）；主机侧 15/15 语义测试已证明（legacy 8→4、`min_danger_n` 误开门 30/30 → 0/30）。
- 部署后抓帧出现 `persons=1` → **track_count 在真机实时工作**。

### 12.4 最终部署状态

| 项 | 值 |
|---|---|
| 二进制 | `7333c4bf7760c0bcb40511c4d7b455f0`（control2 = track_count + fatal_exit=1 + RkLunch 绝对路径） |
| config | `5c79a22a`（= 仓库） |
| pedestrian-risk | active，NRestarts=0，MainPID=2806 |
| pedestrian-watchdog | active |
| cores / df | 0 个 / 76% |
| 开机早段 | 3 分钟内 2 次自动重启（推断 rknn/ISP 驱动晚于服务就绪；journal 该窗口缺失无法逐条回溯），采集全程 0 |

### 12.5 仍未验证（如实标注）

- T12 人数去重/门控语义板端表现（需真人进 ROI）。
- 蓝框颜色（抓帧时无 person 检测框）。
- dec2c 在含 person 场景下的 fps（无人场景 11.86）。
- 帧率进一步提升的「416 输入」未实现（需重标定）。

---

## 10. 声明

- 本报告所有结论均可追到磁盘文件行号 / 真机命令输出 / 编译测试输出；标注为"推断/未验证"的条目未当作结论使用。
- 论文、答辩、评审类文档未写入本仓库、未上传 GitHub；论文一致性核对产物保存在本地 `docs/thesis_consistency/`（已加入 `.gitignore`）。
- 按仓库规则，`docs/review/` 只留本地；本文件是入库的技术汇总，不含论文正文内容。
