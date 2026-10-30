# board-perf 板端性能与稳定性审计报告（T1 / task-1）

- 执行人：board-perf（teammate）  日期：2026-10-30 22:55~23:55 CST
- 写 scope：src/luckfox_rtsp_yolov5/**、docs/review/20261030_round3/board_perf.md
- 真机：Luckfox Pico Max RV1106 @192.168.0.136（pico，service pedestrian-risk）
- 构建机：Ubuntu @192.168.0.63（liao，~/board_sdk + ~/luckfox_sdk = 完整 Luckfox SDK）
- 本机 Windows：没有 arm 交叉工具链、没有 SDK（LUCKFOX_SDK_ROOT 为空）→ 交叉编译全部在构建机完成

---

## 0. 结论摘要

1. 帧率瓶颈是 NPU 推理，不是算法/C++。板端 [PERF] 实测（40s 稳定段）：infer=91.5ms/帧(71%)、prep=31.0ms(24%)、draw=2.9ms、enc=4.0ms、decide=0.03~0.05ms、vi=0.06ms；总 129ms → fps 7.5~7.8。任务书里"谱特征 Jacobi 每帧是已知瓶颈"的先验在真机上不成立（decide 只占 0.03%）。
2. 预处理改造在板端性能中性：A/B 对照（同板同场景，只切 -DBOARD_OLD_PREPROCESS）prep 31.2ms(新) vs 31.0ms(旧)，fps 均 7.7。收益在正确性（NV21→NV12、BGR→RGB）与去掉 1.2MB memcpy、1MB 自拷贝、binary 小 0.8MB。
3. 卡死现场证据：进程 67.5% 采样处于 D（wchan=rknpu_submit_ioctl，NPU 同步 ioctl 的正常表现）；但 38 个 core（约 546MB，根分区 80%）中 core.1403 是 SIGSEGV 且 PC 落在 .bss 静态缓冲 s_rgb_scratch 内部 = 控制流跳进数据区（内存踩踏特征）。最可疑是历史"VENC 只分配 1 个 pack"越界写；已修，修复后观察期无新 core、NRestarts=0。
4. 旧代码确实有永久挂死通道：VI/VENC 三处 timeout=-1 且 SendFrame 返回值不检查；看门狗 pedestrian-watchdog.sh 以日志 mtime 30s 判活重启，正是为这种挂死准备。已改有限超时+连续失败计数+退出交 systemd。
5. 配置失效已修：config/default_config.json 的 conf_threshold(0.35) 在 risk_config.h 没有任何解析分支；且仓库依赖 SDK 的 BOX_THRESH/NMS_THRESH（board_sdk 0.35/0.45、luckfox_sdk 0.25/0.45）→ 改为仓库内显式常量 + 运行时传入。
6. 仓库过去编不过板端：include/yolov5.h 仓库不存在（SDK include/rknn 提供）；yolov5.cc 是 2 参签名而 SDK 头是 3 参带默认值 → 干净仓库+SDK 必编译失败；Makefile 缺 include/rknn、缺 RV1106_1103、链接库不全。全部已修，构建机实测通过（§6.4）。
7. 用户症状归因：帧率差=NPU 91ms 天花板；识别不准=主链路把 BGR 喂给 RGB 模型（NV21 别名）+ conf_threshold 调参无效 + 全屏级 person 假框（现场日志：0.80 置信度、框 (451,-6)-(720,474)）；卡死=无限阻塞 + 看门狗重启 + 内存踩踏 core。

---

## 1. 真机与构建环境（已验证）

| 项 | 值 | 证据 |
|---|---|---|
| 板子 | 192.168.0.136；root 直连密码非 luckfox，改用 pico + sudo | paramiko 实测 |
| 服务 | active，ExecStart=.../luckfox_pico_rtsp_yolov5，StdOut/Err=append:/tmp/enhanced_latest.log | cat unit |
| 看门狗 | STALE_SEC=30，日志 mtime 超 30s 就 restart | cat /usr/local/bin/pedestrian-watchdog.sh |
| 板负载 | load 11.26/9.67/5.28（单核 A7）；free 17~30MB；/ 分区 80% | uptime/free/df |
| core | 38 个，多数 102,141,952 B；core.1403=5,189,632 B(10:39) | ls -lt |
| SDK | board_sdk BOX_THRESH=0.35 / luckfox_sdk BOX_THRESH=0.25 | ssh grep + md5 |
| 部署前板端 | md5 f13ea2df…（5905016 B，22:26），与仓库 results/edge_board 的 5905092 不同 | md5sum |
| 172.32.0.93 | Test-Connection 失败、ssh banner 超时 → 弃用 | 实测 |

---

## 2. 现场取证（已验证）

### 2.1 进程状态
- 40s 采样：state=D 27 次、R 13 次（67.5% in D），wchan=rknpu_submit_ioctl。
- /proc/PID/stack 不存在（内核未开 CONFIG_STACKTRACE）；/proc/PID/schedstat 不存在。
- 结论：这里的 D 是 NPU 同步提交的正常表现，不等于挂死；但同样的 D 若出现在 VENC/VI 的 timeout=-1 上就无法退出。

### 2.2 core.1403 gdb 回溯
    core.1403: ELF 32-bit LSB core file ARM, from '.../luckfox_pico_rtsp_yolov5', real uid 0
    Program terminated with signal SIGSEGV, Segmentation fault.
    #0  0x0062cba0 in pedestrian::(anonymous namespace)::s_rgb_scratch ()
    Backtrace stopped: Cannot access memory at address 0x4146e0
    pc 0x62cba0 <pedestrian::(anonymous namespace)::s_rgb_scratch+2196672>

- s_rgb_scratch 是 preprocess.cc 的 NEON 全帧暂存（1280*720*3=2.76MB），基址约 0x4136E0，PC 偏移 2196672 < 2764800 → 在数组内部执行指令 = 控制流劫持。
- core 被截断（segment extending past end of file），取不到更多栈 → 根因未最终确认。
- 候选根因：历史 stFrame.pstPack = malloc(1 个 VENC_PACK_S) 而驱动可能回多 pack → 堆越界写。已修。
- 本轮观察期（约 40min，含两次部署）无新 core、无 SIGSEGV、NRestarts=0。

### 2.3 启动脆弱性
- 启动日志可见 E RKNN: failed to open /dev/rknpu node → [FATAL] init_yolov5_model 失败 → systemd 重启；journal 里 Oct 27 有 52~54 次重启记录（当时还有 EXEC 权限失败 203/EXEC）。
- 23:37:32 watchdog 有一次"未运行，尝试重启"，原因是我手动 stop 服务部署 → 预期行为。

---

## 3. Bug 表（文件:行 | 严重度 | 现象 | 根因 | 修复 | 状态）

行号为修改前（2026-10-30 22:55 快照）行号。

| # | 文件:行 | 严重度 | 现象 | 根因 | 修复 | 状态 |
|---|---|---|---|---|---|---|
| 1 | src/main.cc:248,648,651,658 | P0 | 撕裂/花屏；core 内存踩踏 | 只 malloc 1 个 VENC_PACK_S 且只发 pstPack[0]；多 pack 时丢包+越界写 | stPacks[8]，按 u32PackCount 全发并钳制 | 已修+编译+上板 |
| 2 | src/main.cc:454 | P0 卡死诱因 | 主循环挂死/看门狗反复重启 | 每帧每框 fprintf(stderr)+printf；stderr 无缓冲，消费端跟不上写阻塞 | BOARD_VERBOSE_DETECT=0 编译开关 + 1Hz [PERF] 心跳（兼作看门狗判活） | 已修+上板（日志 540→200 B/s） |
| 3 | src/main.cc:454,648,651 | P0 卡死 | 背压/ISP stall 永久挂死，SIGTERM 也无效 | VI/VENC 三处 timeout=-1；SendFrame 返回值未检查 | 200/100ms 超时 + 连续失败 50 次 break → cleanup → systemd 重启 | 已修+编译+上板 |
| 4 | src/main.cc:645 | P1 性能/一致性 | 每帧 1MB 无用拷贝；缓存一致性无人做 | memcpy(data,frame.data) 是 data==frame.data 同地址自拷贝 | 删除；补 RK_MPI_SYS_MmzFlushCache(src_Blk, RK_FALSE) 真回写 | 已修+编译+上板 |
| 5 | src/main.cc:436 | P2 | 每帧 stat() | cfgCheckFrame % 1 与注释不符 | 改 %300；并把 conf_threshold 纳入重载比较 | 已修 |
| 6 | src/main.cc:459-468 | P1 识别不准 | 检测精度受损、红蓝颠倒 | cvtColor 用 COLOR_YUV420sp2BGR（OpenCV 里是 NV21 别名）+ BGR 送 RGB 模型 | preprocess：NV12→RGB letterbox 直写 NPU 输入；NV12→BGR 直写 VENC 缓冲 | 已修+编译+上板（正确性增益未量化） |
| 7 | src/main.cc:468 | P1 性能 | 每帧 1.2MB 拷贝 | letterbox 先写 Mat 再 memcpy 到 rknn 输入 | 零拷贝直写 input_mems[0]->virt_addr | 已修+上板 |
| 8 | src/main.cc:669 | P2 | VI 缓冲占用过久（u32Depth=2） | VI 帧在编码后才 Release | 预处理后立即 Release | 已修+上板 |
| 9 | src/yolov5.cc:193 | P0 编译阻断 | 干净仓库+SDK 编译 ambiguous/链接失败 | 仓库 2 参签名 vs SDK 3 参带默认值（构建机还有未提交本地补丁） | 改 3 参，阈值由 main.cc 传 riskCfg.conf_threshold | 已修+构建机通过 |
| 10 | src/yolov5.cc:40-131 | P1 泄漏 | 启动失败反复重启累积泄漏 | 失败路径直接 return -1，ctx/mems/attrs 全泄漏 | goto fail 统一释放 | 已修+编译 |
| 11 | src/yolov5.cc:56-61 | P1 越界 | 换多输出模型会踩内存 | 无 io_num 上限检查，而 input_mems[1]/output_mems[3] 定长 | 校验 1/3 上限 | 已修+编译 |
| 12 | src/yolov5.cc:196-197 + 全仓库 | P0 配置失效 | 现场调参无效、阈值随 SDK 漂移 | BOX/NMS_THRESH 来自 SDK（两套数值不同），仓库不自定义；conf_threshold 无解析 | detect_thresholds.h 显式常量 + risk_config 解析 + main.cc 传入 | 已修+上板（0.35 生效） |
| 13 | src/luckfox_mpi.cc:81-86 | P1 泄漏/掩盖错误 | VI 失败但通道已使能无人 Disable | ret=SetChnAttr 后 ret 与 EnableChn 位或，错误码被掩盖 | 分离检查 + 回滚 DisableChn | 已修+编译 |
| 14 | src/luckfox_mpi.cc:130 | P2 泄漏 | StartRecvFrame 失败后通道残留 | 失败不 DestroyChn | 回滚 DestroyChn | 已修+编译 |
| 15 | src/luckfox_mpi.cc:55 | P2 泄漏 | 绑管失败后 Dev 仍 Enable | 失败不 DisableDev | 回滚 DisableDev | 已修+编译 |
| 16 | src/postprocess.cc:441 | P3 性能 | 每帧构造 std::set | std::set 收集类别 | 80 项 bool 数组 + reserve | 已修+编译 |
| 17 | src/postprocess.cc:491 | P2 越界读 | cls_id<0 野读 labels[] | 只判上界 | 补下界 | 已修+编译 |
| 18 | graph_laplacian.h:232-243 | P1 计数偏低 | 0/1.5/3cm 三人判成 1 人 | 去重与全部历史点比较（链式传递），Python 只比已保留点 | 内层改遍历 dedupPts | 已修+pytest 对齐 |
| 19 | graph_laplacian.h:60 | P3 | float 存点/double 比较 | 内部二次舍入 | 内部统一 double；公共 API 仍 float | 已修+登记剩余表示差异 |
| 20 | crowd_dbscan.h:194 | P2 不可复现 | 并列簇顺序随 STL 漂移 | std::sort 非稳定且无 tie-break | 比较器补 size 降序 + 下标升序 | 已修+pytest 对齐 |
| 21 | bytetrack_tracker.h:206,652 | P0 编译 | 该头从未被编译 | xyahToBox 形参 4 维，实参 kf.mean() 8 维 | 形参改 std::array<double,8> | 已修 + -fsyntax-only 通过 |
| 22 | ema_smoother.h | P1 契约 | 按秒配 tau+毫秒时间戳则 EMA 失效 | Python 秒(float) vs C++ int64 毫秒 | 头文件补契约注释（未改语义，避免破坏 golden；板端未接线） | 已修（注释） |
| 23 | include/yolov5.h 缺失 | P0 可复现性 | 干净仓库 make luckfox 编不过 | 头由 SDK 提供（board_sdk/include/rknn/yolov5.h）；塞进仓库反而 shadow SDK 版本 | Makefile/CMake 补 -I SDK include/rknn 与完整 include 链 | 已修+构建机通过 |
| 24 | Makefile:159-180 / CMake demo 段 | P0 构建 | 旧 luckfox 目标链接必失败、编不到 RV1106 分支 | 缺 -DRV1106_1103、缺 NEON flags、链接库只有 rknnmrt/rtsp_demo | 按 board_sdk 实测 flags.make/link.txt 重写 | 已修+构建机通过 |
| 25 | luckfox_mpi.cc:98-100,115 | P2 未验证 | 全 I 帧 + 码率 10*1024（注释 10Mbps）矛盾 | 疑似单位/参数错误 | 本轮未改（缺真机码流测量，避免盲改） | 未修 |

---

## 4. 改动文件清单

新增：include/detect_thresholds.h、tests/bench_pipeline.cc、docs/review/20261030_round3/board_perf.md

修改（src/luckfox_rtsp_yolov5）：src/main.cc、src/yolov5.cc、src/postprocess.cc、src/luckfox_mpi.cc、include/risk_config.h、include/enhanced_decision.h、src/enhanced_decision.cc、tests/test_enhanced_decision.cc、tests/test_golden_regression.cc、Makefile、CMakeLists.txt（preprocess.cc 本轮接入，未改内容）

修改（跨 scope，按 lead 明确指令，task-2 algo-py 目录）：src/algorithms/graph_laplacian.h、src/algorithms/crowd_dbscan.h、src/algorithms/bytetrack_tracker.h、src/algorithms/ema_smoother.h、tests/test_cpp_python_parity.py

---

## 5. 帧率量化

### 5.1 板端实测（[PERF] 心跳）

| 阶段 | 变体 A（新预处理） | 变体 B（旧预处理 A/B 基线） | 占比(A) |
|---|---|---|---|
| vi | 0.06 ms | 0.06 ms | 0.05% |
| prep | 31.0 ms | 31.0 ms | 24% |
| infer | 91.5 ms | 91.5 ms | 71% |
| decide | 0.03~0.05 ms | 0.02 ms | 0.03% |
| draw | 2.4~3.0 ms | 2.4 ms | 2% |
| enc | 4.0 ms | 4.1 ms | 3% |
| 合计 | 128.9 ms | 128.6 ms | — |
| fps | 7.5~7.8 | 7.5~7.8 | — |

- 两变体同一份源码，只切 -DBOARD_OLD_PREPROCESS=1（旧链路按历史 main.cc 逐行复刻）。
- 结论：预处理改造对帧率中性（31.2 vs 31.0ms，±1%）；去掉 1.2MB+1MB 拷贝的收益被"新链路多一次 NV12→RGB 全帧转换"抵消。
- 部署前旧二进制日志增长率独立测算：476 B/s 除以约 70 B/行 = 6.8 FPS，与历史 EDGE_TEST_REPORT 6.9 FPS 一致 → 本轮 7.5~7.8 FPS 差异主因是负载/场景波动，不能算优化收益。

### 5.2 主机微基准（make bench，x86-64 标量口径）

GraphLaplacian::computeFeatures（稠密=最坏情况）：

    n |  dense us/call | spread us/call
    1 |            0.1 |            0.0
    2 |            0.6 |            0.5
    5 |            1.7 |            1.6
   10 |            9.5 |            3.0
   20 |           51.2 |            5.8
   30 |          190.1 |           11.7
   40 |          459.7 |           13.1

EnhancedDecision::processFrame（稠密，300 帧均值）：

    persons | interval |  ms/frame |  spectralUpdates | 相对 interval=1
         20 |        1 |    0.0718 |              330 |   1.00x
         20 |        2 |    0.0393 |              165 |   1.83x
         20 |        3 |    0.0281 |              110 |   2.55x
         20 |        5 |    0.0213 |               66 |   3.37x
         30 |        1 |    0.2076 |              330 |   0.35x
         30 |        3 |    0.0858 |              110 |   0.84x
         30 |        5 |    0.0665 |               66 |   1.08x

预处理 720x480 NV12 → 640x640（host 标量）：

    旧链路等价（cvtColor + resize + 整幅黑边 + copyTo + 1.2MB memcpy）:  4.74 ms
    新链路（NV12→BGR + NV12→RGB letterbox 单遍直写 NPU）             :  5.90 ms
    → 标量路径新链路反而慢 1.25x；板端走 NEON，实测两者均约 31ms（中性）

memcpy：

    1.2MB NPU 输入拷贝（已由零拷贝消除）: 0.091 ms (host；A7 估算 1~3ms，未上板验证)
    1.0MB 同地址自拷贝                  : 0.000 ms (host 被优化掉；板端推断仍真拷贝)

### 5.3 优化项与预期收益

| 项 | 状态 | 量化 | 结论 |
|---|---|---|---|
| 谱特征 Jacobi 降频（N=3，可配置） | 已实现+上板 | host processFrame n=20: 0.072→0.028ms(2.55x)；板端 decide 0.03~0.05ms/帧 | 对 FPS 无实质贡献（占比 0.03%）；保留作 n>=30 防退化与配置化。推翻"谱特征是瓶颈"旧结论 |
| 预处理零拷贝合并 | 已实现+上板 | prep 31.2 vs 31.0ms | 帧率中性；收益在正确性/健壮性 |
| 删自拷贝 memcpy | 已实现+上板 | host 0.000ms；板端推断 0.3~1ms | 小；关键是把假动作换成真 flush |
| VENC 多 pack 全发 | 已实现+上板 | 消除丢包/越界 | 画质/稳定性，FPS 无变化 |
| 调试打印风暴 | 已实现+上板 | 日志 540→200 B/s | 消除卡死诱因，FPS 影响 <0.5% |
| per-frame malloc（cn_bg_dark/postprocess 容器） | 已实现 | 未单独称量 | <0.1ms 级 |
| 推理降频（每 2 帧 1 次推理 + 中间帧复用框/Kalman 外推） | 未实现（建议 P1） | 预估 1/(0.091/2+0.031+0.008) 约 11~13 fps | 唯一能动 71% 大头；代价检测延迟 +130ms。推断，未验证 |
| CPU/NPU 流水线（双缓冲 pthread 重叠） | 未实现（建议 P1） | 上限约 1/max(91.5, 37) 约 10 fps | 需拆生产者/消费者，风险中。推断，未验证 |
| 模型输入 640→416 | 未实现（建议 P2） | infer 预估约 40ms → 约 13 fps | 需重训/重标定+精度回归。推断，未验证 |
| VENC NV12 直通 | 未实现 | RGB888 720x480 1MB/帧 @7.7fps 约 8MB/s，非 DDR 带宽瓶颈；且与 OpenCV 画框缓冲绑定，改 NV12 需额外 BGR→NV12 全帧转换（约 2-4ms A7） | 不建议本轮改；改前先实测带宽/耗时 |
| 码率/GOP（10*1024 + GOP=1） | 未改 | 真机码流未测 | 保留 vendor 值，未验证 |

---

## 6. 测试与验证证据（原文）

### 6.1 主机单测（mingw32-make）

    MAKE_EXIT=0
    40/40 tests passed      (test_enhanced_decision：26→40，新增降频 3 组)
    32/32 tests passed      (test_minijson)
    58/58 tests passed      (test_risk_config)
    104/104 tests passed    (test_golden_regression)
    结果: 174 通过, 0 失败  (test_preprocess 黄金)
    passed=118 failed=0     (test_hardware_modules)
    passed=80 failed=0      (test_selftest)
    ===== lcd_menu 测试结果: 99 passed, 0 failed =====
    test_ili9341: 123 passed, 0 failed
    test_xpt2046: 98 passed, 0 failed

### 6.2 pytest（跨 scope 修改后复跑）

    313 passed, 4 skipped in 10.92s

### 6.3 bytetrack 单头编译（D4）

    g++ -std=c++11 -fsyntax-only -Isrc/algorithms -include src/algorithms/bytetrack_tracker.h ...  →  BT_SYNTAX_EXIT=0

### 6.4 交叉编译（192.168.0.63，board_sdk CMake，-O2 -mfpu=neon-vfpv4 -DRV1106_1103）

    A: new preprocess   A OK  5113144 bytes   md5 ccfc229d73f04b7c7cad510bde945a23
    B: old preprocess   B OK  5909660 bytes   md5 d53ab66c8eb1d8e30961220de96a4793
    A warnings: 3（main.cc 两处 NULL 参与指针算术 + 一处 system() 返回值未用）

### 6.5 上板部署与运行（final = A 变体）

    md5 与 bp_new 一致（paramiko put + mv + chmod 755）
    systemctl is-active → active；NRestarts=0
    fps 7.47~7.74；per-frame vi 0.06 prep 30.94~33.85 infer 91.4~91.9 decide 0.03~0.13 draw 2.45~3.22 enc 3.97~5.32
    40s 采样 D=27 / R=13；RSS 14,152 kB；ss -tlnp 有 0.0.0.0:554
    观察期无新 core、无 SIGSEGV

---

## 7. 未验证 / 需真机或 SDK

1. 本机无 SDK/交叉工具链（LUCKFOX_SDK_ROOT 空）→ 板端编译全部在 192.168.0.63 完成。
2. 撕裂/花屏是否真消失：只验证了多 pack 全发+真 flush+稳定无新 core，未做像素级长时间画面对比。
3. BGR→RGB 修正对检测精度的影响未量化（缺带标注场景）。
4. conf_threshold 0.35 生效后的准确率/误检变化未量化（板端 persons 0~1 波动，无 GT）。
5. core dump 根因未最终确认（仅一个 5MB 截断 core 可 gdb）；需长跑观察。
6. VENC 码率/GOP 参数未改未测（若单位确为 bps，720x480@7.7fps 画质会明显劣化）。
7. NEON 路径未逐一与标量对照（只确认 -mfpu=neon-vfpv4 → __ARM_NEON）。
8. 建议项（推理降频/流水线/416 输入）全部为推断，未实现未上板。
9. EMA 时间戳量纲：契约已写明，板端未接线无法实测。
10. ByteTrack 未接线；D4 只修编译，"轨迹 ID 参与计数去重"需另设计验证（P2，约 0.5 天）。

---

## 8. 与旧报告的差异

- docs/review/20261030_hub/FIXES_APPLIED.md:34 声称"已移除自 memcpy(data=data)"，但 22:55 磁盘 main.cc:645 仍在 → 旧"已修复"标记不可信。
- 旧结论"Jacobi 每帧是帧率瓶颈"被真机 [PERF] 推翻（decide 0.03~0.05ms）。
- BOX_THRESH/NMS_THRESH 答复：仅来自 SDK include/rknn/postprocess.h（board_sdk BOX=0.35/NMS=0.45；luckfox_sdk BOX=0.25/NMS=0.45），仓库先前无定义；现改为 include/detect_thresholds.h 显式常量 + 运行时配置覆盖。
- 仓库 vs 构建机 ~/ped_build_284 逐文件 md5：只有 src/yolov5.cc（构建机 3 参本地补丁）、include/luckfox_mpi.h、include/chinese_font.h 不同；main.cc/postprocess.cc/preprocess.cc/enhanced_decision.cc/risk_config.h 完全一致。已用仓库版本重建上板，消除漂移。

---

## 9. 复现命令

    # 主机测试 + 微基准（Windows, w64devkit）
    $env:PATH="G:\DevTools\mingw\w64devkit\bin;X:\py311_gpu\Scripts;"+$env:PATH
    cd G:\PedestrianRiskDecision\src\luckfox_rtsp_yolov5; mingw32-make test ; mingw32-make bench
    cd G:\PedestrianRiskDecision; python -m pytest tests/ -q

    # 交叉编译（Ubuntu 192.168.0.63）
    export LUCKFOX_SDK_PATH=/home/liao/board_sdk GLIBC_COMPILER=/usr/bin/arm-linux-gnueabihf-
    cmake -S ~/board_sdk -B ~/ped_build_bp/build -DLIBC_TYPE=glibc \
      -DEXAMPLE_DIR=/home/liao/ped_build_bp -DEXAMPLE_NAME=luckfox_pico_rtsp_yolov5 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-std=c++11 -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard"
    cmake --build ~/ped_build_bp/build -j8     # 追加 -DBOARD_OLD_PREPROCESS=1 得 A/B 基线

    # 板端观察
    ssh pico@192.168.0.136（sudo 密码 luckfox）；grep PERF /tmp/enhanced_latest.log | tail

---

# 附：task-11 轨迹级计数去重接入（board-perf 实施）

## A. 接入内容（main.cc，编译开关 BOARD_TRACK_COUNT，默认 1）
1. include bytetrack_tracker.h + track_counter.h（相对路径 ../../algorithms，板端构建已插桩）。
2. 初始化区构造一次：ByteTrack(0.5,0.1,0.8,30,3,1.0,false) + paramsValid() 显式校验（阈值写反早暴露）；
   track_count::Config：roi_polygon 取 riskCfg.roi.polygon、roi_hysteresis=8、min_dwell_frames=3、
   min_danger_n=riskCfg.fsm.min_danger_n（与 FSM 门控对齐）；运行时开关 risk_config.track_count_enabled。
3. 每帧：在同一套 person 过滤条件下收集显示坐标检测框 dets -> tracker.update(dets)（frame_id 内部自增）
   -> tracker.trackInfo() 同序取 confirmed/misses -> 组装 FrameObs/Obs（foot=IPM 米制，roi=像素框中心）
   -> counter.update(fo) -> 用去重后的 r.persons 脚点替换 groundPts。
   因此 er.n / density / r_gcc / fiedler、DBSCAN 簇数、FSM 的 min_danger_n 门控同时变成"身份级"口径。
4. OSD Dangerous Person 改为 r.dangerous_now（滞回+最短驻留后的 ROI 内身份数）；关闭 track_count 时回退旧口径。
5. ROI 配置热重载时重建 counter（身份/访客计数清零，低频事件）；若将来调用 enhancedDecision.reset()
   需同时 tracker.reset() + counter.reset()（main.cc 已注释标注，当前无 reset 调用点）。

## B. 主机 A/B 数字（tests/test_track_count_integration.cc，make test 内，15/15 checks）
场景：每人每帧 2 个检测框、脚点偏移 6cm（2cm 脚点去重对此无效），12 帧，min_danger_n=3。

    A 4人x2框: legacy_n=8  dedup_persons=4   gate_ok=1/legacy=1      → 去重 -50%
    B 2人x2框: legacy_n=4  dedup_persons=2   gate_ok=0/legacy=1      → 旧口径误开门，新口径不误开
    C 3人x1框(对照): legacy_n=3 dedup_persons=3                      → 无虚假抑制
    D ROI 外 2人x2框: dangerous_now=0                                → ROI 口径正常
    （step2_suppressed=0 说明本例在 claim 阶段即归并同一身份，不代表未去重）

与 track-count 报告的"6cm 偏移下 2cm 去重完全无效""min_danger_n 误开门 30/30→0/30"同向。

## C. 构建机产物（board_sdk CMake，-O2 -mfpu=neon-vfpv4 -DRV1106_1103）
    trackcount（新口径默认）: 5,167,628 B  md5 7699876e1bae807fa1a41a96d3e29701
    legacycount（-DBOARD_TRACK_COUNT=0，A/B 基线）: 5,119,144 B  md5 cf0ed1027b25ec88bcb3e6a99bd6fec6
    perf_tc（task-8 归因开关 + track_count）: 5,167,888 B  md5 368dd2446fa698a69b90e909853aa0a1

## D. 未验证 / 风险
- **未上板验证**（板 SSH 自 00:00 起不可用）：以上帧率/人数均为主机口径；板端 A/B 需等板恢复。
- min_hits=3 → 新目标最晚 3 帧后计入人数（约 0.27s @11fps）；要更即时可用 count_tentative（会抖）。
- 该改动改变人数口径 → FSM 评分/等级会随之变化，需与演示场景一起确认（板端实测后再定默认）。
