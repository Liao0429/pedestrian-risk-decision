# Hub 多Agent并行审计报告——2026-10-30

## 审计方式
- 主审计员：Hermes Agent（亲自阅读全部核心源码）
- 并行 Agent：Codex(j-46) 论文匹配验证 + OpenCode(j-47) 性能分析 + DSH(j-48) 板端排查 + Claude(j-45) 代码审计
- Claude走Magpie网关遇到mock模型未执行，OpenCode失败，Codex和DSH运行中

## 核心发现（Hermes亲自审计）

### 🔴 Bug1：NMS classIds下标错误（裁决B6-P1-1声称已修，实际未落地）
- **文件**：`src/luckfox_rtsp_yolov5/src/postprocess.cc:118,126`
- **问题**：`nms()`函数使用 `classIds[i]` 而非 `classIds[order[i]]`，导致NMS在不同类别间混乱抑制
- **影响**：同一人被anchor的多个尺度检测出两个框时，NMS可能无法正确抑制重复框 → 同一人产生多个检测 → 脚点重复
- **修复**：`classIds[i]` → `classIds[order[i]]`，`classIds[i]` → `classIds[j]`
- **状态**：✅ 已修复

### 🔴 Bug2：脚点坐标去重缺失 —— "伸一只手判出两个人"的直接根因
- **文件**：`src/algorithms/graph_laplacian.h` 和 `src/algorithms/graph_laplacian.py`
- **问题**：C++端 `setPoints/addPoint` 不做去重；Python端 `compute_laplacian_features` 不做 `np.unique`
- **场景**：YOLO对同一人输出两个框（不同anchor尺度），底部中心几乎重合，IPM映射后产生两个几乎相同的地面坐标 → 频谱特征计算中n虚增 → density/r_gcc虚高 → FSM误判
- **修复**：C++端在 `computeFeatures()` 入口增加 2cm 容差去重；Python端同步
- **状态**：✅ 已修复

### 🔴 Bug3：min_danger_n门控未实现（裁决P0-4声称已修，全仓库零匹配）
- **文件**：`src/algorithms/risk_fsm.h`、`src/algorithms/risk_fsm.py`
- **问题**：裁决报告说"min_danger_n=3门控已实现"，但 `search_files` 全仓库零匹配
- **影响**：单人场景（n=1）如果脚点去重后密度虚高（两个重合点=密度100%），评分可触发DANGER
- **修复**：C++/Python 双端新增 `min_danger_n=3` 门控，n<3时禁止升级到DANGER
- **配套更新**：`config/default_config.json` + `risk_config.h` JSON解析 + `risk_fsm.h validate()`
- **状态**：✅ 已修复

### 🟡 Bug4：帧率瓶颈——Jacobi特征分解每帧执行
- **文件**：`graph_laplacian.h:jacobiEigenvalues`
- **问题**：板端每帧跑 100 sweeps × O(N²) Jacobi迭代，人群<10人时完全浪费
- **建议**：降频执行（每3帧算1次谱特征），中间帧复用上次结果
- **状态**：⚠️ 待实施（需要改 enhanced_decision.cc 的 processFrame）

### 🟡 Bug5：板端卡死——caller未处理Jacobi收敛失败
- **文件**：`graph_laplacian.h:computeFeatures` → `enhanced_decision.cc:processFrame`
- **问题**：Jacobi未收敛时 `eigvals.clear()` 返回false → 谱特征回退到0（正确），但调用方在 `processFrame()` 中将 `spectral_converged` 设为false后继续正常流程
- **风险**：极端情况下Jacobi在100 sweeps后未收敛（极小概率但可能），会持续消耗CPU
- **状态**：⚠️ 待观察

### 🟡 Bug6：yolov5.cc out: 死标签
- **文件**：`yolov5.cc:207-208`
- **问题**：`out:` 标签是死代码，`return ret` 之前无 `goto out`
- **状态**：✅ 已确认，不影响正确性（终检F1已知）

## 已修复汇总

| # | Bug | 文件 | 严重度 | 状态 |
|---|-----|------|--------|------|
| 1 | NMS classIds下标错误 | postprocess.cc | P0 | ✅ |
| 2 | 脚点坐标去重缺失 | graph_laplacian.h/.py | P0 | ✅ |
| 3 | min_danger_n门控未实现 | risk_fsm.h/.py | P0 | ✅ |
| 4 | JSON配置缺min_danger_n | default_config.json | P1 | ✅ |
| 5 | risk_config.h JSON解析缺字段 | risk_config.h | P1 | ✅ |

## 裁决报告可信度评估

**严重警告**：第二轮评审裁决（docs/review/20261030/verdict_B.md）声称已修复的多个P0项在磁盘代码中实际未落地：
- B6-P1-1 (NMS下标) — page 3声称"classIds[order[i]] ✅"，实际代码未修改
- P0-4 (min_danger_n门控) — 裁决说"Python+C++ 双端"，全仓库搜索零匹配

**建议**：不要信任裁决报告的"已验证"状态，必须以磁盘代码为唯一事实源。