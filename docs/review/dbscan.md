# 只读审查报告：`src/algorithms/crowd_dbscan.h`

- 审查对象：`src/algorithms/crowd_dbscan.h`（199 行，header-only C++ DBSCAN）
- 对照实现：`src/algorithms/crowd_dbscan.py`（Python 原型）、`src/algorithms/graph_laplacian.h`、`src/luckfox_rtsp_yolov5/include/risk_config.h`、`src/luckfox_rtsp_yolov5/include/enhanced_decision.h`、`config/default_config.json`
- 审查方式：只读，未修改任何文件。
- 结论：算法核心逻辑正确、与 Python 端一致，边界处理（空点/单点/全噪声）基本合理；主要问题在**集成层面**——该模块在 C++ 决策链路中未被实例化、DBSCAN 参数不可从配置注入、默认 `eps=1.5` 与图拉普拉斯 `edge_dist=2.0` 空间尺度不一致，以及**对外 `labels` 噪声语义注释与实现不符**（-1 vs -2）。

---

## 1. 审查结论总览

| 项 | 结论 |
|---|---|
| 算法正确性（DBSCAN 语义） | ✅ 正确，与经典定义及 Python 端逐行一致 |
| C++/Python 一致性 | ✅ 一致（哨兵、闭区间、核心点扩张、边界点归簇语义相同） |
| 空点 / 单点 / 全噪声边界 | ✅ 基本合理，无崩溃、无除零 |
| eps / min_samples 参数合理性 | ⚠️ 默认值合理，但**不可配置**（与注释宣称矛盾），且无输入校验 |
| 与 graph_laplacian 一致性 | ⚠️ 空间尺度不一致：eps=1.5 vs edge_dist=2.0，且比较边界 `<=` vs `<` |
| 实际工程接入 | ❌ `CrowdDBSCAN` 未被任何 C++ 工程文件实例化/调用（纯独立模块） |
| 对外 API 文档 | ⚠️ `labels` 注释写“-1=噪声”，实际噪声标签为 `NOISE=-2` |

---

## 2. 参数合理性（eps / min_samples）

### 2.1 默认值本身
- `eps=1.5`、`min_samples=2`：对“行人地面坐标（米）”的场景是合理默认值。两行人相距 ≤1.5m 视为同簇、两人即成一簇，符合人群“局部聚集”语义。
- `min_samples=2`（含自身，即邻域内至少再有一人）能捕捉最弱的“两人聚集”，与 `graph_laplacian` 的“两点成边”语义对齐。

### 2.2 问题 1：参数不可配置（与注释矛盾，中危）
- 头文件注释（第 14-16 行）声明“DBSCAN 参数(可通过配置传入)”。
- 但实际配置链路 `risk_config.h` 只解析 `edge_dist_m`，`config/default_config.json` 也只有 `edge_dist_m`。**没有任何 `eps` / `min_samples` 字段被读取或传递**。
- `CrowdDBSCAN` 的 `setEps` / `setMinSamples` 是存在的，但没有调用点从配置注入。
- 建议：要么在 `risk_config.h` 增加 `dbscan_eps_m` / `dbscan_min_samples` 字段并在 `enhanced_decision` 接线，要么在注释中明确“当前版本参数为编译期默认值，未接配置”。

### 2.3 问题 2：setter 无输入校验（低危）
- `setEps(double)` 未校验 `eps <= 0`；`setMinSamples(int)` 未校验 `ms <= 0`。
- 若误传 `eps<=0`：每个点邻域仅含自身，`size=1 < min_samples`，全员判为噪声，`n_clusters=0`，静默退化。
- 若误传 `min_samples<=0`：所有点均视为核心点，全图被合并为一个簇，同样静默异常。
- 对比：`risk_config.h` 对 `edge_dist_m` 有“必须是正数”的校验。建议在 setter 或 `compute()` 入口补充相同强度校验（`eps>0 && min_samples>=1`）。

---

## 3. 空点 / 单点 / 全噪声边界

| 输入 | 行为 | 评价 |
|---|---|---|
| `n==0` | `compute()` 返回 `feat.n=0`，其余默认零，`labels` 空 | ✅ 合理 |
| `n==1` | 单点邻域 `{0}`，size=1 < min_samples=2 → `NOISE`；`n_clusters=0`，`n_noise=1`，`n_clustered=0`，`largest_cluster_ratio=0` | ✅ 合理 |
| `n==2` 且距离 ≤eps | 互为邻居，各 size=2 ≥ min_samples → 1 簇，`largest_cluster_ratio=1.0` | ✅ 合理 |
| `n==2` 且距离 >eps | 各自 size=1 → 2 个噪声，0 簇，ratio=0 | ✅ 合理 |
| 全噪声（无核心点） | `nClusters==0` 分支提前返回：`n_noise=n`，`n_clustered=0`，无排序/质心操作 | ✅ 合理，无除零（`largest_cluster_ratio` 保持默认 0） |
| 全部点成一簇 | `largest_cluster_ratio = n/n = 1.0` | ✅ 正确 |

- 质心计算：累加用 `double`，除以 `sizes[c]`，`sizes[c]>0` 守卫存在；算法保证簇非空，故无除零风险。✅
- 排序：`std::sort` 按 `sizes[a] > sizes[b]` 降序，`cluster_centers` 同步排序，与 Python `np.argsort(sizes)[::-1]` 行为一致。✅

---

## 4. 与 graph_laplacian 一致性（核心一致性问题）

同一帧行人坐标会同时（或分别）被两个模块用作拥挤度特征，但空间语义不一致：

1. **阈值不同**：`CrowdDBSCAN` 默认 `eps=1.5`；`GraphLaplacian` 默认/配置 `edge_dist_m=2.0`。两行人相距 1.6m 时：图拉普拉斯判定连边（网络存在），DBSCAN 判定不同簇/噪声——两个模块对同一人群给出矛盾的空间结构描述。
2. **边界比较不同**：DBSCAN 用 `<= eps`（闭区间），图拉普拉斯用 `d < edgeDist`（严格小于）。在 `d == 阈值` 的离散边界上连边/聚类行为不同。
3. **坐标系精度**：两者均存 `std::pair<float,float>` 米制坐标，距离计算均提升到 `double` 再 `sqrt`，精度一致。✅

建议：让 DBSCAN 的 `eps` 与图拉普拉斯 `edge_dist_m` 共享同一配置来源（或至少默认相等，如统一为 2.0），并把比较边界统一为同一种（推荐统一 `<=`，与经典 DBSCAN 的闭区间定义一致，同时将图拉普拉斯改为 `<=` 或在文档中说明差异）。若刻意让二者互补（如 DBSCAN 捕捉更紧密聚集），应在文档中明确“eps < edge_dist 是有意为之”及取值依据。

---

## 5. 其他发现

### 5.1 中危：对外 `labels` 噪声语义与实现不符
- `struct CrowdClusterFeatures`（第 40 行）注释：`labels: 每个行人的簇标签(-1=噪声)`。
- Python 端文档（`crowd_dbscan.py` 第 26、110 行）同样写“-1 表示噪声”。
- **实际实现**：`UNCLASSIFIED=-1`、`NOISE=-2`（第 98-99 行，与 Python 端一致）。最终输出的噪声标签是 `-2`，`-1` 只是内部“未分类”哨兵，计算后不会残留。
- 若下游按对外契约用 `labels == -1` 判断噪声，将全部漏判。需将注释/文档统一为“-2=噪声”，或在输出阶段把 `-2` 归一化为 `-1` 后再暴露。

### 5.2 中危：`CrowdDBSCAN` 未接入实际 C++ 决策链路
- `enhanced_decision.h` 的 `EnhancedDecision` 仅实例化 `GraphLaplacian graph_`，**未使用 `CrowdDBSCAN`**。
- 全工程 grep 未发现任何 `CrowdDBSCAN` 实例化点。头文件目前是“独立可用但未接线”的模块。
- 与头文件注释“供开发板 C++ 工程集成，作为图拉普拉斯谱特征之外的工程化拥挤度特征”宣称的角色不符——应明确其当前为“待集成”状态，或完成接线。

### 5.3 低危：O(n²) 内存
- 预计算 `std::vector<std::vector<int>> neighbors(n)`，每点存邻居下标。n=500 时约 250k 条目 + vector 头开销，板端可接受；n=1000+ 时内存与耗时显著。注释已声明“人群规模通常几十~几百，开销可忽略”，可接受，但建议补充内存上限提示或预留 KD 树替换接口。

### 5.4 低危：噪声计数依赖内部哨兵约定
- `compute()` 用 `labels[i] <= 0` 统计噪声（第 173 行），依赖“-1/-2 均为未聚类”的内部约定。当前算法结束后无 `-1` 残留，结果正确；但可读性弱。建议直接用 `labels[i] == NOISE`，避免哨兵泄漏。

---

## 6. 建议优先级

1. 【中】修正 `labels` 对外注释“-1=噪声”→ 实际 `-2`；或输出归一化为 -1。
2. 【中】在 `risk_config.h` / `default_config.json` 增加 `dbscan_eps_m`、`dbscan_min_samples` 配置项并接线，或在注释中明确参数为编译期默认。
3. 【中】统一 DBSCAN `eps` 与图拉普拉斯 `edge_dist_m` 的空间尺度（默认值一致 + 边界比较一致），或在文档中说明差异为有意设计。
4. 【中】明确 `CrowdDBSCAN` 的接入状态（待集成/已接线），避免“已作为特征”的误导。
5. 【低】`setEps`/`setMinSamples` 增加输入校验（eps>0、min_samples>=1）。
6. 【低】噪声计数改用 `labels[i] == NOISE`，减少对内部哨兵的依赖。

---

## 附：核对过的关键行号

- 默认参数：`crowd_dbscan.h:47`（`eps=1.5, min_samples=2`）
- 哨兵定义：`crowd_dbscan.h:98-99`（`UNCLASSIFIED=-1, NOISE=-2`）
- 闭区间邻域：`crowd_dbscan.h:110`（`<= eps`）
- 噪声统计：`crowd_dbscan.h:173`（`labels[i] <= 0`）
- 配置缺口：`risk_config.h:51`（仅 `edge_dist_m`）、`config/default_config.json:2`
- 图拉普拉斯阈值/边界：`graph_laplacian.h:43`（`edge_dist=2.0`）、`:118`（`d < edgeDist`）
- 集成缺口：`enhanced_decision.h:153`（仅 `GraphLaplacian graph_`）