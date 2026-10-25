# AUDIT.md — 项目现状审计报告

> 审计时间：2026-10-26　审计人：Hermes（系统管理员）
> 审计对象：`H:\PedestrianRiskDecision`
> 本报告是后续所有工作的基线。

---

## 一、项目目的

毕设项目《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》：
在 **Luckfox Pico Max（RV1106，1TOPS NPU，256MB）** 上实现实时行人检测、人群聚集分析与 **SAFE / WARNING / DANGER** 三级风险预警。

核心卖点（答辩口径）：
> 前端轻量化感知 + 几何空间校正（IPM）+ 图论拓扑分析（图拉普拉斯/DBSCAN）+ 时序风险决策（Kalman/ByteTrack + FSM）。

GitHub：`https://github.com/Liao0429/pedestrian-risk-decision.git`（**PRIVATE**，毕设完成后手动公开；论文仅本地保存不上传）。

---

## 二、当前架构

```
摄像头 → YOLOv5n(INT8/RKNN NPU) → 行人 bbox
       → IPM 逆透视(像素脚点→地面米坐标)
       → 复杂网络特征(n/m/density/r_gcc/clustering/Fiedler/谱半径)
       → Kalman/ByteTrack 跟踪 + EMA 平滑(升级)
       → FSM 有限状态机(滞回/保持/去抖)
       → SAFE / WARNING / DANGER → 画面叠加 + RTSP 推流 + 硬件外设
```

- **决策阈值规则**（Python 与 C++ 一致，`config/default_config.json` 可配）：
  - DANGER：`n≥10 && (density≥0.55 || r_gcc≥0.80)`
  - WARNING：`n≥5  && (density≥0.28 || r_gcc≥0.55)`
  - 否则 SAFE；建边距离阈值 2.0m。

---

## 三、关键模块

### 3.1 Python 算法库 `src/algorithms/`（每模块均含 `.py` + header-only `.h`，C++11 仅标准库）
| 模块 | 说明 | 测试 |
|---|---|---|
| `graph_laplacian.py/.h` | 图拉普拉斯谱特征（Fiedler/谱半径/代数连通度/特征熵） | ✅ |
| `kalman_tracker.py` / `kalman_filter.h` | 单目标线性匀速 Kalman 平滑 | ✅ |
| `bytetrack_tracker.py/.h` | 多目标跟踪（IoU 关联、二次匹配） | ✅ |
| `crowd_dbscan.py/.h` | 纯 numpy 手写 DBSCAN 人群聚类 | ✅ |
| `ema_smoother.py/.h` | 指数滑动平均 | ✅ |
| `risk_fsm.py/.h` | FSM 风险状态机（滞回+确认帧+状态保持） | ✅ |
| `ipm_calibrate.py` | IPM 标定/单应矩阵 | ✅ |

### 3.2 开发板 C++ 工程 `src/luckfox_rtsp_yolov5/`
- `main.cc`（单线程 while 循环：取帧→RKNN 推理→RiskEngine→画框→RTSP）
- `risk_engine.cc/.h`（固定阈值规则分级，已真机跑通）
- `postprocess.cc` / `yolov5.cc` / `luckfox_mpi.cc`（RKNN/RTSP 底层）
- **`enhanced_decision.h/.cc`（已生成，未集成）**：封装 Kalman+谱特征+FSM，目前未接入 main.cc

### 3.3 测试脚本 `scripts/`
- `crowd_test.py`：图片/单帧基线（YOLOv8n + IPM + 网络特征 + 规则分级，输出可视化与 JSON）
- `crowd_test_enhanced.py`：视频增强版（+ 谱特征 + Kalman + FSM 时序决策）

### 3.4 文档 `docs/`
SYSTEM_PRINCIPLE（原理详解）／ALGORITHM_OPTIMIZATION（优化建议）／ALGORITHM_UPGRADE（DBSCAN 等升级）／INTEGRATION_PLAN（C++ 集成方案）／PERFORMANCE_COMPARISON（性能对比）／optimization_review（算法审查）／HGPAO_PLAN（攻关与验收计划）／hardware/（硬件扩展）等，共 18 个文件。

---

## 四、数据源（全部真实，可复现）

| 数据 | 规模 | 来源 | 校验 |
|---|---|---|---|
| 图片 `dataset/images500/` | **500 张** COCO 2017 真实含行人图（1~19 人；分散200/聚集150/较拥挤138/拥挤12） | 官方 CDN + Ultralytics coco2017labels（CC BY 4.0） | `verify_images.py` 全 OK，`images_manifest.csv` |
| 视频 `dataset/videos50/` | **111 条** Mixkit 真实人群/行人视频（360p，3.9~50s） | Mixkit（免版权） | ffprobe + HOG 双重校验 111/111 通过 |
| 早期图片 `dataset/images/` | 30 张（早期实验，已被 images500 覆盖） | 同上 | 同上 |

- 数据流水线脚本：`dataset/_work/`（扫描/挑选/下载/校验/日志齐全）。
- **无任何合成/编造数据**。ground-truth 人数来自 COCO 官方标注。

---

## 五、实验结果（真实，可复现）

- **图片基线（500 张）**：检出率 **82.5%**（2548/3089），高密度图（14+人，12 张）79.4%；SAFE 414 / WARNING 61 / DANGER 25；耗时 16.79s（GPU）。
- **视频增强（5 条真实视频）**：
  - 密集人群（avg 14.9 人）：DANGER 264/305 帧，切换仅 4 次（稳定）
  - 稀疏/无人：全程 SAFE、零误报
  - 瞬时聚集：FSM 滞回正确抑制
- **优化前后对比**：单帧规则 → +Kalman+谱特征+FSM，等级切换次数显著下降。
- 凭证：`results/images500_baseline/`、`results/video_enhanced/*_timeline.json`、`results/experiment_summary.json`。
- 单元测试：**121 个全部通过**（`python -m pytest tests -q`，0.73s）。

---

## 六、缺失功能 / 待办（按优先级）

### 🔴 P0（当前最缺，答辩前必须完成）
1. **增强算法 C++ 板端移植**：`enhanced_decision.h/.cc` 已生成但 **未接入 `main.cc`**（main.cc 仍只调用 RiskEngine 固定阈值）。需完成集成 + 交叉编译 + 真机部署。
2. **C++ 工程缺少构建文件**：仓库内 **没有 Makefile / CMakeLists**，交叉编译步骤仅在文档描述，换机无法一键复现。应补充可追踪的构建脚本。

### 🟡 P1
3. **真机增强版实测**：帧率/CPU/内存/等级稳定性/切换次数，输出板端日志+截图作为论文证据（计划：`results/board_test/`）。
4. **IPM 单应矩阵 H 为手填假设值**（非真机标定）——正确性最大隐患。`ipm_calibrate.py` 已提供标定能力，需真机标定并落地。
5. **规则阈值与论文表不一致**：`risk_engine.cc` 用 `#define`（n=10/5），论文表 5-1 典型值（15/8），答辩易被追问。config JSON 已支持但 C++ 端尚未运行时读取。
6. **检测置信度/NMS 阈值固定 0.25/0.45**，密集场景系统性漏检（n 低估→漏报）。建议 person 单独低阈值 + 高 NMS（0.55）并用跟踪去重。
7. **依赖未声明**：无 `requirements.txt` / `pyproject.toml`（ultralytics/opencv/numpy 版本不可复现）。测试仅依赖 numpy 可跑，但实验脚本需完整声明。

### 🟢 P2
8. 毕业论文撰写（本地 `docs/thesis/`，不上传 GitHub）。
9. 硬件扩展实施（舵机/语音/大屏/开关，需采购硬件）与 PCB 打样（嘉立立创，需下单）。

---

## 七、风险

| 风险 | 等级 | 说明/缓解 |
|---|---|---|
| **C++ 增强算法未集成** | 高 | 论文核心卖点（Kalman+谱特征+FSM）目前只有 Python 验证，板端仍是单帧规则；答辩可能被质疑"是否真的落地" |
| **IPM 单应矩阵未真机标定** | 高 | 网络建边完全依赖"地面米距离"，手填 H 有系统误差，特征可能失真 |
| **阈值未运行时配置化** | 中 | C++ 用 `#define`，论文参数可配置承诺未完全兑现 |
| **构建不可复现** | 中 | 无 Makefile/CMake、无 requirements.txt；换环境需手工补依赖 |
| **Git 未同步** | 中 | 本地 `main` 领先 `origin/main` **17 个 commit**，且 `enhanced_decision.h/.cc`、`codex_task_cpp.txt` 未提交；"GitHub 全程留痕"策略要求尽快 push |
| **数据可复现性** | 低 | 下载脚本+校验日志齐全；仅 COCO 图片需联网重下（已提供直链与挑选脚本） |
| **隐私/论文泄露** | 低 | 私人仓库 + 论文不上传，符合要求；`*.pt`/日志已 gitignore |

---

## 八、对后续工作的建议（下一步）

1. 先把 `enhanced_decision.h/.cc` + `codex_task_cpp.txt` 提交并 push（满足留痕策略），再开始 C++ 集成。
2. 补 `requirements.txt`（实验脚本）与 C++ 构建脚本（Makefile 或 CMake），让交叉编译可一键复现。
3. 完成 `main.cc` 接入 `EnhancedDecision` → 老电脑（Ubuntu 22.04 @192.168.0.68）交叉编译 → Luckfox 真机部署 → 采集板端日志/截图作为 P0 验收凭证。
4. 真机标定 IPM（`ipm_calibrate.py`），落地真实单应矩阵。
5. 把 C++ 阈值改为从 JSON 读取（对齐论文可配置承诺）。
6. 论文撰写基于真实结果（`results/`），图表可溯源。