# Agent2 · 计算机视觉与目标跟踪专家 · 独立审查报告

> 审查对象：H:/PedestrianRiskDecision 的检测（YOLOv5n INT8 / RKNN）与跟踪（Kalman / ByteTrack）实现
> 目标板：Luckfox Pico Max（RV1106，1TOPS NPU，Cortex-A7 1.2GHz 单核，256MB），板端实测 RTSP 拉流 ≈6.9FPS
> 审查方式：静态代码审查 + 实验数据交叉核对（未修改任何源码）
> 报告日期：2026-10-26

---

## 0. 结论速览（Top Findings）

| # | 严重度 | 问题 | 证据 |
|---|--------|------|------|
| 1 | **高** | 板端真机从未有行人入镜验证，WARNING/DANGER 端到端未在目标硬件上实测过；实验报告中的检出率 82.5% 是 PC 端 YOLOv8n 在 COCO 图片上的结果，不是板端 YOLOv5n INT8 的结果 | `results/edge_board/EDGE_TEST_REPORT.md` §4 日志全为 `level=SAFE n=0`；`results/experiment_summary.json` 的 det_rate=0.825 来自 `crowd_test.py`（YOLOv8n） |
| 2 | **高** | 板端多目标平滑是"每目标 4 态 CV Kalman + 最近邻贪心关联"，在 6.9FPS + 2.0m 门限下交叉/遮挡场景会 ID 互换；且漏检时输出点数立即下降（C++ 只输出匹配点），与 Python 版（保留全部存活轨迹）语义不一致，FSM 的 n 会在漏检帧跳变 | `enhanced_decision.cc` smoothPoints（只 push 匹配点）；`kalman_tracker.py` `states()` 返回全部轨迹；`agent5_embedded.md` 已确认此差异 |
| 3 | **高** | ByteTrack（Python+C++ 完整实现）没有任何调用方：板端 main.cc / enhanced_decision.cc 均未引用，是未集成的死代码；且与 Kalman 平滑功能重复（图像框级 vs 地面点级） | 全仓库搜索 ByteTrack 仅命中 README/tests；main.cc 实际调用链只有 `enhancedDecision.processFrame` |
| 4 | **高** | IPM 默认单应硬编码且未真机标定；SC3336 为 98.3° 广角镜头，链路无畸变校正；默认源梯形仅覆盖 y=300..480 地面区域，远处/区域外脚点外推无保护 | `main.cc:96-100` 硬编码 srcPts/dstPts；`config/default_config.json` `homography_file:""`；`ipm_transform.h` 仅做 w≠0 归一化，无有效域检查 |
| 5 | **中** | 后处理 NMS 存在跨类别抑制 bug：`nms()` 内层条件 `classIds[i] != filterId` 应为 `classIds[j] != filterId`，等价于所有类别混合 NMS；人骑自行车等 person 与 vehicle 重叠框会被互相误抑制（且该阈值/后处理与 Python 端 ultralytics per-class NMS 语义不同） | `postprocess.cc:126`；对比 `crowd_test.py` 使用 ultralytics 内置 NMS |
| 6 | **中** | 两条后处理路径的阈值语义不一致：RV1106 分支用 `limit_score = box_conf × class_prob > threshold`，通用分支用 `class_prob > threshold` 且置信度存乘积；板端实际用的是前者 | `postprocess.cc:276-291`（process_i8_rv1106）vs `postprocess.cc:243-245`（process_i8） |
| 7 | **中** | `BOX_THRESH` / `NMS_THRESH` / `OBJ_CLASS_NUM` / `OBJ_NUMB_MAX_SIZE` 来自外部 SDK 的 `yolov5.h`，仓库中无此头文件：板端置信度阈值不可见、工程无法独立编译/调参，Python(0.25) 与 C++ 阈值无法对齐 | 全仓库搜索无 yolov5.h；`agent5_embedded.md` 已确认 |
| 8 | **中** | 帧率瓶颈在 NPU 推理 + 全串行流水线，不是换检测模型能解决的：CPU 仅 42.5%、内存余量充足，说明 NPU（1TOPS 跑 YOLOv5n 640 INT8 约百毫秒级）是主瓶颈；换更大模型只会更低，应优先做输入降分辨率/流水线并行/检测降频 | `EDGE_TEST_REPORT.md`：CPU 42.5%、RSS 12.5MB、6.9FPS；main.cc 单线程串行链路 |
| 9 | **中** | Kalman 参数按 30FPS 经验值设定，未按板端 6.9FPS 重新标定；`frame_dt` 配置 0.1s（10FPS）与实测 0.145s/帧仍有偏差；CV 模型对突然转向/停走滞后 | `kalman_filter.h` 默认 dt=1/30、process_noise=1.5、measurement_noise=0.5；`config/default_config.json` frame_dt=0.1 |
| 10 | **低** | 脚点提取为框底边中点 `(left+right)/2, bottom`，在行人倾斜/遮挡/画面边缘截断时偏离真实地面接触点；`mapCoordinates` 用 int 截断丢失亚像素；IPM 区域外无保护 | `main.cc:349-351`、`main.cc:76-82` |

---

## 1. YOLOv5n INT8 板端推理链路与检测缺陷（审查问题 1）

### 1.1 实际链路（main.cc + yolov5.cc + postprocess.cc）

```
VI 取帧(720x480 YUV420SP)
  → cvtColor(YUV420sp→BGR)                     [CPU, OpenCV]
  → letterbox: 720x480 → 640x640              [CPU, OpenCV]
      scale=0.8889, inputWidth=640, inputHeight=426, leftPadding=0, topPadding=107
  → memcpy 到 rknn input_mem (zero-copy, UINT8 NHWC)
  → rknn_run()                                 [NPU, 阻塞等待]
  → post_process(): 3 尺度网格遍历 + INT8 反量化 + 阈值过滤 + 手写 NMS
  → 遍历 person 框：脚点提取 → mapCoordinates → IPM → EnhancedDecision → 画框 → VENC(H264) → RTSP
```

全链路单线程串行，`rknn_run` 为阻塞调用，无流水线重叠。

### 1.2 潜在缺陷

1. **【中】NMS 跨类别抑制 bug**（`postprocess.cc:113-149`）：
   `nms()` 内层循环 `if (m == -1 || classIds[i] != filterId)` 中的 `classIds[i]` 应为 `classIds[j]`。
   由于外层已保证 `classIds[i] == filterId`，该条件退化为只检查 `m == -1`，即**所有类别共用同一份 NMS 抑制**。
   后果：person 与 bicycle/motorcycle（COCO 2/3）等高 IoU 重叠框会被互相抑制。板端虽在决策前用 `target_classes=[0]` 过滤，但过滤发生在 NMS **之后**，被误抑制的 person 无法恢复。

2. **【中】阈值语义不一致**（RV1106 分支 vs 通用分支）：
   - `process_i8_rv1106`（板端实际路径）：预过滤 `box_confidence >= thres_i8`，最终条件 `limit_score = box_conf × class_prob > threshold`（`postprocess.cc:276-291`）。
   - `process_i8`（非 RV1106）：最终条件 `maxClassProbs > thres_i8`（单独类别概率），`objProbs` 存乘积（`postprocess.cc:243-245`）。
   两条路径对同一 `threshold` 的语义不同；板端用的是更严格的乘积阈值。这导致 Python 实验（conf=0.25 作用于综合置信度）与板端不可直接对比。

3. **【中】INT8 量化 + 固定阈值**：YOLOv5n INT8 输出直接反量化（`deqnt_affine_to_f32`），无置信度校准；低照度/小目标下置信度整体偏低。`BOX_THRESH` 位于仓库缺失的 `yolov5.h`，无法确认其值，也无法按场景调参（config 里 `class_conf_thresholds` 是二次过滤，不能替代检测阈值）。

4. **【中】低照度漏检已被实验观察到**：README 明确"31 条未触发风险报警（含少量低照度场景检测器漏检，已用亮度+检测人数双重核对）"。而 `dataset/README.md` 的数据集全部为 COCO/Mixkit 正常光照，板端真实低照度场景无覆盖测试。

5. **【低】最大检测数截断**：`OBJ_NUMB_MAX_SIZE`（外部头文件定义）限制输出框数，高密度人群可能截断；同时 `od_results->results[last_count].prop = obj_conf` 使用排序后分数，无类别置信度校准。

6. **【高】板端真实行人从未验证**：`EDGE_TEST_REPORT.md` 的真机日志全部为 `level=SAFE n=0`（摄像头前无人），检测→IPM→风险分级链路在目标硬件上只有 n=0 一种输入被验证过。实验报告中的检出率 82.5% 是 PC 端 YOLOv8n 在 COCO 图片上的结果（`experiment_summary.json` 来自 `crowd_test.py`），不能外推到板端 YOLOv5n INT8。

---

## 2. Kalman 平滑实现质量（审查问题 2）

### 2.1 实现

- 单目标 4 态常速度（CV）模型：状态 `[px, py, vx, vy]`（米、m/s），观测 `[px, py]`（IPM 地面坐标）。`kalman_filter.h` 为标准预测/更新，接口与 Python 一致。
- 多目标：`enhanced_decision.cc::smoothPoints` 为**每目标一个 KalmanFilter + 最近邻贪心关联**：
  1. 所有轨迹先 `predict()`；
  2. 构造（轨迹×观测）距离候选对，`d <= kalman_gate_m`（默认 2.0m）入候选，按距离升序排序；
  3. 贪心分配（每个轨迹/观测每帧只用一次）；
  4. 未匹配观测新建轨迹；未匹配轨迹 `miss++`，`miss > kalman_max_miss`（默认 5）删除。

### 2.2 问题

1. **【高】单目标模型套多目标的关联局限**：
   - 门限 2.0m 在密集人群（实验 avg_n 最高 14.9 人、建边距离也是 2.0m）中，轨迹门限与建边阈值相同，交叉/遮挡时最近邻贪心极易把两个相邻目标的观测互换（ID switch），且无外观特征、无速度门限、无马氏距离/协方差门限。
   - 贪心（按距离全局排序逐个分配）与标准匈牙利全局最优不同，虽然小规模人群接近最优，但**没有处理一对多冲突后的级联策略**。
   - CV 模型假设匀速：行人急转/停走时，收敛后的 Kalman 增益对新观测响应慢，轨迹滞后。

2. **【中】漏检时输出点数立即下降（C++ 与 Python 语义不一致）**：
   - C++ `smoothPoints` 的 `out` 只包含"匹配到的观测 + 新建轨迹"，**未匹配的存活轨迹不输出** → 漏检一帧，`featN` 立即掉。
   - Python `KalmanTracker.states()` 返回**全部存活轨迹**（含外推），`n` 保持。
   - 影响：FSM 的 `n` 输入在板端比 Python 实验更抖；`trend` 特征会被漏检噪声误导。`docs/algorithms.md` 宣称的"漏检外推，轨迹不中断"在 C++ 板端输出层面并未兑现。

3. **【中】参数未按实际帧率标定**：
   - 默认 `dt=1/30`、`process_noise=1.5`（σ_a m/s²）、`measurement_noise=0.5`（σ_z 米）是 30FPS 经验值；板端实际 6.9FPS（dt≈0.145s）。Q 矩阵随 dt⁴/dt³/dt² 缩放，等效过程噪声变大，平滑变弱。
   - `config/default_config.json` 已将 `frame_dt` 设为 0.1s（10FPS），比默认 1/30 更接近实际，但仍与 0.145s 有 45% 偏差。
   - `measurement_noise=0.5m` 是否匹配 IPM 真实误差（未标定，见 §4）无从验证。

4. **【低】数值兜底路径静默**：`mat22Inverse` 在 det<1e-12 时返回零矩阵（`kalman_filter.h:113-122`），S 正定理论上不会触发，但一旦触发会静默产生错误增益，无日志/标记。

5. **【低】速度估计未被下游使用**：Kalman 输出了 `(vx,vy)`，但 FSM/图特征只用位置，速度能力闲置。

---

## 3. ByteTrack 实现完整性（审查问题 3）

### 3.1 实现质量（代码层面）

- Python `bytetrack_tracker.py` 较完整：8 维框状态 `[cx,cy,a,h,vx,vy,va,vh]`、IoU 代价 + 纯 numpy 匈牙利（e-maxx）、高分首轮 + 低分二次关联、`max_lost/min_hits/confirmed` 生命周期管理，并有合成遮挡测试 `tests/test_bytetrack_tracker.py`。
- C++ `bytetrack_tracker.h` 为 header-only 完整移植（含手写 Gauss-Jordan 4x4 求逆），但头文件自身声明"移植到板端后建议在真实工程中重新编译并做数值验证"——即**从未在 RV1106 上编译/运行过**。

### 3.2 关键问题

1. **【高】没有任何调用方**：全仓库搜索 `ByteTrack/bytetrack`，只命中 README 宣传、tests 单测；板端 `main.cc`、`enhanced_decision.cc`、`risk_engine.cc` 均未 include/调用。ByteTrack 是纯库代码，未接入产品链路。
2. **【中】与 Kalman 平滑功能重复**：Kalman（地面点级 4 态）与 ByteTrack（图像框级 8 态）都在做多目标跟踪。合理的架构应是 ByteTrack 提供稳定轨迹 ID，再按 ID 关联地面脚点做 Kalman 平滑；当前两套并存且互不调用，README 的"Kalman/ByteTrack 多目标跟踪"宣传与实际部署（只有 Kalman）不符。

---

## 4. 脚点提取与 IPM 映射误差（审查问题 4）

### 4.1 脚点提取

- 确认：脚点 = 检测框底边中点 `((left+right)/2, box.bottom)`（`main.cc:349-351`），再经 `mapCoordinates` 还原到原图坐标。
- 误差来源：
  1. **检测框 bottom 本身不是地面接触点**：行人倾斜、下肢遮挡、多人重叠时，框底边可能落在非脚点位置；画面边缘被 `clamp(x,0,model_w)` 截断的框，其 bottom 是图像边界而非脚点。
  2. `mapCoordinates` 使用 `(int)` 截断（`main.cc:80-81`），丢失亚像素（近处 1px≈1~2cm，远处可达 5cm+ 量级）。
  3. 框坐标在 letterbox 坐标系（640x640，topPadding=107）还原：`y = (y - 107)/0.8889`。若后处理输出的 bottom 被 clamp 到 640（黑边底部），还原后为 `(640-107)/0.8889 ≈ 599px`，超过原图 480 高度——**脚点会被错误地推到画面外**（远处目标被裁掉顶部时尤其容易）。

### 4.2 IPM 映射误差

- 默认 H：`srcPts=(0,480),(720,480),(560,300),(160,300)`，`dstPts=(-3,3),(3,3),(3,-1),(-3,-1)`（`main.cc:96-100`），即假设地面为平面、覆盖画面底部 6m×4m 区域。
- 误差来源：
  1. **【高】未真机标定**：`homography_file` 为空，未用 `ipm_calibrate.py` 做真实 4 点标定；相机安装高度/俯仰角与默认假设不符时，整个米制坐标系统性偏移。
  2. **【高】广角畸变未校正**：SC3336 为 98.3° 广角（FOV），画面边缘畸变显著；链路中无 `undistort` 步骤（RV1106 CPU 上也难以实时做）。单应变换假设直线投影，畸变导致地面点越靠图像边缘误差越大——而人群聚集往往发生在画面边缘。
  3. **【中】IPM 有效域外无保护**：`ipm_transform.h` 只做 `w≠0` 归一化，不检查脚点是否在标定梯形内；远处目标（y<300 区域）的脚点会被外推到无意义的坐标，产生虚假图节点/虚假聚集。
  4. **【中】Python 实验的 H 按视频尺寸等比缩放默认梯形**（`crowd_test.py:get_homography`），与板端 720x480 固定 H 的畸变分布不同，实验与板端误差不一致。

---

## 5. Python 实验与 C++ 板端部署的差异（审查问题 5）

| 维度 | Python（crowd_test / crowd_test_enhanced） | C++ 板端 | 影响 |
|---|---|---|---|
| 检测模型 | YOLOv8n（ultralytics，FP32/GPU） | YOLOv5n INT8 RKNN | **模型不同**，检出率 82.5% 不能代表板端 |
| 输入 | 原图直接 `imgsz=640`（内部 letterbox） | 720x480 → 640x640，leftPad=0/topPad=107 黑边 | 填充方式相近，但 Python 的 ultralytics letterbox 用灰边(114)，C++ 用黑边(0)，且后处理还原逻辑不同 |
| 置信度阈值 | `--conf 0.25` 显式 | `BOX_THRESH`（外部 SDK，仓库缺失） | 阈值不可对齐 |
| NMS | ultralytics per-class NMS | 手写混合类别 NMS（§1.2 bug） | 漏检/误检行为不同 |
| 帧率 | 500 张 16.79s ≈ 30FPS | 6.9FPS 串行 | 板端帧间位移更大，Kalman/FSM 时间参数需按 6.9FPS 标定 |
| 类别 | `classes=[0]`（仅推理 person 类） | 推理全部 80 类再过滤 target_classes | 板端多 79 类的 NMS/类别竞争开销与误抑制 |
| Kalman 输出 | `states()` 保留全部存活轨迹（n 不掉） | `smoothPoints` 只输出匹配点（n 掉） | FSM 输入 n 的抖动特性不同 |
| 实验结果 | 111 视频 45021 帧、切换 4.0 次/视频、检出率 82.5% | 真机仅有 n=0 SAFE 日志 | 实验结论（等级分布、稳定收益）未在板端验证 |

---

## 6. 瓶颈判断（审查问题 6）

### 证据

- 板端实测：RTSP 拉流 **≈6.9FPS（145ms/帧）**，进程 CPU **≈42.5%**（单核），内存 RSS 12.5MB（`EDGE_TEST_REPORT.md`）。
- 主循环为**单线程全串行**：VI 取帧 → cvtColor/resize/letterbox（CPU）→ `rknn_run`（NPU 阻塞）→ 后处理（CPU 遍历 8400 网格×3 anchor×80 类）→ 增强决策（Jacobi O(n³)，n≤20 可忽略）→ 绘制 → H264 编码 → RTSP。
- RV1106 NPU 仅 1TOPS：YOLOv5n 640×640 INT8 约 4.2 GFLOPs，按 NPU 实际利用率 30~50% 估算推理本身约 **80~140ms**，已是 145ms 帧预算的大头。

### 结论

1. **主要瓶颈 = NPU 推理时间 + 串行流水线无重叠**。CPU 仅 42.5%、内存充足，说明 CPU/内存不是瓶颈；编码走硬件 VPU。
2. **不应默认换检测模型**：YOLOv5n 已是 YOLOv5 系列最小；换 YOLOv5s 计算量约 4 倍（帧率将降到 ~2FPS）；换 YOLOv8n 计算量相近且需重新导出/量化 RKNN，收益不明确。换模型属于高风险低收益。
3. **更优路径（按收益/成本排序）**：
   - 输入分辨率 640 → 416/320：NPU 时间降到约 1/2~1/4，后处理同步减少；行人监控（中近景）精度损失可接受。
   - 检测降频：每 2 帧检测一次（~3.5Hz 检测），中间帧用 Kalman/ByteTrack 外推，风险决策保持每帧输出。
   - 双缓冲流水线：`rknn_run` 提交后不阻塞等结果，CPU 预处理下一帧 + NPU 推理当前帧重叠（Rockchip zero-copy 多线程示例）。
   - 后处理降耗：当前 80 类全遍历，可只计算 person 类别通道或先用 box_conf 粗筛再算类别。

---

## 7. 改进建议（按严重度）

1. 在板端摄像头前放置真人/人物立牌，完成 WARNING/DANGER 端到端真机验证，采集带行人的真机日志与画面（最高优先级）。
2. 用 `ipm_calibrate.py` 在地面标 4+ 点，生成 `homography_file` 写入 config；同时评估广角畸变校正或限制 IPM 有效域（对区域外脚点做丢弃/标记）。
3. 修复 `postprocess.cc` NMS 类别条件（`classIds[j]`）；统一 RV1106 与通用后处理阈值语义。
4. C++ `smoothPoints` 改为输出全部存活轨迹（匹配 + 外推），与 Python `states()` 对齐，避免漏检帧 n 跳变。
5. 按 6.9FPS 重新标定 Kalman 参数（`frame_dt=0.145`、process/measurement noise），或在配置中增加按实测帧率自适应的 dt 更新。
6. 将 ByteTrack 真正接入板端（先做框级跟踪稳定 ID，再接地面点 Kalman），或从 README 宣传中移除未集成的承诺。
7. 补仓库缺失的 `yolov5.h`（或把 BOX_THRESH/NMS_THRESH 改为 config 可配置项），使板端阈值可调、工程可独立编译。
8. 主循环加阶段耗时打点（VI/cvtColor/rknn_run/post_process/决策/VENC），用数据确认 145ms 分布后再优化；优先做分辨率/降频/流水线，不做换模型。

---

## 8. 审查范围与文件清单

- 已读：`src/luckfox_rtsp_yolov5/src/{main,yolov5,postprocess,enhanced_decision}.cc`、`include/{ipm_transform,enhanced_decision,risk_config}.h`、`src/algorithms/{kalman_filter.h,kalman_tracker.py,bytetrack_tracker.py,bytetrack_tracker.h,ipm_calibrate.py}`、`config/default_config.json`、`scripts/{crowd_test,crowd_test_enhanced,ablation_study}.py`、`dataset/README.md`、`results/{experiment_summary.json,rerun/_ablation.json,rerun/_comparison_summary.json,edge_board/EDGE_TEST_REPORT.md}`、`docs/{algorithms.md,README.md,hardware/system_integration.md,review/agent5_embedded.md}`、`tests/test_bytetrack_tracker.py`。
- 未修改任何源码。
- 局限：无法在板端实测（本环境无硬件），所有运行数据来自仓库内真机记录；`yolov5.h` 缺失导致 BOX_THRESH/NMS_THRESH 等常量不可见。