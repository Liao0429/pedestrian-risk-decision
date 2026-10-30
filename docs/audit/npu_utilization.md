# NPU 推理 91.5ms 归因与算力利用率（task-8）

- 执行人：board-perf
- 日期：2026-10-31 00:37 ~ 01:00 CST
- 状态：**静态审计 + 构建机部分完成；板端五段实测待板恢复（SSH 当前不可用）**
- 相关文件：src/luckfox_rtsp_yolov5/{src/yolov5.cc, src/main.cc, include/board_infer.h}、构建机 ~/ped_build_bp

---

## 0. 结论摘要

1. **调用链已经是零拷贝**：全仓库没有任何 rknn_inputs_set / rknn_outputs_get 调用（grep 证据），输入输出都走 rknn_create_mem + rknn_set_io_mem 常驻 DMA 缓冲；因此"want_float 把反量化搬 CPU"这条路径**根本不存在**（那是非零拷贝 API 的问题）。
2. **没有 rknn_set_core_mask 可用**：librknnmrt.so 未导出该符号（nm -D 实测；加 -DBOARD_RKNN_SET_CORE0=1 会链接失败：undefined reference to rknn_set_core_mask）。RV1106 单核 NPU，"多核掩码/跑满"在本 SDK 上不存在可调项。
3. **每帧唯一的 CPU→DMA 交互是修缓存（task-1 新增的 rknn_mem_sync ×2）** 与 letterbox 直写；前者是否真耗时必须 A/B（已构建 nosync 变体）。旧代码完全不做 sync（历史 6.9FPS 也没有），所以 mem_sync 不太可能是 91.5ms 的主因，但要用数据排除。
4. 91.5ms 是 **rknn_run 的墙钟时间**（含提交 ioctl + 驱动调度 + NPU 执行）。要判定"NPU 真算 vs 开销"，需要 RKNN_QUERY_PERF_RUN（本次已实现开关）；同时用 non_block + rknn_wait 把 submit/wait 拆开（本次已实现开关）。
5. 理论核算（推断）：YOLOv5n@640 约 4.5 GMACs ≈ 9 GOPs；按 1 TOPS 峰值理想 ~9ms。91.5ms 对应约 10% 峰值利用率 → **确实偏低**，但 RV1106 上 INT8 dense 利用率普遍只有 10~30%，所以"91.5ms 异常"需要板端 PERF_RUN 判定是"利用率低"还是"模型/驱动限制"。

---

## 1. 静态审计（已完成，均带证据）

### 1.1 初始化链（yolov5.cc:init_yolov5_model）
    rknn_init(ctx, model_path, 0, flag, NULL)        flag 原来=0（=RKNN_FLAG_PRIOR_HIGH）
    rknn_query(IN_OUT_NUM) → 校验 1 输入 / <=3 输出
    rknn_query(NATIVE_INPUT_ATTR) / (NATIVE_NHWC_OUTPUT_ATTR)   ← 零拷贝必要查询
    input_mems[0] = rknn_create_mem(size_with_stride)
    rknn_set_io_mem(input_mems[0]) / 每输出 rknn_set_io_mem(output_mems[i])
- 结论：**真零拷贝**（输入输出都是 NPU 直接访问的 DMA 内存，没有每帧 malloc/memcpy）。
- 改进（本次已做，开关默认关）：init flag 支持 RKNN_FLAG_COLLECT_PERF_MASK；init 后查询并打印 RKNN_QUERY_SDK_VERSION（api/drv 版本），使性能口径可追溯。

### 1.2 每帧链（main.cc + yolov5.cc）
    NV12 → yuv420sp_to_rgb_letterbox(...)  直写 input_mems[0]->virt_addr   ← 唯一 CPU 写输入
    rknn_run(ctx, extend)                   ← 墙钟 = 提交 + NPU 执行（同步）
    post_process(ctx, output_mems, ...)     ← 直接读 output_mems（零拷贝输出）
- **无 rknn_inputs_set**（重复设置同一常驻 buffer 的问题不存在）。
- **无 rknn_outputs_get / want_float**（不存在 int8→float 反量化到 CPU 的开销；解码在 CPU 上做的是 int8→score 的定点乘法，属于 post_process 的 CPU 成本，已含在 infer 之后的 postprocess 里——注意：当前 [PERF] 的 infer 只包 rknn_run，post_process 在 inference_yolov5_model 内部、同属 infer 段计时范围）。
    * 说明：main.cc 的 t_infer 时间戳包住整次 inference_yolov5_model（rknn_run + post_process），所以 91.5ms **可能包含 CPU 后处理**。本次新增 npu_avg（PERF_RUN）正是为了把这部分剥离出来。
- rknn_run 原先传 nullptr（无 timeout）；本次改为 rknn_run_extend{timeout_ms=2000}，避免驱动异常时无超时阻塞。

### 1.3 同步/拷贝相关
- task-1 新增：每帧 rknn_mem_sync(input, TO_DEVICE) + 每个输出 mem_sync(FROM_DEVICE)。
- 旧代码不做任何 sync（依赖缓冲不可缓存）。是否真有开销 → 用 -DBOARD_RKNN_SYNC_MODE=0 的 nosync 变体 A/B。
- A/B 开关已实现：BOARD_RKNN_SYNC_MODE = 0/1/2、BOARD_RKNN_NONBLOCK、BOARD_RKNN_COLLECT_PERF、BOARD_RKNN_SET_CORE0、BOARD_INFER_DECIMATE。

### 1.4 能力探测（构建机实测）
    nm -D ~/board_sdk/lib/glibc/librknnmrt.so | grep ...
        rknn_init  rknn_run  rknn_wait  rknn_mem_sync  rknn_query  rknn_inputs_set  rknn_outputs_get
        （无 rknn_set_core_mask → RV1106 单核运行时不提供核心掩码）
    rknn_api.h: 支持 RKNN_FLAG_ASYNC_MASK(0x4)、RKNN_FLAG_COLLECT_PERF_MASK(0x8)、
                rknn_run_extend{non_block, timeout_ms, fence_fd}、rknn_wait、
                RKNN_QUERY_PERF_RUN{rknn_perf_run.run_duration(us)}、RKNN_QUERY_PERF_DETAIL
- 可用于真异步：flag=ASYNC + non_block 提交 + rknn_wait（需要双输入缓冲，避免第 N+1 帧覆写
  第 N 帧仍在读的输入）。本轮先做"non_block + 立即 wait"以拆分 submit/wait，不改语义。

---

## 2. 五段拆解（已实现，待板端读数）

| 段 | 工具 | 变量 |
|---|---|---|
| 前处理 | main.cc 时间戳 | prep（letterbox 直写 + BGR） |
| 提交 | non_block 提交前后时间戳 | submit_avg |
| 等待（=NPU 执行） | rknn_wait 前后时间戳 | wait_avg |
| 取结果 | post_process 前后时间戳 | 待加（当前并入 infer 墙钟） |
| NPU 侧真算 | RKNN_QUERY_PERF_RUN | npu_avg（需 COLLECT_PERF=1） |

心跳格式（本次扩展）：
    [PERF] fps=... infer=... | ... | npu_avg=.. submit_avg=.. wait_avg=.. npu_samples=.. infer_skips=.. decimate=.. sync_mode=..

---

## 3. 理论核算（推断，未实测）

- YOLOv5n @640x640：约 4.5 GMACs ≈ 9 GOPs（来自模型结构量级的公开数据，非本机实测）。
- RV1106 标称 1 TOPS INT8 → 理想 9ms；若实际有效算力为峰值的 10~30% → 30~90ms。
- 实测 infer=91.5ms → 约 = 1 TOPS 峰值的 10%，**偏低**；但需要 npu_avg 才能区分：
    * 若 npu_avg ≈ 85~90ms → 是 NPU 真算慢（利用率/模型/驱动限制），C++ 侧无可优化空间，只能降频/换模型/换输入尺寸；
    * 若 npu_avg ≪ 91.5ms → 差额是提交/同步/后处理开销，C++ 侧可优化（去 sync、异步、降频）。
- **未上板验证**：以上数字均属推断。

---

## 4. A/B 变体（构建机已编译，板端待跑）

| 变体 | 编译宏 | 产物 md5 | 目的 | 预期（推断） |
|---|---|---|---|---|
| default | 无 | a9f3da5a095c16fdce891be52d74253d | 基线 | 7.7 fps |
| perf | -DBOARD_RKNN_COLLECT_PERF=1 -DBOARD_RKNN_NONBLOCK=1 | e5cc51ff1fbdbb20e8c869c447843821 | 拿 npu_avg 与 submit/wait；判定 91.5ms 构成 | 若开销占>20% → 优化空间 |
| nosync | -DBOARD_RKNN_SYNC_MODE=0 | 432f85e74efec21717e8e982ff4231ed | 判定 mem_sync 实耗 | 0 或几 ms |
| dec2 | -DBOARD_INFER_DECIMATE=2 | 82cc4617ecce4a84abb687856c251cd6 | 每 2 帧推理 1 次 + 中间帧沿用框 | 帧时间 ≈ 0.5×91+31+8 ≈ 84.7ms → **约 11.8 fps**；代价检测延迟最坏 +1 帧 |
| core0 | -DBOARD_RKNN_SET_CORE0=1 | **链接失败**（undefined reference to rknn_set_core_mask） | 验证核心掩码 | N/A：本 SDK 无该符号 |

- 已实现但未做：真异步流水线（non_block 提交 + 双输入缓冲 + 与 CPU 预处理/绘制重叠）→ 理论上限帧时间 ≈ max(91.5, 31+8) ≈ 91.5ms → **约 10.9 fps**（+41%）；再叠加 dec2 → 上限 ≈ 2 帧/NPU 周期 ≈ 21 fps（推断，收益递减且实现复杂）。
- 已实现但未做：输入尺寸 416（需重标定模型，最后选项）。

---

## 5. 板恢复后的执行清单（板 SSH 恢复即执行）

    # 1) 回填 task-6 终验（优先）
    scp bp_r6(d19f88f2…) + 仓库 config → 板；重启；抓帧 + [PERF]
    # 2) task-8 五段拆解
    for v in default perf nosync dec2; do
        部署 ~/bp_r8_$v → 重启 → 采 60s [PERF] → 记录 fps/infer/npu_avg/submit_avg/wait_avg
    done
    # 3) 判定
    diff(npu_avg, infer)：若 npu_avg 接近 infer → NPU 真算瓶颈；若差很多 → 同步/后处理可优化
    # 4) 结论回填本文件 §6

---

## 6. 板端实测回填（2026-10-31 01:57~02:03，受控采集）

### 6.1 采集条件（引用结论时必须一并引用）
- 看门狗 pedestrian-watchdog 已停用（避免它在部署窗口把旧二进制拉起）；
- 板子独占：lead 与 board-hardening 均已停手上板；
- 每批 = 部署 -> 重启 -> 60s，1Hz 心跳窗口；
- 全程记录 self_cpu（/proc/self/stat utime+stime）与 estab_rtsp（/proc/net/tcp 中本地端口 554/8554 的 ESTABLISHED 数）；
- 原始行（含 t= 墙钟、mono_ms、cpu%、estab_rtsp）：tmp/net/t8_default_r8.txt、t8_control_q.txt、t8_perf2.txt；
- estab_rtsp 全程 = 0 -> 采集期间没有任何外部 RTSP 消费者，排除拉流背压。

### 6.2 受控数据（1s 窗口统计）

| 变体 | md5 | 窗口数 | fps mean / p10 / p50 / min / max | infer (ms/帧) | prep p50 (ms/帧) |
|---|---|---|---|---|---|
| default_r8 | a9f3da5a095c16fdce891be52d74253d | 63 | 7.69 / 7.52 / 7.74 / 7.13 / 7.78 | 91.2 | 31.05 |
| control_q（当前 main.cc） | fca01005d0b7b4cccd05d54065b0cd52 | 64 | 7.67 / 7.45 / 7.74 / 7.17 / 7.77 | 91.2 | 30.96 |
| perf2（COLLECT_PERF+NONBLOCK） | 891288030d37bea974b21a4832f26cee | 63 | 7.66 / 7.45 / 7.73 / 7.13 / 7.75 | 91.6 | 30.92 |

perf2 的细分（关键）：

    submit_avg = 88.01 ms   (p10 87.87 / p50 87.94 / min 87.79 / max 89.11)
    wait_avg   =  0.11 ms   (0.10 ~ 0.20)
    [RKNN] RKNN_QUERY_PERF_RUN ret = -1      <- 该 runtime/driver 不支持 PERF_RUN
    self_cpu 约 33~35%（每窗口，与 default/control 一致）
    npu_avg = -1（因 PERF_RUN 不可用，未用估算填充）

### 6.3 结论（T8 核心问题）

1. **91.6ms/帧 中约 88ms 花在 NPU 提交路径本身**：rknn_run 一调用即阻塞 88ms，随后 rknn_wait 只等 0.11ms。
   -> 这不是我们代码的同步、不是 post_process、也不是调度等待；剩余约 3.5ms 才是 rknn_mem_sync（输入+输出）与 post_process（int8 解码 + NMS）的合计。
   -> **C++ 侧可优化空间不超过约 3.5ms/帧（约 2.7%）**；要把 7.7fps 显著抬上去，只能动推理本身。
2. **该 runtime/driver（RKNN api 1.6.0 / drv 0.9.2）不支持 RKNN_QUERY_PERF_RUN（ret=-1）**，无法从 runtime 拿纯 NPU 计算时间；按 lead 要求不用估算冒充实测，改用 submit/wait 拆分作为等价证据。
3. **non_block 在该驱动上不产生真异步**：设 rknn_run_extend.non_block=1 后提交仍阻塞 88ms，rknn_wait 仅 0.11ms。
   -> 异步提交 + 与 CPU 预处理重叠 这条优化路线在 RV1106 + 该驱动上**不可行**（负面结论，关闭该选项；若要重叠只能靠多线程双缓冲，且 NPU 仍是 88ms 瓶颈）。
4. **帧率下探的归因纠正**：受控窗口 fps min=7.13 / p10=7.45，已无 5~6 段；而并发时段（lead 的 SSH/journal/取证、我自己的抓帧与构建）出现 prep 40~72ms、fps 5.4~6.9。
   -> 下探来自**外部 CPU 争抢**（prep/encode 是纯 CPU 阶段，对整机负载敏感），不是 go2rtc 拉流背压（estab_rtsp=0）、不是 config reload、不是内部偶发长尾。此前报告里周期性下探的说法据此更正为负载相关下探。

### 6.4 本次采集中发现并修复的崩溃 bug（dec2 变体，已修待验）

- 现象：dec2（-DBOARD_INFER_DECIMATE=2）启动即 SIGABRT：systemd status=6/ABRT、restart counter 13 到 14、Start request repeated too quickly；手工前台运行 exit 134，日志 terminate called after throwing an instance of std::length_error what(): vector::reserve。
- 根因（我的实现缺陷，非板子问题）：object_detect_result_list od_results 是栈上结构体，main.cc 只 memset 了 rknn_app_ctx；正常路径靠 post_process 每帧清零，但**首帧跳过推理**时 od_results.count 是栈垃圾 -> dets/groundPts.reserve(垃圾) -> std::length_error -> terminate。
- 泛化风险（已按 lead 要求加固为永远生效）：任何跳过推理的路径（降频、推理失败、模型初始化失败、未来新增开关）都会踩到它。修复：
    1) 声明处 memset(&od_results, 0, sizeof(object_detect_result_list))（无条件）；
    2) 每帧入口 board_clamp_detection_count(count, OBJ_NUMB_MAX_SIZE)（无条件，纯函数在 include/board_infer.h）；
    3) 调度 board_should_run_inference(frame_seq, decimate) —— 首帧必推理。
- host 回归测试：tests/test_detection_count_clamp.cc（make test 内，40/40 checks）锁死两条不变量：任意垃圾 count 钳制后 reserve 不抛；首帧必推理、decimate<=1 恒推理。
- 影响：T8 的 dec2 数据作废（t8_dec2b.txt 0 行）；修复版 dec2c 已构建（md5 8ec95258274e72007b7e7b438879db44），**待板恢复后重测**。
- 附带现场（给 T10）：这次崩溃-重启循环（13+ 次，每次重建 VI/ISP/VENC/RKNN 栈）在约 3 分钟内把整机拖到 ping 通、22/554 端口开、但 SSH 12s 不出 banner，与 00:05 冻结同型 -> 支持 崩溃/重启风暴导致整机失联 这条根因线。

### 6.5 仍未验证 / 后续
1. **dec2 修复后的真实帧率**：未上板验证（预期帧时间 约 0.5x91.6 + 31 + 8 = 84.8ms -> 约 11.8fps，**推断**）。
2. 输入尺寸 416：需要重新量化/标定模型，未做。
3. 模型/驱动侧优化（更高效的 rknn 编译、NPU 频率、DDR 带宽）：未做，需板端进一步确认。
4. submit_avg=88ms 的物理含义（NPU 实际占用 vs 驱动轮询/中断等待）：在该驱动缺少 PERF_RUN 的情况下无法再细分，**未验证**。

---

## 7. 未验证与风险

1. 本节所有板端数字待测；91.5ms 的构成尚未拆分（**未上板验证**）。
2. YOLOv5n MACs 与 RV1106 有效算力均为公开量级推断，非本机实测。
3. dec2 变体会让检测框滞后最多 1 帧（33→130ms），演示场景需确认可接受；代码已默认关闭（BOARD_INFER_DECIMATE=1）。
4. 真异步流水线需要双输入缓冲（2× rknn_tensor_mem + 每帧 rknn_set_io_mem 切换 + 只在需要结果时 rknn_wait），本轮未实现，需专门任务与板上回归。
5. COLLECT_PERF_MASK 会略微降低帧率（SDK 注释明示），只用于归因，不作为生产默认。
