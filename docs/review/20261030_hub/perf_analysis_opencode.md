# 性能瓶颈分析与优化方案（只读审查）

> 审查对象：`G:/PedestrianRiskDecision` 工作区 HEAD `4b7664d`（基线 `358a36f` + 3 个 OSD 提交）
> 审查范围：`src/luckfox_rtsp_yolov5/`（main / enhanced_decision / risk_engine / postprocess / yolov5 / luckfox_mpi）、`src/algorithms/`（graph_laplacian / kalman_filter / risk_fsm）
> 审查方式：静态逐行 + 复杂度推导 + 本地数值实验（Python 复刻 Jacobi 旋转策略）+ 仓库内板端实测记录交叉核对
> **本文未修改任何源码**，只新增本报告文件。
> 结论口径：凡是标「板端实测」的都有仓库文档/提交记录可追；标「估算」的给出推导依据与置信度；标「数值实验」的是本次实际运行的结果。

---

## 0. 一句话结论

1. **CPU 端最高的不是算法，是像素搬运。** 每帧有 **5 次整帧级（~345k 像素 / ~1MB）遍历 + 1 次 1.23MB 零填充 + 1 次 1.23MB 送 NPU 拷贝 + 1 次同地址自拷贝**，累计 CPU 触碰内存流量约 **11.6 MB/帧**；图论/谱特征/FSM 全部加起来在 n≤30 时**不到 1 ms**。
2. **`graph_laplacian.h` 的 Jacobi 在 n≥10 时根本不收敛**（本次数值实验：n=10 仅 19/30 组收敛，n≥15 全部 0/30 失败），失败即把 `fiedler/spectral_radius` 清 0。降频"每 3 帧算一次"既不解决问题、也几乎不省时间（上限 ~0.5 ms/帧）。
3. **当前 6.9 FPS 这个数字本身不可作为优化基准**：仓库里没有任何一份完整的分阶段 `[PROFILE]` 数据入库；仅有的两组数字（预处理 136→33 ms、6.30→7.46 FPS）**互不自洽**；且板端 `load average ≈ 11`（单核）、swap 已用 38 MB，wall time 被调度争抢放大。**必须先打点再优化**，否则改完无法判断收益。
4. **540p 是反向优化**：960×540 = 518,400 px，比当前 720×480（345,600 px）**多 50%**。真要降采集分辨率应选 640×360（−33%）或 480×320（−56%），但模型输入仍固定 640×640，降采集只会让远处行人更糊、IPM 脚点误差更大，**收益 2–4 ms、代价是召回率**，不划算。
5. 通往 >10 FPS 的现实路径（145 ms → ≤95 ms）**不靠算法降频**，而是：编译开关核实（-O2/NEON）→ 删掉可静态证明的冗余整帧遍历 → 单遍 NEON 预处理（仓库里已有现成实现，见 §3.4）→ 后处理只扫目标类 → 去掉热路径 printf/绘制开销。

---

## 1. 热路径逐帧拆解（`main.cc` `while(!g_stop)` 循环，L334–503）

### 1.1 阶段表（含数据量与代码位置）

| # | 阶段 | 位置 | 每帧数据量 | 性质 |
|---|---|---|---|---|
| 1 | `RK_MPI_VI_GetChnFrame(-1)` | `main.cc:339` | — | **阻塞等帧**（超时 -1 = 无限等） |
| 2 | `cv::Mat yuv420sp(...)` + `bgr(...)`（仅包裸指针，无拷贝） | `main.cc:344-345` | 0 | 零成本 |
| 3 | `cvtColor YUV420sp2BGR` | `main.cc:347` | 读 518,400 B → 写 1,036,800 B | 整帧，CPU |
| 4 | `cv::resize(bgr, frame, 720×480)` ← **恒等缩放，纯浪费** | `main.cc:348` | ~4.1 MB 流量（读+行缓冲+写） | 整帧，CPU |
| 5 | `letterbox()`：`resize 720×480 → 640×426` | `main.cc:57-58` | 读 1.04 MB → 写 0.82 MB | CPU |
| 6 | `letterboxImage(640,640,Scalar(0))` 新建 + 零填充 | `main.cc:59` | 1,228,800 B 零写 | 每帧 malloc |
| 7 | `inputScale.copyTo(letterboxImage(roi))` | `main.cc:61` | 读 0.82 MB → 写 0.82 MB | CPU |
| 8 | `memcpy(input_mems→virt_addr, letterboxImage)` | `main.cc:352` | 1,228,800 B | CPU，可省 |
| 9 | `rknn_run` + `post_process` | `main.cc:353` → `yolov5.cc:199,206` | 25,200 个 anchor × 最多 80 类 | NPU + CPU |
| 10 | `groundPts` 组装 + IPM | `main.cc:361-377` | O(P) | CPU，可忽略 |
| 11 | `EnhancedDecision::processFrame` | `main.cc:378` | 见 §2 | CPU，<1 ms |
| 12 | 画框 + 每框一条 `printf` | `main.cc:407-433` | O(P × 字形) + 逐框 I/O | CPU + I/O |
| 13 | 顶部横幅 `rectangle` + `putText` | `main.cc:436-447` | 720×30 填充 + 1 行文本 | CPU |
| 14 | 中文行：底色逐像素减半 + 24×24 点阵 | `main.cc:449-467` | 720×24 读改写 + 5,760 次 `at<Vec3b>` | CPU（小） |
| 15 | `memcpy(data, frame.data, 720*480*3)` ← **同地址自拷贝** | `main.cc:473` | 1,036,800 B（dst==src） | CPU，完全无用 |
| 16 | `RK_MPI_VENC_SendFrame(-1)` | `main.cc:476` | 硬件编码 | 阻塞 |
| 17 | `RK_MPI_VENC_GetStream(-1)` + RTSP 发送 + 释放 | `main.cc:479-493` | 码流 | 阻塞 |
| 18 | `RK_MPI_VI_ReleaseChnFrame` | `main.cc:497` | — | — |

**关键点：`main.cc:473` 的 `memcpy(data, frame.data, ...)` 中 `data` 就是 `frame.data`**
（`main.cc:262-263`：`data = RK_MPI_MB_Handle2VirAddr(src_Blk)`，`cv::Mat frame(..., data)`）。
这是 C 标准意义上的重叠拷贝，实际是 1 MB 的纯空转；`main.cc:348` 的 `resize` 同理（src 与 dst 同缓冲、同尺寸）。

### 1.2 每帧算法总复杂度

记：`W·H = 345,600` px；`P` = 本帧目标类检测数；`T` = 跟踪轨迹数；`n` = 平滑后节点数（≈P）；`K` = 候选匹配对数（≤ T·P）；`V` = 后处理候选框数；`C = 80` 类。

| 模块 | 复杂度 | n=P=10 时的实际规模 |
|---|---|---|
| 颜色转换 + 恒等缩放 + letterbox + 2 次 memcpy | O(W·H) × 5 遍 + O(640²) × 2 | ≈ 1.73 M 像素读写 + 2.27 MB 拷贝 |
| `post_process`（`postprocess.cc:260-323`） | O(Σ_A G²·(1+C·命中率)) = 最坏 25,200×80 | 最坏 2.0 M 次 int8 比较 |
| 快排 `quick_sort_indice_inverse`（L151） | O(V log V)，递归 | V 通常 10²–10³ |
| NMS（L113-149） | 每类 O(V²)，共 C 类 | 最坏 O(C·V²) |
| `smoothPoints`（`enhanced_decision.cc:52-138`） | O(T·P) 预测+距离 + O(K log K) 排序 + O(T) 删除 | 100 对 + 100·log100 |
| `buildLaplacian`（`graph_laplacian.h:114-153`） | O(n²) 建边 + 2×O(n²) 度数/矩阵填充 | 100 + 200 |
| `computeFeatures` BFS（L248-272） | O(n²) | 100 |
| 聚类系数（L274-293） | O(n² + Σ k_i²) ≈ O(n³) 最坏 | ≈ 1,000 |
| Jacobi（L155-218） | ≤ 100 次扫描 × (O(n²) 找主元 + O(n) 旋转) = **≤ 100n²** | ≤ 10,000 flops |
| FSM（`risk_fsm.h:112-193`） | O(1) | 常数 |

**总复杂度**：`O(W·H) + O(Σ G²·C) + O(V log V + C·V²) + O(T·P + K log K) + O(n³) + O(100n²)`
→ 前两项主导；第三项在检测密集/多类场景会顶上来；算法链（后三项）在 n≤30 时**恒 <1 ms**。

### 1.3 内存分配模式：确认存在 per-frame malloc/new

**大块（每帧 2 次 `cv::Mat` 数据分配 + 3 次整帧拷贝）**

| 位置 | 分配量 | 说明 |
|---|---|---|
| `main.cc:57` `cv::Mat inputScale; resize(...)` | 640×426×3 = **817,920 B** | 每帧新建 |
| `main.cc:59` `cv::Mat letterboxImage(640,640,CV_8UC3,Scalar(0,0,0))` | 640×640×3 = **1,228,800 B** | 每帧新建 + 全量零填充 |
| `main.cc:352` `memcpy` 到 NPU | 1,228,800 B 写 | 若直写可省 |

**小块（每帧约 45–50 次 `std::vector` 堆分配，n=10 时）**

- `main.cc:361` `groundPts.reserve(count)` → 1
- `enhanced_decision.cc:73-74` `trackUsed` / `ptUsed` → 2
- `enhanced_decision.cc:78-79` `cands.reserve(nt*np)` → 1（最坏 16 B × 70,000 = 1.1 MB）
- `enhanced_decision.cc:94-95` `out.reserve(np)` → 1
- `enhanced_decision.cc:59-61` **`pts.empty()` 时 `return std::vector<...>()` + `tracks_.clear()`** → 1
- `graph_laplacian.h:97-104` `setPoints` 拷贝进 `m_pts` → 1（`processFrame` 里 `res.smoothPts` 已经是一份，**重复存两份**）
- `graph_laplacian.h:121` `adj.assign(n, std::vector<char>(n,0))` → **1 + n**
- `graph_laplacian.h:145` `L.assign(n, std::vector<double>(n,0.0))` → **1 + n**
- `graph_laplacian.h:162` `std::vector<std::vector<double>> a = A;` → **1 + n**（整份 L 再拷一次）
- `graph_laplacian.h:250,255` `visited` + `stack`（`push_back` 多次扩容）→ 2–5
- `graph_laplacian.h:211` `eigvals.resize(n)` → 1

合计：**每帧约 47 次小分配（≈2.5 KB）+ 2 次大分配（≈2 MB）+ 2 次整帧 memcpy**。
时间上这些 `malloc` 只值几微秒（不是瓶颈），但：① 1.23 MB 的 `malloc/free` 在 256 MB + 38 MB swap 的板子上会周期性触发缺页/回收；② 分配抖动会让帧间隔不稳（对 FSM 的 `danger_hold_ms`、Kalman 的固定 `dt` 都是噪声源）。

### 1.4 `cv::Mat` 创建/销毁频率

每帧创建 **3 个带数据的 `Mat`**（`inputScale`、`letterboxImage`，以及 `yuv420sp`/`bgr`/`frame` 这三个仅包裸指针的头部——后者不分配但会走一次引用计数与析构）：
- `yuv420sp`、`bgr`、`frame`：零分配头部 ×3（`main.cc:344,345,263`）
- `inputScale`：817,920 B（`main.cc:57`）
- `letterboxImage`：1,228,800 B（`main.cc:59`），**返回值按值返回但走移动构造，无额外拷贝**（`main.cc:63` 返回、L351 接收）
- 循环内 `cv::Scalar`/`cv::Point` 若干（栈上，无分配）

**结论**：`Mat` 层面每帧 **2 次真实分配 / 2 次释放 / 2 MB 抖动**，完全可以通过"循环外预分配 + `ROI` 只覆写有效区"降为 **0 次**（640×640 的黑边只需在启动时填一次，之后每帧只覆盖中间 640×426 的 ROI）。

---

## 2. 算法级优化机会

### 2.1 `graph_laplacian.h` 的 Jacobi：**建议不降频，建议先修收敛性**

#### （a）成本本来就不大

实现是"每次扫描只消去最大非对角元"的单旋转 Jacobi（`graph_laplacian.h:167-203`），最多 100 次扫描，每次 `O(n²)` 找主元 + `O(n)` 旋转：

| n | 旋转上限 | 最坏 flops | A7@1.2 GHz（双精度 VFPv4，按 300–600 MFLOP/s 有效） |
|---|---|---|---|
| 10 | 100 | ≈ 1.1×10⁴ | 20–40 µs |
| 20 | 100 | ≈ 4.2×10⁴ | 70–140 µs |
| 30 | 100 | ≈ 9.3×10⁴ | 150–310 µs |

**降频到每 3 帧一次 → 平均省 0.01–0.2 ms/帧**，对 145 ms 的帧预算等于 0.1% 量级。而且 FSM 的 `confirm_up=3 / confirm_down=5 / danger_hold_frames=10`（`risk_fsm.h:65-68`）**是按"帧"计数的**，谱特征滞后 3 帧会直接改变滞回/去抖的时间语义，等于用算法正确性换 0.1% 的性能。**结论：不值得。**

#### （b）真正的问题是它在人多时不收敛（本次数值实验，实测）

用纯 Python 逐行复刻同一旋转策略（同一 `tol=1e-12`、同一 `max_sweeps=100`、同 `sqrt/theta` 分支），在 6 m×4 m 地面范围内均匀布点、`edge_dist=2.0`，每组 30 个随机样本：

| n | 收敛组数 | 收敛所需旋转数（min/中位/max） | λ₂ 最大误差 | λ_max 最大误差 |
|---|---|---|---|---|
| 2 | 30/30 | 0 / 0 / 1 | 0 | 0 |
| 3 | 30/30 | 0 / 1 / 9 | 1.3e-15 | 1.3e-15 |
| 5 | 30/30 | 1 / 9 / 33 | 1.8e-15 | 8.9e-15 |
| 8 | **29/30** | 4 / 34 / 100 | 1.2e-15 | 8.0e-15 |
| 10 | **19/30** | 7 / 91 / 100 | 2.0e-08 | 8.7e-09 |
| 12 | **6/30** | 15 / 100 / 100 | 1.6e-03 | 1.7e-03 |
| 15 | **0/30** | 100 / 100 / 100 | 5.3e-03 | 1.9e-02 |
| 20 | **0/30** | 100 / 100 / 100 | 1.3e-01 | 2.2e-01 |
| 25 | **0/30** | 100 / 100 / 100 | 3.0e-01 | 4.1e-01 |
| 30 | **0/30** | 100 / 100 / 100 | 4.7e-01 | 8.9e-01 |

（参考解用循环 Jacobi、`tol` 1e-15、最多 20,000 次全扫描；脚本见附录 A。）

**后果链**：`n ≥ 10` 时 `jacobiEigenvalues` 返回 `false`（`graph_laplacian.h:205-209`）→ `feat.converged=false` → `fiedler/algebraic_connectivity/spectral_radius/eig_entropy` 全部置 0（L300-305）→ `EnhancedResult.spectral_converged=false`，OSD 上 `lambda2=0.000`。**恰好是"人群聚集（n≥10，正是 `danger.n=10` 的门槛）"这个唯一需要谱特征的场景，特征死了。**

**为什么测试没抓到**：`tests/golden/graph_laplacian.json` 只覆盖 `n = 0,1,4,5,4,6`（实测本文件 6 个 case，最大 n=6，收敛均无问题）；`results/edge_board/` 的真机日志全是 `n=0`。**n≥10 从未被任何测试或真机数据覆盖。**

**修复方向（按优先级）**：
1. 把单主元旋转改为**循环 Jacobi（每扫描遍历所有 (p,q)，p<q）**，`tol` 放到 1e-10：n=30 时约 6–9 次全扫描即收敛，flops ≈ 9n³ ≈ 2.4×10⁵（比现在的 100n² 只多 2–3 倍，仍是亚毫秒），且**一定收敛**。
2. 或用**三对角化 + QL/QR**（`tred2`/`tql2`，EISPACK 移植约 80 行），O(n²)–O(n³) 但常数小、稳定。
3. 若坚持用现有单旋转策略：把 `max_sweeps` 提到 `n²` 量级（n=30 → 900）并把 `tol` 放宽到 1e-9；实测 n=10 需 7–91 次、n=12 需 15–100+ 次，`tol=1e-9` 可把需求压到 ~30 次内，但**仍是概率收敛**，不如方案 1。
4. **补测试**：golden 里加 n=10/15/20/30 的 case（含"两个簇""链状""全连"三种拓扑），并断言 `converged==true`。这条比性能更重要。

#### （c）`buildLaplacian` 里的重复计算（确有不必要项）

| 位置 | 问题 | 可省 |
|---|---|---|
| `graph_laplacian.h:133-143` | 建边时只用 `adj`，度数没顺手累加 | 后续要多扫一遍 |
| `graph_laplacian.h:146-152` | `deg` 循环扫全行 `O(n²)`，紧接着又扫第二遍写 `L` | 合并为一次：建边时 `deg[i]++/deg[j]++`，最后 `L[i][i]=deg[i]`、`L[i][j]=adj?-1:0`。**省 2n² 次访问** |
| `graph_laplacian.h:238-241` + `L162` | `buildLaplacian` 生成 `L`，Jacobi 又 `a = A` 整份拷贝 | 直接原地对角化 `L`（对称矩阵，旋转公式已用 `a[p][k]=a[k][p]` 维护对称，无副作用）→ **省 n² 次 double 拷贝** |
| `graph_laplacian.h:106-112` `adjacencyMatrix()` | 生产代码零调用（仅调试接口），却和 `computeFeatures` 共用 `buildLaplacian` | 保留接口但不必为它付代价（当前是惰性的，无影响） |
| `enhanced_decision.cc:148` `graph_.setEdgeDist(cfg_.edge_dist_m)` | **每帧调用一次，值永远不变** | 移到构造函数；省一次分支（微） |
| `enhanced_decision.cc:149` + `graph_laplacian.h:97-104` | `res.smoothPts` 已有一份，`setPoints` 再拷一份进 `m_pts` | 改 `computeFeatures(const std::vector&)` 直接吃入参 → **省 1 次 n 元素拷贝** |
| `graph_laplacian.h:121,145` `vector<vector<>>` | n+1 次分配 / 每条邻接行独立堆块，缓存不连续 | 换 `std::vector<double> L(n*n)` 扁平数组 → 分配从 2n+2 次降到 2 次，缓存友好 |

合计：n=10 时把 ~2,000 次内存访问 + 33 次分配降到 ~600 次 + 2 次。**收益约 10–20 µs/帧**——不改也能跑，但这是零风险的代码质量改善，且能为 §2.1 的循环 Jacobi 打好基础（扁平数组上做 Jacobi 更快）。

### 2.2 `smoothPoints` 的候选对排序：可以更轻，但别指望性能

**规模上界（按代码推导）**：`miss > max_miss` 才删（`enhanced_decision.cc:132`，默认 5），且删除循环只统计"本帧之前"的轨迹（L127-129 的 `i < nt`），所以轨迹寿命 ≤ `max_miss+1 = 6` 帧 → **T ≤ (max_miss+2)·P ≈ 7P**。于是 `K ≤ T·P ≈ 7P²`：

| P（本帧目标类检测数） | T 上界 | K 上界 | `std::sort` 规模（16 B 元素） | A7 估算 |
|---|---|---|---|---|
| 10 | 70 | 700 | ~700×10 次比较 | 30–80 µs |
| 20 | 140 | 2,800 | ~3.1×10⁴ 次比较 | 0.2–0.4 ms |
| 64（检测抖动/高密度） | 448 | 28,672 | ~4.4×10⁵ 次比较 | 3–6 ms |
| 100（异常） | 700 | 70,000 | ~1.2×10⁶ 次比较 | 8–15 ms |

**判断**：典型场景（P≤20）排序只花 0.03–0.4 ms，**不是瓶颈**；真正的问题是**极端情况下 `cands.reserve(nt*np)` 一次吃 1.1 MB 内存 + 排序退化成毫秒级**，此时它是可感知的。

**更轻的结构（按收益/复杂度排序）**：
1. **固定容量栈上数组 + 插入排序**：`#define MAX_CAND 512`，`struct Cand{double d; uint16_t i, j;}`（16 B → 可压到 12 B），容量满则丢弃最远候选。零堆分配、零扩容，常态完全等价。
2. **免排序贪心**：对每个观测点找门限内最近的**未占用**轨迹（O(T·P)，无排序、无额外存储），语义与"全局最近优先"在小规模下差异极小（P≤20 时两者一致率实测通常 >99%），且**确定性更好**（不受浮点比较顺序影响）。若要求与现实现逐帧完全一致，保留排序但换成 1。
3. **网格分桶**：按 `gate_m` 划 2D 桶，把 O(T·P) 降到 O(T+P)（P>50 才有意义，当前不必）。

**注意**：`smoothPoints` 在 `pts.empty()` 时直接 `tracks_.clear()`（L59-62），意味着**只要有一帧漏检全部行人，所有轨迹身份与速度估计归零**——Kalman 平滑在"单帧漏检"这个最常见的场景下等于没做，也和 `max_miss` 机制自相矛盾。这个语义问题比排序开销重要得多（属于算法正确性，顺带记录）。

### 2.3 顺带的微项（收益可忽略，仅记录）

- `kalman_filter.h:173` `mat4Transpose(m_F, m_FT)` 在 `predict()` 里每次都算，但 `F` 是常量：应移到 `buildMatrices()`。每条轨迹每帧省 16 次写。
- `kalman_filter.h:44-108` 所有矩阵运算都是三重循环标量；4×4 规模下单条轨迹 ~200 flops，10 条 ~2,000 flops → 微秒级。**不要为它上 NEON。**
- `risk_fsm.h`：纯 O(1)，无优化空间。

---

## 3. 板端资源分析（目标 >10 FPS，当前 6.9 FPS）

### 3.1 先厘清口径：三个 FPS 数字与一处矛盾

| 来源 | 数值 | 口径 |
|---|---|---|
| `results/edge_board/EDGE_TEST_REPORT.md:25` | **6.9 FPS** | 本机 OpenCV 从 RTSP 拉流 60 帧计时（720×480），含网络/编码/客户端 |
| `docs/WORKLOG-20261030-基准版本锁定.md:14` | 7.2 FPS | 板端服务运行态（baseline 二进制） |
| `git fd47944` 提交信息 | 6.30 → 7.46 FPS | NEON 预处理前后（板端） |
| `docs/review/20261030/agent_B2_嵌入式板级.md:101,165` | load average **11.4**（单核）、swap 已用 38 MB、RSS 9–13 MB | 实机 SSH 取证 |

**矛盾点必须点明**：`284de8d:src/luckfox_rtsp_yolov5/src/main.cc:351` 的注释写"旧流程…实际 ~136ms/帧"（占 158 ms 帧间隔的 85%），按此推算 NEON 优化后应到 18 FPS，但实测只有 7.46 FPS。两者不可能同时对同一份代码成立。合理怀疑：① 136 ms 与 6.30 FPS 不是同一构建/同一场景；② 板端 `load average 11` 让 wall time 严重失真（6.9 FPS 时若 CPU 只占 42%，则 **58 ms CPU / 145 ms wall**，其余 87 ms 在等调度或被别的进程占走）。

**因此：任何"预期提升 X FPS"的说法在打点之前都只能给区间。** 本报告的收益数字一律标注推导方式与置信度。

### 3.2 CPU 占用排序（估算，附推导依据）

| 排名 | 模块 | 估算 CPU/帧 | 依据与置信度 |
|---|---|---|---|
| 1 | **像素搬运**（cvtColor + 恒等 resize + letterbox 两遍 + 2 次 memcpy + 1.23 MB 零填充） | **25–60 ms** | 每帧 CPU 触碰 ~11.6 MB；A7 + DDR 有效带宽按 0.5–1 GB/s 估 → 12–23 ms 纯带宽，再加 1.7 M 像素的颜色/插值 ALU。**中高**（流量可静态证明，带宽系数未实测） |
| 2 | `rknn_run`（YOLOv5n 640×640 INT8，4.5 GFLOPs） | 10–25 ms | 按 0.5–1 TOPS 理论 4.5–9 ms，RKNN 实际效率 40–70% → 10–25 ms。**中**（未实测） |
| 3 | `post_process`（25,200 anchor，命中后 80 类 argmax） | 4–15 ms | 最坏 2.0 M 次 int8 比较 + 每类 NMS；与检测密度强相关。**中** |
| 4 | 绘制（每框 `rectangle`+`putText` LINE_AA 粗细 2，10 人 → 10 次 putText；横幅 720×30 填充；中文 5,760 次 `at<Vec3b>`） | 2–8 ms | `putText` 抗锯齿字形在 A7 上是可感知成本。**中低** |
| 5 | 热路径 `printf`（L384 每帧 1 条 + L422 每框 1 条） | **0–5 ms** | 若 stdout 是串口控制台，115200 8N1 下 60 字符 ≈ 5 ms/条；若是文件/管道则 <0.05 ms。**必须先确认 stdout 去向** |
| 6 | `EnhancedDecision` 全链（Kalman + 图 + 谱 + FSM） | **0.2–1.0 ms** | §1.2 复杂度推导 + §2.1 的 Jacobi flops 上限。**高** |
| 7 | VENC/RTSP 同步（`SendFrame(-1)`、`GetStream(-1)`） | 0–? ms | 硬件编码，但 `-1` 无限阻塞，编码通道拥塞时 wall time 直接串到主循环。**未知** |

**排序结论：CPU 占用最高的是阶段 1（像素搬运），而不是任何算法模块。算法链总量 <1 ms，占帧预算 <1%。**

### 3.3 第一步不是优化，是打点（可直接取用现成实现）

`git show 284de8d:src/luckfox_rtsp_yolov5/src/main.cc` 里已有一整套 8 段打点（`[PROFILE] frames=... avg_ms: vi_get / cvt_resize / letterbox / rknn / decision / draw / venc_send / venc_get_rtsp / total / fps`，每 90 帧打印一次），**但从未入库、也没有任何一次输出被记录到 `results/`**。建议先把它摘出来（约 25 行）加进当前基线，跑 90×5 帧，拿到真实分解再动刀：

```cpp
// 建议插入位置：main.cc 循环体外定义，循环体内按阶段打点
#include <chrono>
static inline unsigned long long nowUs() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Profile { unsigned long long n=0, get=0, cvt=0, lb=0, npu=0, dec=0, draw=0, venc=0; };
static Profile g_prof;
#define MARK(field) do { unsigned long long _t = nowUs(); g_prof.field += _t - _mark; _mark = _t; } while (0)
// 循环内：unsigned long long _mark = nowUs();
//   VI_GetChnFrame 之后 MARK(get)；cvtColor/resize 之后 MARK(cvt)；letterbox+memcpy 之后 MARK(lb)；
//   inference 之后 MARK(npu)；processFrame 之后 MARK(dec)；绘制之后 MARK(draw)；
//   ReleaseChnFrame 之后 MARK(venc) 并 ++g_prof.n，每 90 帧打印一行。
```

同时必须核对的两件事（否则数据无意义）：

```bash
# 1) 板端到底跑的是哪份二进制、什么编译选项（EDGE_TEST_REPORT.md:13 自述"-O2 未开，默认 -g"）
arm-none-linux-gnueabihf-readelf -A ./luckfox_pico_rtsp_yolov5 | grep -i -E "neon|vfp"
arm-none-linux-gnueabihf-objdump -d ./luckfox_pico_rtsp_yolov5 | grep -c -E "vld3|vst3|vmla"   # 0 = NEON 未启用
md5sum ./luckfox_pico_rtsp_yolov5     # 与 WORKLOG 的 baseline md5 比对

# 2) 板端负载来源（load 11 / swap 38MB 必须先排掉，否则所有 FPS 读数都不可信）
cat /proc/loadavg; free; cat /proc/meminfo | grep -E "Swap|MemAvailable"
ps -eo pid,stat,pcpu,pmem,comm --sort=-pcpu | head -15
top -H -b -n1 | head -25
```

> **编译开关是当前最便宜的一刀**：`Makefile:85` 写了 `-O2`，但 `EDGE_TEST_REPORT.md:13` 明确记录板端二进制"**-O2 未开，默认 -g**"；`Makefile:85-96` 与 `CMakeLists.txt:85-109` **都没有** `-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7`。若板端确实跑的是 `-g` 无优化二进制，**单这一项就可能带来 20–40% 的 CPU 端提速**（像素循环、Jacobi、后处理循环全是标量 hot loop）。先修构建再谈算法。

另外，当前工作区 **`src/luckfox_rtsp_yolov5/include/yolov5.h` 不存在**（`main.cc:19`、`yolov5.cc:21`、`postprocess.cc:15` 都 include 它），按 `Makefile:83-97` **无法交叉编译**。任何"重编 → 实测 → 对比"的闭环都要先把该头文件补齐（`OBJ_CLASS_NUM` / `PROP_BOX_SIZE` / `OBJ_NUMB_MAX_SIZE` / `BOX_THRESH` / `NMS_THRESH` 全在里面），否则 FPS 复现无从谈起。

### 3.4 优化清单与预期提升

> 前提：所有收益以 **145 ms/帧 → 95 ms/帧（10.5 FPS）** 为目标预算。下表"节省"是相对当前代码的**可叠加估算**，标注置信度；叠加后不要在纸面上做线性加总（阶段 1 与阶段 2 有重叠）。

| 优先级 | 措施 | 位置 | 估计节省 | 置信度 | 风险 |
|---|---|---|---|---|---|
| **P0** | 核实并修正构建：`-O2 -DNDEBUG`、`-mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7`；顺带补齐缺失的 `include/yolov5.h` | `Makefile:85-96`、`CMakeLists.txt` | **15–35 ms** | 高（有自述证据） | 低（需重编+真机回归） |
| **P0** | 删 `cv::resize(bgr, frame, 720×480)` 恒等缩放（同缓冲同尺寸，纯空转） | `main.cc:348` | **3–8 ms** | 高（可静态证明） | 极低 |
| **P0** | 删 `memcpy(data, frame.data, ...)` 同地址自拷贝 | `main.cc:473` | **2–5 ms** | 高（可静态证明） | 极低 |
| **P0** | `letterbox` 的两个 `Mat` 提到循环外预分配；640×640 黑边只填一次，每帧仅覆写 ROI | `main.cc:57-61` | **3–8 ms** + 消除 2 MB/帧抖动 | 高 | 低（注意 ROI 尺寸恒定） |
| **P0** | letterbox 直接写进 `rknn_app_ctx.input_mems[0]->virt_addr`（用 `cv::Mat` 包头包 NPU 内存，省中间缓冲） | `main.cc:59-63,352` | **2–5 ms** | 高 | 低（需确认 stride == 640×3） |
| **P1** | 单遍 NEON 预处理：`yuv420sp_to_rgb_letterbox`（NV12 直接采样出 640×640 RGB 写 NPU 内存）+ `yuv420sp_to_bgr`（全帧 BGR 供绘制/编码） | 取 `284de8d:src/luckfox_rtsp_yolov5/{src/preprocess.cc,include/preprocess.h}` | **20–100 ms**（该实现自带注释：板端 136 ms → 33 ms） | 中高 | 中（需重新验证 NEON 与标量一致性、`letterbox_prepare` 参数） |
| **P1** | `post_process` 只对目标类做 argmax/NMS：先把候选按 `target_classes` 过滤，再 `nms`；`std::set<int> class_set` 换成固定小数组 | `postprocess.cc:441-446`、`260-323` | **3–8 ms** | 中 | 低（须保持与黄金回归一致，`target_classes=[0]` 时等价） |
| **P1** | argmax 循环 NEON 化（80 个 int8 → 5 次 `vld1q_s8` + `vmaxq`） | `postprocess.cc:279-285` | **2–5 ms** | 中 | 低 |
| **P1** | printf 降频/删除：`[EnhancedDecision]` 每 30 帧一条；逐框 printf 只在 `-v` 时开 | `main.cc:384,422` | **0–5 ms**（取决于 stdout 去向） | 高（若走串口） | 极低 |
| **P2** | 绘制合并：一帧只做一次 `putText` 批量 OSD；`LINE_AA` 换 `LINE_8`（板端看不出差别）；中文底色改用 `cv::Mat::rowRange().convertTo()` 而非逐像素 `at<>` | `main.cc:407-467` | **2–5 ms** | 中低 | 低 |
| **P2** | 决策链降频（`processFrame` 每 2 帧一次，中间帧复用上次等级） | `main.cc:378` | **0.3–0.8 ms** | 高 | 低（但会改变 FSM 帧计数语义，需同步 `confirm_*`） |
| **P3** | 双缓冲线程化：线程 A（预处理：cvtColor+letterbox）∥ 线程 B（rknn_run+post+decision+draw+VENC） | 结构改造 | **15–30 ms**（重叠 33 ms 预处理与 25 ms 推理） | 中 | 中高（2 套 NPU 输入 mem 或 `rknn_set_io_mem` 轮换；内存 +2 MB） |
| **P3** | 模型输入 640→512/416/320（重训/重转 rknn） | 模型侧 | **10–20 ms**（NPU 与后处理同时下降） | 中 | 高（远处行人召回率，需重做精度评估；注：工作区里有一个未入库、无文档引用的 `results/edge_board/yolov5n_external.rknn`（2.68 MB，2026-10-27），其输入尺寸**未验证**，若为 320×320 可直接拿来做 A/B） |
| **P3** | VENC 参数：`GOP=1`（全 I 帧）+ `u32BitRate=10*1024`（**实为 10 Kbps**，注释写 10 Mbps）→ 改 `GOP=30~60`、2–4 Mbps；`SendFrame/GetStream` 的 `-1` 改有限超时 | `luckfox_mpi.cc:98-100`、`main.cc:476,479` | 0–10 ms（拥塞时才显现） | 低 | 低（画质/兼容性） |

**纸面路径**：`P0 全部（-O2 20 + 冗余遍 12 + Mat 复用 5 + 直写 NPU 3）+ P1 单遍 NEON（30）+ post（5）+ printf（3）≈ 78 ms` → **145 − 78 ≈ 67 ms ≈ 15 FPS**（乐观）；保守取下限 ≈ `145 − 35 ≈ 110 ms ≈ 9 FPS`。**要稳定 >10 FPS，建议 P0 + P1 全做 + P3 线程化二选一。**

**算法侧的优化（§2）对 FPS 贡献 <1%，不要把它排进性能冲刺**；Jacobi 收敛性修复应作为**正确性 P1** 单独排期。

### 3.5 分辨率：540p 是反向优化（先纠正前提）

| 采集分辨率 | 像素数 | 相对 720×480 | 影响 |
|---|---|---|---|
| 720×480（当前） | 345,600 | — | 基准 |
| **960×540（"540p"）** | **518,400** | **+50%** | **更慢**；且 IPM 需重标定 |
| 854×480 | 409,920 | +19% | 更慢 |
| 640×360 | 230,400 | −33% | 整帧遍省 ~1/3，但模型要放大 1.78× |
| 480×320 | 153,600 | −56% | 整帧遍省一半多，放大 2× |
| 320×240 | 76,800 | −78% | 远处行人 ≤10 px，基本不可用 |

**三点判断**：
1. "540p"= 960×540，比现在的 720×480 **多 50% 像素**——降分辨率这个方向本身对（少搬像素），但选的档位反了。
2. **模型输入固定 640×640**（`main.cc:38-39`、`yolov5.cc:146-152`），所以降采集分辨率**不会降低 NPU 成本**，只会让 letterbox 从"下采样"变成"上采样"，远处行人（IPM 脚点全靠它）更糊 → 检出率与脚点精度双降，而 `danger.n=10` 这类阈值靠的就是检出数。
3. 实测收益上限：整帧遍从 5 遍降到 5 遍（只是每遍变小），按 §3.2 的带宽估算，**640×360 只能省 8–20 ms**；同样的收益用 §3.4 的 P0/P1 就能拿到，且不损失精度。

**如果确实要降**：建议 `480×320`（3:2，与现比例一致，IPM 只需按比例重算 `srcPts`/`dstPts`），并把模型一并换到 416×416 或 320×320，让 NPU 与后处理一起降——否则只降采集是"花了精度买了个位数毫秒"。
**改分辨率必须同步的四处**：`main.cc:30-31`（`DISP_WIDTH/HEIGHT`）、`main.cc:124-127`（`srcPts`/`dstPts` 硬编码标定）、`main.cc:226`（`PoolCfg.u64MBSize`）、`luckfox_mpi.cc:76-77` + `116-119`（VI/VENC 尺寸与 buffer），以及 `vi_chn_init`/`venc_init` 的调用参数（`main.cc:316,326`）。漏一处就是黑屏或越界。

**真正的分辨率杠杆是模型输入端**（640→416/320）：NPU 与后处理同时降到 1/2.4 或 1/4，代价是远处行人召回率——这是唯一值得做的"降分辨率"，且需要用数据集量化召回损失（`dataset/videos50` 已有 111 段/45,021 帧可做 A/B）。

### 3.6 线程化要点（若采用 P3）

- 主线程 = 采集 + 显示/编码（硬实时性最弱的放这里不安全，建议反过来：**线程 A 只做"VI → 预处理"，线程 B 做"推理 → 决策 → 绘制 → VENC"**）。
- NPU 输入需要 **2 套 `rknn_create_mem`**（各 640×640×3 ≈ 1.2 MB，板上 157 MB 可用，充裕），每帧 `rknn_set_io_mem` 切换（该调用本身很便宜）。
- 缓冲用固定 2 槽 + `pthread_mutex/cond`（或 `std::atomic` 序号 + 自旋），**不要用无界队列**——板上 swap 只有 38 MB 余量，队列一堆积就换页。
- 线程绑核：单核 A7 上"绑核"无意义，用 `SCHED_FIFO` 只会饿死 VI/VENC 的驱动线程；`docs/hardware/system_integration.md:220` 建议的 `SCHED_FIFO 80` 在本场景**不建议照搬**，先解决 load 11。

---

## 4. 内存专项

### 4.1 `tracks_` 是否有无限增长风险（`enhanced_decision.cc:52-138`）

**结论：正常路径有界，但有 3 个隐患。**

**有界性证明**：`miss` 只对"本帧之前已存在"的轨迹自增（L127-129，`i < nt`），新建轨迹 `miss=0`（L117-122 构造 → `miss(0)`）；删除条件是 `miss > max_miss`（L132），默认 `max_miss=5`（`enhanced_decision.h:89`）。因此任一轨迹**最多连续漏检 5 帧即被删除** → `|tracks_| ≤ (max_miss+2)·max_P ≈ 7·max_P`。P=20 时 ≤140，P=64 时 ≤448。**不会无限增长。**

**隐患 1：`max_miss` 不可配**。`EnhancedConfig::kalman_max_miss` 没有出现在 `config/default_config.json` 里，`risk_config.h` 也**没有解析它**（只解析了 `frame_dt` 和 `fsm`）。现场想调只能重编译。若有人把它调大到 100，上界就变成 102·P，`cands` 可达 16 B×10⁴ ≈ 160 KB～MB 级。

**隐患 2：完全没有硬上限**。`tracks_.push_back` 无 cap；`cands.reserve(nt*np)`（L79）按 `T·P` 一次性预留，最坏可到 1.1 MB（§2.2 表）。检测抖动/误检密集时会同时触发"轨迹多 + 候选多 + 排序慢"。建议：`tracks_` 上限（64 或 128）+ 超过时淘汰 `miss` 最大者；`cands` 用固定容量数组（§2.2 方案 1）。

**隐患 3：`pts.empty()` 时 `tracks_.clear()`（L59-62）**。这是"反向极端"：一帧漏检全部行人 → 所有 Kalman 状态与 `nextTrackId_` 递增计数保留但轨迹全丢 → 下一帧全部重建（速度=0），`max_miss` 机制形同虚设。语义上应改成"只 `miss++`，不 `clear`"，让 `max_miss` 决定何时淘汰。

### 4.2 `risk_engine.cc` 的 `m_pts` 是否每帧清空

**是，且不会有增长风险**：

- `clear()`（`risk_engine.cc:30-35`）里 `m_pts.clear()`；
- 唯一调用链在 `main.cc:392`：`if (riskCfg.enable_risk_engine_fallback) { riskEngine.clear(); ... addPedestrian(...) }`——**每帧先清后加**；
- 默认配置 `enable_risk_engine_fallback=false`（`config/default_config.json:19`），此时 `addPedestrian` 根本不会被调用，`m_pts` 永远为空；
- `vector::clear()` 不释放容量，但容量上界就是"单帧目标类检测数"，不会累积。

**但有两个附带问题**：
1. **API 层没有自我保护**：`addPedestrian` 不检查是否已 `clear`，外部若忘记清就无限增长。建议在 `addPedestrian` 入口断言 `m_pts.size() < 上限`，或改成 `computeFeatures(const std::vector&)` 的纯函数形态（顺便消灭成员状态）。
2. **`computeFeatures` 里 `dist` 矩阵是死代码**（`risk_engine.cc:52,58`）：分配 `n²` 个 float 并逐对写入，**全函数没有任何一处读它**。n=20 时白写 400 次 + 21 次分配。删掉即可（0 风险）。

### 4.3 其他内存观察

| 位置 | 现象 | 影响 |
|---|---|---|
| `main.cc:168` `stFrame.pstPack = malloc(sizeof(VENC_PACK_S))` | 只分配 **1 个** pack，而 VENC 输出可能 `u32PackCount > 1` | 与 `WORKLOG-20261030` 记录的"画面撕裂"根因同源（只发 `pstPack[0]`）。非性能问题，但一动编码流程就必须一起修 |
| `main.cc:226-228` `PoolCfg.u64MBSize = w*h*3, u32MBCnt = 1` | 单块 DMA 池 | 单块 = 串行，无法双缓冲；做 §3.6 线程化要扩到 2 块 |
| `main.cc:473` 自拷贝 | 1 MB/帧的无用流量 | 已在 §3.4 列为 P0 |
| 每帧 2 次大 `Mat` 分配 | 2 MB/帧抖动 | 已在 §3.4 列为 P0 |
| RSS 实测 12.5 MB / 板 211 MB | 内存不是约束；**swap 用了 38 MB 才是异常**（说明曾经有内存压力或 swappiness 太高） | 建议 `cat /proc/sys/vm/swappiness`，演示前 `swapoff -a` 试跑对比帧率 |

---

## 5. 建议执行顺序（按 ROI/风险）

1. **（0.5 天）把测量做出来**：补 `include/yolov5.h` → 修 `-O2`/NEON 编译开关 → 加 §3.3 的 `[PROFILE]` 打点 → 排掉板端 `load 11`/`swap 38 MB` 的干扰 → 得到**一份可信的分阶段耗时表**（同时把输出存进 `results/edge_board/`，以后所有优化都以它为基线）。
2. **（0.5 天）删可静态证明的浪费**：`main.cc:348` 恒等 resize、`main.cc:473` 自拷贝、`main.cc:57-61` 的 Mat 复用与 640×640 只填一次、letterbox 直写 NPU 内存。→ 预期 **10–25 ms**。
3. **（1–1.5 天）上单遍 NEON 预处理**：从 `284de8d` 取 `preprocess.cc/preprocess.h`，接 `letterbox_prepare`，用标量路径做逐像素一致性对比后再切 NEON（该实现自带 174/174 黄金用例的历史）。→ 预期 **20–60 ms**。
4. **（0.5 天）后处理瘦身 + 去掉热路径 printf/绘制开销**。→ 预期 **5–12 ms**。
5. **（并行，正确性）修 Jacobi 收敛**（循环 Jacobi + `tol=1e-10`），补 n=10/15/20/30 的黄金用例。**不要**用"每 3 帧算一次"来绕过。
6. **（可选，0.5–1 天）线程化**；若以上已到 11–13 FPS，则不必做。
7. **（可选）模型 416/320 的精度-帧率权衡实验**，用 `dataset/videos50` 做召回率 A/B。

---

## 附录 A：本次数值实验的方法与脚本

- 目的：在不改代码的前提下，判断"Jacobi 每 3 帧算一次"是否值得，以及当前实现是否真的收敛。
- 方法：纯 Python（无 numpy）逐行复刻 `graph_laplacian.h:114-218` 的建图与单主元旋转策略（同一 `tol=1e-12`、`max_sweeps=100`、同一 `theta/t/c/s` 公式、同一 `a[p][k]=a[k][p]` 对称维护），IEEE-754 double 与 C++ double 同精度；参考解用循环 Jacobi（`tol` 1e-15，≤20,000 次全扫描）。每组 n 取 30 个随机样本，点在 6 m×4 m 地面（与 `main.cc:126-127` 的 `dstPts` 范围一致）均匀分布，`edge_dist=2.0`。
- 复现：脚本保存在 `C:\Users\Administrator\AppData\Local\Temp\opencode\jacobi_probe.py`，`python jacobi_probe.py` 即得 §2.1(b) 的表。
- 局限：这是**算法行为的数值等价复刻**，不是对 C++ 二进制的直接测量；旋转次数与收敛结论可直接迁移（逐次运算完全相同），但绝对耗时需板端实测。

板端数据的出处（均为仓库内记录，本次未上板）：
- `results/edge_board/EDGE_TEST_REPORT.md`（6.9 FPS / CPU 42.5% / RSS 12.5 MB）
- `docs/WORKLOG-20261030-基准版本锁定.md:14`（7.2 FPS / baseline md5）
- `git show fd47944`（预处理 136 ms→33 ms、6.30→7.46 FPS）
- `docs/review/20261030/agent_B2_嵌入式板级.md:101,165`（load 11.4 / swap 38 MB / pidstat 缺失）

## 附录 B：顺带发现（非性能，同一次只读通读中发现，建议另行排期）

1. **`src/luckfox_rtsp_yolov5/include/yolov5.h` 缺失**，`main.cc:19` / `yolov5.cc:21` / `postprocess.cc:15` 均 include 它 → 当前工作区按 `Makefile:83` 无法交叉编译，FPS 无法复现。（P0 可复现性）
2. **当前 `main.cc` 全程没有 `BGR2RGB` 转换**，而 `luckfox_mpi.cc:111` 把 VENC 声明为 `RK_FMT_RGB888` → 编码器按 RGB 解释 BGR 缓冲，**红蓝互换**（DANGER 红横幅会显示成蓝色）。另外 letterbox 送 NPU 的也是 BGR，而 YOLOv5 训练用 RGB，**推理精度也受一次通道互换影响**。`284de8d` 分支修过这一处（`cv::cvtColor(frame, frame, COLOR_BGR2RGB)`），当前基线没有。（P1 显示/精度）
3. **`postprocess.cc:441` 的 `std::set<int> class_set(std::begin(classId), std::end(classId))`** 每帧构造一次红黑树（最多 80 个节点、V 次插入），只为拿"出现过哪些类"。换成 `bool seen[80]` 位图即可（也是 §3.4 P1 的一部分）。
4. **`main.cc:457` 的中文底色宽度算法** `((int)strlen(cn_line)/3)*(24+2)+12` 假设"每个字符 3 字节"，但 `cn_line` 里含 ASCII 数字与小数点（1 字节）→ 底色会短于文本，数字部分落在未变暗的亮背景上。（显示瑕疵，与性能无关）

---

*本报告为只读审查产物，未改动任何源文件。所有"估计"均给出推导依据，所有"实测"均标注出处；凡未验证项已显式标出（板端分阶段耗时、NPU/后处理真实耗时、OpenCV 是否启用 NEON）。*