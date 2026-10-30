# 交叉质疑报告：A1 系统架构师 × A4 PCB 硬件专家（cross_R09_a1_vs_a4）

> 审查对象：`docs/review/20261029/agent4_pcb.md`（A4 PCB 硬件专家审查报告，230 行）
> 审查人：A1 系统架构师
> 方式：**只读交叉验证**。未修改任何项目文件；仅在系统临时目录（`$TMPDIR/pcb_v1_sim`、`$TMPDIR/pcb_v2_sim`）复制 `generate_pcb.py` 副本并翻转 `V2` 开关运行，用于复现 v1/v2 的 DRC 结果。
> 独立验证依据：`design.json`/`generate_pcb.py`/`嘉立创下单说明.md`（Gerber 包）、`docs/hardware/voice_module.md`、`docs/hardware/touchscreen.md`、`docs/hardware/tracking_servo.md`、`docs/hardware/PCB_BOARD_DESIGN.md`（v1.0）、`docs/工作日志.md`、`docs/hardware/system_integration.md`、`agent8_docs.md`、`agent1_architect.md`、BOM v2 手册全文。

---

## 一、总体结论（先说重点）

A4 报告的**三项 P0 引脚结论全部正确且证据充分**（我独立验证了 5 组以上项目内既有文档与 Gerber 数据，与 A4 一致）；"**实际 Gerber v1.5 已正确**"也成立（design.json U1-40=5V、U1-38=GND，Gerber 走线与之一致，v1 DRC 实测通过）。

但 A4 有两个关键结论**不准确**：

1. **"BOM v2 手册是唯一错误源"不成立**。真正的错误源头是 `docs/hardware/tracking_servo.md`（PWM8=第5脚、PWM9=第4脚、VBUS=第20脚），BOM v2 与 PCB_BOARD_DESIGN.md v1.0 都是它的传播者。只修 BOM v2 治标不治本。
2. **v2"脚本已备"言过其实**。实测 `V2=True` 运行 `generate_pcb.py`，**DRC 报 7 处短路**；且 v2 代码根本没有布 SPI 触摸屏的任何信号。"v2 免跳线版"目前既跑不通、也名不副实。

此外 A4 遗漏了一个架构级事实：**BOM v2 手册描述的是一块与 Gerber v1.5 完全不同的板子**（J8=USB-A、J9~J15=LED/按键、U2=MP1584 等位号在 Gerber v1.5 中不存在，J11 在两边同名不同物）。"修正 BOM v2"不是改几行引脚表就能完成的。

---

## 二、对三个 P0 分级的交叉验证

### 质疑点 1（P0-1）：PWM_PAN=Pin7/PWM_TILT=Pin6 是否正确、P0 分级是否确凿？

- **A4 报告行**：L57「`PWM8_M1 = Pin7`，不是 Pin5；`PWM9_M1 = Pin6`，不是 Pin4」；L86-87 实测 Pin5=PWM10_M1、Pin4=PWM11_IR_M1；L112 P0-1 表。
- **我的独立证据**：
  - `generate_pcb.py` L103-107（v2 走线注释）：`PWM8=Pin7/PWM9=Pin6`（基于 OCR 引脚图，10-28 写入，早于 A4）。
  - `嘉立创下单说明.md` L106-107/L117-118：`PWM8_M1→Pin 7`、`PWM9_M1→Pin 6`。
  - `voice_module.md` L95-96：Pin6=GPIO1_C5_d/UART4_TX_M1、Pin7=GP1_C4_d/UART4_RX_M1——与 PWM8/PWM9 同脚复用，间接锁定 Pin6/7。
  - `tracking_servo.md` L103-108（**错误方**）：PWM8_M1=第5脚、PWM9_M1=第4脚——即 BOM v2/PCB v1.0 错误值的出处。
  - `PCB_BOARD_DESIGN.md` v1.0 L53-54 与 BOM v2 L95-96 采用同一错误值。
- **判断：支持（P0 成立）**。正确映射有 4 组项目内独立来源佐证，错误值有明确传播链（tracking_servo → PCB v1.0/BOM v2）。
- **建议**：修正时**不要只改 BOM v2**，必须同步修正 `tracking_servo.md`（错误源头）与 `PCB_BOARD_DESIGN.md` v1.0，否则答辩时评委按仓库文档核对仍会看到矛盾。

### 质疑点 2（P0-2）：VBUS=Pin40、BOM v2 的 Pin20 会烧板，P0 分级是否确凿？

- **A4 报告行**：L58「VBUS = Pin40，不是 Pin20」；L79-82 BOM v2 L91 的 Pin20 是 GPIO1_D1（UART3_RX），5V 灌入有烧板风险；L113 P0-2 表。
- **我的独立证据**：
  - `design.json`：U1-40 net=5V、U1-38 net=GND；走线 `5V:(66,8)→(66,40)→(58.26,40)→(58.26,36.54)` 直达 U1 Pin40、`GND:右竖干→(53.18,36.54)` 直达 Pin38。**与 A4 §6.2 描述完全一致**。
  - 实际 Gerber `PedestrianRisk_F.Cu.gtl` 含 `X0058260000...X0058260000Y0036540000` 线段、`PedestrianRisk_B.Cu.gbl` 含 `X0053180000Y0036540000` 线段——**Gerber 文件与 design.json 一致，确认 v1.5 打样文件本身正确**。
  - `voice_module.md` L98/L102：Pin40=VBUS、Pin39=VSYS。
  - `工作日志.md` L51：v1.5「5V/GND 直连 U1 排母 Pin40(VBUS)/Pin38(GND)」。
  - `agent8_docs.md` P0-1 独立得出同一结论。
- **判断：支持（P0 成立）**。这是本轮评审唯一有**烧硬件风险**的引脚错误，P0 当之无愧。
- **建议**：A4 报告中"官方引脚图 OCR + 实机 pinmux"证据链正确，但**未附原始证据**（OCR 文本/命令输出/时间戳）。建议在 BOM v2 修正时附一条可复现实测步骤（如 `cat /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins | grep GPIO1_D1`），供答辩自查。

### 质疑点 3（P0-3）：DFPlayer/UART4 方案矛盾列为 P0 是否合理？

- **A4 报告行**：L59 UART4 与 PWM8/PWM9 同脚；L72-74 固件无 /dev/ttyS4；L114 P0-3 表；L220 §6.5 P0#2。
- **判断：支持，但需澄清两点**：
  1. **"无 /dev/ttyS4"与仓库文档矛盾**：`voice_module.md` L91 明确写「推荐使用 UART4（物理引脚 6/7），该串口默认固件已使能（设备节点 /dev/ttyS4）」。A4 实测（Ubuntu 22.04 镜像）与 voice_module（基于官方 buildroot 镜像）矛盾。A4 应指出该文档本身过时/冲突，而不是只给实测结论——否则答辩时评委翻到 voice_module.md 会质疑。
  2. P0-3 的性质是**方案未收敛 + 同脚物理冲突**，不是引脚映射错误。因为"云台 PWM 必须用 Pin6/7"与"DFPlayer UART4 想用 Pin6/7"在物理上互斥，无论如何接线都有一边不工作——作为答辩执行手册的硬伤，P0 分级可接受。
- **建议**：BOM v2 与 voice_module.md 需要统一"语音走 USB、UART4 仅作 GPIO/PWM 复用说明"的口径，并在 voice_module.md 头部标注"DFPlayer 方案已废弃"。

### 补充质疑：PWM 全 disabled 的断言缺少系统版本上下文

- **A4 报告行**：L89「12 个 PWM 控制器节点全部 status=disabled」；L176 Top5 #4。
- **判断：存疑但方向正确**。`tracking_servo.md` L138 称「官方镜像通常带 pwmchip1、pwmchip7、pwmchip10 等」——与 A4 的"全部 disabled"表面矛盾。合理解释是镜像不同（A4 实机为 Ubuntu 22.04，官方 buildroot 镜像可能预使能部分 PWM）。A4 应注明**实测板子的固件版本/系统**，否则 P1-4 的"必须重编固件"结论可能过度（若 buildroot 镜像已带 pwmchip10，可先用 PWM10/PWM11 兜底，见 tracking_servo L109）。

---

## 三、对「实际 Gerber 已正确，BOM v2 手册是唯一错误源」结论的质疑

### 质疑点 4（反对）：错误源头是 tracking_servo.md，BOM v2 只是传播者；且 BOM v2 描述的板子与 Gerber v1.5 根本不是同一块板

- **A4 报告行**：L186「实际 Gerber 已正确，BOM v2 手册是**唯一错误源**，必须修正手册」；L224 P2#6 仅把 PCB_BOARD_DESIGN.md 列为"评审后统一更新"。
- **我的证据（错误传播链）**：
  1. `tracking_servo.md` L103-108：PWM8_M1=第5脚、PWM9_M1=第4脚、PWM10=第2脚、PWM11=第1脚；L177：VBUS=第20脚。该表头自注"排针标号(Pico 风格)"，实为把 GP0~GP3 标签误映射到 Raspberry Pi Pico 的物理脚位，与 Luckfox Pico Max 实际 40pin 位序不符。
  2. `PCB_BOARD_DESIGN.md` v1.0 L53-54：PWM_PAN=GPIO1_C4_d（脚5）、PWM_TILT=GPIO1_C5_d（脚4）——直接抄自 tracking_servo。
  3. BOM v2 L6「依据 docs/hardware/ 下全部既有方案合并去重」、L95-96 同错。
  4. `system_integration.md` L143-144 使用另一套编号（Pin54/55=PWM10_1/11_1，实为 GPIO sysfs 号），同样错误。
- **结论**：仓库里**有 3 份文档**携带错误引脚（tracking_servo、PCB v1.0、BOM v2），另 1 份（system_integration）使用易混淆的编号体系。**"唯一错误源"不成立**。
- **更严重的事实**：BOM v2 §3.2（L74-86）的位号表是 **J8=USB-A、J9~J11=LED、J12~J15=按键、U2=MP1584、C1/C2=100μF、R1~R3=330Ω**；而 Gerber v1.5（design.json 98 焊盘）实际只有 **J1/J2a/J2b/J3/J4/J5/J6/J7/J8/J10A/J10B/J11/SW1/U1**，其中 **J8=告警开关 2P、J11=SPI 触摸屏 8P**，**不存在 J9、J12~J15、U2、LED、按键**。即：
  - BOM v2 的 J11 是"LED 绿 3P"，Gerber 的 J11 是"SPI 屏排针"——**同名不同物**；
  - BOM v2 的 J8 是"USB-A"，Gerber 的 J8 是"告警开关"——**同名不同物**；
  - BOM v2 组装步骤 L126-127「USB 麦克风插 J8」「LED 插 J9~J11，按键插 J12~J15」在 Gerber v1.5 上**无对应位置**。
- **判断：反对**。BOM v2 手册是"未来理想 PCB v2"的设计稿，不是当前 Gerber v1.5 的操作手册。A4 把 BOM v2 当作"当前板子的唯一错误手册"来修，方向对但工作量被低估：修正不是改 3 行引脚，而是要么**把 BOM v2 重写为 Gerber v1.5 的真实手册**（删 J9~J15/U2，补 J10A/J10B 跳线、J11 SPI、J8 告警开关），要么**明确标注 BOM v2 为 PCB v2 设计稿、另写 v1.5 手册**。
- **建议**：A4 的 P2#6"PCB_BOARD_DESIGN.md 仍 v1.0"升级为 **P1**，且修正范围必须覆盖 tracking_servo.md / PCB_BOARD_DESIGN.md / BOM v2 / system_integration.md 四份文档，统一采用 Pin7/Pin6/Pin40 映射。

### 质疑点 5（反对/加强）：v2"脚本已备"言过其实——实测 7 处短路，且 SPI 屏信号完全未布线

- **A4 报告行**：L209-213 §6.4「v2 信号直连（脚本已备，未生成 Gerber）」「未经验证前不要直接打 v2」；L230「若时间允许再出 v2 免跳线版」。
- **我的实测**（复制 `generate_pcb.py` 至临时目录，仅将 `V2=False` 改为 `V2=True` 后运行）：
  ```
  ⚠️ DRC 发现 7 个问题：
  - SHORT: 层T PAN_SIG (85,42)-(40,42) × TILT_SIG (44,46)-(44,22)
  - SHORT: 层T PAN_SIG (85,42)-(40,42) × 5V (66,48)-(66,8)
  - SHORT: 层T TILT_SIG (70,46)-(44,46) × 5V (66,48)-(66,8)
  - SHORT: 层T TILT_SIG (44,22)-(93,22) × 5V (66,48)-(66,8)
  - SHORT: 层T TILT_SIG (44,22)-(93,22) × 5V (66,8)-(66,40)
  - SHORT: 层B TILT_SIG (93,22)-(93,75) × GND (90.08,60)-(95,60)
  - SHORT: 层B TILT_SIG (93,22)-(93,75) × GND (95,40)-(53.18,40)
  ```
  7 处短路全部位于 v2 新增的 PAN/TILT 走线与 5V/GND 干线的交叉区。**v2 不是"未验证"，而是有确定的设计错误，任何一次运行都会 DRC 失败**。
- **另一个事实**：v2 代码（L103-124）只布了 PAN_SIG/TILT_SIG/VOICE_TX/VOICE_RX/SCREEN_TX/SCREEN_RX/SW_ALARM 七根线；**J11 SPI 屏的 SCLK/MOSI/MISO/CS/DC/RST 六根信号在 v2 中一根都没有**。所以即使修好短路，v2 仍然需要为屏幕飞线——"免跳线版"名不副实。
- **判断：反对 §6.4 表述，支持其打样结论**。A4 建议"先打 v1"是对的，但理由应改为：**v2 代码当前有 7 处短路且 SPI 屏走线缺失，属于未完成状态，短期内不具备打样条件**。
- **建议**：若未来要做 v2，需重排 TILT_SIG 走线（避开 5V x=66 竖干与 GND 横干，或改走线层/加过孔换层），并补齐 J11 SPI 六根信号；做完后跑 DRC + 检查 SVG 再导出 Gerber。

---

## 四、对「推荐打样 v1 跳线版」的评估

### 质疑点 6（支持，但需明示 v1 的边界）

- **A4 报告行**：L228-230 §6.6「当前 v1 Gerber 可直接打样」「打样前逐项核对 J11 屏幕排针」「若时间允许再出 v2」。
- **我的验证**：
  - v1（V2=False）在临时目录复跑：**DRC 全部通过**（98 焊盘 / 18 走线 / 12 过孔），Gerber 生成正常。
  - design.json 电源网络正确（U1-40=5V、U1-38=GND），无烧板风险。
  - `嘉立创下单说明.md` 接线清单正确（J3-SIG→Pin7、J4-SIG→Pin6、J6-RX→Pin19/J6-TX→Pin20、J8-1→Pin27）。
- **判断：支持打 v1**，但 A4 的表述需补充三个边界条件：
  1. **v1 不是"即插即用"**：J11 SPI 屏 6 根信号在 v1.5 上全部靠杜邦线飞线到 Pin12/14/15/16 + DC/RST；主显示组件依赖手飞线，焊接/接线工作量不小。
  2. **舵机 PWM 前置条件是软件**：A4 自己列为 P1-4，但 §6.6"可直接打样"的表述容易让人忽略"打样回来还得重编固件使能 PWM8/9"。建议在打样建议中把"固件使能 PWM"与"J11 飞线"列为打样后的两个必做前置项。
  3. **J11 引脚序未核对**（A4 P1#5）：ILI9341 模块排针顺序五花八门，到货前不能完全冻结飞线顺序——这进一步说明 v1 是"电源安全板"，不是"装配即用板"。
- **建议**：§6.6 改为"**v1 可打样，但属于半成品方案：屏幕 6 线需飞线、PWM 需重编固件、J11 引脚序待实测**；若答辩准备期小于 2 周，建议以 USB 外设 + 面包板 + 现有开发板直接演示，PCB 作为加分项"。

---

## 五、A4 遗漏的架构级风险清单

| # | 风险 | A4 是否覆盖 | 说明 |
|---|---|---|---|
| 1 | **BOM v2 与 Gerber v1.5 位号系统性错位**（J8/J9~J15/U2/J11 同名不同物，BOM v2 描述的是另一块板） | ❌ 未发现 | 本次交叉评审最重要的新增发现，见质疑点 4 |
| 2 | **v2 代码存在 7 处 DRC 短路** | ⚠️ 仅说"未经验证" | 实测失败，见质疑点 5 |
| 3 | **v2 未布 J11 SPI 屏信号，"免跳线"不成立** | ⚠️ P1#3 提了"v2 版补 SPI 走线"，但未点明 v2 当前一根 SPI 线都没有 | 见质疑点 5 |
| 4 | **错误源 tracking_servo.md 未被识别** | ❌ 未发现 | 见质疑点 4 |
| 5 | **"无 /dev/ttyS4"与 voice_module.md L91 矛盾** | ⚠️ 只给了实测结论 | 应标注仓库文档冲突，见质疑点 3 |
| 6 | **USB Hub + WiFi + USB 喇叭 + USB 麦克风全部从开发板 USB Host 取电，电流预算未核算**（5V/3A 电源在舵机堵转+WiFi 传输+播报峰值时余量有限） | ❌ 未提 | 建议加一条 P2：答辩前实测整机峰值电流，或给 USB Hub 单独 5V 供电 |
| 7 | go2rtc 端口 8080 vs 1984 | ✅ P1#7 | agent8 P0-2 也覆盖，属交叉印证 |
| 8 | J11 SPI 信号未直连 U1 | ✅ P1#3 | 但未上升到"v1 主显示必须飞线"的边界条件 |
| 9 | DFPlayer 与 USB 语音方案矛盾 | ✅ P0#3/§6.5 P0#2 | 覆盖充分 |

---

## 六、最高价值质疑（Top 5）

1. **【反对】"BOM v2 手册是唯一错误源"不成立**：错误源头是 `docs/hardware/tracking_servo.md`（L103-108/L177），经 PCB_BOARD_DESIGN.md v1.0 传播到 BOM v2；`system_integration.md` 还有第三套错误编号（Pin54/55）。修正范围必须覆盖 4 份文档，否则答辩时仓库内部仍互相打架。
2. **【新发现】BOM v2 描述的是一块与 Gerber v1.5 不同的板子**：BOM v2 的 J8=USB-A、J9~J15=LED/按键、U2=MP1584 在 Gerber v1.5 上不存在；J11 在两边同名不同物（LED vs SPI 屏）。"修正手册"实为"重写手册"级别的工作，不是改 3 行引脚表。
3. **【实测证据】v2 不是"脚本已备"，而是有 7 处短路**：临时目录实跑 `V2=True`，DRC 报 7 个 SHORT（PAN/TILT 与 5V/GND 交叉）。且 v2 未布 J11 SPI 六根信号，"免跳线版"名不副实。这反而**加强**了 A4"先打 v1"的建议。
4. **【支持】三个 P0 分级成立，但证据展示不足**：P0-1/P0-2 有 5 组项目内独立来源印证（design.json、Gerber、下单说明、voice_module、工作日志），结论可靠；但 A4 未附 OCR 原文/pinmux 原始输出，且"无 /dev/ttyS4"与 voice_module.md L91 矛盾，应补充原始证据与系统版本上下文。
5. **【支持】打 v1 跳线版合理，但要明示边界**：v1 DRC 实测通过、电源网络正确、下单说明接线正确；但 v1 的主显示（J11 SPI 屏）依赖 6 根手飞线、PWM 需重编固件使能、J11 引脚序未核对——是"电源安全板"而非"即插即用板"。建议 A4 在打样建议中把这三点列为显式前置条件。

---

## 七、最终结论

- **P0 引脚结论**：PWM8=Pin7 / PWM9=Pin6 / VBUS=Pin40——**支持**，分级合理，证据充分（A4 实机 + 5 组项目内独立来源 + Gerber 数据交叉印证）。
- **Gerber 正确性**：**支持**。design.json U1-40=5V、U1-38=GND，Gerber 文件一致，v1 DRC 实测通过。
- **"唯一错误源"**：**反对**。错误源头在 tracking_servo.md，BOM v2 只是传播者；且 BOM v2 与 Gerber v1.5 存在系统性位号错位。
- **打样 v1**：**支持**，但需补充"屏幕飞线 / PWM 固件 / J11 引脚序"三个前置条件；v2 目前有 7 处短路且无 SPI 走线，不可打样。
- **建议的下一步**：① 把 BOM v2 重写为 Gerber v1.5 的真实手册（或明确标注为 PCB v2 设计稿）；② 同步修正 tracking_servo.md / PCB_BOARD_DESIGN.md / system_integration.md 的引脚；③ 若做 v2，先修短路 + 补 SPI 走线，跑通 DRC 再打样；④ BOM v2 的 go2rtc 端口、软件集成清单随文档重写一并修正。

*报告人：A1 系统架构师 ｜ 日期：2026-10-29 ｜ 方式：只读交叉验证（v1/v2 DRC 复现仅在临时目录进行，未改动任何项目文件）*