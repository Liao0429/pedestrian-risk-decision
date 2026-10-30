# 交叉质疑报告：A8（文档/论文审稿人）vs A3（嵌入式 C++ 专家）

> 轮次：R16 交叉评审
> 日期：2026-10-29
> 质疑对象：`docs/review/20261029/agent3_embedded.md`（A3 独立评审报告）
> 核实范围（只读）：
> - `src/luckfox_rtsp_yolov5/src/main.cc`、`yolov5.cc`、`postprocess.cc`、`luckfox_mpi.cc`、`preprocess.cc`
> - `src/luckfox_rtsp_yolov5/include/luckfox_mpi.h`、`risk_config.h`
> - `src/hardware/sysfs_spi.cc`、`ili9341.h`、`xpt2046.h`、`selftest.h`
> - `src/luckfox_rtsp_yolov5/tests/test_ili9341.cc`、`test_xpt2046.cc`
> - `docs/hardware/lvgl_board_port.md`、`selftest.md`
> 方式：全程只读，未修改任何现有项目文件；本报告为唯一新增文件。

---

## 质疑 1：RKNN 返回值被吞 + `od_results` 未初始化定为 P1 —— 从答辩风险角度应升 P0

### 判断（同意技术事实，不同意严重度定级）

A3 的技术事实**全部准确**，已逐行核实：

- `main.cc:151` `object_detect_result_list od_results;` 未初始化（只有 `rknn_app_ctx` 被 `memset`）；
- `main.cc:382` `inference_yolov5_model(&rknn_app_ctx, &od_results);` 返回值被直接忽略；
- `yolov5.cc:199-203` `rknn_run` 失败时返回 -1，`post_process` 不会被调用；
- `postprocess.cc:400` 的 `memset(od_results, 0, ...)` 仅在成功路径执行 → **失败路径下 `od_results.count` 为栈垃圾**；
- `main.cc:397` 与 `:446` 两条循环均以 `od_results.count` 为边界遍历 `results[i]`。

但严重度应升 **P0**，理由：

1. **形式上是未定义行为**（读取未初始化对象 + 潜在越界访问 `results[i]`）。这是答辩评委最容易当场抓住的 C++ 硬伤，代码审查一眼可见，且本项目其余部分质量高，这个漏洞反而更扎眼。
2. **A3 未点名的第二条故障路径**：`main.cc:396` `groundPts.reserve((size_t)od_results.count)` 在垃圾 count 巨大时会先抛 `std::length_error`/`std::bad_alloc`，无捕获 → `std::terminate`。即失败路径不止"遍历垃圾内存"，还有"异常终止"。
3. **修复成本几乎为零**（一行 `memset` + 一次返回值检查 + 失败计数）。按"低修复成本 × 高可见性 × 潜在现场崩溃"的答辩风险矩阵，没有理由留在 P1。
4. A3 自己也指出"看门狗只监控 VI，对 NPU 失败完全无感知"，这与 `main.cc:34-43` 注释宣称的加固设计意图自相矛盾。评委若追问"看门狗为什么漏了 NPU"，现状无法自洽。

### 建议

- 将 P1-1 升为 **P0**（必须修）。
- 修复项采纳 A3 原建议（置零 + 检查返回值 + 计入看门狗失败计数并跳过本帧/退出），但须**同步更新 `main.cc` 顶部看门狗注释**（见质疑 4）。

---

## 质疑 2：SPI CS 事务拆分 —— 诊断准确，但修复建议对 ILI9341 不完整

### 判断（部分同意）

**诊断部分完全正确：**

- `sysfs_spi.cc:59-73` 每次 `transfer` 只发 `SPI_IOC_MESSAGE(1)`，CS 在两次 ioctl 之间必然释放；
- `xpt2046.h:126-133`（命令 1 字节 + 读 2 字节拆两次 transfer）违反 XPT2046"CS 上升沿中止转换"的协议要求；
- `ili9341.h:290-303`（0xD3 命令 + 读 4 字节拆两次 transfer）违反 ILI9341 读命令"CS 必须连续保持"的要求；
- `FakeSpi`（`test_ili9341.cc:46-76`、`test_xpt2046.cc:45-75`）确实无 CS 事务语义，主机测试无法暴露——A3 该判断准确。

**但 A3 的修复建议有技术缺口：**

`SpiBus` 增加多段事务接口 + `SPI_IOC_MESSAGE(N)` 只能解决 XPT2046（纯 SPI、无 DC 引脚）。对 ILI9341 读 ID **无效**：命令段需要 DC=低、数据段需要 DC=高，而 DC 是外部 GPIO，**用户态无法在原子 ioctl 中途切换**。按 A3 建议实现后，结果是 XPT2046 好了、`Ili9341::readId` 真机依然失败，且 FakeSpi 若只补"CS 保持"而不建模 DC 约束，测试仍然暴露不了。

正确修法（三选一，需按硬件约束决策）：

1. `SPI_NO_CS` 模式 + GPIO 手动控制 CS：两次 transfer 之间保持 CS 低电平，同时用 GPIO 切换 DC（需新增"CS 保持"语义接口）；
2. 若 RV1106 SPI 控制器支持 9-bit 模式（DC 作第 9 位）且屏端接线允许，改 9-bit 协议（硬件/接线需变更）；
3. 仅对读路径特判，写路径维持现状（ILI9341 写在 CS 释放后仍能正确锁存命令，业界 spidev 驱动普遍如此，A3 未区分读写路径的 CS 敏感性）。

### 建议

- 修复建议中补充 ILI9341 读路径的 CS 保持方案，不能只提"多段事务接口"。
- `FakeSpi` 增加"事务/CS 状态机"语义（连续 transfer 建模为事务，并断言 CS 保持期间 DC 切换的合法性），否则真机问题仍无法在主机测试暴露。

---

## 质疑 3：中文注释质量评估 —— 事实准确，结论框架偏高

### 判断（部分同意）

抽查 5 个源文件，A3 的统计**基本准确**：

- `postprocess.cc`：520 行仅 2 行中文（`:126-127` NMS 修正注释）——准确；
- `luckfox_mpi.cc` / `luckfox_mpi.h`：0 行中文——准确；
- `preprocess.cc`（BT.601 定点系数、letterbox 数学语义）、`risk_config.h`（校验语义）、`ili9341.h`/`xpt2046.h`（协议/设计动机）：中文注释质量确实高，8.5~9.5 分合理。

但"**整体优秀（加权约 8.5/10）**"的结论框架偏高：

1. 用户要求是"写好中文注释"（硬性要求）。4 个核心派生文件（RKNN 封装、后处理、视频管线）恰是项目中最复杂、最需要注释的部分，且 `postprocess.cc` 已有 2 行中文说明"旧版误用 classIds[i] 已修正"——证明项目**曾在此文件补过中文注释，但只补了 2 行就停手**。
2. "加权"口径未说明：按文件数加权 vs 按行数加权结论不同。4 个重灾区文件合计约 900 行，占审查域总行数近 1/4，按行数加权整体不可能有 8.5。
3. 答辩场景下，评委打开 `luckfox_mpi.cc`/`yolov5.cc` 看到全英文 Rockchip 代码，与"已按用户要求写好中文注释"的陈述直接冲突。

### 建议

- 结论改为"**自研/硬件/算法文件优秀，4 个 Rockchip 派生文件未达标，整体不能宣称'优秀'**"。
- 明确加权口径（建议按行数 + 文件重要度双因子）。
- 将 4 个派生文件的中文补齐列为修复轮**硬性验收项**（A3 已列优先级清单，这点同意）。

---

## 质疑 4：修复建议与文档/注释同步 —— A3 完全未覆盖文档侧

### 判断（风险存在，且 A3 漏报）

A3 的修复建议全部是纯代码视角，未包含文档/注释同步清单。逐项评估：

- **SPI 事务接口变更**：会触及 `ili9341.h` 头部设计注释（`:11-14`、`:62-64`、`:94-95`、`:288-289`）、`xpt2046.h` 协议注释（`:15-18`）、`sysfs_spi.h` 集成示例、`docs/hardware/lvgl_board_port.md` §5.4 与路线图步骤 4/6、`docs/hardware/selftest.md` 与 `touchscreen.md`、FakeSpi 测试注释。A3 只提了"FakeSpi 补 CS 语义"，其余全部未提。
- **RKNN 看门狗修复**：`main.cc:34-43` 注释明确写"看门狗只监控 VI"，扩展 NPU 失败计数后，该注释及任何描述看门狗设计的文档必须同步。
- **NEON `group_ok` 重写**：`preprocess.cc` 快速路径注释与 `preprocess.h` 数学语义注释需同步。

**额外发现（A3 漏报的现存文档-实现脱节错误）：**

`docs/hardware/selftest.md:126-130` 声称"无屏幕时读到非 0x9341 属预期，**待 ILI9341 到货接线后 `SPI_LCD_ID` 应转为 PASS**"。但实际：

1. `selftest.h:390` 默认探测命令是 `cat /sys/bus/spi/devices/spi0.0/of_node/compatible`（读**设备树 compatible 字符串**），`selftest.h:319` 检查输出是否含 `"9341"`——该字符串是 `rockchip,spidev`，**永远不含 "9341"**。即当前自检项根本没在读屏幕芯片 ID，文档对"SPI0 硬件通路打通"的解读不成立；
2. 即使把探测命令换成真实 `Ili9341::readId`，质疑 2 的 CS 拆分问题会使读 ID 必然失败；
3. 因此"接线后应转 PASS"的预期在两条路径下都不成立，文档必须修正。

### 建议

- 修复轮建立"**代码 + 注释 + 文档**"三方同步清单，SPI 修复需同步更新 `lvgl_board_port.md`/`selftest.md`/`touchscreen.md`。
- 单独派一条 docs 任务：修正 `selftest.md` 对 `SPI_LCD_ID` 的错误解读与 PASS 预期（含自检命令改为真实读 ID 的计划）。

---

## 最高价值质疑（供裁决）

1. **【P0 升级】** `od_results` 未初始化 + RKNN 返回值被吞：技术事实无误，但按答辩风险（UB 可被评委一眼发现、现场偶发崩溃、`reserve` 异常终止第二条路径、修复成本近零）应升 **P0**。
2. **【修复缺口】** SPI 多段事务接口只能救 XPT2046，救不了 ILI9341 读 ID——DC 是外部 GPIO，无法在原子 `SPI_IOC_MESSAGE(N)` 内切换；需 `SPI_NO_CS`+GPIO CS 保持或 9-bit 方案，且 FakeSpi 需同时建模 CS 与 DC 约束。
3. **【文档错误】** `selftest.md` 对 `SPI_LCD_ID` 的 PASS 预期不成立：默认探测命令读的是设备树 compatible（`rockchip,spidev`），不是芯片 ID；且 `readId` 的 CS 拆分问题使真机读 ID 必然失败——A3 未发现此文档-实现脱节。
4. **【结论框架】** "中文注释整体优秀 8.5/10"与硬性要求存在张力：4 个核心派生文件（约 900 行）零/近零中文，应改判"未完全达标"并明确加权口径。
5. **【文档同步】** A3 全部修复建议缺少文档/注释同步清单，实施后必然造成头文件注释、硬件文档与代码再次脱节。

---

> 声明：本交叉质疑报告基于只读核查，未修改任何现有项目文件。所有行号均以当前工作区文件为准，已逐一核对。