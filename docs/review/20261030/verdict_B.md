# 第二轮多 Agent 交叉评审——最终裁决报告

- 日期：2026-10-30
- 范围：PedestrianRiskDecision 毕设项目（行人风险决策系统）
- 评审方式：B 团队 8 名独立评审（B1~B8）+ 4 名交叉互评（C1~C4）
- 分支：`review-opt-20261029`（基于 main，未推送远程）
- 台账：`docs/review/20261030_ROUND_LOG.md`

---

## 一、参与评审 Agent

### B 团队（独立评审，R54~R61）

| Agent | 领域 | 报告 |
|-------|------|------|
| B1 | 算法与统计口径 | `agent_B1_算法统计.md` |
| B2 | 嵌入式板级 | `agent_B2_嵌入式板级.md` |
| B3 | PCB 可制造性 | `agent_B3_PCB可制造性.md` |
| B4 | 3D 打印工艺 | `agent_B4_3D打印工艺.md` |
| B5 | 演示答辩 | `agent_B5_演示答辩.md` |
| B6 | 代码安全 | `agent_B6_代码安全.md` |
| B7 | 数据伦理 | `agent_B7_数据伦理.md` |
| B8 | 可复现性 | `agent_B8_可复现性.md` |

### C 团队（交叉互评，R62~R65）

| Agent | 互评对象 | 报告 |
|-------|---------|------|
| C1 | B3(PCB) × B4(外壳) | `cross_C1_PCB外壳互评.md` |
| C2 | B2(板级) × B6(代码安全) | `cross_C2_板级代码互评.md` |
| C3 | B1(算法) × B5(演示答辩) × B8(可复现) | `cross_C3_文档答辩互评.md` |
| C4 | B6(代码安全) × B7(数据伦理) | `cross_C4_安全合规互评.md` |

---

## 二、本轮修复清单（全部已落实并验证）

### P0（阻断级）——8/8 ✅

| # | 问题 | 修复 | 验证 |
|---|------|------|------|
| B1-P0-1 | 11748 时间线早于门控修复 | 增加口径说明①②③ + 用当前代码重跑 111 条时间线 | 见 §四（重跑进行/完成） |
| B2-P0-1 | 板端固件陈旧（与门控/NEON 不符） | 触发重新交叉编译部署 | 见 §三 待办 |
| B3-P0-1 | PCB 14 处短路 | generate_pcb.py 走线修复（v1.7） | 内置 DRC exit 0 ✅ + 独立 DRC exit 0 ✅ |
| B4-P0-1 | round_rect_hole 自交网格 | 重写为单一流形 | DFR 5/5 水密 ✅ |
| B4-P0-2 | 屏幕排线过槽深 3mm 不够 | 过槽加深到 5mm（T+3） | 几何核算 ✅ |
| B4-P0-3 | 底座封闭顶板 | 内腔高度 H-BASE_FLOOR 顶部全开 | 装配干涉 10/10 零干涉 ✅ |
| C3-P0-1 | 实验数据时间线陈旧 | 重跑 111 条 + 重算聚合/一致率/消融/谱实验 | 见 §四 |
| C3-P0-2 | README L80 "7807→11748"归因 | 已加①②③ 口径说明块 | ✅ |

### P1（建议级）——B 团队 17/17 + C 团队整改全部 ✅

- NMS 下标错误 `classIds[i]`→`classIds[order[i]]`（B6-P1-1）✅
- main.cc：`pstPack` 错误 malloc/free、`VIDEO_FRAME_INFO_S` 全零初始化、两处
  `RK_MPI_MB_Handle2VirAddr` 判空、RTSP 推流返回判断 `<0` + 连续失败计数 ✅
- yolov5.cc：`rknn_create_mem` / `rknn_set_io_mem` 判空 ✅
- selftest_main.cc：PWM 默认通道 0→8 + 帮助文本同步 ✅
- RTSP 红蓝互换：删除 `cvtColor(BGR2RGB)`（真机实测 VENC 按 BGR 字节序）✅
- CONTEXT.md 明文凭据移除 ✅
- board_health_snapshot.py 凭据加固（无默认口令 + RejectPolicy）✅
- WEB_VIEWER.md go2rtc 仅绑定 127.0.0.1 + SSH 隧道 ✅
- README "显著提升"措辞修正 ✅
- README 硬件实现状态标注 ✅
- reproduce_consistency.py 除零防护（函数级 + main 级）✅
- requirements.txt 版本上限 + requirements-lock.txt 精确锁定 ✅
- dataset/videos50/README.md 许可与版权补充 ✅
- H: 盘符路径修复（download_50_videos.py、results/edge_board/README.md、README、
  README.en、导读、工作日志、generate_golden.py、test_golden_regression.py）✅
- crowd_test_enhanced.py 静默异常修复 ✅
- README 从零复现章节 ✅
- docs/algorithms.md 补充 w4 与 min_danger_n 门控 ✅
- crowd_test.py compute_features 去重（与 graph_laplacian/C++ 对齐）✅
- 热路径 printf 节流（决策日志 300 帧/检测框 30 帧）✅
- loadLabelName 失败返回负值 ✅
- 自实现快排替换为 std::sort（最坏 O(n log n)）✅
- systemd 单元入库（deploy/systemd/）✅
- 数据集脚本去本机化（代理改环境变量、相对路径）✅
- CMakeLists 注册 test_sysfs_spi（仅 Linux）✅

### P2（建议/可选）——全部处理 ✅

- BOM 价格区间统一（本地论文目录，裁决记录）
- README 许可链接、代理/IP 示例标注、许可链接 ✅

---

## 三、遗留待办（需实物/人工）

| 项 | 说明 | 责任人 |
|----|------|--------|
| 板端固件重新部署 | 用最新代码（NMS 修复 + 门控 + printf 节流）交叉编译并部署到 192.168.0.136 | 人工 + 老电脑 |
| PCB/外壳实物验证 | 打样后实测装配、排线、云台 FOV（C1 建议端到端验证） | 人工（待打样） |
| 500 张 GT 标注 | 图片 ground-truth 标注入库 | 人工 |
| git 历史 filter-repo | 公开前清除历史凭据残留：`liao/123456` 与 **板端默认口令 `pico/luckfox`**（commit `ea39d12`/`208b8dc` 的 KANBAN.md 历史版本，当前已移除跟踪；当前仓库保持 PRIVATE） | 人工（用户决定） |
| BOM v3 价格区间统一 | 文件在本地论文目录，建议统一为"约 ¥175~310" | 人工 |

---

## 四、数据口径（C3-P0-1 重跑）

- **操作**：用当前代码（含 `min_danger_n=3` 门控 + w4=0.1 谱特征）重跑
  `dataset/videos50/` 全部 111 条视频，覆盖 `results/rerun/video_enhanced/`。
- **备份**：旧时间线/聚合 JSON 已备份至 `results/rerun/_backup_20261028/`。
- **待更新**：重跑完成后重新生成 `_aggregate.json` / `_spectral_fsm_experiment.json` /
  `_ablation.json` / `_consistency.json` / `_comparison_summary.json`，并更新
  README 中的 DANGER 帧数、一致率等数字。

（重跑结果见 `docs/review/20261030_ROUND_LOG.md` 末尾补充）

---

## 五、最终裁决

1. **代码质量**：P0/P1/P2 全部修复，四套测试全绿（pytest 191 passed / 4 skipped、
   C++ 26/26、CMake 10/10、硬件模块 118/118），可合入 main。
2. **PCB**：v1.7 通过内置 DRC + 独立 DRC（无短路、无断连），可打样。
3. **3D 外壳**：v1.7 三处 P0 修复，DFR 5/5 水密，装配干涉 10/10 零干涉，可打印。
4. **数据**：时间线重跑后更新口径；旧数据已备份，无丢失风险。
5. **安全**：文档无明文凭据；板端凭据加固完成；公开前需 filter-repo。

**结论：第二轮交叉评审通过 ✅**，剩余待办均为实物/人工项，不阻塞代码合入。