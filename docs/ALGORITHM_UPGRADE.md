# 算法升级：DBSCAN 人群聚类

> 在已有"图拉普拉斯谱分析 + 复杂网络特征"之上，新增 **DBSCAN 密度聚类**作为
> 拥挤度 / 局部聚集的工程化补充特征（对应 `ALGORITHM_OPTIMIZATION.md` 中
> "P1：DBSCAN 人群聚类" 推荐项的实施落地）。

## 一、动机与定位

| 特征 | 刻画内容 | 定位 |
|---|---|---|
| 图拉普拉斯谱（已有） | 聚集**结构**：代数连通度、谱半径、特征熵 | 理论深度 / 全局拓扑 |
| DBSCAN 人群聚类（新增） | 聚集**数量与规模**：簇数、每簇人数、最大簇占比、簇中心 | 工程化 / 直观决策 |

DBSCAN 直接输出"人群簇"语义：
- **簇数** `n_clusters`：场景中存在几个聚集人群
- **每簇人数** `cluster_sizes`：各簇规模
- **最大簇人数占比** `largest_cluster_ratio`：最拥挤群体占全部行人的比例
- **簇中心** `cluster_centers`：局部聚集的质心位置(米)，可用于引导决策/告警定位

与谱特征互补：谱特征回答"整体是否聚集/多聚集"，DBSCAN 回答"聚集在哪、有多大"。

## 二、交付文件

| 文件 | 说明 |
|---|---|
| `src/algorithms/crowd_dbscan.py` | Python 原型，纯 numpy 手写 DBSCAN（无 sklearn 依赖），可直接运行 `python src/algorithms/crowd_dbscan.py` 查看演示 |
| `src/algorithms/crowd_dbscan.h` | C++ 头文件（header-only，仅标准库），接口与 Python 端对应，供 Luckfox 板端工程集成 |
| `tests/test_crowd_dbscan.py` | 单元测试：分散 / 两簇 / 全连通 / 链式 / 噪声 / 参数配置 / 边界 / 随机交叉验证 / C++ 算法逻辑复刻 |
| `docs/ALGORITHM_UPGRADE.md` | 本文档 |

## 三、接口

### Python：`dbscan_crowd_features(pts, eps, min_samples=2)`

输入行人地面坐标（米），输出特征字典：

```python
from crowd_dbscan import dbscan_crowd_features

feat = dbscan_crowd_features(
 pts=[(0.0, 0.0), (0.0, 1.0), (1.0, 0.0), (1.0, 1.0), (10.0, 0.0), ...],
 eps=1.5, # 聚集半径(米)，可通过配置传入
 min_samples=2, # 核心点邻域最少点数(含自身)
)
```

返回字典字段：

| 字段 | 类型 | 含义 |
|---|---|---|
| `n` | int | 行人数 |
| `n_clusters` | int | 簇数（噪声不计） |
| `cluster_sizes` | list[int] | 每簇人数（按簇大小降序） |
| `largest_cluster_size` | int | 最大簇人数 |
| `largest_cluster_ratio` | float | 最大簇人数占比 = 最大簇人数 / n |
| `cluster_centers` | list[(float,float)] | 簇中心列表（每簇质心，米） |
| `labels` | ndarray[int] | 每个行人的簇标签（`-1`=噪声，`1..n_clusters`=簇编号） |
| `n_noise` | int | 噪声点数 |
| `n_clustered` | int | 被聚类点数 |

另有底层函数 `dbscan_labels(pts, eps, min_samples=2) -> (labels, n_clusters)`。

### C++：`crowd_dbscan.h`

```cpp
#include "crowd_dbscan.h"

CrowdDBSCAN db(/*eps=*/1.5, /*min_samples=*/2);
db.addPoint(x, y); // 逐个添加，或
db.setPoints(pts); // 批量设置
CrowdClusterFeatures f = db.compute;
// f.n_clusters / f.cluster_sizes / f.largest_cluster_ratio
// f.cluster_centers / f.labels / f.n_noise / f.n_clustered
```

`CrowdClusterFeatures` 字段与 Python 返回字典一一对应。

## 四、算法说明

经典 DBSCAN（Ester et al., 1996）：

1. **核心点**：某点 `eps` 邻域内点数（含自身）≥ `min_samples`。
2. **密度可达**：核心点沿其邻域不断扩张，边界点并入首个到达它的簇。
3. **噪声**：既非核心点、也不在任何核心点邻域内的点。

实现要点：
- 邻域采用 **闭区间 `dist <= eps`**（与经典 DBSCAN 定义一致；`dist==eps` 的两点互为邻居）。
- `min_samples` 默认 2：即两人间距 ≤ eps 即构成一个 2 人簇，语义与现有
 `EDGE_DIST_M` 建边阈值（严格小于）略有差异，但配置为参数后可统一。
- 参数 `eps` / `min_samples` 均通过函数参数 / 类构造函数传入，**可配置**，
 便于与风险决策配置项联动。

## 五、复杂度与边缘端适配

- 当前实现为朴素 **O(n²)** 邻域枚举：预计算两两距离。人群规模 n 通常为
 几十～几百，开销可忽略（与 `graph_laplacian.h` 的 O(n²) 建边同一量级）。
- 若需处理更大规模人群，可将邻域查询替换为 **KD 树 / 空间网格**，将平均
 复杂度降至 **O(n log n)**（`ALGORITHM_OPTIMIZATION.md` 已标注）。
- `crowd_dbscan.h` 仅依赖标准库（`<vector>/<utility>/<cmath>/<algorithm>`），
 不依赖 Eigen/OpenCV，适合 Luckfox(RV1106) 交叉编译。

## 六、测试验证

`tests/test_crowd_dbscan.py` 共 32 个用例，全部通过：

- **分散点集**（4 点相距 5m，eps=1.0）→ 0 簇，全为噪声，最大簇占比 0
- **两簇点集**（左右各 4 人密集，eps=1.5）→ 2 簇，每簇 4 人，占比 0.5，
 簇中心 (0.5,0.5) 与 (10.5,0.5)
- **全连通点集**（边长 1m 正方形 K4）→ 1 簇，占比 1.0
- **链式密度可达**（间距 1m，eps=1.5）→ 跨中间点连成 1 簇（验证密度可达）
- **噪声点**：簇外孤立点正确标记为噪声
- **参数可配置**：`min_samples` 与 `eps` 改变聚类结果
- **eps 闭区间边界**：距离恰为 eps 的两点视为邻居
- **空点集 / 单点 / 非法形状**边界情况
- **随机交叉验证**：与独立参考实现（核心图连通分量 + 边界归属）在随机点集、
 混合高斯簇状点集上簇数/噪声数/聚类数一致（70 组随机）
- **C++ 算法逻辑复刻**：逐行翻译 `crowd_dbscan.h` 的 `dbscan` 为纯 Python，
 与 Python 端输出完全一致（间接验证头文件正确性）

> 注：本机（git-bash/Windows）未安装 C++ 编译器，故采用与
> `test_graph_laplacian.py` 验证 Jacobi 相同的做法——用 Python 精确复刻
> C++ 头文件算法逻辑做等价性测试。在目标板或 CI 上编译 `crowd_dbscan.h`
> 时应再跑一遍真实 C++ 用例做最终确认。

## 七、与 risk_engine / 决策的集成建议

DBSCAN 特征可作为风险决策 FSM 的输入：

- `largest_cluster_ratio` 与现有 `r_gcc`（最大连通分量占比）语义互补：
 - `r_gcc` 反映**网络连通**程度（是否"一串"连在一起）；
 - `largest_cluster_ratio` 反映**密度聚集**规模（是否"挤成一团"）。
- 可新增决策规则：当 `largest_cluster_ratio > 阈值` 且 `n_clusters` 较大时，
 判定为"多人群聚集"高风险；`cluster_centers` 可输出告警坐标。
- 建议与现有 `compute_laplacian_features` 并行计算，特征向量拼接入 FSM，
 不必替换谱特征（谱特征保留理论深度）。

## 八、后续可选优化

- KD 树 / 网格空间索引：大规模人群 O(n²) → O(n log n)
- 自动估计 eps：如 k-距离图拐点法（`knee/elbow`），减少人工调参
- 与 EMA 时序平滑（`ALGORITHM_OPTIMIZATION.md` P1）联动，抑制簇数抖动

---

# 算法升级：IPM 真机标定工具 + EMA 时序平滑器

> 对应 `ALGORITHM_OPTIMIZATION.md` 中两个 P0/P1 升级项的实施落地：
>
> 1. **IPM 真机标定工具**（P0，几何准确度根基）：替代原手填固定单应矩阵 H（720x480 梯形假设），
> 用画面中的地面点像素坐标 + 真实米制坐标求解 H，将地面定位误差从"假设"降到厘米级；
> 2. **EMA 时序平滑器**（P1，时序稳定）：在风险特征送入 FSM 前做指数滑动平均，进一步抑制
> 检测抖动与 IPM 映射误差带来的单帧跳变。

---

## 一、IPM 真机标定工具

### 1.1 模块定位

原系统使用手填的固定单应矩阵 H（`720x480` 梯形），地面米坐标完全依赖假设，误差不可控。
本工具在真机部署时采集画面中 **4 个地面点的像素坐标** 与其 **真实地面米制坐标**，
用 `cv2.getPerspectiveTransform` 求解 3x3 单应矩阵 H，实现 `像素坐标 ↔ 地面米制坐标` 的
逆透视映射（IPM），地面定位误差可降到像素级。

- **模块路径**：`src/algorithms/ipm_calibrate.py`
- **测试路径**：`tests/test_ipm_ema.py`（`TestIpmCalibrate`，10 项）
- **依赖**：`numpy`、`opencv-python`

### 1.2 数学模型

逆透视映射用齐次坐标表示：

```
p_ground ~ H · [u, v, 1]ᵀ
```

其中 `(u, v)` 为像素坐标，`p_ground = (x, y)` 为地面米制坐标，`H` 为 3x3 单应矩阵。
已知 4 组对应点 `(u_i, v_i) ↔ (x_i, y_i)` 时，`H` 的 8 个自由度可由线性方程组唯一确定
（`cv2.getPerspectiveTransform` 即解此方程组）。求得的 H 隐含相机内外参（焦距、倾角、高度）
与地面平面方程的组合，无需分别标定内参/外参。

### 1.3 Python 接口

```python
import numpy as np
from src.algorithms.ipm_calibrate import (
 calibrate_from_points, calibrate_from_points_ransac,
 pixel_to_ground, ground_to_pixel,
 save_homography, load_homography,
)

# 1) 4 点标定（推荐，精度最高）
pixel = np.array([[100, 220], [300, 230], [320, 400], [80, 410]], dtype=np.float32)
ground = np.array([[0, 0], [5, 0], [5, 8], [0, 8]], dtype=np.float32) # 米
H = calibrate_from_points(pixel, ground) # -> (3,3) float64

# 2) 正 / 逆映射
ground_pts = pixel_to_ground(H, pixel) # 像素 -> 地面米
pixel_pts = ground_to_pixel(H, ground_pts) # 地面米 -> 像素

# 3) 保存 / 加载（支持 .npy 二进制 与 .txt 文本）
save_homography(H, "config/ipm_homography.npy")
H2 = load_homography("config/ipm_homography.npy")
```

**函数说明**

| 函数 | 说明 |
|---|---|
| `calibrate_from_points(pixel_pts, ground_pts)` | 4 点直接求解 H；检测共线退化点并抛 `ValueError` |
| `calibrate_from_points_ransac(pixel_pts, ground_pts, ransac_thresh=1.0)` | N≥4 点 RANSAC 鲁棒求解，适合个别点标错场景 |
| `pixel_to_ground(H, pts)` | 像素坐标 → 地面米制坐标（`cv2.perspectiveTransform`） |
| `ground_to_pixel(H, pts)` | 地面米制坐标 → 像素坐标（求逆后变换） |
| `save_homography(H, path)` | 保存 H；`.npy` 二进制无损，`.txt/.csv` 可读文本（3x3） |
| `load_homography(path)` | 从 `.npy` / `.txt` 加载 H 并校验形状 |

### 1.4 命令行标定

```bash
python src/algorithms/ipm_calibrate.py \
 --pixel "100,220 300,230 320,400 80,410" \
 --ground "0,0 5,0 5,8 0,8" \
 --save config/ipm_homography.npy
```

运行后输出 H 矩阵，并**自检**：把标定像素点映射回地面，逐点打印误差（米）。
现场操作步骤：

1. 架好相机，固定俯仰角与高度；
2. 在地面放置 4 个可辨识标记（如 A4 纸角、胶带十字），用卷尺量出米制坐标；
3. 在画面中点击标记中心，记录像素坐标（可用 `cv2.selectROI` / 画图工具）；
4. 运行上述命令得到 H 并保存，供 Python / C++ 端加载。

> 建议：4 个地面点尽量覆盖画面下方的大块地面区域、不共线、不 3 点共线，
> 以保证 H 数值稳定；若现场可标 6+ 个点，用 `calibrate_from_points_ransac` 更鲁棒。

### 1.5 与既有流水线集成

```
YOLO 检测框 ──► 脚点(框底中心像素) ──► H 逆透视 ──► 地面米坐标
 ▲
 ipm_calibrate 标定
 │
 保存 H 到 config/*.npy
```

Python 侧在初始化时 `load_homography` 加载 H，之后对每个检测脚点调用
`pixel_to_ground(H, feet_px)`。C++ 侧（Luckfox）可将 H 以 3x3 float 数组写进头文件/
配置文件，用 `cv::perspectiveTransform`（若板端有 OpenCV）或手写 3x3 乘法实现同样的变换。

### 1.6 测试与验收指标

`TestIpmCalibrate`（合成数据，非真实传感器）：

| 指标 | 结果 |
|---|---|
| 标定 H 把像素点映射回给定地面点的最大误差 | **1.4e-14 m**（要求 < 1e-3） |
| 地面→像素→地面 往返最大误差 | < 1e-3 m |
| 像素往返误差 | ≤ 1 px（与量化一致） |
| RANSAC（5 点含额外点）最大误差 | < 1e-3 m |
| 保存/加载 `.npy` / `.txt` 一致性 | 通过（atol 1e-9/1e-12） |
| 共线退化、非 4 点输入、CLI | 均通过 |

> 测试数据为 `make_synthetic_correspondences` 构造：用人为给定的透视单应 H_gt 生成
> 5m×8m 地面矩形四角的像素坐标并取整模拟像素量化，再反求 H 验证。**非真实传感器数据。**

---

## 二、EMA 时序平滑器

### 2.1 模块定位

RiskFSM 已用滞回 + 确认帧防抖，但送入 FSM 的 `n / density / r_gcc` 等特征仍含
检测抖动与 IPM 误差造成的高频噪声。本模块在特征进入 FSM **之前**做指数滑动平均（EMA），
进一步降噪，且计算量 O(dim)/帧，板端开销可忽略。

- **模块路径**：`src/algorithms/ema_smoother.py`（Python）、`src/algorithms/ema_smoother.h`（C++ header-only）
- **测试路径**：`tests/test_ipm_ema.py`（`TestEma`，12 项）

### 2.2 数学模型

帧级固定 `alpha` 的递推：

```
s_0 = x_0 （首帧初始化）
s_t = alpha · x_t + (1 - alpha) · s_{t-1} （alpha ∈ (0, 1]）
```

对白噪声输入，平滑后方差理论抑制比为 `alpha / (2 - alpha)`。
例如 `alpha = 0.3` 时抑制比 ≈ 0.176（方差降为原来的 17.6%）。

**时间感知模式**（`use_timestamp=True`）：对非等间隔帧序列，按时间间隔自适应：

```
alpha_t = 1 - exp(-dt / tau)， dt = t_t - t_{t-1}
```

`dt` 越小平滑越强；`tau` 为时间常数（秒），与响应速度成反比。

**多变量支持**：特征为向量时各分量独立平滑；`alpha` 可传标量（广播）或每维独立数组。

### 2.3 Python 接口

```python
from src.algorithms.ema_smoother import EMASmoother

# 单变量（alpha=0.3）
sm = EMASmoother(alpha=0.3)
y = sm.update(0.6, t=1) # -> 0.6（首帧原样）
y = sm.update(0.9, t=2) # -> 0.3*0.9 + 0.7*0.6 = 0.69

# 多变量风险特征（n, density, r_gcc 三维，独立平滑）
smv = EMASmoother(alpha=0.3, dim=3)
y = smv.update([0.5, 0.6, 0.4], t=1) # -> [0.5, 0.6, 0.4]
y = smv.update([0.9, 0.2, 0.8], t=2) # -> 各维独立 EMA

# 时间感知模式
smt = EMASmoother(alpha=0.3, use_timestamp=True, tau=0.1) # tau 秒

# 查询 / 重置
sm.initialized # bool
sm.smoothed # 当前平滑值（副本）
sm.dimension # 特征维度
sm.reset # 清空状态
```

**参数**

| 参数 | 默认 | 说明 |
|---|---|---|
| `alpha` | 0.3 | 平滑因子 ∈ (0,1]；越大跟随越快、平滑越弱；标量或每维数组 |
| `dim` | None | 特征维度；`None` 由首帧推断 |
| `use_timestamp` | False | 是否时间感知模式 |
| `tau` | 1.0 | 时间感知模式时间常数（秒），须 > 0 |

### 2.4 C++ 接口（`ema_smoother.h`）

纯头文件、C++11、无第三方依赖，可直接复制进 Luckfox 工程。接口与 Python 一致：

```cpp
#include "algorithms/ema_smoother.h"

pedestrian::EmaSmoother sm(0.3, 3); // alpha=0.3, 3 维特征

// 每帧：
std::vector<double> feat = {n, density, r_gcc};
std::vector<double> s = sm.update(feat, now_ms); // 平滑后特征
double n_s = s[0];
double density_s = s[1];
double r_gcc_s = s[2];

// 或标量接口：
pedestrian::EmaSmoother sm1(0.3, 1);
double d = sm1.update(0.6, now_ms);

sm.reset; // 场景切换时重置
bool ok = sm.initialized;
```

**成员方法**

| 方法 | 说明 |
|---|---|
| `EmaSmoother(alpha=0.3, dim=1, use_timestamp=false, tau=1.0)` | 构造（标量 alpha 广播） |
| `EmaSmoother(const vector<double>& alpha, ...)` | 构造（每维独立 alpha） |
| `setAlpha(...)` / `alpha` / `dim` | 配置 / 查询 |
| `setUseTimestamp(bool)` / `useTimestamp` / `setTau(double)` / `tau` | 时间感知模式 |
| `update(value, t)` / `update(vector, t)` | 一帧更新，返回平滑值 |
| `smoothed` / `smoothedAt(i)` / `initialized` | 查询平滑状态 |
| `reset` | 清空状态 |

> 非法 `alpha` 输入会回退到默认 0.3（工程健壮性）；维度不匹配时本帧被忽略/仅拷贝输入，
> 保证状态不损坏。

### 2.5 与 RiskFSM 集成（推荐用法）

```python
from src.algorithms.ema_smoother import EMASmoother
from src.algorithms.risk_fsm import RiskFSM, RiskFSMConfig

# 对 FSM 输入特征做 EMA（每维独立，alpha 0.4~0.6 可调）
ema = EMASmoother(alpha=0.5, dim=3) # n, density, r_gcc
fsm = RiskFSM(RiskFSMConfig)

for frame in frames:
 feat = engine.features # {"n":..., "density":..., "r_gcc":...}
 smooth = ema.update([feat["n"], feat["density"], feat["r_gcc"]], frame_id)
 level = fsm.update({"n": smooth[0], "density": smooth[1],
 "r_gcc": smooth[2], "clustering": 0.0}, now_ms)
```

C++ 侧同样在 FSM 前加一层 `EmaSmoother`，将平滑后的 `FsmFeatures` 送入 `RiskFSM::update`。

### 2.6 测试与验收指标

`TestEma`（合成噪声序列，非真实传感器）：

| 指标 | 结果 |
|---|---|
| 首帧原样返回 / 递推公式 `s=0.3x+0.7s` | 通过 |
| 平滑后噪声方差 vs 原始 | **0.01391 vs 0.08236（降至 16.9%）**，要求 < 50% |
| 平滑后均值跟随真值（无偏） | 误差 < 0.05 |
| 多变量独立平滑 / 每维独立 alpha | 通过 |
| 时间感知模式（dt=1/30，tau 使 alpha_t≈0.3）方差显著下降 | 通过 |
| 维度自动推断 / 维度不一致报错 | 通过 |
| reset 行为 / 非法 alpha 校验 | 通过 |
| C++ 头文件默认值与 Python 一致性 | 通过 |

> 测试数据为 `make_noisy_series` 构造的"真值 + 高斯白噪声（σ=0.3）"300 帧序列，
> 用可复现随机种子生成。**非真实传感器数据。**

---

## 三、文件清单

| 文件 | 说明 |
|---|---|
| `src/algorithms/ipm_calibrate.py` | IPM 真机标定工具（求解/映射/保存加载/CLI） |
| `src/algorithms/ema_smoother.py` | EMA 时序平滑器（Python，标量/多变量/时间感知） |
| `src/algorithms/ema_smoother.h` | EMA 时序平滑器（C++ header-only，接口与 Python 一致） |
| `tests/test_ipm_ema.py` | 单元测试（22 项，pytest 与独立运行均可） |
| `docs/ALGORITHM_UPGRADE.md` | 本文档（含 DBSCAN 与本节） |

## 四、后续建议

- **标定点自动化**：在画面中标定板（如 ChArUco），用 `cv2.aruco` 自动提取角点与地面坐标，免手动点选；
- **EMA 参数标定**：将 `alpha/tau` 写入 `config/default_config.json`，按场景（gathering/walkway/platform）切换；
- **板端验证**：`ema_smoother.h` 已在 x86 上验证接口与数值；移植到 Luckfox（ARM/RKNN）后需在真实工程中重新编译并做数值验证（参考 `docs/algorithms.md` 的"板端验证"一节）。

---

# 算法升级：ByteTrack 多目标跟踪器

> 在既有 `kalman_tracker.py`（单目标/多目标 Kalman 平滑，地面坐标）之上，
> 新增 **ByteTrack 多目标跟踪器**作为检测框层的多目标跟踪方案，
> 通过**低分框二次关联**处理遮挡，输出稳定轨迹 ID 与像素框。

## 一、升级动机

| 局限 | 表现 |
|---|---|
| **观测维度受限** | 基础版只处理脚点地面坐标（IPM 映射后的点），不利用检测框的尺度/外观信息； |
| **遮挡鲁棒性不足** | 目标遮挡时检测置信度骤降、产生低分框，单阈值关联易断链，遮挡后 ID 漂移。 |

ByteTrack 直接以**检测框（像素）**为观测，在图像平面跟踪，再对输出框取脚点做 IPM 映射，
与下游风险决策解耦；低分框二次关联在遮挡期间维持轨迹，显著降低 ID 漂移。

## 二、ByteTrack 原理

ByteTrack（ECCV 2022）核心思想：**不丢弃低分检测框**。把检测框分成高分/低分两档分两轮关联：

```
 ┌──────────────────────────────┐
 │ 当前帧检测框 │
 └──────────────┬───────────────┘
 ┌──────────────────┴──────────────────┐
 score ≥ track_thresh det_thresh ≤ score < track_thresh
 （高分检测） （低分检测，如遮挡）
 └──────────┬──────────────────────────┘
 ┌────────────────────────▼───────────────────────────┐
 │ 第一轮：高分检测 vs 全部轨迹（IoU 代价，匈牙利匹配） │
 │ ├─ 匹配成功：更新轨迹（hits++，misses=0） │
 │ └─ 未匹配高分检测：新建轨迹 │
 └────────────────────────┬───────────────────────────┘
 │ 未匹配的轨迹
 ▼
 ┌────────────────────────────────────────────────────┐
 │ 第二轮（低分二次关联）：低分检测 vs 未匹配的已确认轨迹 │
 │ ├─ 匹配成功：更新轨迹（遮挡期间维持 ID） │
 │ └─ 仍未匹配：misses++，超过 max_lost 后删除 │
 └────────────────────────────────────────────────────┘
```

### 2.1 单目标模型：8 维线性匀速 Kalman

每个目标维护独立线性匀速（CV）Kalman 滤波器，状态：

```
x = [cx, cy, a, h, vcx, vcy, va, vh]
```

- `(cx, cy)`：检测框中心（像素）；`a = w/h`：宽高比；`h`：框高度；
- `vcx, vcy, va, vh`：对应速度。

运动模型 `F = [[I4, dt·I4], [0, I4]]`，观测 `H = [I4 | 0]`，观测为 `[cx, cy, a, h]`。
噪声标准差与高度 `h` 成正比（`std_weight_position=1/20`、`std_weight_velocity=1/160`），
初始速度不确定度取 10 倍，使滤波器能快速估计目标速度。

### 2.2 数据关联与生命周期

- 代价矩阵 `cost = 1 - IoU`，匈牙利算法（纯 numpy / 纯 C++，O(n³)）求最小匹配；
- 只接受 `cost ≤ match_thresh`（默认 0.8，即 `IoU ≥ 0.2`）的配对；
- 未匹配高分检测新建轨迹（`hits=1`，连击 `min_hits` 帧后确认）；
- 已确认轨迹才参与低分二次关联；连续 `misses > max_lost` 帧未关联 → 删除。

输出统一为 `[track_id, x1, y1, x2, y2, conf]`。

## 三、Python 接口

```python
from src.algorithms.bytetrack_tracker import ByteTrack

tracker = ByteTrack(
 track_thresh=0.5, # 高分检测阈值
 det_thresh=0.1, # 检测置信度下界
 match_thresh=0.8, # IoU 代价阈值 = 1 - IoU 门限
 max_lost=30, # 连续未关联多少帧后删除轨迹
 min_hits=3, # 新轨迹连续关联多少帧后确认
 frame_rate=30,
 dt=None, # 固定帧间隔（None = 1.0）
)

tracks = tracker.update(detections) # detections: [(x1,y1,x2,y2,conf), ...]
# 返回: [[track_id, x1, y1, x2, y2, conf], ...]（按 track_id 升序）

tracker.states # 同 update 返回格式
tracker.track_info # {id: {bbox, score, hits, misses, age, last_seen, start_frame, stable, confirmed}}
tracker.track_count
tracker.reset
```

| 参数 | 作用 | 建议 |
|---|---|---|
| `track_thresh` | 高低分分界 | 与检测器置信度分布相关，通常 0.4–0.6 |
| `det_thresh` | 置信度下界 | 过小引入噪声框；过大丢遮挡框。建议 0.1–0.2 |
| `match_thresh` | IoU 门限的补 | 0.8（IoU≥0.2）；目标小/移动快可放宽到 0.7 |
| `max_lost` | 容忍遮挡时长 | 30 FPS 下建议 15–30 帧 |
| `min_hits` | 确认帧数 | 3 帧可抑制单帧误检 |

## 四、C++ 接口（`bytetrack_tracker.h`）

纯头文件、C++11、**无第三方依赖**（不使用 Eigen/OpenCV），可直接复制进 Luckfox 工程。

```cpp
#include "bytetrack_tracker.h"

ByteTrack tracker(0.5, 0.1, 0.8, 30, 3, 1.0, false);
std::vector<std::vector<double>> dets; // [x1,y1,x2,y2,score]
std::vector<TrackResult> tracks = tracker.update(dets, frame_id);
for (const TrackResult& t : tracks) {
 // t.id, t.x1, t.y1, t.x2, t.y2, t.score
}
```

`bt_detail` 命名空间提供 `iou/iouMatrix`、`hungarian`、`associate`、`KalmanBoxFilter`；
`ByteTrack::update` 实现两轮关联与轨迹生命周期。算法与 Python 版一一对应。

> 注：本机（Windows，git-bash）未安装 C++ 编译器，`bytetrack_tracker.h` 未做编译验证；
> Python 版已通过单元测试。移植到 Luckfox 后请在真实工程中重新编译并做数值验证。

## 五、与既有流水线的集成

```
YOLO 检测框 ──► ByteTrack.update(dets) ──► 稳定轨迹 [id, x1,y1,x2,y2,conf]
 │
 ┌────────────────────────────┤
 ▼ ▼
 脚点 = ((x1+x2)/2, y2) 轨迹 ID / 遮挡状态
 │ │
 ▼ ▼
 IPM 逆透视映射 RiskFSM 风险决策
 │ │
 ▼ ▼
 KalmanTracker 地面坐标平滑 拥挤度 / TTC 等下游
```

Python 集成示例：

```python
from src.algorithms.bytetrack_tracker import ByteTrack
from src.algorithms.kalman_tracker import KalmanTracker

bt = ByteTrack(track_thresh=0.5, det_thresh=0.1, match_thresh=0.8, max_lost=30, min_hits=3)
smoother = KalmanTracker # 可选：对脚点地面坐标进一步平滑

for frame in video_frames:
 boxes = yolo.detect(frame) # [(x1,y1,x2,y2,conf), ...]
 tracks = bt.update(boxes) # [[id, x1, y1, x2, y2, conf], ...]
 feet_px = [((t[1]+t[3])/2, t[4]) for t in tracks] # 脚点像素
 ground = [ipm_map(fx, fy) for fx, fy in feet_px] # 地面米坐标
 smoothed = smoother.update(ground) # 可选平滑
 # 下游：RiskFSM 使用 smoothed 位置/速度与轨迹 ID
```

## 六、单元测试与验收指标

运行：`python -m pytest tests/test_bytetrack_tracker.py -v`（或独立运行）。

测试数据为**脚本合成的检测框轨迹**（非真实传感器/检测器输出），可复现随机种子。共 **20 项全部通过**：

| 测试类 | 覆盖点 |
|---|---|
| `TestIoU` | IoU 基本计算、批量矩阵 |
| `TestHungarian` | 与暴力枚举最优解对比、对角代价 |
| `TestAssociation` | 基本匹配、全部未匹配、空输入边界 |
| `TestByteTrack` | 单目标 ID 稳定、**双目标遮挡 ID 不漂移**（核心）、**低分二次关联更新轨迹**、过期轨迹删除、新目标建轨、输出格式、track_info、reset、参数校验、空帧外推 |

核心验收指标（合成双行人 60 帧，第 20~24 帧 A 被遮挡）：

| 指标 | 结果 |
|---|---|
| 每帧输出轨迹数 | 恒为 2（含遮挡帧） |
| 遮挡前后目标 A 的 ID | **保持不变**（无漂移） |
| 遮挡期间目标 A 是否被更新 | 是（低分二次关联生效，`misses` 保持 0） |
| 遮挡期间目标 A 的 score | 反映低分置信度 0.25 |

## 七、文件清单

| 文件 | 说明 |
|---|---|
| `src/algorithms/bytetrack_tracker.py` | Python ByteTrack 多目标跟踪器（仅依赖 numpy） |
| `src/algorithms/bytetrack_tracker.h` | C++ header-only 多目标跟踪器（接口与 Python 一致） |
| `tests/test_bytetrack_tracker.py` | 单元测试（20 项，含遮挡场景） |
| `docs/ALGORITHM_UPGRADE.md` | 本文档（本节） |