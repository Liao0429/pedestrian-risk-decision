# graph_laplacian.h 代码审查报告

- **审查对象**: `src/algorithms/graph_laplacian.h`（C++11 header-only 图拉普拉斯谱特征计算，板端 Luckfox RV1106 集成）
- **对照**: `src/algorithms/graph_laplacian.py`（Python 原型，用于一致性核对）
- **审查方式**: 只读审查，未修改任何文件
- **审查范围**: 数值稳定性 / 异常处理与鲁棒性 / 性能 / 可读性与注释 / 接口设计

---

## 一、总体结论

核心算法正确：建图 `L = D - A` 无误；Jacobi 旋转公式（`graph_laplacian.h:158-181`）与经典实现一致，无除零、无越界；空输入 / 单点 / 两节点边界均有处理。**未发现确定性崩溃或必然错误结果**。

但存在若干**潜在缺陷与鲁棒性风险**，其中最重要的是：**特征分解不收敛时静默返回错误特征值**，以及 **O(n³) 复杂度的谱分解 + 聚类系数在板端单核上可能超时**。若人群规模较大（n>200）且未做降级，`computeFeatures()` 可能拖垮实时链路。

---

## 二、潜在缺陷 / Bug 风险（按严重度排序）

### P1-1. Jacobi 特征分解无收敛检测，失败时静默返回错误结果（`graph_laplacian.h:146-189`）
- `max_sweeps = 100` 固定；循环结束后**没有任何"是否已收敛"的标记或返回**。
- 若 100 轮仍未使最大非对角元 `off < 1e-12`，函数直接以当前对角元作为特征值返回。
- 此时 `fiedler / spectral_radius / eig_entropy` 全部基于错误特征值，且调用方无从知晓。
- 收敛判据 `off < tol` 采用**绝对阈值**（`tol = 1e-12`）。对大矩阵，浮点舍入下界约为 `n·eps·‖L‖`，矩阵规模越大，绝对阈值越难以达到，需要的 sweep 数随之增加，`max_sweeps` 存在被耗尽的可能。
- **建议**: 增加收敛标志（如 `bool converged`）并入 `SpectralFeatures`；达到 `max_sweeps` 未收敛时返回失败状态或降级策略，并记录告警日志。

### P1-2. 负特征值裁剪不一致（`graph_laplacian.h:188-189`）
- C++ 端只把 `(-1e-9, 0)` 区间的微小负值归零；Python 端用 `np.maximum(eigvals, 0.0)` **将所有负值一律截断为 0**（`graph_laplacian.py:179`）。
- 在 Jacobi 正常收敛时二者等价；但若 P1-1 未收敛产生较大负特征值，C++ 端会保留负值，导致 `fiedler` 为负、`eig_entropy` 中对应项被 `e <= 0` 跳过，与 Python 端行为分叉。
- **建议**: 与 Python 对齐，统一策略（如所有 `< 0` 且 `> -1e-6·λmax` 的值归零），并优先保证收敛。

### P1-3. 输入校验缺失：`edgeDist` 与坐标非法值未处理
- `setEdgeDist` / 构造函数对 `d` 无校验：`d <= 0` → 全图无边；`d = NaN` → 所有 `d < NaN` 为 false → 全图无边，静默异常。
- `addPoint` / `setPoints` 对 `NaN / Inf` 坐标无校验：`dx*dx+dy*dy` 为 NaN/Inf → 建边判断失效。
- **建议**: 在 `setEdgeDist` 中钳制 `d >= 0`（或拒绝非正/NaN）；在 `addPoint` 中跳过或拒绝非有限坐标。

### P2-4. 收敛判据的绝对阈值缺乏依据（`graph_laplacian.h:143`）
- `tol = 1e-12`、`max_sweeps = 100` 均为魔法数，无注释说明来源。
- 对大矩阵建议改用**相对判据**（如 `off < tol * (|app|+|aqq|+off)` 或基于 Frobenius 范数），或至少以矩阵范数为基准，使收敛判定与规模无关。

### P3-5. `r_gcc` / `clustering` 边界行为与 Python 端一致但需文档化
- `n=1` 时 `r_gcc = 1.0`（单点自成最大连通分量）、`n=0` 时 `r_gcc = 0.0`（`graph_laplacian.h:200`）。逻辑合理，但建议在头文件注释中显式说明，避免使用者误解。

---

## 三、数值稳定性分析

- **建边距离**: 坐标存 `float`，距离计算时提升为 `double`（`graph_laplacian.h:115-117`），精度合理；但 `float` 仅约 7 位有效数字，若坐标量级较大（>1000m），距离平方的舍入误差可能影响 `d < edgeDist` 边界判断。建议**坐标内部统一用 `double`**（Python 端已是 `float64`，二者更一致）。
- **Jacobi 除零**: `off > tol > 0` 保证最大非对角元 `apq ≠ 0`，`theta = (aqq-app)/(2*apq)` 无除零风险（`graph_laplacian.h:161`）。✓
- **旋转更新**: 行/列旋转、对角元更新公式正确，`a[p][q]=a[q][p]=0` 正确。✓
- **微小负特征值修正**: 见 P1-2。
- **特征熵**: 以 `s = Σλ = 2m` 归一化，`e <= 0` 项跳过，无 `log(0)`/负对数问题。✓ 但若特征值含较大负值（P1-2），熵值会偏差。

---

## 四、性能分析（板端 Luckfox RV1106，单核 ~1.2GHz）

| 计算项 | 复杂度 | 风险 |
|---|---|---|
| `buildLaplacian` 距离矩阵 | O(n²)，含 `sqrt` | n=1000 时约 50 万次 sqrt，可接受 |
| 最大连通分量 BFS | O(n²) | 可接受 |
| 平均聚类系数（`graph_laplacian.h:248-262` 三重循环） | **O(n·k_max²)**，稠密图退化为 **O(n³)** | 稠密人群 n=1000 时约 10⁹ 次循环，板端严重超时 |
| Jacobi 特征分解 | **O(n³)** × sweep 数 | n=1000 时数千万次浮点运算 × 多次 sweep，实时不可行 |

- **核心矛盾**: 谱特征本身就是 O(n³)，在板端对大规模人群做实时计算不现实。
- **建议**:
  1. **规模上限与降级**: 在 `computeFeatures` 入口设 `max_n`（如 200/300），超限时返回简化特征（仅 n/m/density/r_gcc）或将谱特征置为无效，保证实时性。
  2. **聚类系数优化**: 对每个节点只枚举其邻居两两之间（用 `adj` 的行索引，而非全矩阵 `a<b` 双重全遍历），复杂度从 O(n·k²) 降到 O(Σ k_i²) 仍过大时，可对稠密图采用 `(A³)_ii` 计数或直接跳过。
  3. **稀疏化**: 邻接矩阵用稀疏存储（CSR），BFS/聚类/度数计算更快。
  4. **Jacobi 替代**: 板端只关心最小几个特征值（Fiedler）与最大特征值，可用 **Lanczos / 幂法**（对稀疏拉普拉斯，每步 O(m)），避免完整 O(n³) 分解。若只需风险决策，可用 `fiedler` 的**上/下界估计**（如 Cheeger 不等式、度数相关近似）替代精确谱。
  5. **复用中间结果**: `computeFeatures` 内部已构建 `adj` 与 `L`，`adjacencyMatrix()` 又重复建图；可提供内部一次性构建函数供二者复用，避免重复 O(n²)。

---

## 五、可读性与注释完整性

- 文件头注释清晰说明用途、依赖、板端验证提醒。✓
- 各字段注释完整，`n/m/density/r_gcc/clustering` 公式均给出。✓
- **不足**:
  - Jacobi 算法无出处引用（建议注明 "Numerical Recipes" 或数学参考），`tol`/`max_sweeps` 无依据。
  - `algebraic_connectivity` 与 `fiedler` 字段重复，注释已说明等价，但建议明确"冗余保留以兼容 Python 字典字段"。
  - 聚类系数"平均"的分母（`sumC/n`，孤立节点计 0）建议注释说明，与 Python `_avg_clustering` 一致。
  - 建议补充一段 `@warning`：本实现 O(n³)，适用于小规模人群；大规模时需降级。

---

## 六、接口设计

- **优点**: 纯标准库、header-only、inline 避免 ODR 问题；`computeFeatures` 为 `const`，线程安全（无共享可变状态）；接口简洁。
- **改进点**:
  1. **缺少命名空间**: `SpectralFeatures` / `GraphLaplacian` 直接暴露在全局命名空间，板端工程多模块时易冲突。建议包一层 `namespace risk { ... }`（或 `crowd`）。
  2. **无状态/收敛标志**: `SpectralFeatures` 无 `valid` / `converged` 字段，调用方无法判断特征值是否可信（见 P1-1）。
  3. **`setPoints` 仅 const& 拷贝**（`graph_laplacian.h:87-89`）: 建议增加按值 + `std::move` 重载，减少大向量拷贝。
  4. **坐标类型不一致**: `addPoint(float,float)` 与内部 `double` 混用，建议统一 `double`。
  5. **缺少查询节点数方法**: 可加 `size()` 便于调用方判断是否值得做谱分析。
  6. **`clear()` 只清点集，不重置 `edgeDist`**: 语义合理，但建议注释说明。

---

## 七、与 Python 端一致性

逐项核对 `graph_laplacian.h` 与 `graph_laplacian.py`：

- 建边规则：均 `d < edge_dist` 严格小于，无自环。✓
- `density` 公式：C++ `m/[n(n-1)/2]` = Python `2m/[n(n-1)]`。✓
- `r_gcc`、`clustering` 公式与边界：一致。✓
- 负特征值裁剪策略不完全一致（P1-2），正常收敛时等价，异常时可能分叉。
- 坐标精度：Python 全程 `float64`，C++ 存 `float` 提升 `double`，边界处可能有微小差异。
- **建议**: 增加一组固定输入（如 `graph_laplacian.py` 末端的演示用例：分散 4 点、P5 路径图、K4 全连接）作为 C++ 端单元测试的黄金参考，在 x86 与板端分别交叉验证数值输出。

---

## 八、改进点清单（汇总）

1. **Jacobi 增加收敛检测与失败上报**（高优先级）
2. **负特征值裁剪策略与 Python 统一**（高优先级）
3. **`edgeDist` / 坐标输入校验（非正、NaN/Inf）**（高优先级）
4. **性能降级：设 `max_n` 上限，超限返回简化特征**（高优先级）
5. **聚类系数与 BFS 改用稀疏邻居枚举 / 稀疏存储**（中优先级）
6. **用 Lanczos/幂法替代完整 O(n³) Jacobi（板端只关心极值特征值）**（中优先级，长期）
7. **收敛判据改用相对阈值（基于矩阵范数）**（中优先级）
8. **坐标类型统一为 `double`**（低优先级）
9. **`computeFeatures` / `adjacencyMatrix` 复用中间建图结果**（低优先级）
10. **加命名空间 `namespace risk`**（低优先级）
11. **`SpectralFeatures` 增加 `valid/converged` 状态字段**（与 1 配套）
12. **补充注释：Jacobi 出处、tol/max_sweeps 依据、clustering 定义、边界行为**（低优先级）
13. **`setPoints` 增加 move 语义；增加 `size()` 查询**（低优先级）
14. **建立与 Python 端一致的黄金测试用例（x86 + 板端交叉验证）**（中优先级）

---

## 九、优先级汇总

### 高优先级（上线前必改）
1. Jacobi 收敛检测与失败上报
2. 负特征值裁剪与 Python 统一
3. 输入校验（edgeDist / 坐标 NaN/Inf）
4. 大 n 性能降级策略（max_n 上限）

### 中优先级
5. 稀疏化 / 邻居枚举优化聚类与 BFS
6. Lanczos/幂法替代完整特征分解（长期）
7. 相对收敛判据
8. 与 Python 端黄金测试用例

### 低优先级
9. 命名空间、move 语义、size()、坐标统一 double、注释完善

---

*报告生成时间: 2026-10-26。仅审查，未修改任何源文件。*