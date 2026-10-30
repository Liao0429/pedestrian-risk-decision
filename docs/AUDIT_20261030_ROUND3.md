# 2026-10-30 Round3 多 Agent 交叉审计报告

> 报告范围：仓库源码 + 真机（Luckfox Pico Max / RV1106）+ 构建机 + PCB Gerber/3D 外壳（只读）
> 组织方式：1 名 Lead + 5 名专业 Agent 并行，写 scope 互斥，Lead 独立复跑全部测试与真机测量
> 详细过程报告留在本地 `docs/review/20261030_round3/`（按仓库隐私规则不入库），本文件是入库的技术结论汇总

---

## 1. 环境与真机

| 角色 | 地址/路径 | 说明 |
|---|---|---|
| 开发板 | `192.168.0.136` pico/luckfox | armv7l 5.10.160，211MB RAM，systemd 托管 `pedestrian-risk.service` + 看门狗 + go2rtc |
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

## 8. 明确未验证 / 仍在进行

- 画面撕裂是否彻底消失：需长时抓帧对比。
- BGR→RGB 修正与 `conf=0.35` 对识别精度的增益：无人工 GT，未量化。
- core dump 根因：仅一个被截断的 core（`core.1403`，SIGSEGV，PC 落在 `.bss` 数组 `s_rgb_scratch` 内部 → 控制流劫持/内存踩踏），未最终确认。
- 码率 / GOP（`10*1024` + `GOP=1`，vendor 值）未做真机码流测量。
- 帧率进一步提升的三项（推理降频、CPU/NPU 流水线重叠、输入 640→416）为**推断，未实现**。
- 硬件自检的真机实跑、USB host 模式能否开启：进行中。
- 排针↔GPIO 物理通断、ILI9341 模块是否自带 3.3V 稳压：需万用表/实物确认。

---

- **板卡在 2026-10-31 00:00 起 SSH/RTSP 握手不可用**：ping 通、22/554/1984 端口开着、go2rtc API 仍有响应，但 sshd 拿不到 banner（Windows 与构建机两个独立主机都失败）。现场指标：load > 11、可用内存 ~18MB（总 211MB）、根分区 80% 满、**38 个 core dump 共 546MB**。
  → T6 的终版二进制（含 R/B 修复）与仓库 config 的**最后一次部署 + 终验被阻塞**，等板恢复后一条命令即可完成（步骤见本地 `docs/review/20261030_round3/ref_video_alignment.md` §7）。
  → 这本身是可用性缺陷：建议清掉 core dump/日志、给 `pedestrian-risk.service` 加 core 限制与日志上限，并复核 `MemoryMax=200M` 在 211MB 板上的合理性。

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

## 10. 声明

- 本报告所有结论均可追到磁盘文件行号 / 真机命令输出 / 编译测试输出；标注为"推断/未验证"的条目未当作结论使用。
- 论文、答辩、评审类文档未写入本仓库、未上传 GitHub；论文一致性核对产物保存在本地 `docs/thesis_consistency/`（已加入 `.gitignore`）。
- 按仓库规则，`docs/review/` 只留本地；本文件是入库的技术汇总，不含论文正文内容。
