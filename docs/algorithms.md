# 算法文档

> 行人感知与风险决策系统的算法模块文档汇总。当前收录：

1. **Kalman 时序状态估计（离散 Kalman 滤波追踪器）** —— 见「一、模块定位」起。
2. **RiskFSM 风险决策有限状态机** —— 见「九、RiskFSM 风险决策有限状态机」。
3. **图拉普拉斯与谱特征分析** —— 见「十、图拉普拉斯与谱特征分析」。

---

## Kalman 时序状态估计（离散 Kalman 滤波追踪器）

> 模块路径：`src/algorithms/`（Python 实现 `kalman_tracker.py`，C++ 头文件 `kalman_filter.h`）
> 测试路径：`tests/test_kalman_tracker.py`

## 一、模块定位

在行人感知流水线中，YOLO 逐帧检测的脚点（框底中心）经 IPM 逆透视变换映射为地面米坐标后，仍存在两类问题：

1. **单帧抖动**：检测框抖动、IPM 映射误差叠加，导致地面坐标在高频噪声中波动；
2. **单帧漏检**：遮挡、目标过小导致个别帧检不到，轨迹出现空洞。

本模块用离散 Kalman 滤波对连续帧的地面坐标轨迹做**时序状态估计**：

- **平滑**：在常速度（CV）模型约束下，输出位置显著优于单帧观测；
- **漏检外推**：某帧无观测时，按运动模型外推位置，轨迹不中断；
- **速度估计**：输出平滑后的速度 `(vx, vy)`，供风险决策（如碰撞时间 TTC）使用；
- **多目标**：按检测 ID 或最近邻 + 距离门限自动关联，维护多条轨迹。

## 二、数学模型

### 2.1 状态空间模型

- 状态向量（4 维）：

  ```
  x = [px, py, vx, vy]ᵀ
  ```

  其中 `(px, py)` 为地面坐标（米），`(vx, vy)` 为地面速度（m/s）。

- 观测向量（2 维）：

  ```
  z = [px, py]ᵀ
  ```

- 常速度（CV）离散运动模型，固定采样间隔 `dt`：

  ```
  x_{k+1} = F · x_k + w_k
  z_k      = H · x_k + v_k
  ```

  状态转移矩阵：

  ```
        ┌ 1  0  dt  0 ┐
        │ 0  1  0  dt │
  F =   │ 0  0  1   0 │
        └ 0  0  0   1 ┘
  ```

  观测矩阵：

  ```
       ┌ 1  0  0  0 ┐
  H =  │            │
       └ 0  1  0  0 ┘
  ```

### 2.2 噪声模型

- **过程噪声** `Q`：假设随机加速度扰动，标准差 `σ_a`（m/s²，参数 `process_noise`）。

  ```
  Q = σ_a² · G · Gᵀ,   G = ┌ ½dt²  0  ┐
                          │  0   ½dt² │
                          │  dt   0   │
                          └  0   dt   ┘
  ```

- **观测噪声** `R`：两方向独立同分布，标准差 `σ_z`（米，参数 `measurement_noise`）。

  ```
  R = diag(σ_z², σ_z²)
  ```

### 2.3 滤波递推

标准线性 Kalman 滤波：

```
预测： x̂ₖ⁻ = F·x̂ₖ₋₁
       Pₖ⁻ = F·Pₖ₋₁·Fᵀ + Q
更新： S = H·Pₖ⁻·Hᵀ + R
       K = Pₖ⁻·Hᵀ·S⁻¹
       x̂ₖ = x̂ₖ⁻ + K·(zₖ − H·x̂ₖ⁻)
       Pₖ = (I − K·H)·Pₖ⁻
```

## 三、Python 接口

### 3.1 `KalmanFilter`（单目标）

```python
from src.algorithms.kalman_tracker import KalmanFilter

kf = KalmanFilter(dt=1/30.0, process_noise=1.5, measurement_noise=0.5)
kf.init(px, py, vx=0.0, vy=0.0)   # 首个观测初始化
kf.predict()                       # 无观测时外推
kf.correct([px, py])               # 有观测时更新
kf.update([px, py])                # predict + correct 一步
# 读取
kf.state          # [px, py, vx, vy]
kf.position       # [px, py]
kf.velocity       # [vx, vy]
kf.covariance     # 4x4 协方差 P
kf.is_initialized
```

### 3.2 `KalmanTracker`（多目标，推荐使用）

```python
from src.algorithms.kalman_tracker import KalmanTracker

tracker = KalmanTracker(
    dt=1/30.0,          # 固定帧间隔（秒）
    process_noise=1.5,  # 过程噪声 σ_a（m/s²）
    measurement_noise=0.5,  # 观测噪声 σ_z（米）
    gate_dist=2.0,      # 最近邻关联门限（米）
    max_missed=5,       # 连续漏检多少帧后删除轨迹
    min_hits=1,         # 最少关联帧数（判定稳定轨迹）
    match_by_id=True,   # True: 按检测 ID 匹配；False: 强制最近邻
)

# 每帧调用
smoothed = tracker.update(detections)
# detections 支持:
#   dict:      {track_id: (x, y)}
#   list:      [(x, y)] 或 [(x, y, track_id)]
#   ndarray:   shape (N,2) 或 (N,3)
# 返回:
#   {track_id: (x, y, vx, vy)}

# 整帧漏检时外推
smoothed = tracker.predict()

# 辅助接口
tracker.positions()      # {id: (x, y)}
tracker.velocities()    # {id: (vx, vy)}
tracker.track_info()     # {id: {position, velocity, hits, misses, age, last_seen, stable}}
tracker.track_count()
tracker.reset()
```

#### 关联策略

- **按 ID 匹配**（`match_by_id=True` 且检测携带 ID）：轨迹 ID 稳定，适合检测框 ID 在帧间可靠的场景；
- **最近邻匹配**（检测无 ID 或 `match_by_id=False`）：以各轨迹预测位置为基准，构造“检测 × 轨迹”距离代价矩阵，贪心从小到大分配，超过 `gate_dist` 的配对拒绝并新建轨迹。适合检测 ID 不稳定的场景。

## 四、C++ 接口（`kalman_filter.h`）

头文件即实现（header-only），**无第三方依赖**，仅用 C++ 标准库 `<array>`，可直接复制进 Luckfox 嵌入式工程。接口与 Python `KalmanFilter` 一致。

```cpp
#include "kalman_filter.h"

KalmanFilter kf(1.0/30.0, 1.5, 0.5);   // dt, process_noise, measurement_noise
kf.init(1.2, 3.4);                      // 首个观测

while (true) {
    kf.predict();
    kf.correct(zx, zy);                 // 当前帧地面坐标
    double px = kf.x(), py = kf.y();
    double vx = kf.vx(), vy = kf.vy();
    // kf.position() / kf.velocity() 亦可
}
```

成员方法：

| 方法 | 说明 |
|---|---|
| `KalmanFilter(dt, process_noise, measurement_noise)` | 构造，默认 `1/30, 1.5, 0.5` |
| `init(x, y, vx=0, vy=0)` | 首个观测初始化 |
| `predict()` | 一步状态预测（漏检外推） |
| `correct(zx, zy)` | 卡尔曼更新 |
| `update(zx, zy)` | predict + correct 一步 |
| `x() y() vx() vy()` | 读取状态分量 |
| `position()` / `velocity()` | 位置 / 速度（`std::pair`） |
| `state()` | 状态数组引用 |
| `covariance(i, j)` | 协方差元素 |
| `isInitialized()` | 是否已初始化 |

> 多目标追踪在 C++ 侧由上层维护 `std::map<int, KalmanFilter>` 实现，关联逻辑与 Python `KalmanTracker` 一致（最近邻 + 门限）。本头文件提供单目标滤波内核。

## 五、参数调优建议

| 参数 | 作用 | 调优方向 |
|---|---|---|
| `process_noise` σ_a | 对机动的信任度 | 值大 → 跟随机动快、平滑弱；值小 → 轨迹平滑、但急转弯滞后。行人步行建议 `0.5–2.0` |
| `measurement_noise` σ_z | 对单帧观测的信任度 | 值大 → 平滑强；值小 → 更贴近原始检测。与 IPM 定位误差量级相当，建议 `0.3–1.0` |
| `gate_dist` | 关联门限（米） | 过小 → 漏检时容易误建轨迹；过大 → 邻近目标混淆。行人间距一般 `1.5–3.0` |
| `max_missed` | 容忍连续漏检帧数 | 与帧率相关，30 FPS 下建议 `3–10` |

**经验起始值**：`dt=1/30, process_noise=1.5, measurement_noise=0.5, gate_dist=2.0, max_missed=5`。

## 六、与既有流水线集成

```
YOLO 检测框 ──► 脚点(框底中心) ──► IPM 逆透视映射 ──► 地面米坐标
                                                       │
                                                       ▼
                                               KalmanTracker.update(z)
                                                       │
                              ┌────────────────────────┤
                              ▼                        ▼
                        平滑位置 (x,y)            平滑速度 (vx,vy)
                              │                        │
                              ▼                        ▼
                       RiskEngine 拥挤风险          TTC / 碰撞风险
```

集成示例（Python）：

```python
from src.algorithms.kalman_tracker import KalmanTracker

tracker = KalmanTracker()

for frame in video_frames:
    boxes = yolo.detect(frame)            # [(x1,y1,x2,y2,conf), ...]
    feet_px = [( (x1+x2)/2, y2 ) for (x1,y1,x2,y2,_) in boxes]  # 脚点像素
    ground  = [ipm_map(fx, fy) for fx, fy in feet_px]           # 地面米坐标
    smoothed = tracker.update(ground)     # {id: (x, y, vx, vy)}
    # 下游：风险决策引擎使用 smoothed 的位置与速度
```

## 七、测试

运行：

```bash
python -m pytest tests/test_kalman_tracker.py -v
# 或独立运行：
python tests/test_kalman_tracker.py
```

测试覆盖（共 15 项，全部通过）：

| 测试类 | 覆盖点 |
|---|---|
| `TestKalmanFilter` | 初始化、常速度外推、观测更新、协方差正定性、异常参数 |
| `TestSingleTargetSmoothing` | **平滑效果**（核心验收）、速度估计精度 |
| `TestMultiTarget` | 最近邻多目标区分、漏检外推、过期轨迹清理、ID 匹配保持身份 |
| `TestApi` | 输入格式兼容（dict/list/ndarray）、reset、track_info |

**核心验收指标**（合成测试数据，匀速轨迹 + 高斯观测噪声 σ=0.5 m，80 帧，30 FPS）：

| 指标 | 结果 |
|---|---|
| 观测位置 RMSE | 0.429 m |
| Kalman 平滑后 RMSE | **0.147 m**（下降约 66%） |
| 速度估计（真值 [1.5, 0.5] m/s） | ≈ [1.523, 0.507] m/s |
| 单帧漏检外推位置误差 | ≈ 0.10 m |
| 双目标最近邻保持 2 条轨迹 | 37 / 40 帧 |

> 说明：测试数据为脚本生成的**合成带噪声匀速轨迹**（`make_noisy_track`，可复现随机种子），非真实传感器数据，仅用于验证滤波算法正确性与平滑增益。

## 八、文件清单

| 文件 | 说明 |
|---|---|
| `src/algorithms/kalman_tracker.py` | Python 多目标 Kalman 追踪器（含单目标 `KalmanFilter`） |
| `src/algorithms/kalman_filter.h` | C++ header-only 单目标 Kalman 滤波器 |
| `tests/test_kalman_tracker.py` | 单元测试（15 项） |
| `docs/algorithms.md` | 本文档 |

---

## 九、RiskFSM 风险决策有限状态机

> 模块路径：`src/algorithms/risk_fsm.py`（Python）、`src/algorithms/risk_fsm.h`（C++ header-only）
> 测试路径：`tests/test_risk_fsm.py`（13 项，`python tests/test_risk_fsm.py` 可直接运行）

### 9.1 动机

原有 `RiskEngine::evaluate()` 用固定阈值硬切，**无内部状态**，单帧结果直接判定；
检测抖动、脚点抖动、置信度临界波动会使等级在 SAFE/WARNING/DANGER 之间逐帧闪烁，
且推理偶发掉帧（`n=0`）时直接判 SAFE。RiskFSM 用带内部状态的有限状态机解决三类问题：

| 问题 | 手段 |
|---|---|
| 阈值附近抖动 | **滞回（hysteresis）**：升级阈值 > 降级阈值，形成重叠滞回带 |
| 单帧突变/误检 | **确认帧（debounce）**：升级需连续 `confirm_up` 帧，降级需连续 `confirm_down` 帧 |
| DANGER 瞬时回落 | **状态保持（hold）**：进入 DANGER 至少保持 `danger_hold_frames` 帧 / `danger_hold_ms` 毫秒 |

### 9.2 综合拥挤度评分

```
score = w1 * norm(n) + w2 * density + w3 * r_gcc
norm(n) = min(n / n_ref, 1.0)
score += trend_bias * clamp(trend, -1, 1)   # 可选运动趋势加成
```

- `n` 行人数、`density` 网络密度、`r_gcc` 最大连通分量占比（∈ [0,1]）；
- `norm(n)` 以 `n_ref=20` 为参考值封顶；
- `trend`（可选）∈ [-1,1]，`>0` 表示聚集/人数上升，小幅抬升评分；
- 默认权重 `w1=0.4, w2=0.3, w3=0.3`。

默认阈值依据 `results/baseline/results.json` 真实检测结果抽样标定：DANGER 样本评分约
0.67~0.68，WARNING 样本约 0.62~0.66，故取 `up_danger=0.65`、`up_warning=0.40`。

### 9.3 状态转移与滞回

状态集合 `{SAFE, WARNING, DANGER}`，**DANGER 只能从 WARNING 阶梯式到达**。采用状态相关
（动态）阈值形成滞回带：

```
SAFE    --(score >= up_warning)-->    WARNING
WARNING --(score >= up_danger )-->    DANGER
WARNING --(score <= down_safe )-->    SAFE
DANGER  --(score <= down_warning)-->  WARNING
```

要求 `up_warning > down_safe`、`up_danger > down_warning`。默认
`up_warning=0.40, up_danger=0.65, down_safe=0.28, down_warning=0.55`。

- **确认帧**：升级累计连续满足帧数达 `confirm_up`（默认 3）才转移；降级累计连续
  满足帧数达 `confirm_down`（默认 5）才转移；中途回落计数清零。
- **状态保持**：进入 DANGER 时记录 `entry_ms / entry_frame`，降级前须同时满足已过
  `danger_hold_frames` 帧（默认 10）与 `danger_hold_ms` 毫秒（默认 1000）。任一维度
  为 0 即禁用；`now_ms=None`（C++ `<0`）时禁用毫秒维度。

### 9.4 配置项

`RiskFSMConfig`（Python dataclass 与 C++ 结构体字段一致）：

| 字段 | 默认值 | 含义 |
|---|---|---|
| `w1` / `w2` / `w3` | 0.4 / 0.3 / 0.3 | 评分权重 |
| `n_ref` | 20 | 人数归一化参考值 |
| `up_warning` | 0.40 | SAFE→WARNING 升级阈值 |
| `up_danger` | 0.65 | WARNING→DANGER 升级阈值 |
| `down_safe` | 0.28 | WARNING→SAFE 降级阈值 |
| `down_warning` | 0.55 | DANGER→WARNING 降级阈值 |
| `confirm_up` | 3 | 升级需连续帧数（≥1） |
| `confirm_down` | 5 | 降级需连续帧数（≥1） |
| `danger_hold_frames` | 10 | DANGER 至少保持帧数（0=禁用） |
| `danger_hold_ms` | 1000 | DANGER 至少保持毫秒 |
| `trend_bias` | 0.05 | 运动趋势加成幅度 |

构造时校验：权重非负且和 > 0、`n_ref>0`、确认帧 ≥1、阈值 ∈ [0,1] 且满足滞回一致性，
否则抛 `ValueError`。

### 9.5 Python 用法

```python
import sys; sys.path.insert(0, "src")
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig

fsm = RiskFSM(RiskFSMConfig(confirm_up=3, confirm_down=5))
level = fsm.update(
    {"n": 13, "density": 0.615, "r_gcc": 0.769, "clustering": 1.0, "trend": 0.5},
    now_ms=1234,
)   # -> "SAFE" / "WARNING" / "DANGER"
print(level, fsm.score)
```

### 9.6 C++ 用法（`risk_fsm.h`）

纯头文件、C++11、无第三方依赖，可直接 include 到 Luckfox 边缘端工程：

```cpp
#include "algorithms/risk_fsm.h"

pedestrian::RiskFSMConfig cfg;      // 默认值与 Python 一致
pedestrian::RiskFSM fsm(cfg);
// 每帧：
pedestrian::FsmFeatures f;
f.n = engine.features().n;
f.density = engine.features().density;
f.r_gcc   = engine.features().r_gcc;
f.clustering = engine.features().clustering;
int64_t now_ms = /* 单调时钟毫秒 */;
pedestrian::RiskLevel lvl = fsm.update(f, now_ms);
const char* name = pedestrian::RiskFSM::levelName(lvl);
```

`FsmFeatures` 为 `risk_engine.h` 中 `RiskFeatures` 的轻量子集，便于直接转换。
建议将 FSM 挂在独立决策线程（1~5Hz），与推理/编码主循环解耦。

### 9.7 单元测试

运行 `python tests/test_risk_fsm.py`（无需 pytest），覆盖 13 项：

- 短时抖动不触发（交替单帧高分/低分不升级）；
- 持续聚集才升级（连续确认帧达标才 SAFE→WARNING→DANGER）；
- DANGER 不可直达（不允许 SAFE 单帧跳 DANGER）；
- WARNING 滞回带（带内从 WARNING 不掉回 SAFE）；
- 降级需确认帧（连续 `confirm_down` 帧跌破降级阈值才降级）；
- DANGER→WARNING→SAFE 阶梯降级；
- DANGER 状态保持（帧数 / 毫秒双维度）；
- 评分公式与 [0,1] 封顶、配置校验、reset；
- C++ 头文件默认值与 Python 默认值一致性。

### 9.8 后续优化建议

- 特征 EMA 平滑：对 `n/density/r_gcc` 先做指数滑动平均（alpha≈0.4~0.6）再送入 FSM；
- 无数据保持：`n=0` 时当前依赖确认帧与 DANGER 保持兜底，如需更严格策略可增加
  `no_data_hold_frames` 分支并打 `no-data` 标记；
- 阈值参数化：写入 `config/default_config.json`，按场景档位（gathering/walkway/platform）
  运行时切换。

---

### 文件清单

| 文件 | 说明 |
|---|---|
| `src/algorithms/risk_fsm.py` | Python 风险决策有限状态机 |
| `src/algorithms/risk_fsm.h` | C++ header-only 风险决策有限状态机 |
| `tests/test_risk_fsm.py` | 单元测试（13 项） |

---

## 十、图拉普拉斯与谱特征分析

> 模块路径：`src/algorithms/graph_laplacian.py`（Python）、`src/algorithms/graph_laplacian.h`（C++ header-only）
> 测试路径：`tests/test_graph_laplacian.py`（19 项，`python -m unittest discover -s tests -v`）

### 10.1 目标

实现「图论空间拓扑与聚类」算法增强：基于行人地面坐标(米)构建邻接矩阵 `A`，
推导图拉普拉斯矩阵 `L = D - A`，计算谱特征（Fiedler 值/代数连通度、谱半径、
特征熵）用于评估人群聚集结构，并与既有 `RiskEngine::computeFeatures()` 兼容。

### 10.2 数学模型

**邻接矩阵**（`edge_dist` 默认 2.0m，与 risk_engine 一致）：

```
A[i][j] = 1,  若 i ≠ j 且 ||P_i - P_j||₂ < edge_dist
A[i][j] = 0,  否则
```

**拉普拉斯矩阵** `L = D - A`，其中度数矩阵 `D = diag(d_i)`，`d_i = Σ_j A[i][j]`：

```
L[i][i] = d_i
L[i][j] = -1,  若 i 与 j 相连；否则 0
```

`L` 对称半正定，特征值 `0 = λ₁ ≤ λ₂ ≤ ... ≤ λ_n`；零特征值重数 = 连通分量数；
`trace(L) = Σλ_i = 2m`。

### 10.3 谱特征

| 字段 | 定义 | 说明 |
|------|------|------|
| `fiedler` | `λ₂`（第二小特征值） | 图连通 ⟺ `λ₂>0`；越大越难切开 |
| `algebraic_connectivity` | `λ₂` | 与 `fiedler` 数学等价（同一数值） |
| `spectral_radius` | `λ_max`（最大特征值） | 人群网络整体稠密/活跃程度；`K_n` 为 `n` |
| `eig_entropy` | `-Σ_i p_i ln p_i`，`p_i = λ_i/Σλ`（`λ_i>0`） | 特征谱均匀程度(nats)；`K_n` 为 `ln(n-1)`；空图为 0 |

### 10.4 网络特征（与 risk_engine 兼容）

`compute_laplacian_features` 返回的 `n/m/density/r_gcc/clustering` 与
`src/luckfox_rtsp_yolov5/src/risk_engine.cc` 及 `scripts/crowd_test.py::compute_features`
完全一致：`density = m/[n(n-1)/2]`、`r_gcc`=最大连通分量占比(BFS)、
`clustering`=平均局部聚类系数。可无缝替换/增强现有特征。

### 10.5 Python 用法

```python
import sys; sys.path.insert(0, "src/algorithms")
from graph_laplacian import build_adjacency, compute_laplacian_features

pts = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]  # 全连接 K4
feat = compute_laplacian_features(pts, edge_dist=2.0)
# {'n':4, 'm':6, 'density':1.0, 'r_gcc':1.0, 'clustering':1.0,
#  'fiedler':4.0, 'spectral_radius':4.0, 'algebraic_connectivity':4.0,
#  'eig_entropy':1.0986...}
```

### 10.6 C++ 用法（`graph_laplacian.h`）

单头文件、仅标准库（`<vector>/<utility>/<cmath>/<algorithm>`），不依赖 Eigen/OpenCV，
适合 Luckfox 板端交叉编译。特征值采用经典 **Jacobi 旋转法**（对称矩阵，`O(n³)`），
适合 `n` 通常 < 100 的行人人群图。

```cpp
#include "algorithms/graph_laplacian.h"
GraphLaplacian gl(2.0);          // edge_dist = 2.0m
gl.addPoint(x0, y0);             // 地面坐标(米)
// ...
SpectralFeatures f = gl.computeFeatures();
// f.n, f.m, f.density, f.r_gcc, f.clustering,
// f.fiedler, f.spectral_radius, f.algebraic_connectivity, f.eig_entropy
```

### 10.7 板端验证说明

- Jacobi 算法逻辑已在 x86 上通过 `numpy.linalg.eigvalsh` 交叉验证（见测试类
  `TestJacobiAlgorithmLogic`）；
- 移植到 Luckfox(RKNN/ARM) 后需在真实交叉编译链下重新编译验证（`-std=c++11`），
  用与 Python 测试相同的分散/聚集/全连接点集比对输出，误差应在 `1e-6` 量级；
- 实时视频流建议限制单帧行人数量上限（如 `n ≤ 64`）以保证帧周期内完成。

### 10.8 单元测试

`python -m unittest discover -s tests -p "test_*.py" -v`，19 项覆盖：

- 分散点集：无连边，特征值全 0，`r_gcc=1/n`，特征熵=0；
- 聚集点集（路径图 P5）：验证 `fiedler=2-2cos(π/5)`、`spectral_radius=2-2cos(4π/5)`；
- 全连接点集（完全图 K4）：验证 `density=1`、`clustering=1`、`fiedler=n`、`eig_entropy=ln(n-1)`；
- 谱特征单调性：分散 → 聚集 → 全连接，密度/代数连通度/谱半径单调递增；
- 邻接矩阵性质：对称、零对角、0/1；
- 与 `risk_engine::computeFeatures` 兼容性（含随机点集比对）。

### 10.9 文件清单

| 文件 | 说明 |
|---|---|
| `src/algorithms/graph_laplacian.py` | Python 谱特征分析模块 |
| `src/algorithms/graph_laplacian.h` | C++ header-only 谱特征分析 |
| `tests/test_graph_laplacian.py` | 单元测试（19 项） |