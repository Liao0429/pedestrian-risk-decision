# 算法集成方案与改造点说明

> 文档状态：方案设计（不落实现有代码，仅给出改造点与关键代码片段）
>
> 目标项目：**边缘端轻量化行人感知与风险决策系统**（Luckfox Pico Max / RV1106）
>
> 新增算法模块：图拉普拉斯谱特征（graph_laplacian）、Kalman 轨迹平滑（kalman_tracker）、风险决策有限状态机（risk_fsm）
>
> 集成目标：① Python 验证脚本 `scripts/crowd_test.py`；② 开发板 C++ 工程 `src/luckfox_rtsp_yolov5/`

---

## 0. 现状与关联文件（已核实的基线）

| 文件 | 现状要点 | 本方案改造位置 |
|---|---|---|
| `scripts/crowd_test.py` | 单帧/目录模式：`compute_features()`（L156）算 n/m/density/r_gcc/clustering；`evaluate_level()`（L234）组合逻辑判级；`process_image()`（L294）单帧流水；`main()`（L406）argparse 无视频/多帧模式 | 新增谱特征调用、新增视频/多帧序列处理路径、新增 Kalman+FSM 集成 |
| `src/luckfox_rtsp_yolov5/src/risk_engine.cc` | 阈值宏 L7-13；`computeFeatures()`（L50）O(n²) 建边 + BFS + 聚类；`evaluate()`（L108）规则判级；`clear()`（L25）逐帧清空（无状态） | 增加谱特征计算接口；保留现有特征，新增 `computeSpectralFeatures()` |
| `src/luckfox_rtsp_yolov5/include/risk_engine.h` | `RiskEngine` 类接口（L26-54）；`RiskFeatures` 结构 L17-23 | 扩展 `RiskFeatures` 增加谱特征字段；声明谱特征计算 |
| `src/luckfox_rtsp_yolov5/src/main.cc` | **单线程** while 循环（L160）：取帧→letterbox→推理→`riskEngine.clear()`（L182）→`addPedestrian`（L189）→`computeFeatures`（L192）→`evaluate`（L193）→画框/OSD→H264→RTSP | 线程化改造：拆出“采集推理 / 决策 / 编码推流 / 外设”四线程；Kalman 放决策线程推理后处理，FSM 放决策线程末段 |
| `src/algorithms/` | 目录存在但**为空**（已核实） | 新增 `graph_laplacian.py`、`kalman_tracker.py`、`risk_fsm.py`（Python）；对应 C++ 头/实现 |
| `config/default_config.json` | 含 `edge_dist_m`、`danger{n,rho,gcc}`、`warning{n,rho,gcc}` | 扩展 `spectral`、`temporal`（FSM 参数）、`tracker` 配置段 |
| `docs/optimization_review.md` | 明确指出当前单线程、无跟踪/滞回；R3（滞回+EMA）、R4（跟踪）为 P0 建议 | 本方案即为 R3/R4 的落地实现 |
| `docs/hardware/system_integration.md` | 定义了 1 主线程 + 4 扩展线程模型与 `SystemState` 共享结构（L214-280） | 本方案的四线程拆分与之兼容，并扩展 `SystemState` 字段 |

> **说明**：现有 `main.cc` 是单线程串行流水线（`optimization_review.md` L37 已指出），本方案把它拆成四线程，与 `system_integration.md` 的线程化方向一致。`src/algorithms/` 为空，以下文件均为**新增**。

---

## 1. 三个新算法的定位与原理

### 1.1 图拉普拉斯谱特征（graph_laplacian）

**定位**：作为现有复杂网络特征（density / r_gcc / clustering）的**连续化补充**。

**原理**：对行人空间网络的无向图 `G=(V,E)`（顶点=行人，边=地面距离 < 2.0m），构造拉普拉斯矩阵
`L = D - A`（`D` 为度对角阵，`A` 为邻接矩阵）。对实对称半正定 `L` 求特征值 `0 = λ₁ ≤ λ₂ ≤ … ≤ λ_n`：

- **λ₂（Fiedler 值 / 代数连通度）**：刻画图的整体连通强度。λ₂ = 0 ⇔ 图不连通；λ₂ > 0 越大表示“群体越难被切开”。
- **λ_max（谱半径）**：等于最大度上界，反映局部聚集强度。
- **谱间隔 spectral_gap = λ₃ − λ₂**：度量连通簇之间“分离度”，用于识别多个独立小群体。

现有 `r_gcc` 是 0/1 的硬连通性离散量（BFS，`risk_engine.cc` L73-88），对单帧漏检/误检很敏感（少一个节点就改变连通分量）；λ₂ 是连续量，对拓扑扰动更鲁棒，且能给出“连通裕度”，可解释为“让群体分裂成两团所需切断的最小边权”。

### 1.2 Kalman 轨迹平滑（kalman_tracker）

**定位**：解决当前逐帧无状态导致的**脚点抖动 → 2.0m 阈值边翻转 → 特征抖动**问题。

**原理**：对每个行人轨迹建立恒速运动模型，状态 `x = [x, y, vx, vy]ᵀ`，测量为检测脚点地面坐标 `z = [x, y]ᵀ`。每帧先 `predict()`（状态与协方差外推），再做数据关联（最近邻/马氏距离门控贪心匹配），最后 `update()` 融合测量，输出平滑脚点与速度。数学本质是**最小化估计误差协方差的递归贝叶斯滤波**。

### 1.3 风险决策有限状态机（risk_fsm）

**定位**：把当前组合逻辑判级（`evaluate_level()` L234 / `RiskEngine::evaluate()` L108）升级为**时序状态机**，消除单帧闪烁。

**原理**：风险等级作为状态 `S ∈ {SAFE, WARNING, DANGER}`。输入是“单帧组合逻辑等级 + 是否有检测数据”。转移规则：
- 升级：需**连续 `escalate_frames`（默认 3）帧**满足更高等级；
- 降级：需**连续 `deescalate_frames`（默认 5）帧**满足更低等级；
- **无数据保持**：推理返回 0 检测时不立即回 SAFE，保持当前等级最多 `no_data_hold`（默认 5）帧。

对应 `optimization_review.md` 的 R3（滞回 + 无数据保持）建议。

---

## 2. Python 侧集成（`scripts/crowd_test.py`）

### 2.1 新增模块与接口（`src/algorithms/`）

```python
# src/algorithms/graph_laplacian.py
import numpy as np

def adjacency_from_distances(pts, edge_dist=2.0):
    """由地面米制坐标列表 [(x,y),...] 构造 0/1 邻接矩阵（与 compute_features 建边规则一致）"""
    n = len(pts)
    A = np.zeros((n, n), dtype=np.float64)
    for i in range(n):
        for j in range(i + 1, n):
            d = np.hypot(pts[i][0] - pts[j][0], pts[i][1] - pts[j][1])
            if d < edge_dist:
                A[i, j] = A[j, i] = 1.0
    return A

def spectral_features(adj):
    """输入 n×n 0/1 邻接矩阵，返回 dict:
    lambda2: Fiedler 值(代数连通度, n<2 时返回 0)
    lambda_max: 谱半径(最大特征值)
    spectral_gap: 谱间隔(λ3-λ2, n<3 时返回 0)
    """
    n = adj.shape[0]
    out = {"n": n, "lambda2": 0.0, "lambda_max": 0.0, "spectral_gap": 0.0}
    if n == 0:
        return out
    D = np.diag(adj.sum(axis=1))
    L = D - adj
    eig = np.linalg.eigvalsh(L)          # 实对称矩阵用 eigvalsh
    eig = np.maximum(eig, 0.0)            # 数值误差修正
    eig.sort()
    out["lambda_max"] = float(eig[-1])
    if n >= 2:
        out["lambda2"] = float(eig[1])   # Fiedler 值
    if n >= 3:
        out["spectral_gap"] = float(eig[2] - eig[1])
    return out
```

```python
# src/algorithms/kalman_tracker.py
import numpy as np

class KalmanTrack:
    """单轨迹恒速模型 Kalman 滤波器"""
    F = np.array([[1,0,1,0],[0,1,0,1],[0,0,1,0],[0,0,0,1]], dtype=np.float64)  # 状态转移(Δt=1帧)
    H = np.array([[1,0,0,0],[0,1,0,0]], dtype=np.float64)                    # 测量矩阵
    _next_id = 1
    def __init__(self, x, y):
        self.id = KalmanTrack._next_id; KalmanTrack._next_id += 1
        self.x = np.array([x, y, 0.0, 0.0], dtype=np.float64)
        self.P = np.eye(4) * 10.0
        self.Q = np.eye(4) * 0.05      # 过程噪声(地面坐标, 米)
        self.R = np.eye(2) * 0.25    # 测量噪声(脚点抖动)
        self.age = 1
        self.hits = 1
        self.lost = 0
    def predict(self):
        self.x = self.F @ self.x
        self.P = self.F @ self.P @ self.F.T + self.Q
        return self.x[:2]
    def update(self, z):
        y = z - self.H @ self.x
        S = self.H @ self.P @ self.H.T + self.R
        K = self.P @ self.H.T @ np.linalg.inv(S)
        self.x = self.x + K @ y
        self.P = (np.eye(4) - K @ self.H) @ self.P
        self.age += 1; self.hits += 1; self.lost = 0
    @property
    def smoothed(self):
        return (float(self.x[0]), float(self.x[1]))

class KalmanTracker:
    """贪心最近邻匹配跟踪器(面向脚点，n≤64 场景微秒级)"""
    def __init__(self, gate_m=1.0, max_lost=5, min_hits=2):
        self.gate_m = gate_m
        self.max_lost = max_lost
        self.min_hits = min_hits
        self.tracks = []
    def update(self, detections):
        """输入: list of (x, y) 地面米制脚点; 返回: 存活轨迹列表"""
        for t in self.tracks:
            t.predict()
        matched = set(); used = set()
        # 贪心：按预测位置与测量距离升序匹配
        pairs = []
        for i, t in enumerate(self.tracks):
            px, py = t.x[0], t.x[1]
            for j, (dx, dy) in enumerate(detections):
                d = np.hypot(px - dx, py - dy)
                if d < self.gate_m:
                    pairs.append((d, i, j))
        for _, i, j in sorted(pairs, key=lambda v: v[0]):
            if i in matched or j in used:
                continue
            self.tracks[i].update(np.array(detections[j], dtype=np.float64))
            matched.add(i); used.add(j)
        # 未匹配的新检测 → 新轨迹
        for j, det in enumerate(detections):
            if j not in used:
                self.tracks.append(KalmanTrack(det[0], det[1]))
        # 清理丢失轨迹
        for t in list(self.tracks):
            if t.id not in [self.tracks[i].id for i in matched]:
                t.lost += 1
        self.tracks = [t for t in self.tracks if t.lost <= self.max_lost]
        # 只返回达到 min_hits 的成熟轨迹(过滤瞬时误检)
        return [t for t in self.tracks if t.hits >= self.min_hits]
```

```python
# src/algorithms/risk_fsm.py
class RiskFSM:
    SAFE, WARNING, DANGER = 0, 1, 2
    _NAME = ["SAFE", "WARNING", "DANGER"]
    def __init__(self, escalate_frames=3, deescalate_frames=5, no_data_hold=5, cfg=None):
        if cfg is None:
            cfg = {}
        self.escalate   = int(cfg.get("escalate_frames", escalate_frames))
        self.deescalate = int(cfg.get("deescalate_frames", deescalate_frames))
        self.no_data_hold = int(cfg.get("no_data_hold_frames", no_data_hold))
        self.level = self.SAFE
        self.counter = 0
        self.no_data_left = self.no_data_hold
    def update(self, raw_level, has_data=True):
        """raw_level 为单帧组合逻辑等级(0/1/2)；has_data=False 表示本帧无检测"""
        if not has_data:
            if self.no_data_left > 0:
                self.no_data_left -= 1          # 保持当前等级
            else:
                self.level = self.SAFE              # 超时回 SAFE
            self.counter = 0
            return self.level
        self.no_data_left = self.no_data_hold
        if raw_level == self.level:
            self.counter = 0                       # 同级清零
        elif raw_level > self.level:
            self.counter += 1                      # 升级计数
            if self.counter >= self.escalate:
                self.level = raw_level; self.counter = 0
        else:
            self.counter += 1                        # 降级计数
            if self.counter >= self.deescalate:
                self.level = raw_level; self.counter = 0
        return self.level
    @property
    def name(self):
        return self._NAME[self.level]
```

### 2.2 单帧模式改造（集成谱特征）

在 `process_image()`（`crowd_test.py` L294）的 `compute_features()` 之后增加谱特征计算，并把 λ₂ 加入 `feat` dict 与可视化：

```python
# 在 crowd_test.py 顶部新增导入
from algorithms.graph_laplacian import adjacency_from_distances, spectral_features
from algorithms.kalman_tracker import KalmanTracker
from algorithms.risk_fsm import RiskFSM

# 在 process_image() 内, L350 compute_features 之后插入：
    n, m, density, r_gcc, clustering = compute_features(pts, edge_dist)
    # ---- 新增: 图拉普拉斯谱特征(可选 --spectral) ----
    if use_spectral:
        adj = adjacency_from_distances(pts, edge_dist)
        spec = spectral_features(adj)
        feat.update(spec)                     # 加入 lambda2 / lambda_max / spectral_gap
    else:
        spec = {"lambda2": 0.0, "lambda_max": 0.0, "spectral_gap": 0.0}
    level = evaluate_level(n, density, r_gcc, cfg)
```

对应 `draw_annotation()`（L251）的 `info` 字符串扩展（L279）：

```python
info = "CROWD:%s n=%d rho=%.2f r_gcc=%.2f λ2=%.3f" % (
    level, feat["n"], feat["density"], feat["r_gcc"], feat.get("lambda2", 0.0))
```

### 2.3 新增视频/多帧序列模式（集成 Kalman + FSM）

`crowd_test.py` 目前**没有**视频/多帧路径（`main()` 只支持 `--input` 图片/目录）。新增两条处理路径：

1. `--input video.mp4`：按帧读取视频，逐帧走“检测→Kalman→网络特征→谱特征→单帧等级→FSM→绘制→写输出视频”。
2. `--input images_dir --sequence 1`：把目录下**按文件名排序**的图片视为时间序列（对应开发板逐帧推流场景），逐帧同样处理，输出标注序列到 `output/vis_seq/`。

新增处理函数 `process_sequence()`：

```python
def process_sequence(model, frames, out_dir, conf, imgsz, H, scale_mpp, device,
                      edge_dist, cfg=None, write_video=False):
    """逐帧处理: 检测 -> Kalman 平滑脚点 -> 网络/谱特征 -> 单帧等级 -> FSM 输出稳定等级
    frames: 视频路径或已排序图片路径列表; 返回轨迹记录与等级时间线"""
    tracker = KalmanTracker(gate_m=cfg.get("tracker", {}).get("gate_m", 1.0),
                            max_lost=cfg.get("tracker", {}).get("max_lost", 5))
    fsm = RiskFSM(cfg=cfg.get("temporal", {}))          # escalate/deescalate/no_data_hold
    timeline = []                                        # 每帧: {frame_id, level, n, lambda2, tracks}
    prev_pts = []                                        # 上一帧地面脚点(平滑用, 可选 EMA)
    for fid, frame in enumerate(frames):
        # 1) 检测(复用 process_image 内逻辑, 抽出为 detect_pedestrians())
        boxes_xyxy, detections, raw_pts = detect_pedestrians(model, frame, conf, imgsz,
                                                             device, to_ground)
        # 2) Kalman 后处理: 用平滑脚点替代原始脚点
        tracks = tracker.update(raw_pts)                 # 只保留成熟轨迹(过滤瞬时误检)
        smooth_pts = [t.smoothed for t in tracks]        # 平滑后的地面米制脚点
        # 3) 网络特征 + 谱特征
        n, m, density, r_gcc, clustering = compute_features(smooth_pts, edge_dist)
        spec = spectral_features(adjacency_from_distances(smooth_pts, edge_dist))
        # 4) 单帧组合逻辑等级 -> FSM
        raw_level = LEVEL_IDX[evaluate_level(n, density, r_gcc, cfg)]   # SAFE=0 ...
        has_data = n > 0
        stable_level = fsm.update(raw_level, has_data=has_data)
        # 5) 记录 + 可视化(画框/轨迹/等级/λ2)
        timeline.append({...})
        draw_sequence_frame(frame, boxes_xyxy, tracks, stable_level, spec, ...)
    if write_video:
        write frames to out_path with cv2.VideoWriter
    return timeline
```

### 2.4 argparse 参数变更（`main()` L406）

新增参数：

```python
parser.add_argument("--spectral", action="store_true",
                    help="启用图拉普拉斯谱特征(λ2/谱半径/谱间隔), 加入输出与可视化")
parser.add_argument("--video", action="store_true",
                    help="输入为视频文件或按序图片序列, 启用 Kalman+FSM 时序处理")
parser.add_argument("--sequence", action="store_true",
                    help="把输入目录内图片按文件名排序视为时间序列逐帧处理")
parser.add_argument("--no-data-hold", type=int, default=5,
                    help="FSM 无检测保持帧数(默认 5)")
parser.add_argument("--escalate", type=int, default=3,
                    help="FSM 升级所需连续帧数(默认 3)")
parser.add_argument("--deescalate", type=int, default=5,
                    help="FSM 降级所需连续帧数(默认 5)")
```

`process_image()` 签名增加 `use_spectral` 参数（L294），`main()` 调用处（L467-469）同步传入。

### 2.5 Python 侧数据流

```
视频/图片序列 ──▶ YOLO 检测(person) ──▶ 脚点(像素) ──IPM──▶ 地面米坐标
                                              │
                                   KalmanTracker.update()  # 推理后处理: 平滑+去瞬时误检
                                              ▼
                                    平滑脚点列表 ──▶ compute_features()
                                              │        + spectral_features()
                                              ▼
                                   evaluate_level(单帧) ──▶ RiskFSM.update()
                                              ▼
                                    稳定等级 + 轨迹 + 谱特征 ──▶ 可视化/JSON/视频
```

---

## 3. C++ 板端集成（`main.cc` / `risk_engine.cc`）

### 3.1 四线程架构总览

现有 `main.cc` 是单线程（L160-267 while 循环）。按 `system_integration.md` 的线程化方向，拆为**四线程**：

| 线程 | 职责 | 数据 | 与现有代码对应关系 |
|---|---|---|---|
| **T1 采集推理线程** | 取帧→letterbox→RKNN 推理→NMS→提取 person 脚点(像素) | 输出 `raw detections`(脚点+置信度) 到共享缓冲 | 现 `main.cc` L162-190 的取帧/推理/`addPedestrian` 前段 |
| **T2 决策线程** | ① **Kalman 推理后处理**：`mapCoordinates`→IPM 地面坐标→`KalmanTracker.update()` 取平滑脚点；② `computeFeatures()`+谱特征；③ **RiskFSM.update()** 输出稳定等级 | 输出 `SystemState.risk / n / rho / lambda2 / tracks` | 现 L181-193 的逻辑拆出，加状态 |
| **T3 编码推流线程** | 取最新帧 + 读 `SystemState` 叠加 OSD(等级/人数/λ2) → H264 编码 → RTSP | 读共享状态 | 现 L195-255 |
| **T4 外设线程** | OLED 刷新 / 语音告警 / 云台控制（按 `system_integration.md` 可再细分） | 读共享状态 | 对应 `system_integration.md` 扩展线程 |

> **兼容性说明**：`system_integration.md` 原有“1 主线程 + 4 扩展线程”模型，本方案把视觉主线程进一步拆为 T1/T2/T3，T4 为原扩展线程的聚合语义；`SystemState` 共享结构继续复用并扩展字段。

### 3.2 新增 C++ 模块文件

| 文件 | 内容 |
|---|---|
| `src/algorithms/graph_laplacian.h` / `src/luckfox_rtsp_yolov5/src/graph_laplacian.cc` | 谱特征计算 |
| `src/algorithms/kalman_tracker.h` / `src/luckfox_rtsp_yolov5/src/kalman_tracker.cc` | 轨迹跟踪器 |
| `src/algorithms/risk_fsm.h` / `src/luckfox_rtsp_yolov5/src/risk_fsm.cc` | 风险状态机 |

> 说明：`src/algorithms/` 现有仓库内为空；Luckfox 工程编译时把 `src/algorithms` 加入头文件搜索路径（或在 SDK 的 CMakeLists 中 `include_directories(../algorithms)`），头文件作为算法模块的 C++ 接口定义，实现文件放入工程 `src/` 参与编译。**本仓库内无既有构建脚本**，需在 Luckfox SDK 外部工程的 CMakeLists/Makefile 中追加 `graph_laplacian.cc / kalman_tracker.cc / risk_fsm.cc`。

#### 3.2.1 graph_laplacian.h

```cpp
#ifndef GRAPH_LAPLACIAN_H
#define GRAPH_LAPLACIAN_H
#include <vector>
#include "risk_engine.h"   // 复用 RiskFeatures / CrowdLevel

struct SpectralFeatures {
    int    n;
    double lambda2;        // Fiedler 值(代数连通度)
    double lambda_max;     // 谱半径
    double spectral_gap;   // λ3 - λ2
};

// 输入 0/1 邻接矩阵(与 RiskEngine::computeFeatures 建边一致), 输出谱特征。
// 内部: 小型 Jacobi 特征分解(自包含, 无需 Eigen)。n≤128 时开销 <1ms(与聚类同量级)。
SpectralFeatures computeSpectralFeatures(const std::vector<std::vector<bool>>& adj);

// 由地面点坐标直接构造邻接矩阵(建边规则同 risk_engine.cc: EDGE_DIST_M)
std::vector<std::vector<bool>> buildAdjacency(
    const std::vector<std::pair<float,float>>& pts, float edge_dist_m);

#endif
```

`graph_laplacian.cc` 要点：对实对称半正定矩阵 `L=D-A` 用 Jacobi 旋转法求全部特征值（约 50 行 Givens 旋转，旋转角 `θ = 0.5*atan2(2*Aij, Aii-Ajj)`），取排序后的 `eig[1]`、`eig[2]`、`eig[-1]`。

#### 3.2.2 kalman_tracker.h

```cpp
#ifndef KALMAN_TRACKER_H
#define KALMAN_TRACKER_H
#include <vector>
#include <utility>
#include <cmath>

struct KalmanState { float x, y, vx, vy; };   // 地面米制坐标 + 速度

class KalmanTrack {
public:
    explicit KalmanTrack(float x, float y);
    void predict();                      // 恒速外推
    void update(float mx, float my);     // 融合测量
    int    id, age, hits, lost;
    KalmanState state;
};

class KalmanTracker {
public:
    KalmanTracker(float gate_m = 1.0f, int max_lost = 5, int min_hits = 2);
    // 输入: 本帧检测脚点(地面米制); 输出: 存活轨迹列表(已平滑)
    std::vector<KalmanTrack*> update(const std::vector<std::pair<float,float>>& detections);
private:
    float m_gateM; int m_maxLost; int m_minHits;
    std::vector<KalmanTrack> m_tracks;
    int m_nextId;
};
#endif
```

实现与 Python 版 `kalman_tracker.py` 逻辑一致（恒速模型、贪心最近邻匹配、`lost` 清理、`min_hits` 过滤瞬时误检）。板端用 `float` 精度即可满足。

#### 3.2.3 risk_fsm.h

```cpp
#ifndef RISK_FSM_H
#define RISK_FSM_H
#include "risk_engine.h"   // CrowdLevel 枚举

class RiskFSM {
public:
    RiskFSM(int escalate = 3, int deescalate = 5, int noDataHold = 5);
    CrowdLevel update(CrowdLevel rawLevel, bool hasData = true);
    CrowdLevel level() const { return m_level; }
private:
    CrowdLevel m_level; int m_counter;
    int m_escalate, m_deescalate, m_noDataHold, m_noDataLeft;
};
#endif
```

`risk_fsm.cc` 实现与 Python 版 `risk_fsm.py` 完全一致（滞回计数 + 无数据保持）。

### 3.3 改造点：`risk_engine.cc` 增加谱特征

在 `RiskEngine` 中扩展 `RiskFeatures` 并暴露接口（`risk_engine.h` L17-23）：

```cpp
typedef struct {
    int    n;
    int    m;
    double density;
    double r_gcc;
    double clustering;
    // ---- 新增 ----
    double lambda2;        // Fiedler 值
    double lambda_max;      // 谱半径
    double spectral_gap;    // 谱间隔
} RiskFeatures;
```

`risk_engine.cc` 新增成员函数（在 `computeFeatures()` 内或独立方法）：

```cpp
void RiskEngine::computeSpectralFeatures() {
    // 复用 computeFeatures 已建的邻接矩阵逻辑; 为省内存可直接在 computeFeatures 内填 m_feat
    // 这里给出独立实现: 由 m_pts 构造邻接 -> 调 graph_laplacian.h 的 computeSpectralFeatures
    std::vector<std::vector<bool>> adj = buildAdjacency(m_pts, EDGE_DIST_M);
    SpectralFeatures sp = ::computeSpectralFeatures(adj);
    m_feat.lambda2      = sp.lambda2;
    m_feat.lambda_max   = sp.lambda_max;
    m_feat.spectral_gap = sp.spectral_gap;
}
```

在 `risk_engine.h` 声明 `void computeSpectralFeatures();`。`computeFeatures()`（L50）内已有距离矩阵与邻接计算，可顺手把 `dist` 保存为成员以便谱特征复用，避免重复 O(n²)。**建议**：在 `computeFeatures()` 内直接计算谱特征填入 `m_feat`（一次遍历），不额外开新方法，减少板端开销。

### 3.4 改造点：推理后处理线程（Kalman）

从现 `main.cc` 的检测循环（L183-191）抽取“推理后处理”逻辑放入决策线程 `decision_thread`：

```cpp
// 决策线程: 推理后处理 + 网络/谱特征 + FSM
void* decision_thread(void* arg) {
    DecisionCtx *ctx = (DecisionCtx*)arg;
    KalmanTracker tracker;                 // 状态跨帧保存
    RiskFSM fsm;                          // 状态机跨帧保存
    while (ctx->running) {
        // 1) 从共享缓冲取最新 raw detections(互斥锁保护)
        std::vector<RawDet> dets;
        if (!detection_ring_pop(ctx, dets)) { usleep(10000); continue; }
        // 2) Kalman 后处理: 像素脚点 -> 地面坐标 -> 平滑
        std::vector<std::pair<float,float>> raw_pts, smooth_pts;
        for (auto &d : dets) {
            if (d.cls_id != 0) continue;                       // COCO person
            float gx, gy;
            mapToGroundPixel(ctx->H, d.foot_x, d.foot_y, gx, gy);
            raw_pts.emplace_back(gx, gy);
        }
        auto tracks = tracker.update(raw_pts);                 // 平滑+去瞬时误检
        for (auto *t : tracks) smooth_pts.emplace_back(t->state.x, t->state.y);
        // 3) 网络特征 + 谱特征
        RiskEngine re; re.setHomography(ctx->H);                // 或复用单例
        for (auto &p : smooth_pts) re.addPedestrianRaw(p.first, p.second);
        re.computeFeatures(); re.computeSpectralFeatures();
        // 4) 单帧等级 -> FSM
        CrowdLevel raw = re.evaluate();
        CrowdLevel stable = fsm.update(raw, /*hasData=*/ !smooth_pts.empty());
        // 5) 写共享状态
        ctx->st->lock();
        ss_set_risk(ctx->st, stable, re.features().n,
                    re.features().density, re.features().r_gcc);
        ctx->st->lambda2 = re.features().lambda2;                 // 新增字段
        ctx->st->track_count = (int)tracks.size();              // 新增字段
        ctx->st->unlock();
    }
    return NULL;
}
```

> 注意：`risk_engine.h` 中 `addPedestrian(int footX, int footY)` 输入像素坐标并内部 `mapToGround`。此处建议**新增**重载 `addPedestrianRaw(float gx, float gy)` 直接接收地面坐标（避免二次 IPM），或在调用前先把像素坐标 IPM 成地面坐标再传给现接口。为最小侵入，可先在地面坐标上直接调用现有 `addPedestrian` 的像素版本——但这会重复映射。**推荐**在 `risk_engine.h` 增加地面坐标直传接口：

```cpp
// 新增: 直接接收地面米制坐标(决策线程 Kalman 平滑后的坐标)
void addPedestrianGround(float gx, float gy);
// risk_engine.cc:
void RiskEngine::addPedestrianGround(float gx, float gy) {
    m_pts.push_back(std::make_pair(gx, gy));
}
```

### 3.5 改造点：决策线程中 FSM 的参数来源

FSM 的 `escalate/deescalate/no_data_hold` 从配置加载（`config/default_config.json` 扩展）：

```json
{
  "edge_dist_m": 2.0,
  "spectral": { "enabled": true },
  "temporal": { "escalate_frames": 3, "deescalate_frames": 5, "no_data_hold_frames": 5 },
  "tracker":  { "gate_m": 1.0, "max_lost": 5, "min_hits": 2 },
  "danger":  { "n": 10, "rho": 0.55, "gcc": 0.8 },
  "warning": { "n": 5,  "rho": 0.28, "gcc": 0.55 }
}
```

在 `main.cc` 初始化阶段读取（当前阈值是编译期宏 `risk_engine.cc` L7-13；本方案不改默认宏，仅把 `temporal/tracker/spectral` 作为新配置项，由 `RiskFSM`/`KalmanTracker` 构造函数接收）。

### 3.6 改造点：`main.cc` 线程化

将现 while 循环拆为 T1/T3 两线程 + 启动 T2/T4。骨架：

```cpp
// 新增共享缓冲(简单双缓冲/环形, 互斥锁保护)
struct DetectionRing { std::vector<RawDet> buf; pthread_mutex_t lock; };
// 新增 SystemState 扩展字段
typedef struct {
    // ... 原有字段(见 system_integration.md L239-262) ...
    double lambda2;       // 决策线程写: 谱特征
    int    track_count;   // 决策线程写: 存活轨迹数
} SystemState;

int main(int argc, char *argv[]) {
    // ... 现有初始化(模型/ISP/MPI/RTSP)保持不变 ...
    SystemState st; system_state_init(&st);
    DetectionRing ring; pthread_mutex_init(&ring.lock, NULL);

    pthread_t t_cap, t_dec, t_enc, t_periph;
    pthread_create(&t_periph, NULL, peripheral_thread, &st);   // OLED/语音/云台(扩展)
    pthread_create(&t_dec,   NULL, decision_thread,   &ctx);  // Kalman+FSM
    pthread_create(&t_enc,   NULL, encode_rtsp_thread, &st);  // 编码推流
    pthread_create(&t_cap,   NULL, capture_infer_thread, &ctx); // 取帧推理
    // join...
}
```

- **T1 采集推理线程**：复用现 L162-190 逻辑，把检测结果（`cls_id, foot_x, foot_y`）push 进 `ring`，不再直接调用 `riskEngine`。
- **T3 编码推流线程**：从 `SystemState` 读稳定等级与特征，叠加 OSD（现 L221-235 的 `sprintf(info,...)` 增加 `λ2`），再 H264 编码/RTSP（现 L240-255）。
- 锁粒度：T1 只写 `ring`（短锁），T2 只写 `st`，T3/T4 只读快照，与 `system_integration.md` L280 的“锁粒度要小”一致。

### 3.7 C++ 侧数据流

```
T1 采集推理: 帧 → letterbox → RKNN → NMS → person 脚点(像素) → [ring]
                                                              │(mutex)
T2 决策线程:  [ring] → IPM地面坐标 → KalmanTracker.update()
                        → 平滑脚点 → RiskEngine::computeFeatures + Spectral
                        → evaluate()单帧 → RiskFSM.update() → [SystemState]
                                                              │(读快照)
T3 编码推流: 读 SystemState → OSD(等级/人数/λ2) → H264 → RTSP
T4 外设:     读 SystemState → OLED / 语音告警 / 云台
```

---

## 4. 模块接口定义汇总

| 模块 | Python 接口（`src/algorithms/*.py`） | C++ 接口（`*.h`） | 输入 | 输出 |
|---|---|---|---|---|
| 图拉普拉斯 | `spectral_features(adj: ndarray) -> dict` | `computeSpectralFeatures(adj) -> SpectralFeatures` | 0/1 邻接矩阵 | λ₂、λ_max、谱间隔、n |
| 图拉普拉斯 | `adjacency_from_distances(pts, edge_dist)` | `buildAdjacency(pts, edge_dist)` | 地面米制点集 | 邻接矩阵 |
| Kalman | `KalmanTracker.update(detections) -> tracks` | `KalmanTracker::update(detections) -> vector<KalmanTrack*>` | 本帧脚点(地面米) | 存活轨迹(平滑脚点/速度/id/age) |
| FSM | `RiskFSM.update(raw_level, has_data) -> level` | `RiskFSM::update(rawLevel, hasData) -> CrowdLevel` | 单帧等级+数据标志 | 稳定等级(滞回) |

**约定**：Python 与 C++ 的建边规则、FSM 转移参数、Kalman 状态模型保持完全一致（`config/default_config.json` 为唯一阈值来源），保证 `crowd_test.py` 的验证结果可移植到板端。

---

## 5. 分阶段实施步骤

| 阶段 | 目标 | 主要工作 | 验收标准 | 工作量 |
|---|---|---|---|---|
| **阶段 1** | Python 单帧谱特征验证 | 写 `graph_laplacian.py`；`crowd_test.py` 加 `--spectral`；用 `results/` 现成 30 图数据集跑基线对比 | 输出每图 λ₂/λ_max/谱间隔；画出 λ₂ 与 r_gcc 的相关性散点（论文图）；DANGER 样例 λ₂ 显著 > SAFE 样例 | 0.5~1 天 |
| **阶段 2** | Python 视频/多帧时序验证 | 写 `kalman_tracker.py`、`risk_fsm.py`；`crowd_test.py` 加 `--video/--sequence` 路径 | 用一段行人视频生成带轨迹+稳定等级的输出视频；对比“平滑前后等级翻转次数”，验证 FSM 消除单帧闪烁；验证瞬时误检被 `min_hits` 过滤 | 1~2 天 |
| **阶段 3** | C++ 谱特征 + Kalman（决策线程雏形） | 写 3 个 `.h/.cc`；`risk_engine.h/.cc` 扩展谱特征与 `addPedestrianGround`；先不拆线程，在现主循环里串行跑 Kalman+谱特征 | 板端 OSD 显示 λ₂；实测 `computeSpectralFeatures` 在 n≤64 时 <1ms（打时间戳验证）；RTSP 画面稳定 | 2~3 天 |
| **阶段 4** | C++ 四线程 + FSM 落地 | 按 §3.6 拆 T1~T4；`RiskFSM` 挂决策线程；`SystemState` 扩展字段；OSD 显示 FSM 稳定等级 | 视频流等级不再逐帧闪烁；0 检测时等级保持 5 帧不闪绿；长跑 30 分钟无崩溃、不掉流 | 2~3 天 |
| **阶段 5** | 标定 + 消融 + 论文图表 | 真机地面标定 H（`optimization_review.md` R1）；用 Python 链路做“有无谱特征/有无Kalman/有无FSM”消融 | 论文得到：① 谱特征 vs 拓扑特征对比表；② 平滑前后等级翻转统计表；③ FSM 去抖前后时间线图 | 2~3 天 |

> **排序理由**：先 Python 验证算法有效性（成本最低、可快速出图），再移植板端；板端先串行跑通算法（阶段 3），最后才做线程化（阶段 4），避免“算法没验证就先上多线程”的调试困难。

---

## 6. 答辩要点

### 6.1 每个算法原理一句话

| 算法 | 原理一句话 | 创新点/卖点 |
|---|---|---|
| **图拉普拉斯谱特征** | “把行人空间网络表示为拉普拉斯矩阵 L=D−A，其第二小特征值 λ₂（Fiedler 值）就是群体的代数连通度，连续刻画‘人群被撕裂的最小代价’。” | 用**连续谱特征**替代离散的 r_gcc 硬连通性，对单帧漏检/误检鲁棒；λ₂ 可解释为“群体连通裕度”，补充了现有 4 个拓扑特征之外的“连通强度”维度。 |
| **Kalman 轨迹平滑** | “对每个行人用恒速运动模型做递归最小化估计误差协方差的贝叶斯滤波，融合检测脚点后输出平滑位置与速度。” | 把逐帧抖动脚点变成**稳定轨迹**，消除 2.0m 阈值附近的边翻转；同时输出速度，为滞留/快速移动告警（设计文档承诺的四告警）提供基础。 |
| **风险决策 FSM** | “把风险等级建模为有限状态机，升级需连续 K 帧确认、降级需连续 M 帧、并带无数据保持，抑制单帧闪烁。” | 把**组合逻辑判级升级为时序状态机**，输出稳定、可记录转移事件（供告警联动触发），是“从感知到决策”闭环的关键。 |

### 6.2 可能追问与回答

1. **“λ₂ 与现有 r_gcc 冗余吗？”**
   答：不冗余。r_gcc 是 0/1 连通分量占比，只区分“是否连通”；λ₂ 给出**连通强度**（连通的群体 λ₂>0，越大越难拆开）。完全图 n=10 的 λ₂ 与链状 n=10 的 λ₂ 不同，但 r_gcc 可能都为 1。实验可用“同 r_gcc 不同 λ₂”样本证明增量价值（阶段 5 消融）。

2. **“板端算特征值会不会太慢？”**
   答：用 Jacobi 特征分解，n≤64 时与现有聚类系数同量级（约 1ms 内，见 `optimization_review.md` §7.1 的估算方法），决策线程 1~5Hz 下 CPU 占用可忽略。

3. **“Kalman 的测量噪声/过程噪声怎么定？”**
   答：测量噪声 R 取脚点经 IPM 后在地面坐标的典型抖动（约 0.25m²），过程噪声 Q 反映行人速度突变；实际用留一法或现场视频标定。答辩时说明这是**可调参数**，来自 `config/default_config.json` 的 `tracker` 段。

4. **“FSM 参数为什么升级 3 帧、降级 5 帧？”**
   答：升级从严（3 帧连续确认，避免瞬时误判升级造成误报），降级从宽（5 帧，避免短暂漏检就立刻撤警）。数值来自 `temporal` 配置，可在现场调节。

5. **“四线程之间如何同步？”**
   答：T1→T2 用互斥锁保护的检测环形缓冲；T2→T3/T4 用 `SystemState` 共享结构 + 小锁粒度读写快照（沿用 `system_integration.md` 的约定）。决策线程 1~5Hz 低频写，不阻塞 25fps 的编码推流。

---

## 7. 风险与注意事项

- **不引入外部库依赖**：谱特征用自包含 Jacobi 分解（~50 行），避免在 RV1106 上引入 Eigen 的编译/内存负担。若 SDK 已带 Eigen 也可直接 `SelfAdjointEigenSolver`。
- **内存**：`computeFeatures()` 每帧堆分配 n×n 的 `adj/dist`（`optimization_review.md` §7.2 已指出）。谱特征复用同一邻接矩阵，不额外重复分配。
- **数据一致性**：Python 与 C++ 的谱特征算法、建边规则、FSM 参数必须一致；阶段 1 的 Python 结果作为阶段 3 C++ 实现的回归基准（同一输入应得到相同 λ₂）。
- **阈值语义**：本方案不改动现有 `danger/warning` 阈值宏（`risk_engine.cc` L7-13），只**新增**谱特征与 FSM 配置；避免与论文表 5-1 的数值一致性讨论（见 `optimization_review.md` §6）发生冲突。
- **四线程改造风险**：线程化是重构，建议阶段 3 先在单线程串行跑通 Kalman+谱特征，验证算法正确后再拆线程；每个阶段保持可运行可演示。