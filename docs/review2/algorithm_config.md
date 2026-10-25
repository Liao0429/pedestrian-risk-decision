# 审查报告：多目标切换配置（target_classes）与阈值合理性

- 审查对象（只读审查，不修改代码/配置）：
  - `config/default_config.json`（`target_classes` 数组与 danger/warning 阈值）
  - `src/luckfox_rtsp_yolov5/include/risk_config.h`（JSON 解析/校验）
  - `src/luckfox_rtsp_yolov5/src/main.cc`（`std::find` 目标类别过滤）
  - 脚本端：`scripts/crowd_test.py`、`scripts/crowd_test_enhanced.py`
  - 参考：`docs/review/risk_engine_config.md`（上一轮审查结论）
- 审查日期：2026-10-26

---

## 1. 结论摘要

多目标切换在 **C++ 端实现正确且完整**：`risk_config.h` 已把 `target_classes` 数组解析进 `RiskConfig`（含 0~79 的 COCO id 校验，并兼容旧 `target_class` 单值），`main.cc` 的增强决策与旁路 RiskEngine 两条路径都用 `std::find` 按配置过滤检测框，逻辑一致，默认 `[0]`（person）与既有行为完全兼容。但存在 **4 个实质问题**：

1. **[高] 配置注释中的 COCO 类别 ID 错误**：`_comment` 写“狗[17]、猫[16]”，实际 COCO 中 15=cat、16=dog、17=horse（马）。用户照注释填 `target_classes` 会把马当狗、把狗当猫。
2. **[高] 单一全局阈值对多类目标不合理**：`danger`/`warning`（n=10/5、rho=0.55/0.28、gcc=0.80/0.55）与 `edge_dist_m=2.0` 全部按**行人拥挤度**标定，直接切换到车辆/动物后语义失效。
3. **[中] 需要按类别差异化阈值**：车辆（数量少、体型大、风险来自接近速度）与动物（个体小、数量少）都不适合用“聚集密度”语义，当前配置结构无法按类别分别设阈值。
4. **[中] 脚本端未同步支持多目标**：`crowd_test.py` 与 `crowd_test_enhanced.py` 的 `model.predict(..., classes=[0], ...)` 仍硬编码 person，既不读 `target_classes`，也没有切换类别的 CLI 参数，无法复现 C++ 端多目标验证。

---

## 2. 多目标切换实现核对

### 配置端（config/default_config.json）

```json
"target_classes": [0],
"_comment": "… target_classes=检测目标类别集合（COCO id，默认[0]=person）；车辆[2,5,7,3]、狗[17]、猫[16]、动物[18,19,20]。"
```

### 解析端（risk_config.h）

- 第 94-105 行：读取 `target_classes` 数组，校验非空、元素为 0~79 整数，存入 `cfg.target_classes`。
- 第 106-112 行：兼容旧字段 `target_class`（单值）自动转成数组。
- 默认值 `std::vector<int> target_classes{0}`（第 53 行）与 JSON 默认一致。
- 校验边界：0~79 覆盖 COCO 全部 80 类，合理；`target_class` 旧名兼容良好。

### 使用端（main.cc）

- 第 213 行（增强决策）：`std::find(...) == end()` → 跳过非目标类别，收集地面脚点。
- 第 235 行（旁路 RiskEngine）：`std::find(...) != end()` → 仅对目标类别建图。
- 两处过滤逻辑方向相反但语义一致，实现正确。冗余点：同一循环对每个检测框做两次线性查找，目标类别数 K 通常 ≤8，性能可忽略；可合并为一次过滤后同时喂给两条路径，属优化而非缺陷。

**结论**：C++ 端多目标切换功能本身实现正确、向前兼容，无需修改逻辑。

---

## 3. 问题 1：配置注释 COCO 类别 ID 错误（[高]）

对照 COCO 80 类标准索引（Ultralytics/COCO 官方）：

| id | 15 | 16 | 17 | 18 | 19 | 20 |
|----|----|----|----|----|----|----|
| 类别 | **cat** | **dog** | **horse** | sheep | cow | elephant |

- `default_config.json` 注释写“狗[17]、猫[16]”——**把 17（马）标成狗、把 16（狗）标成猫**，均错。
- “动物[18,19,20]”（sheep/cow/elephant）正确，但遗漏 14=bird、21=bear 等同类动物，注释口径不完整。
- 影响：`target_classes` 默认 `[0]` 不受影响；但用户若按注释切换类别，会选到错误类别 id（把马当狗、把狗当猫），导致检测过滤结果与预期不符且不易察觉。
- 建议：把注释修正为“狗[16]、猫[15]、动物[14,18,19,20,21]”（或列出完整备选）。

---

## 4. 问题 2：单一全局阈值对多类目标不合理（[高]）

当前 `danger`/`warning` 与 `edge_dist_m` 是**一套全局值**，所有 target_classes 共用。按类别切换后：

| 类别组 | n 阈值(10/5) | rho/gcc（聚集度） | edge_dist=2.0m |
|--------|--------------|-------------------|---------------|
| person[0] | 合理（拥挤度语义） | 合理 | 合理（行人间距） |
| 车辆[2,5,7,3] | 严重不匹配：画面内车辆通常 <10 辆，DANGER 几乎永不触发，等级恒 SAFE | 车辆离散分布，路口排队时 2m 建边→低密度高连通，gcc 虚高误报 | 过小：车身长 4~5m，2m 等于贴脸，建边语义失真 |
| 动物[18,19,20]/狗[16] | 数量少，n=10 基本不可达，预警失效 | 聚集度对动物群意义有限 | 小动物 2m 建边易把相邻个体误连 |
| 猫[15] | 更难达标 | 意义弱 | 同上 |

根因：整套阈值从“行人拥挤度风险”迁移而来，而车辆/动物的风险语义是**运动速度与接近威胁**，不是“人群聚集密度”。直接复用会得到恒 SAFE 或误报 DANGER 两类错误结果。

上一轮 `docs/review/risk_engine_config.md` 已指出默认阈值对 6m×4m 地面区域偏保守（尤其 edge_dist=2.0），在多目标场景下该问题被进一步放大——不同类别对 edge_dist 的敏感度差异极大。

---

## 5. 问题 3：需要按类别差异化阈值（[中]）

建议方向（供后续方案参考，本次不实施）：

- **配置结构**：将阈值从全局改为按类别（或类别组）分设，例如
  ```json
  {
    "target_classes": [0],
    "classes": {
      "person":  { "edge_dist_m": 2.0, "danger": {"n":10,"rho":0.55,"gcc":0.8},  "warning": {"n":5,"rho":0.28,"gcc":0.55} },
      "vehicle": { "edge_dist_m": 5.0, "danger": {"n":3,"rho":0.5,"gcc":0.7},    "warning": {"n":2,"rho":0.25,"gcc":0.5} },
      "animal":  { "edge_dist_m": 1.5, "danger": {"n":8,"rho":0.5,"gcc":0.7},    "warning": {"n":4,"rho":0.25,"gcc":0.5} }
    }
  }
  ```
  并让 `risk_config.h` 按 `target_classes` 命中的类别组选择对应阈值；多个类别组共存时取最严或按组分别评估取高等级。
- **语义层面**：车辆/动物建议改用“接近速度 / 进入危险区”等运动学指标，而非聚集密度；当前谱/图特征体系主要面向点聚集，对车辆流意义有限。
- 至少：当 `target_classes` 不含 person 时，应显著放宽 `edge_dist_m`（车辆 4~6m）并下调 n 阈值，避免恒 SAFE。

---

## 6. 问题 4：脚本端未同步支持多目标（[中]）

- `crowd_test.py` 第 319 行：`model.predict(..., classes=[0], ...)`，且第 336 行 `if int(cls_id) != 0: continue`——双重硬编码 person。
- `crowd_test_enhanced.py` 第 29 行：`model.predict(..., classes=[0], ...)`——同样硬编码。
- 两脚本虽已实现 `load_config()` 读取 JSON 的 danger/warning/edge_dist，但 **没有读取 `target_classes`，也没有任何 CLI 参数指定检测类别**。C++ 端切换到车辆/动物后，Python 端脚本仍只检测人，无法复现/验证 C++ 端的多目标行为，C++ 与 Python 的结果不再可比。
- 建议：为两脚本增加 `--classes 0,2,5,7` 参数并默认读取配置的 `target_classes`，`classes=` 传参用列表而不是固定 `[0]`；`crowd_test.py` 第 336 行的 `cls_id != 0` 硬过滤改为按目标类别集合判断。

---

## 7. 问题清单（按优先级）

| 级别 | 位置 | 问题 | 建议 |
|------|------|------|------|
| 高 | `default_config.json` `_comment` | 狗/猫的 COCO id 写错（应为 16/15，写成 17/16），用户按注释切换会选错类别 | 修正注释为“狗[16]、猫[15]、动物[14,18,19,20,21]” |
| 高 | `default_config.json` + `risk_config.h` | 全局单一 danger/warning/edge_dist 按行人拥挤度标定，切换车辆/动物后等级恒 SAFE 或误报 | 按类别组差异化阈值，见 §5 |
| 中 | `risk_config.h` `RiskConfig` | 结构只支持一组全局阈值，无法表达按类别配置 | 扩展为类别组映射结构并向后兼容旧字段 |
| 中 | `scripts/crowd_test*.py` | 硬编码 `classes=[0]`，不读 `target_classes`，多目标无法在脚本端复现 | 加 `--classes` 参数并默认读配置 |
| 低 | `main.cc:213,235` | 同一循环对每个框做两次 `std::find` 线性查找 | 合并为一次过滤，同时喂两条路径 |
| 低 | `risk_config.h:99-104` | 类别数组允许重复、未排序，不影响正确性但冗余 | 可选去重/排序 |

---

*报告结束。本次为只读审查，未修改任何源代码或配置文件；仅新增本审查报告。*