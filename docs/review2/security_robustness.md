# 安全与健壮性评估报告（review2）

> 评估范围：`src/luckfox_rtsp_yolov5/` 板端 C++ 工程（Luckfox RV1106，RKNN/RTSP/OpenCV），
> 以及其依赖的 `src/algorithms/*.h` 算法头文件与 `config/default_config.json`。
> 方式：静态代码审查（只读，未修改任何文件）；评估环境无 make/g++，主机单元测试未能实跑，
> 结论基于源码分析 + 真机日志（`results/edge_board/enhanced_final_20261026.log`、`EDGE_TEST_REPORT.md`）。
> 威胁模型：局域网可访问的监控设备（RTSP/HTTP 无认证暴露）+ 本地可写文件系统 + root 运行。

---

## 一、高优先级（P0/P1，建议尽快修复）

### 1. [P0] main.cc 栈缓冲区溢出（`sprintf` 越界写）
- 位置：`src/luckfox_rtsp_yolov5/src/main.cc:265`
  ```c
  char text[16];
  ...
  sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
  ```
- 问题：COCO 80 类中多个类名长度 ≥ 9（`traffic light`=13、`dining table`=12、`refrigerator`=12、
  `toothbrush`=10、`stop sign`=9、`surfboard`=9、`snowboard`=9 等）。格式化后最长可达
  13 + 1 + 6（`100.0%`）+ 1（`\0`） = **21 字节 > 16 字节缓冲区**，直接越界写栈。
- 可达性：绘制循环（`main.cc:246-270`）对**所有**检测类别都执行画框/标注，不受
  `target_classes=[0]`（仅 person）过滤。只要画面出现任一长名类别即可触发。
- 后果：栈破坏 → 内存损坏/崩溃，可被构造的输入（如恶意贴图）升级为任意代码执行。
  这是本次评估**最严重**的内存安全问题。
- 附带：`main.cc:315` `memset(text,0,8)` 只清零 8/16 字节，应为 `sizeof(text)`。
- 修复：`char text[32]` + `snprintf(text, sizeof text, ...)`；全工程启用
  `-Wformat-overflow -fstack-protector-strong`，交叉编译时加 ASan 单测。

### 2. [P1] `system("RkLunch-stop.sh")` —— root 执行外部命令，无路径/失败检查
- 位置：`main.cc:85`
  ```c
  system("RkLunch-stop.sh");
  ```
- 问题：
  - 程序以 `sudo`（root）运行，却通过 `system()` 经 **PATH + 当前工作目录**解析外部脚本名。
    若 CWD 或 PATH 前部被写入同名恶意脚本，将以 root 执行 → **本地提权/供应链投毒**。
  - 无失败检查。真机日志第一行即为 `sh: 1: RkLunch-stop.sh: not found`，说明该命令在目标板上
    每次启动都拉起一个 shell 并报错，属于无用开销 + 噪音。
- 修复：使用绝对路径（如 `/usr/bin/RkLunch-stop.sh`）或直接调用 Rockchip 库函数停止 Rk 服务；
  至少改为 `if (system("...") != 0) perror(...)`；启动前校验文件存在且为可信路径。

### 3. [P1] RTSP / Web 推流无任何认证，摄像头画面局域网裸奔
- 位置：`main.cc:167-170`（`create_rtsp_demo(554)`、`rtsp_new_session(..., "/live/0")`）；
  `docs/WEB_VIEWER.md`（go2rtc `:1984`、HLS `:8554`、WebRTC `:8555`）。
- 证据：`results/edge_board/EDGE_TEST_REPORT.md` 实测 `ss -tlnp` 显示 **0.0.0.0:554** 监听；
  文档公开静态 IP `192.168.0.136`。
- 后果：同一局域网内任意设备可拉取实时摄像头视频与行人检测叠加画面，无鉴权、无加密。
  对行人/人群监控这类隐私敏感场景属高危暴露。
- 修复：RTSP 增加认证或绑定可信网卡；go2rtc 开启 basic auth；防火墙上对 554/1984/8554/8555
  做来源白名单；SSID/凭据不入库（见 §五-11）。

---

## 二、中等优先级（P1/P2）

### 4. [P1] 关键初始化返回值未检查，失败后走未定义行为
- `main.cc:114` `init_yolov5_model(...)` 返回值被忽略：模型缺失/损坏时，后续
  `inference_yolov5_model` 会对未初始化的 `rknn_app_ctx` 操作 → 空指针/崩溃。
- `main.cc:116` `init_post_process()` 返回值被忽略。
- `main.cc:132/136/147` `RK_MPI_MB_CreatePool` / `RK_MPI_MB_GetMB` /
  `RK_MPI_MB_Handle2VirAddr` 均未检查；失败时 `data=NULL`，`cv::Mat(..., data)` 构造后首帧即崩。
- 建议：统一错误检查，返回非 0 时打印原因并返回明确退出码；板端可配合日志 + 看门狗。

### 5. [P1] 无崩溃恢复/看门狗，主循环无信号处理
- `main.cc:182` `while(1)`：无 SIGINT/SIGTERM 处理（无法优雅清理）、无看门狗、无自动重启。
  一旦因 §1 等内存错误崩溃，行人风险监控永久中断，不会自愈。
- 对“行人风险预警”类安全系统，建议：硬件看门狗或 supervisor（systemd `Restart=always`）；
  增加信号处理做资源释放（释放 model/pool/VI/VENC/RTSP）。

### 6. [P2] 配置校验缺口：`loadRiskConfigOr` 静默吞错 + 阈值缺少上界
- 位置：`include/risk_config.h:161-167`（`loadRiskConfigOr` catch-all 返回 fallback）、
  `risk_config.h:88/101/119/138`（`edge_dist_m` 仅要求 >0、`n` 仅要求 ≥0）。
- 问题：
  - 配置文件缺失/损坏/路径错误时**静默回退默认值**，用户以为自定义阈值生效、实则用默认
    → 决策与预期不符，可能漏报。
  - `edge_dist_m` 无上限：可设 `1e9` 使全图连边 → 恒 DANGER（误报风暴）；
    `n` 无上限：可设 `1e9` 使 DANGER 永不触发 → 恒 SAFE（漏报）。
- 建议：启动时打印实际生效配置；给阈值加合理上界（如 edge_dist≤20m、n≤1000）；
  区分“文件缺失回退”与“配置非法告警”（后者应显式告警并拒绝启动或打印）。

### 7. [P2] 模型/配置/标签均用相对路径加载，CWD 敏感
- `main.cc:112` `./model/yolov5.rknn`、`postprocess.cc:26` `./model/coco_80_labels_list.txt`、
  `main.cc:98` `./config/default_config.json`。
- 文档要求先 `cd` 到固定目录再运行；若经 systemd/其它 CWD 启动，模型加载失败（崩溃）或
  配置静默回退（§6）。建议基于 `/proc/self/exe` 解析绝对路径，或要求命令行传入绝对路径。

---

## 三、低优先级 / 健壮性建议（P2/P3）

### 8. 类型/数值健壮性
- `main.cc:73-81` 与 `risk_engine.cc:44-53` 的 `H.at<double>()` 假设 H 为 `CV_64F`；
  若传入整数/单通道 Mat 将类型混淆 UB。建议 `setHomography` 校验 `H.type()==CV_64F && H.rows==3 && H.cols==3`。
- `mapToGroundPixel`/`mapToGround` 对 `w≈0` 静默返回原像素坐标，可能把像素值当米制坐标；
  `main.cc` 的 `groundPts` 未过滤 NaN/Inf（`graph_laplacian.h` 虽已过滤，但 NaN 仍会进入 FSM 趋势计算）。
  建议在 `groundPts.push_back` 前校验 `std::isfinite(gx) && std::isfinite(gy)`。

### 9. NMS 递归 + O(n²)，对抗场景 CPU DoS / 栈风险
- `postprocess.cc:151` `quick_sort_indice_inverse` 为递归快排；`nms` 为 O(validCount²)。
  validCount 理论可达 ~3 万（三网格全高响应），递归深度可能数百~上千，嵌入式栈易溢出；
  O(n²) 在杂乱/对抗画面下造成推理停顿。建议 NMS 改迭代实现并对 validCount 设上限。

### 10. 日志含行人位置明细，长期留存构成隐私面
- `main.cc:259-260` 每帧打印 `%s @ (x1 y1 x2 y2) prop`（检测框坐标+类别+置信度），
  `main.cc:227` 打印 `[EnhancedDecision] n/rho/r_gcc/fiedler`。
  `results/edge_board/*.log` 已长期留存此类数据。建议加日志开关，生产默认关闭或脱敏。

### 11. 仓库未发现真实凭据（正面）
- 全仓无硬编码密钥/私钥/真实密码。`docs/WIFI_SETUP.md:31` 的 `MyHotspot/12345678` 为示例占位，
  建议在文档中明确标注“示例密码”，避免被照抄进真实部署。
- 文档中的静态 IP `192.168.0.136`、`rtsp://...` 属开发环境信息，公开仓库前应替换为占位符。

### 12. 坏味道：未初始化指针 / 忽略返回值
- `postprocess.cc:86` `char *s;` 未初始化即传给 `readLine`（虽 `readLine` 内部会重新赋值，
  仍属未初始化指针传递）。
- `loadLabelName`（`postprocess.cc:96-101`）忽略 `readLines` 返回值；标签文件缺失时
  `init_post_process` 仍返回 0，`coco_cls_to_name` 静默返回 `"null"`，建议显式告警。

---

## 四、正面发现（已在审计后改进的部分）

- `risk_config.h` + `minijson.h`：JSON 解析有深度限制 128、控制字符/代理对校验、数值范围检查、
  `warning.n ≤ danger.n` 语义校验，配置层防御较扎实。
- `graph_laplacian.h` / `kalman_filter.h` / `risk_fsm.h`：对 NaN/Inf 坐标、奇异协方差矩阵、
  特征值不收敛均有安全回退，算法层防御意识好。
- 主机单元测试覆盖 `EnhancedDecision` 确定性、配置读取、JSON 解析；CMake 支持 `ctest`。
- 内存占用小（真机 ~12.5MB），未见明显泄漏热点；帧循环内资源释放路径完整。

---

## 五、修复优先级总结

| 编号 | 等级 | 问题 | 建议 |
|---|---|---|---|
| 1 | P0 | `sprintf` 栈溢出（main.cc:265） | `snprintf`+加长缓冲区+编译器告警 |
| 2 | P1 | `system("RkLunch-stop.sh")` root 执行 | 绝对路径/替换/失败检查 |
| 3 | P1 | RTSP/Web 无认证暴露摄像头 | 认证+绑定网卡+防火墙白名单 |
| 4 | P1 | 关键 init 返回值未检查 | 统一错误处理+明确退出码 |
| 5 | P1 | 无看门狗/崩溃恢复 | systemd Restart + WDT + 信号清理 |
| 6 | P2 | 配置静默回退+缺上界 | 打印生效配置+阈值上界+区分错误类型 |
| 7 | P2 | 相对路径 CWD 敏感 | 基于 /proc/self/exe 解析绝对路径 |
| 8 | P2 | H 类型假设/NaN 脚点 | 校验类型+isfinite 过滤 |
| 9 | P3 | 递归快排/O(n²) NMS | 迭代 NMS+validCount 上限 |
| 10 | P3 | 日志含位置数据 | 日志开关/脱敏 |