# B6 代码安全与质量 评审报告

> 角色：代码安全与质量评审（B6）
> 日期：2026-10-30（第二轮：合并实机验证结果 + C++ 板端深走查）
> 依据：真实检查（pytest 全量、reproduce_consistency.py 复跑、P0-4/P0-5 修复复测、C++/Python 源码走读、CI/脚本/文档核查）

## 一、总体结论

代码质量整体良好，未发现答辩阻断级（P0）代码缺陷：

- `pytest tests -q`：**190 passed / 4 skipped**（4 个跳过为 Windows MSYS 路径差异，CI 覆盖）。
- 第一轮 P0-4（评分门控+去重）、P0-5（RKNN 返回值）修复**有效且已落单测**：复测两人 0.5m 不再 DANGER、重复脚点去重为 n=1、三人链经防抖后正常 DANGER；`yolov5.cc:224` 已先 `memset(od_results,...)` 并检查 `rknn_run` 返回值。
- C++ 头文件实现严谨：`minijson.h` 有深度限制 128/越界检查；`graph_laplacian.h` 对 NaN/Inf 输入过滤、Jacobi 不收敛时安全回退 0；`risk_fsm.h` 与 Python 端逻辑逐项对齐。
- `scripts/reproduce_consistency.py` 复跑成功：M0(ms33)=89.6737%、PROD ms33=91.2974%、PROD fps=91.6350%，与 `results/rerun/_consistency.json` 一致；README 对 98.21% 的不可复现标注准确。
- 统计复算：全量 Spearman ρ=0.6882；fiedler>0 子集 ρ=0.3503；按视频 111 条 mean=0.2273/median=0.1959（75 正/31 零/5 负）；fiedler–density ρ=0.8049，证实"共线"表述。

**问题集中在部署/运维外围 + C++ 板端失败路径**：未认证的监控 Web 服务、SSH 运维脚本默认凭据+不校验主机密钥、评审文档明文凭据复发；C++ 后处理存在 1 个**类别过滤下标错误（跨类 NMS 误抑制 → 行人漏检）**、退出路径 1 处无效 free、VENC 帧结构未初始化、RKNN 零拷贝内存分配返回值未检查。共 **P1×8、P2×14**。

## 二、P0（无）

正常运行 28h 路径上未发现必然触发且可直接导致崩溃/内存破坏的问题；所有已知异常路径均有返回值检查或看门狗兜底。

## 三、P1（优先修复）

### P1-1：后处理 NMS 类别过滤用错下标，导致跨类别误抑制（漏检行人）【第二轮新增】

- **证据**：`src/luckfox_rtsp_yolov5/src/postprocess.cc:138` 与 `:148`
  ```cpp
  if (order[i] == -1 || classIds[i] != filterId) { continue; }   // L138
  ...
  if (m == -1 || classIds[j] != filterId) { continue; }          // L148
  ```
- **分析**：`post_process` 在 `postprocess.cc:476` 用 `quick_sort_indice_inverse(objProbs, 0, validCount-1, indexArray)` 将 `objProbs` 降序并置换 `indexArray`。此后 `indexArray[i]` 是第 i 高置信度框的**原始下标**，而 `classIds[i]` 是**原始下标 i 处框的类别**，二者不是同一框；应使用 `classIds[order[i]]`。注释声称"旧版误用 classIds[i] 已修正"（`:146-147`），但修正不彻底。
- **影响**：混合场景（行人+车辆）中，NMS(class=person) 可能把汽车框当 person 锚点，并按 IoU 抑制与之重叠的行人框 → `od_results.count` 偏小 → 增强决策 `featN` 偏小 → **风险等级被低估**。该缺陷位于检测层，直接绕过已修复的"死标签/评分门控"防线。
- **修复建议**：`:138` 改为 `classIds[order[i]]`、`:148` 改为 `classIds[m]`；在 `tests/test_enhanced_decision.cc` 或新增后处理测试中加入"person 与 vehicle 重叠"用例。

### P1-2：退出路径对 SDK 内部指针执行 free()，堆破坏风险【第二轮新增；资源生命周期领域——修复后仍存在】

- **证据**：`src/luckfox_rtsp_yolov5/src/main.cc:154-156` 对 `VENC_STREAM_S stFrame` memset 后 `malloc` 一个 `pstPack`；`:538` `RK_MPI_VENC_GetStream` 会将 `pstStream->pstPack` 覆写为编码通道内部缓冲指针（rockit 实现）；`:188` 退出时 `free(stFrame.pstPack)` 释放 SDK 内部指针 → **无效 free → 堆损坏/ABRT**。`:156` 的 malloc 在 GetStream 后即被覆写泄漏（一次性）。
- **修复建议**：删除 `main.cc:156` 的 malloc；cleanup 中移除 `free(stFrame.pstPack)` 分支；`frame_pack_ready` 不再基于 `stFrame.pstPack` 判空。

### P1-3：VIDEO_FRAME_INFO_S 仅部分字段初始化（未定义行为）【第二轮新增；编码失败路径领域——修复后仍存在】

- **证据**：`src/luckfox_rtsp_yolov5/src/main.cc:262-271` 只赋值 6 个字段，其余栈上垃圾值；`:521` 每帧传给 `RK_MPI_VENC_SendFrame`。
- **修复建议**：`memset(&h264_frame, 0, sizeof(h264_frame));` 后再赋值。
- **验证状态**：**未验证** SDK 是否读取未初始化字段（板端编码实测正常，SDK 升级有风险）。

### P1-4：RKNN 零拷贝内存/属性 malloc 返回值未检查（OOM 空指针解引用）【第二轮新增；RKNN 返回值检查领域——修复后仍存在】

- **证据**：`src/luckfox_rtsp_yolov5/src/yolov5.cc:119`、`:130` `rknn_create_mem` 未判空；`:153-156` `malloc` 后立即 `memcpy`（malloc 失败即写 NULL）。下游解引用：`main.cc:376` `input_mems[0]->virt_addr`、`postprocess.cc:445/454/459` `_outputs[i]->virt_addr/.buf`。
- **修复建议**：`yolov5.cc:119/130` 判空并统一失败处理；`:153/155` 改用 `calloc`+判空或 `std::vector`。

### P1-5：`RK_MPI_MB_Handle2VirAddr` 返回值未判空（句柄异常时写空指针）【第二轮新增】

- **证据**：`src/luckfox_rtsp_yolov5/src/main.cc:270`（cv::Mat 包装）、`:358`（`vi_data` 直接给 `yuv420sp_to_bgr`）、`:543`（`pData` 直接给 `rtsp_tx_video`）。
- **修复建议**：三处均判空，失败时跳过本帧并复用看门狗计数，避免 SIGSEGV。

### P1-6：go2rtc 监控 Web 服务无认证，局域网内任意设备可查看实时画面并调用 API【第一轮】

- **证据**：`docs/WEB_VIEWER.md:31-33` 配置 `api: listen: ":1984"`（绑定所有接口）；`docs/WEB_VIEWER.md:20` 访问地址 `http://192.168.0.136:1984` 打开即看；文档 L47-49 用 `curl http://127.0.0.1:1984/api/streams` 验证返回 cam 流，证明 API 无鉴权；端口 1984/8554/8555 全开。
- **影响**：局域网内任何设备可实时查看监控画面；go2rtc API 可创建指向任意地址/本地文件的流（file:// 已在本项目使用），存在 SSRF/本地文件读取面。
- **修复建议**：go2rtc 配置 `api: { listen: "127.0.0.1:1984", auth: "用户名:密码" }`，外网访问走 SSH 隧道/反向代理；`WEB_VIEWER.md` 增加安全警告段。

### P1-7：board_health_snapshot.py 硬编码默认 SSH 凭据 + AutoAddPolicy（可被 MITM）【第一轮】

- **证据**：`scripts/board_health_snapshot.py:38-41` `HOST=192.168.0.136 / USER=pico / PASS=luckfox`（Luckfox 出厂默认密码，当前代码为 `os.environ.get("BOARD_PASS", "luckfox")`）；`:176` `paramiko.AutoAddPolicy()`（接受任意主机密钥）；`:179` 明文密码直连。
- **影响**：局域网中间人可冒充开发板，脚本自动接受其主机密钥并把明文密码发给攻击者；板端未改出厂密码时拿到脚本即可登录。
- **修复建议**：删除默认密码，改为必填环境变量/命令行参数（缺省报错退出）；主机密钥校验改 `WarningPolicy` 或显式 known_hosts；文档补充"修改板端默认密码"步骤。

### P1-8：明文 SSH 凭据再次写入评审文档（修复后仍存在）【第一轮】

- **证据**：`docs/review/20261030/CONTEXT.md:13` 明文记录 `老电脑 Ubuntu 192.168.0.63（liao/123456）`。第一轮已清理 git 历史中的同类凭据（见 `agent_B7_数据伦理.md` L79-80），且 B7 报告 L46-47 声称"当前工作区敏感字符串 无"——与事实矛盾，说明清除动作未覆盖新生成的评审文档。
- **影响**：`docs/review/` 虽被 `.gitignore:52` 忽略不入库，但目录若被打包/转发/误传，凭据即泄露。
- **修复建议**：立即将 CONTEXT.md 中的 IP/用户名/密码替换为占位符；评审目录约定禁止写入真实凭据；CI/本地加 grep 检查防止复发。

## 四、P2（次要）

### C++ 板端

- **P2-1：`system()` 启动外部脚本且不检查返回值** — `src/luckfox_rtsp_yolov5/src/main.cc:108` `system("RkLunch-stop.sh")`，依赖 PATH 与脚本存在，失败静默；建议改 `execl` + 检查返回值。
- **P2-2：初始化失败路径泄漏 RKNN 上下文** — `src/luckfox_rtsp_yolov5/src/yolov5.cc:54` 后多个失败分支（`:64-67/:87-90/:105-108/:123-126/:132-135`）未 `rknn_destroy(ctx)`，systemd 反复重启会累积泄漏。
- **P2-3：`loadLabelName` 恒返回 0，标签文件加载失败被掩盖** — `src/luckfox_rtsp_yolov5/src/postprocess.cc:112-117` 忽略 `readLines` 的 -1，`init_post_process`（`:518-528`）永远检测不到失败；标签路径为相对路径 `./model/coco_80_labels_list.txt`（`:37`），systemd CWD 非项目目录时必缺，`coco_cls_to_name` 静默返回 "null"。
- **P2-4：快速排序最坏情况递归深度与 O(n²)** — `src/luckfox_rtsp_yolov5/src/postprocess.cc:174-204` 取 `input[left]` 为枢轴，已降序输入时递归深度≈validCount（最大约 25200），栈占用约 1.2~1.6MB（RV1106 主线程 8MB 栈不溢出但时间 O(n²)）。建议非递归/三数取中/插入排序兜底。
- **P2-5：硬编码三输出尺度** — `src/luckfox_rtsp_yolov5/src/postprocess.cc:436` `for (int i = 0; i < 3; i++)` 访问 `output_attrs[i]`（数组大小=`io_num.n_output`），换模型即越界读。建议用 `io_num.n_output` 并校验。
- **P2-6：配置回退静默** — `src/luckfox_rtsp_yolov5/src/risk_engine.cc:36-38` 非法配置静默回退默认值，建议加 `fprintf(stderr, ...)` 告警。
- **P2-7：硬件模块 `system()` 拼接 shell 字符串** — `src/hardware/voice_alert.cc:84`（`aplay -q -D <dev> <dir>/<file> &`）与 `:73`（`pkill -f 'aplay .*<dir>'`），`m_device/m_audioDir` 若来自外部配置可注入命令（当前为编译期常量，风险低）；`:95-96` 工厂返回裸 `new`，调用方需手动 delete。
- **P2-8：未忽略 SIGPIPE** — `src/luckfox_rtsp_yolov5/src/main.cc:106-107` 未注册 `SIGPIPE` 忽略；RTSP 客户端断开时 `send()` 可能触发 SIGPIPE 终止进程（`:546`）。**未验证** rtsp_demo 是否已处理。
- **P2-9：注释/调试代码残留** — `src/luckfox_rtsp_yolov5/src/yolov5.cc:142` 空 `// TODO`；`yolov5.cc:69,80,97,118`、`main.cc:277-278, 361-363, 517` 注释掉的旧代码；`main.cc:465-466` 每帧对每框 `printf`，建议节流。

### Python 端

- **P2-10：数据生成脚本静默吞掉谱特征计算异常** — `scripts/crowd_test_enhanced.py:121-122` 与 `scripts/ablation_study.py:84-85` `except Exception: pass`，fiedler 静默置 0 继续产出数据，与生产口径混淆且无日志。建议 `logger.warning` + 在输出 JSON 统计失败帧数。
- **P2-11：README 仍残留"显著提升"表述（修复后仍存在）** — `README.md:104` "谱特征在中等/密集聚集场景**显著**提升高风险判定灵敏度"；第一轮 P2-8 已要求弱化，`README.md:95` 已加描述性免责但 L104 未同步。建议改为"提升（描述性，6 条代表性视频消融，未做统计检验）"。
- **P2-12：README "Fiedler 均值随等级单调上升"未披露条件均值反转** — `results/rerun/_aggregate.json` `fiedler_by_level`：SAFE positive_mean=1.3176（356 帧）、WARNING positive_mean=0.6084（2250 帧）；SAFE 帧主要为 FSM `confirm_up=3` 防抖过渡帧。建议 README L80 加脚注说明条件均值与防抖帧。
- **P2-13：输入校验在模型加载之后** — `scripts/crowd_test_enhanced.py:185-192` 先加载 YOLO 模型（可能触发权重下载）再检查输入存在性，建议先 `Path.exists()` 校验。
- **P2-14：VideoWriter 打开失败无感知 / 批量脚本设备号硬编码** — `scripts/crowd_test_enhanced.py`、`scripts/ablation_study.py` 未检查 `cv2.VideoWriter.isOpened()`；`ablation_study.py` 检测调用 `device=0` 硬编码 GPU。建议检查 writer 并让设备号可配置。

## 五、C++ 与 Python 行为一致性测试覆盖

| 测试 | 覆盖点 | 评估 |
|---|---|---|
| `tests/test_risk_fsm.py::test_cpp_header_defaults_consistent` | 解析 `src/algorithms/risk_fsm.h` 中 `name = value` 文本与 `RiskFSMConfig` dataclass 默认值比对 | ✅ 有效；弱点：仅匹配文本格式，且只覆盖 `risk_fsm.h`，`risk_config.h`/`enhanced_decision.h` 默认值无自动测试 |
| `tests/test_golden_regression.py` / `test_golden_independent.py` | C++ 预处理与 Python/OpenCV 黄金数据逐像素回归（容差 2） | ✅ 强覆盖，黄金文件由 `scripts/generate_golden.py` 独立生成，避免循环依赖 |
| `tests/test_integration_pipeline.py` | 端到端场景（空/稀疏/聚集/密集/双簇/长链）驱动真实脚本函数 | ✅ 强覆盖，含配置阈值影响测试 |
| `src/luckfox_rtsp_yolov5/tests/*.cc`（12 个：test_preprocess、test_enhanced_decision、test_risk_config、test_minijson、test_golden_regression、test_hardware_modules 等） | C++ 侧单元/黄金回归 | ✅ 由 `make test` 统一运行 |
| C++ 板端二进制 ↔ Python 端实机 RTSP/RKNN 链路 | — | ⚠️ **未验证**（需真实硬件），建议用离线回放脚本人工对比并归档 |

**结论**：算法/预处理层一致性测试覆盖充分；板端实机链路一致性为**未验证**盲区。

## 六、调试代码 / 硬编码凭据 / 危险操作专项检查

1. **调试代码残留**：少量注释掉的 printf/旧代码（见 P2-9），无 TODO 型未实现逻辑混入主路径；`profileReport` 默认开启但每 90 帧一次，可接受。
2. **硬编码凭据**：`scripts/board_health_snapshot.py:38-41` 默认密码 `luckfox`（见 P1-7）；`docs/review/20261030/CONTEXT.md:13` 明文 SSH 凭据（见 P1-8）；其余脚本未发现 API Key/Token。
3. **危险操作**：`main.cc:108` 启动时执行 `RkLunch-stop.sh`（P2-1）；`voice_alert.cc:84/73` shell 拼接（P2-7）；`board_health_snapshot.py` 远端执行 `rm`/`kill` 类命令（有 `|| true` 保护，命令来自常量列表，无外部注入面）。

## 七、未验证项

- **NMS 跨类误抑制的实际影响（P1-1）**：未在板端/混合场景视频上实测触发率。
- **退出路径 invalid free（P1-2）**：rockit 源码行为为静态分析结论，需板端 `kill -TERM` 后观察退出码/dmesg。
- **VENC 未初始化字段（P1-3）**：板端实测正常，SDK 版本变化未验证。
- **RKNN OOM 路径（P1-4）**：未在内存紧张时验证。
- **SIGPIPE 是否被 rtsp_demo 忽略（P2-8）**：需查板端 rtsp_demo 源码或实测。
- **`make test` / CMake ctest 在 Linux 上的实际执行**：本机无 g++/cmake，未复跑；仅以仓库内 `cpptest.log`（2026-10-29 全过）与 CI 配置为旁证。
- **GitHub Actions 远程实际运行状态**：本环境无法访问远程运行记录。
- **go2rtc 实际暴露面（P1-6）**：依赖实际网络环境。
- **Luckfox 板端真机 FSM/谱特征数值与 Python 一致性**：需真机与 RKNN 环境。

## 八、证据与复现记录

| 检查项 | 结果 |
|---|---|
| `env -u PYTHONPATH G:/py311_gpu/Scripts/python.exe -m pytest tests -q` | 190 passed / 4 skipped |
| `python scripts/reproduce_consistency.py` | M0_ms33=89.6737%，PROD_ms33=91.2974%，PROD_frame=90.5911%，PROD_fps=91.6350% |
| P0-4 复测（两人 0.5m / 重复点 / 三人链） | SAFE / n=1 SAFE / 5 帧后 DANGER |
| 统计复算（45021 帧） | 全量 ρ=0.6882；fiedler>0 子集(10814帧) ρ=0.3503；按视频 111 条 mean=0.2273/median=0.1959；fiedler–density ρ=0.8049，fiedler–r_gcc ρ=0.7730 |
| 敏感字符串扫描 | `git grep` 无 token/私钥；`docs/review/20261030/CONTEXT.md:13` 含明文 SSH 凭据（P1-8） |
| 本轮 C++ 深走查 | 7 个 .cc + 7 个 include + 6 个 hardware 文件 + 5 个 Python 算法 + 8 个脚本 + 4 个测试文件逐行审查 |

*报告结束。*