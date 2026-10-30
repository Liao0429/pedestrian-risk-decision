# T11 轨迹级计数去重（role: track-count）

> 范围：`src/algorithms/track_counter.{h,py}`、`src/algorithms/bytetrack_tracker.h`（加固）、
> `tests/`（新增 3 个测试/探针文件 + 扩展 parity 清单）、本报告。
> **未改** `src/luckfox_rtsp_yolov5/`（main.cc / enhanced_decision 属 board-perf scope），
> 只给出接入清单（第 7 节）。
> 数据来源：合成框序列 + 真实编译产物（C++/Python 逐值比较）。**未上板**（开发板
> 192.168.0.136 SSH 不可用），所有"板端数字"均为主机口径，已标注。

---

## 0. 结论

1. 板端至今**没有接 ByteTrack**（`grep -r "ByteTrack" src/luckfox_rtsp_yolov5/` = 0 命中）。
   人数 = 当帧 person 检测框数：`main.cc:585/609`（`featN = er.n`）与
   `enhanced_decision.cc:52-138`（贪心最近邻 + Kalman，每个未匹配点都新建轨迹并原样输出）。
   同一人被 YOLO 出两个框时，只要脚点距离超过 2cm，
   `graph_laplacian.h:230-249` 的 2cm 去重（只与"已保留点"比，非传递合并）就兜不住，
   人数直接 +1 —— 即参考作品第 4 个坑"跟踪器重复计数"在本项目的同构形态。
2. 交付了独立可测模块 `track_counter.h/.py`：人数口径从"框"提升到"轨迹身份"，
   并给出 ROI 进出事件（滞回 + 最短驻留）、re-id（ID 切换/短时丢失）合并、
   tentative 不计、`min_danger_n` 门控交互、与 2cm 几何去重的先后顺序。
3. `bytetrack_tracker.h` 的可用性已补齐：8 维 `xyahToBox`（board-perf 已修）之上，
   补了**输入防御**（长度<5 / NaN 行丢弃，避免越界读 UB）、**参数校验**
   （`paramsValid`，条件与 Python `ValueError` 逐条一致）、
   **生命周期契约文档**（`frame_id<=1` 自动确认的陷阱、`reset()` ID 复用、
   滑行轨迹仍在 `states()` 里）。`-Wall -Wextra -Wpedantic -Wshadow` 零告警。
4. 逐值 parity：8 个真实序列（多人交叉 / 遮挡重现 / ID 切换 / 重复框 / 低分二次关联 /
   长期丢失）+ 5 个计数器用例，C++ 真实编译产物 vs Python **逐值一致**
   （`%.17g`，每帧每轨迹 12 个字段）。
5. 量化（合成序列，见第 5 节）：`max 人数 6 -> 2`、`Σ 人数 180 -> 60`；
   `min_danger_n=3` 门控 `旧口径 30/30 帧误开门 -> 去重后 0/30`。
6. 全部测试绿：`python -m pytest tests/ -q` = **364 passed, 4 skipped**（原 313 passed）；
   `cd src/luckfox_rtsp_yolov5 && mingw32-make test` = **退出码 0**。

---

## 1. 现状与根因（证据）

| 位置 | 现状 | 后果 |
|---|---|---|
| `main.cc:583-606` | `groundPts`：每个 person 检测框脚点经 `mapCoordinates` + `mapToGround(H,...)` -> IPM 地面米制点，**一框一点** | 同一人两框 = 两个点 = 2 人 |
| `main.cc:609` | `featN = er.n`（`er.n = smoothPts.size()`） | OSD "Num of Person" 与 FSM 的 n 都被重复框放大 |
| `main.cc:647-705` | `dangerPersons`：逐个检测框判"框中心是否在 ROI 多边形内"，`main.cc:744` 直接显示 | OSD "Dangerous Person" 重复计；这是**像素空间**判据（与 IPM 米制脚点两套坐标） |
| `enhanced_decision.cc:52-138` | `smoothPoints`：Kalman 预测 + 距离门限贪心最近邻；未匹配点**原样输出并新建轨迹**（`t.id` 只用于调试） | 输出点数 ≡ 输入点数（无去重），重复框照样过 |
| `enhanced_decision.cc:169` | `feat.n = res.smoothPts.size()` | 人数逐帧刷新但仍是"点数" |
| `graph_laplacian.h:230-249` | 脚点 2cm 去重，只与"已保留点"比较（单链非传递） | 6cm 偏移的重复框漏判（第 5 节有实测） |
| `grep -r ByteTrack src/luckfox_rtsp_yolov5/` | **0 命中** | 没有任何轨迹级去重 |
| `risk_fsm.h:171-177` | `n >= min_danger_n`（默认 3）才允许 DANGER | 重复框把 1 人顶到 2、2 人顶到 4 -> 误升 DANGER |

---

## 2. 交付物

| 文件 | 行数 | 作用 |
|---|---|---|
| `src/algorithms/track_counter.py` | 810 | Python 参考实现（仅标准库 + dataclass） |
| `src/algorithms/track_counter.h` | 865 | C++11 header-only 镜像（仅标准库），逐值对齐 Python |
| `src/algorithms/bytetrack_tracker.h` | 775 | 加固：输入防御 / `paramsValid` / 生命周期契约 |
| `tests/test_track_counter.py` | 480 | 31 条语义测试 + 端到端量化（含报告表格） |
| `tests/test_bytetrack_sequences.py` | 366 | 8 个真实序列 parity + 防御/参数自检（14 条） |
| `tests/cpp_parity/bt_sequence_probe.cc` | 126 | ByteTrack 序列探针（`SEQ`/`PARAMS`/`BAD`，`%.17g`） |
| `tests/cpp_parity/parity_harness.cc` | 289 | 追加 `TC` 命令（原命令一行未动） |
| `tests/test_cpp_python_parity.py` | 694 | 追加 5 个 TC 用例 + 1 条路径覆盖断言（31 -> 37 条） |

---

## 3. 语义契约（写进了两个实现的文件头）

1. **同一轨迹跨帧只计 1 人**：`n` = 本帧"出现且计入"的**身份**数，不是轨迹条数、不是检测框数。
2. **ID 切换 / 短时丢失的合并（re-id merge）**：`track_id -> identity_id` 映射；新 `track_id`
   若脚点落在"本帧未出现、且丢失 ≤ `reid_max_gap` 帧"的身份的 `reid_radius` 内，
   并入该身份（最近者优先，并列取身份 ID 小者）。**粘性**合并，帧间不翻转。
3. **未确认轨迹（tentative）不计人数、不产生 ROI 事件**：只做身份登记
   （`count_tentative=True` 可放开）。与 ByteTrack `min_hits=3` 叠加，新目标最多晚 3 帧进入人数。
4. **与 `min_danger_n` 的交互**：`gate_n = n`（去重后人数）喂给 FSM；同时输出
   `gate_ok` / `legacy_gate_ok`（旧口径）用于量化"旧口径是否误开门"。
5. **与 2cm 脚点去重的先后顺序**：(a) 跨帧身份归并 -> (b) 同帧空间去重（三判据）-> (c) `graph_laplacian`
   内部 2cm 去重（几何兜底，保留不动）。`n_spatial_dedup` 单独输出（b 之前的"仅几何"口径），便于消融。
6. **滑行帧 `coast_frames`（默认 1）**：ByteTrack 在漏检时依然把轨迹放进 `states()`
   （`misses > 0`，用 Kalman 外推框）。本模块只在"本帧真关联上（`misses==0`）
   或最后一次关联距今 ≤ `coast_frames` 帧"时才计入人数 —— 否则人走出画面后，
   人数要 `max_lost`（默认 30 帧 ≈ 1 秒 @30fps）才回落。滑行期间 ROI 状态机不翻转。
   单位约定：`foot` = IPM 米制（`dedup_tol`/`dup_foot_max`/`reid_radius` 同单位），
   `roi_point` = 像素（`roi_polygon`/`roi_hysteresis` 同单位）——正好对应
   `main.cc` 里"米制 groundPts + 像素 ROI 多边形"的两套现成坐标。

**同帧"同一个人"判据（三取一）**：脚点 ≤ 2cm（与 GraphLaplacian 同口径）｜脚点 ≤ `dup_foot_max`=20cm
（两个成年人脚点相距 20cm 以内等同同一人）｜`IoMin ≥ 0.75` 且高度比 ≤ 1.5 且两框中心互含
（覆盖"不同 anchor 尺度导致框底不一致"的重复框；高度比挡住"远小近大"的两个人）。
合并是**粘性**的：只在首次判同的那一帧合并一次，之后永不再拆（置信度高者为规范身份，并列取 ID 小者）。

---

## 4. 测试与逐值 parity

**全量**：`python -m pytest tests/ -q` -> ` 364 passed, 4 skipped`（改动前 313 passed, 4 skipped）；
`cd src/luckfox_rtsp_yolov5 && mingw32-make test` -> 退出码 0（全部板端主机测试通过；需 `g++` 在 PATH，
见第 9 节）。

**ByteTrack 真实序列（`tests/test_bytetrack_sequences.py`，14 条）**：8 个场景逐值比较，
C++ 与 Python 每帧每条轨迹的 `(id,x1,y1,x2,y2,score,confirmed,hits,misses,age)` 完全一致：

| 场景 | 帧数 | 覆盖点 |
|---|---|---|
| multi_crossing | 30 | 3 人交叉 + 不同尺寸 |
| occlusion_reappear | 30 | 遮挡 10 帧（< max_lost）后重现 |
| id_switch_swap | 40 | 两人相向交叉（匈牙利 + 预测） |
| duplicate_boxes | 20 | 同一人 3 框（脚点差 2/6px） |
| low_score_and_empty | 24 | 低分二次关联 + 空帧 |
| many_enter_leave | 30 | 5 人并排，中间一人遮挡 8 帧 |
| reid_new_id | 30 | 丢失 12 帧后跳变 250px -> 新 ID |
| long_loss_beyond_max_lost | 60 | 丢失 40 帧 > max_lost -> prune + 新 ID |

**计数器 parity（`parity_harness.cc` 的 `TC` 命令）**：`tc_duplicate_and_coast` /
`tc_reid_merge` / `tc_roi_hysteresis` / `tc_gate_min_danger_n` / `tc_identity_ttl`，
逐帧比较 `n/det/spat/trk/supp/dang/vis/gate/lgate` + 10 项累计统计 + 每身份
`(id, track_ids, box, score, foot, roi, in_roi, dwell, confirmed)` + 每个 ROI 事件
`(identity_id, track_id, entering, since_frame, signed_distance)` —— 全部一致。
另有覆盖断言防止"两边都空"的假通过（ROI 用例必须恰好 1 ENTER + 1 LEAVE）。

---

## 5. 量化：重复计数从 N 降到 M

口径：`legacy` = 旧口径（当帧 person 检测框数，`main.cc` 现行为）；
`spatial` = 只做同帧 2cm 脚点去重（GraphLaplacian 现有能力）；
`states` = "接了 ByteTrack 但直接数 `states()` 条数"；
`tracked` = 本模块（轨迹身份去重）。
数据由 `python -m pytest tests/test_track_counter.py::TestReportTable -q -s` 打印，
同一条用例里被断言钉住（`tests/test_track_counter.py:TestReportTable`）：

| 场景 | 帧数 | legacy max | spatial max | states max | tracked max | legacy Σ | spatial Σ | states Σ | tracked Σ | legacy 开门帧 | tracked 开门帧 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 单人 3 框 | 20 | 3 | 3 | 3 | **1** | 60 | 60 | 60 | **20** | 20 | **0** |
| 双人各 3 框（min_danger_n=3） | 30 | 6 | 6 | 6 | **2** | 180 | 180 | 180 | **60** | 30 | **0** |
| 丢失 12 帧后 ID 切换 | 30 | 1 | 2 | 2 | **1** | 18 | 39 | 40 | **16** | 0 | 0 |
| 重复框在 ROI 内 | 12 | 2 | 2 | 2 | **1** | 24 | 24 | 24 | **12** | 0 | 0 |

读法与结论：
- **单人 3 框**：Σ 60 -> 20（-66.7%），max 3 -> 1。注意 `spatial` 也是 60 —— 说明
  6cm 脚点偏移下 **2cm 几何去重完全无效**，必须靠身份级判据（这正是本模块存在的理由）。
- **双人各 3 框**：旧口径稳定 6 人、`min_danger_n=3` **30/30 帧全部误开门**；
  去重后 2 人，`gate_ok` **0/30**。这条直接量化了"重复计数 -> 误报警"的链路。
- **ID 切换**：`states` Σ 40 -> `tracked` 16（-60%），max 2 -> 1；
  这一段是"接了 ByteTrack 也还会重复计数"的第二条路径（旧 ID 滑行 + 新 ID 观测同时存在），
  re-id 合并（`reid_radius=0.6` m / `reid_max_gap=15`）把它吃掉。
- **ROI 重复框**：`dangerous_now` max 2 -> 1（OSD "Dangerous Person" 不再重复计）。

**未做**：真实摄像头视频上的召回/误报评估（需要上板取流或离线跑真实视频，见第 8 节）。

---

## 6. `bytetrack_tracker.h` 加固清单

| 项 | 内容 | 验证 |
|---|---|---|
| 数值/语义一致性 | 与 Python `bytetrack_tracker.py` 在 8 个真实序列上逐值一致（第 4 节） | `tests/test_bytetrack_sequences.py` |
| 输入防御 | `update()` 丢弃长度<5 与含 NaN/Inf 的检测行（原实现 `d[4]` 越界读是 UB） | `BAD` 探针：`after_bad count=1`、`after_good count=2` |
| 参数校验 | 新增 `ByteTrack::paramsValid(...)`（静态）与 `paramsValid()`（实例），条件与 Python `ValueError` 逐条一致；不抛异常（板端常 `-fno-exceptions`） | 11 组参数矩阵 C++ vs Python 一致 |
| 生命周期契约 | 文件头写清：`frame_id<=1` 会让新建轨迹直接确认（恒传 0 的陷阱）；`reset()` 后 ID 从 0 复用（下游必须同步 reset）；轨迹删除唯一路径 `misses > max_lost`；`states()` 含未确认轨迹；`trackInfo()` 与 `states()` 同序 | 文档 + 测试断言 |
| 编译告警 | `-std=c++11 -Wall -Wextra -Wpedantic -Wshadow` 独立 include 编译 0 告警 | 手动编译验证（第 9 节命令） |

---

## 7. 接入 main.cc / enhanced_decision 的最小改动清单（交给 board-perf）

> 原则：**不新增线程、不新增依赖**；改动集中在 `main.cc` 每帧的"行人空间网络"块（583-625）、
> 绘制块（645-706）与初始化块（201-236）。

1. **include + 构造**（初始化区，`main.cc:201-236` 附近，风险等级配置加载之后）：
   ```cpp
   #include "../../algorithms/bytetrack_tracker.h"   // 或 -I../algorithms 后 "bytetrack_tracker.h"
   #include "../../algorithms/track_counter.h"

   ByteTrack tracker(0.5, 0.1, 0.8, 30, 3, 1.0, false);   // track/det/match/max_lost/min_hits
   track_count::Config tcCfg;
   for (const auto& v : riskCfg.roi.polygon) tcCfg.roi_polygon.push_back({(double)v.first,(double)v.second});
   tcCfg.roi_hysteresis  = 8.0;                  // 像素；ROI 边界抖动带
   tcCfg.min_dwell_frames= 3;                    // 最短驻留（进/出都要）
   tcCfg.min_danger_n    = riskCfg.fsm.min_danger_n;   // 必须与 FSM 对齐，否则门控口径不一致
   track_count::TrackCounter counter(tcCfg);
   ```
2. **每帧喂检测框（display 像素坐标即可，IoU 关联与单位无关）**：在 `591-606` 的循环里
   顺手收集 `std::vector<std::vector<double>> dets{{sX,sY,eX,eY,prop}, ...}`
   （注意：`589-605` 现在直接在循环内 `mapToGround`；把"框的四角/脚点"和"米制脚点"
   分开取，`dets` 用 `591-599` 同一套过滤条件）。
3. **就地取轨迹**：`const std::vector<TrackResult> trks = tracker.update(dets);`
   （**不要传 frame_id**，让内部自增，避免 `frame_id<=1` 陷阱；也不要每帧传 0）
   `const std::vector<TrackInfo> tinfo = tracker.trackInfo();`（与 `trks` 同序，可同下标对齐）
4. **组装 `FrameObs`**：
   ```cpp
   track_count::FrameObs fo;
   fo.frame_id     = ++frameSeq;         // 严格递增
   fo.n_detections = (int)dets.size();   // 旧口径基线（用于报告/回归对比）
   for (size_t i = 0; i < trks.size(); ++i) {
     track_count::Obs o;
     o.track_id = trks[i].id;
     o.x1=trks[i].x1; o.y1=trks[i].y1; o.x2=trks[i].x2; o.y2=trks[i].y2; o.score=trks[i].score;
     o.confirmed = tinfo[i].confirmed;   // tentative 不计人数
     o.misses    = tinfo[i].misses;      // coast_frames 语义依赖它
     float gx = (float)((o.x1+o.x2)*0.5f), gy = (float)o.y2;   // 显示像素脚点
     pedestrian::mapToGround(H, gx, gy);                        // -> IPM 米制
     o.has_foot = true; o.foot_x = gx; o.foot_y = gy;           // 米（2cm/20cm/re-id 阈值用）
     o.has_roi  = true; o.roi_x = (o.x1+o.x2)*0.5; o.roi_y = (o.y1+o.y2)*0.5;  // 像素（ROI 用）
     fo.tracks.push_back(o);
   }
   const track_count::Result r = counter.update(fo);
   ```
   （若想少改代码，也可以直接用 `has_foot=false` 让模块取 box 底边中点 —— 但那样去重就是
   **像素**口径，2cm/20cm 阈值失去物理意义，**不推荐**。）
5. **用去重后的脚点替换 `groundPts`**：把 `589-605` 的循环改成
   `for (const auto& p : r.persons) groundPts.push_back({(float)p.foot_x,(float)p.foot_y});`
   —— 这样 `er.n`、`er.density/r_gcc/fiedler`、DBSCAN 簇数（`613-620`）、
   `RiskFSM` 的 n 门控（`min_danger_n`）**同时**变成去重口径，不必逐个改。
   注意：`r.persons` 已经是"已确认 + 未滑行超时"的集合，与旧口径相比新目标晚 ≤3 帧出现。
6. **OSD "Dangerous Person"**：`main.cc:647` 的 `dangerPersons` 改为 `r.dangerous_now`
   （滞回 + 最短驻留后的 ROI 内身份数），仍可选保留逐框描边（画框仍用检测框，计数用 `r.n`）。
   若要显示事件，用 `r.events`（`entering/since_frame`）打日志即可。
7. **配置热重载**（`main.cc:498-503`）：ROI 多边形变化时**重建** `counter`（`counter = track_count::TrackCounter(tcCfg);`）
   并同步 `tracker.reset();` —— 代价是一次身份/访客计数清零（ROI 变更是低频事件，可接受）。
   **重要**：任何地方调用 `enhancedDecision.reset()` 时，必须同时 `tracker.reset(); counter.reset();`，
   否则 ByteTrack 的 `track_id` 从 0 复用而身份映射错位。
8. **可选日志/自检**：启动时打印
   `fprintf(stderr, "[TRACK] bytetrack=%d/%d/%d/%d/%d counter(dedup=%.2f,foot=%.2f,reid=%.2f,dwell=%d,min_n=%d)\n", ...)`，
   以及 `assert(ByteTrack::paramsValid(...))` —— 阈值写反时早暴露（旧实现是静默跑偏）。
9. **性能**（主机口径估算，**未上板实测**）：每帧 O(T²) 次脚点比较 + `T` 次 `std::map` 操作
   （T = 活跃轨迹数，演示场景 T ≤ 30），远小于 91.5ms 的 NPU 推理；无动态内存增长
   （身份记录有 `identity_ttl` 回收）。

---

## 8. 局限与未验证项（诚实标注）

1. **未上板**：开发板 192.168.0.136 SSH 不可用（TCP22 无 banner），本报告所有数字都是
   **主机 g++ / CPython 口径**，板端交叉编译后的耗时/行为未实测。
2. **数据是合成框序列**（`tests/`，纯测试数据），不是真实摄像头序列；场景是人工构造的
   "最不利形态"（同人 3 框、6cm 脚点偏移、丢失 12 帧后跳变）。真实视频里这些比例是多少**未测**。
3. **阈值是物理推断 + 合成序列标定，未用真实视频标定**：`dup_foot_max=20cm`、
   `dup_iou_min=0.75`、`dup_height_ratio=1.5`、`reid_radius=0.6m`、
   `reid_max_gap=15` 帧。其中 `reid_radius` 与 `coast_frames` 与板端 11fps（91.5ms/帧）
   的实际漏检分布没有对应实测；建议上板后按"同一人两个框的脚点距离分布"做一次直方图标定。
4. **误并风险**：粘性合并在极端拥挤/遮挡下可能把两个贴得极近的人并成 1 个（欠计 1）。
   第 3 节的三判据（高度比 + 中心互含 + 20cm 脚点界）是为此设的，但**没有真实拥挤场景的
   误并率实测**；`dup_iou_min=0.75` 可上调到 0.85 保守化（代价：6cm 级别的重复框会漏判）。
5. **`min_hits=3` 的计数时延**：新目标最多 3 帧后才计入人数（约 0.27s @11fps）。
   演示若要求"立刻计数"，可把 `min_hits` 降到 2，或用 `count_tentative=true`（会引入抖动）。
6. **ROI 判定单位是像素**：相机位姿/分辨率变化后 `roi_hysteresis=8px` 的物理含义会变；
   与 `config/default_config.json` 的 `roi_zone` 同坐标系。
7. `n_spatial_dedup`（消融口径）只复刻了"同帧 2cm 脚点贪心去重"，与
   `graph_laplacian.h` 的实现同口径但**不是同一段代码**，用于对比趋势而非逐值对齐。

---

## 9. 复现命令（主机）

```powershell
# 1) 全量 Python 测试（含 C++/Python 逐值 parity，需要 g++）
cd G:\PedestrianRiskDecision
& X:\py311_gpu\Scripts\python.exe -m pytest tests/ -q

# 2) 报告量化表（-s 才打印表格）
& X:\py311_gpu\Scripts\python.exe -m pytest tests/test_track_counter.py::TestReportTable -q -s

# 3) ByteTrack 真实序列差分（8 场景）
& X:\py311_gpu\Scripts\python.exe -m pytest tests/test_bytetrack_sequences.py -q

# 4) 板端主机测试（mingw 工具链需在 PATH，否则 make 找不到 x86_64-w64-mingw32-g++）
$env:Path = 'G:\DevTools\mingw\w64devkit\bin;' + $env:Path
cd src\luckfox_rtsp_yolov5; mingw32-make test

# 5) 头文件零告警自检
& G:\DevTools\mingw\w64devkit\bin\g++.exe -std=c++11 -Wall -Wextra -Wpedantic -Wshadow `
    -fsyntax-only -I src\algorithms -x c++ - <<< '#include "track_counter.h"
#include "bytetrack_tracker.h"'
```

> `mingw32-make test` 直接跑（不带 `CXX=g++`）在**没有把 w64devkit\bin 加进 PATH** 时会失败：
> make 调用 git 的 `sh` 去找 `x86_64-w64-mingw32-g++` 而 PATH 里没有。加 PATH 后退出码 0
> （本次验证即如此）。这是环境问题，不是代码问题。
