# 最终鉴定 F2：PCB 与硬件文档

> 鉴定人：终检员 F2（PCB/硬件文档方向）
> 日期：2026-10-29 · 审查基准 commit：`284de8d`（工作区干净，无未提交改动）
> 方式：**只读审查**，未修改任何项目文件；本报告为唯一落盘产物。
> 权威基准：`G:/毕设论文/02_答辩准备/答辩硬件展示方案_v3_BOM与PCB手册.md`（BOM v3）
> 交叉依据：`PCB_Gerber/design.json`、`PCB_Gerber/generate_pcb.py`、`PCB_Gerber/嘉立创下单说明.md`、
> `docs/hardware/{voice_module,system_integration,tracking_servo,PCB_BOARD_DESIGN,touchscreen,HARDWARE_EXPANSION_PLAN}.md`、
> `docs/review/20261029/` 下 agent4_pcb.md、cross_R09~R16、verdict_R17 等历史评审报告。

---

## 一、BOM v3 引脚映射核对

**检查项**：PWM8_M1=Pin7、PWM9_M1=Pin6、VBUS=Pin40、VSYS=Pin39、GND=Pin38、3V3(OUT)=Pin36 是否与 v3 手册一致。

| 要求值 | BOM v3 手册 | design.json | 判定 |
|---|---|---|---|
| PWM8_M1=Pin7 | §五 信号直连映射表 L176、§4.4 接线清单 L153：「J3-SIG→**Pin7**（PWM8_M1）」 | U1-40 网络 `5V`；U1 排母位 40P 2.54mm | ✅ 一致 |
| PWM9_M1=Pin6 | §五 L177、L153：「J4-SIG→**Pin6**（PWM9_M1）」 | — | ✅ 一致 |
| VBUS=Pin40 | L15「**VBUS=Pin40**」、L41「Pin40=5V」、L187「电源 5V \| VBUS \| **Pin 40**」 | U1-40 net=`5V` | ✅ 一致 |
| VSYS=Pin39 | L15「VSYS=Pin39」、L188「电源 5V 备选 \| VSYS \| **Pin 39**」 | U1-39 焊盘存在（net=`U1_39`，未布线，符合“备选/不推荐”定位） | ✅ 一致 |
| GND=Pin38 | L190「GND \| Pin 3/8/13/18/23/28/33/38」、L41「Pin38=GND」 | U1-38 net=`GND` | ✅ 一致 |
| 3V3(OUT)=Pin36 | L15「3V3=Pin36」、L189「3.3V \| 3V3(OUT) \| **Pin 36**」 | U1-36 焊盘存在（net=`U1_36`，逻辑电平参考/跳线用） | ✅ 一致 |

**结论：【通过】。** 六项引脚值在 BOM v3 的 §五“唯一事实源”表、§4.4 接线清单与 design.json 中全部一致，无 P0-1 类残留错误。

---

## 二、BOM v3 位号表与 PCB_Gerber design.json 一致性

**位号逐项核对**（BOM v3 §2.1 位号表 vs design.json/generate_pcb.py 实际焊盘）：

| 位号 | BOM v3 描述 | design.json / Gerber 实际 | 判定 |
|---|---|---|---|
| U1 | 40pin 排母 2.54mm | U1-1~U1-40，2.54mm 间距，U1-40=5V、U1-38=GND | ✅ |
| J1 | 电源端子 2P 5.08mm（VIN/GND） | J1-1 VIN / J1-2 GND，5.08mm 间距 | ✅ |
| SW1 | 自锁开关 2P（VIN→5V） | SW1-1 VIN / SW1-2 5V，4.5mm 间距（开关封装） | ✅ |
| J2a / J2b | 2P 排针 1×2P（5V/GND、5V_SERVO/GND） | J2a-1 5V / J2a-2 GND；J2b-1 5V_SERVO / J2b-2 GND | ✅ 位号一致（间距问题见下） |
| J3 / J4 | 舵机三针排针 | J3-SIG/VCC/GND、J4-SIG/VCC/GND，2.54mm | ✅ |
| J5 | 语音预留 6P | J5 VCC/GND/TX/RX/BUSY/NC，2.54mm | ✅ |
| J6 | 串口屏预留 4P | J6 VCC/GND/RX/TX，2.54mm | ✅ |
| J7 | 喇叭端子 2P 5.08mm | J7-1 SPK_P / J7-2 SPK_N，5.08mm | ✅ |
| J8 | 告警开关排针 2P | J8-1 SW_ALARM / J8-2 GND（4.5mm 间距，与 SW1 同封装） | ✅ 位号一致（间距问题见下） |
| J10A / J10B | 跳线排针 8P | J10A-{PAN/TILT/VTX/VRX/STX/SRX/ALM/BUSY}、J10B 对应 U1 侧 8 焊盘 | ✅ 网络名全对 |
| J11 | ILI9341 SPI 触摸屏 8P | J11-5V/GND/SPI_SCLK/SPI_MOSI/SPI_MISO/SPI_CS/SPI_DC/SPI_RST，2.54mm | ✅ 网络名全对 |
| C1 / C2 / R1 | 1000µF 电解 / 0.1µF 0805 / 10kΩ 0805 | C1+/C1-、C2-1/C2-2、R1-1/R1-2 | ✅ |
| LED / 按键 | 无位号（经 J10A 跳线或直连开发板 GPIO） | Gerber 无对应幻影焊盘 | ✅ 与 v3 一致 |
| 云台 / USB | 无位号（云台经 J3/J4；USB 喇叭/Hub 直插开发板 USB） | Gerber 无对应幻影焊盘 | ✅ 与 v3 一致 |

位号本身**全部一致**（P0-2 修复到位）。但本次终检发现 **2 个新不一致**：

### 发现 A（P1 级）：BOM v3 的 J11 SPI 引脚映射与 Gerber 包内文档/实机 pinmux 矛盾

- BOM v3 §四 接线清单 L154 与 §五 L183~186：
  - `SPI_SCLK→Pin14`、**`MOSI→Pin12`**、**`MISO→Pin15`**、**`CS→Pin16`**
- 但 Gerber 打样包自带的 **`PCB_Gerber/嘉立创下单说明.md`** L70~73（v3 手册自称的来源）：
  - `SPI_SCLK=Pin14`、**`SPI_MOSI=Pin15`**、**`SPI_MISO=Pin16`**、**`SPI_CS=Pin12/CS0`**
- **实机 pinmux（agent4_pcb.md L95，已实机确认 `ff500000.spi`）**：
  - `SPI0_CS0_M0=Pin12`、`SPI0_CLK_M0=Pin14`、`SPI0_MOSI_M0=Pin15`、`SPI0_MISO_M0=Pin16`

即 v3 手册把 **MOSI/MISO/CS 三路整体旋转**（MOSI↔CS、MISO↔MOSI、CS↔MISO）。若按 v3 接线，
模块 MOSI 会接到开发板 CS0、模块 CS 会接到 MISO、且开发板 MOSI（输出）会与模块 MISO（输出）
形成潜在 GPIO 冲突——**触摸屏将无法工作**。`system_integration.md` v2 的 L68/L119~122 也带着同一个错误，
且 v3 手册 §五自称“唯一事实源，评审已双重验证”，与其引用来源自相矛盾。此为**本次终检新发现**，
历史评审（cross_R09~R16、verdict_R17）均未捕获。

### 发现 B（P2 级）：BOM 第 12 项“2.54mm 排针”与 Gerber 焊盘间距不符

- BOM v3 L100 第 12 项按 **2.54mm 排针** 采购：`8P×2（J10A/J10B）、2P×4（J1/J2a/J2b/J7/J8）、开关`
- 但 generate_pcb.py/design.json 实际焊盘间距：
  - **J2a/J2b**：x=85.0 → 90.08，**5.08mm 间距**（与 J1/J7 端子同封装）
  - **J10A/J10B**：x=50.0 → 85.56（步进 5.08），**5.08mm 间距**
  - **J8**：x=74.0 → 78.5，**4.5mm 间距**（与 SW1 同开关封装，非 2.54mm）
- 影响：按 BOM 买 2.54mm 排针，J2a/J2b/J10A/J10B/J8 **插不进 PCB**。J1/J7 本身是 5.08mm 端子，也不属于 2.54mm 排针。此问题此前评审未覆盖。

**结论：【需注意】。** 位号与网络名整体一致（通过），但存在 J11 SPI 映射错误（P1）与排针间距采购不符（P2），需在打样/接线前修正。

---

## 三、语音方案一致性（统一 USB 喇叭，无 DFPlayer 兜底）

| 文档 | 表述 | 判定 |
|---|---|---|
| `docs/hardware/voice_module.md`（v2） | 标题即“统一 USB 喇叭”；L5「删除 DFPlayer 兜底方案」；L16~19 废弃 UART4；L31/33 唯一推荐方案 E（USB 喇叭）；L289「不再购买 DFPlayer、独立 TF 卡」 | ✅ |
| `docs/hardware/system_integration.md`（v2） | L232「USB 喇叭 + 系统 TF 卡软件播放，**无串口、无 DFPlayer/JQ8900**」；L18 变更说明；附录 A 无任何 DFPlayer 命令 | ✅ |
| BOM v3 | L48「USB 语音方案不使用」J5；L64/65 删除 DFPlayer 位号；L94 只买 USB 喇叭；L110~111「明确不买 DFPlayer」；L192「不得再以 UART4 接语音模块」 | ✅ |

三处完全一致，P0-3 修复无回退。**结论：【通过】。**

---

## 四、BOM v3 新增第 16 项 FFC 加长（40cm 24P 0.5mm）合理性

- **长度合理性**：✅ 合理。cross_R11 L100 已实测评估：SC3336 原装 FFC 通常 15~20cm，而云台顶部（约 z=190）经走线腔到开发板 MIPI 座（z≈16）的路径约 **30~40cm**，40cm 恰好覆盖且留有余量；BOM 同时注明“购买前先量原装排线方向（同向/反向）再买”，正确处理了 cross_R11 指出的 **MIPI FFC 方向性**（非同向不可混用）问题。
- **需注意 1**：`docs/hardware/HARDWARE_EXPANSION_PLAN.md` L28/L77 仍写 **“15pin 0.5mm 反向，20~30cm”**，与 v3 BOM（24P 0.5mm、40cm、同向）冲突；该旧文档还残留 MP1584/USB-A 母座等已被 P0-2 删除的幻影物料，属于未同步的过时文档。
- **需注意 2**：MIPI CSI-2 走 40cm 0.5mm FFC 属信号完整性高风险场景（通常建议 ≤20cm），云台转动时排线反复弯折会进一步劣化；本项目为 1080p 演示尚可接受，但建议**尽早实物验证**，并保留原装短线作为应急兜底。

**结论：【需注意】。** FFC 加长方向正确、长度合理、防呆说明到位；但存在旧文档规格冲突与 MIPI 长线 SI 风险。

---

## 五、tracking_servo.md 4.3 节 FOV 校验公式与数值自洽性

- **垂直半角 29.6°**：由 SC3336 标称 **98.3° 对角线 FOV**（16:9，1920×1080）推导，实测计算 `垂直半角 = 29.55° ≈ 29.6°`，垂直 FOV ≈ 59.1°≈59.2° —— **数值自洽** ✅
- **tan(69.6°) ≈ 2.68**：`69.6° = 29.6°（垂直半角）+ 30°（tilt 限幅）+ 10°（工程余量）`；实际 `tan(69.6°) = 2.6889 ≈ 2.68` —— **数值自洽** ✅
- **几何公式**：`h/d ≤ tan(α_max + θ_v/2 + 10°)`，方向正确（下边界视线角需 ≥ 目标点视线角，`≤` 号正确；L263~264 已显式更正早期 `≥` 笔误）✅
- **示例核算**：h=1.0m/d=0.5m → 2.0≤2.68 ✅；h=1.0m/d=0.4m → 2.5≤2.68 ✅；h=1.5m/d=0.5m → 3.0>2.68 ❌（文档正确判定为不覆盖）✅

**结论：【通过】。** 公式与数值完全自洽，可直接用于答辩讲解。

> 附带发现（P2 级，不属本项判分）：tracking_servo.md §2.3 表中 PWM10_M1=“第 2 脚”、PWM11_IR_M1=“第 1 脚”，
> 与 agent4_pcb 实机 pinmux（**PWM10_M1=Pin5、PWM11_IR_M1=Pin4**）不一致；同节 L109 的“PWM10_M1（第5脚）”反而正确，
> 造成表内自相矛盾（疑为旧的“Pico 风格 GP0~GP3=第1/2脚”映射残留）。默认方案（PWM8/PWM9）不受影响，建议顺手订正。

---

## 六、PCB_BOARD_DESIGN.md 旧 PWM 引脚错误检查

- L52：`PWM_PAN（Yaw）| GPIO1_C4_d（PWM8_M1，第7脚）| J3 SIG` ✅
- L53：`PWM_TILT（Pitch）| GPIO1_C5_d（PWM9_M1，第6脚）| J4 SIG` ✅
- 全文无正向的“PWM8/PWM9=Pin5/Pin4”旧错误；涉及 Pin5/Pin4 的表述均为“Pin5/Pin4 应为 PWM10/PWM11”的否定/纠正语境（符合任务给定豁免条件），不算错误。

**结论：【通过】。** P0-1 修复在 PCB_BOARD_DESIGN.md 中无回退。

---

## 总体结论

**4 项通过（第 1、3、5、6 项），2 项需注意（第 2、4 项）。**

P0 级问题（舵机引脚、位号错位、语音方案矛盾）经 commit `284de8d` 已全部修复到位，PCB Gerber 与 BOM v3 的位号/网络名整体一致，FFC 加长与 FOV 校验科学合理，**可以进入打样与采购**。但终检在“唯一事实源”文档中发现 **1 个 P1 级新问题（J11 SPI 引脚映射三路旋转）** 和 **3 个 P2 级不一致**，建议在接线/采购前修复，否则触摸屏无法点亮且可能发生 GPIO 冲突、排针买错插不上。

## 剩余风险清单

| 级别 | 风险 | 影响 | 建议 |
|---|---|---|---|
| **P1** | BOM v3 §四/§五与 system_integration.md v2 的 J11 SPI 映射错误：MOSI→Pin12、MISO→Pin15、CS→Pin16（应分别为 Pin15/Pin16/Pin12） | 触摸屏无法工作；开发板 MOSI（输出）与模块 MISO（输出）可能冲突 | 以 `嘉立创下单说明.md` + agent4_pcb 实机 pinmux 为准，将两处文档改为 **SPI_SCLK→Pin14、MOSI→Pin15、MISO→Pin16、CS→Pin12**；修改后重新交叉验证 |
| **P2** | BOM 第 12 项排针规格与 Gerber 焊盘间距不符：J2a/J2b、J10A/J10B 为 5.08mm 间距，J8 为 4.5mm 间距（BOM 按 2.54mm 采购） | 排针买错，J2a/J2b/J10A/J10B/J8 插不上 | 方案一：BOM 改买 **5.08mm 间距 2P/8P 排针**与 **4.5mm 间距 2P 开关**；方案二：修改 generate_pcb.py 将上述封装改为 2.54mm 后重新生成 Gerber/design.json（更推荐，与“跳线区”定位一致） |
| **P2** | `HARDWARE_EXPANSION_PLAN.md` 与 v3 BOM 冲突（FFC 15P/反向/20~30cm vs 24P/同向/40cm；MP1584/USB-A 等幻影物料） | 若按旧文档采购会买错 FFC | 将该文档标记为“已废弃/仅历史参考”，或同步为 v3 规格 |
| **P2** | tracking_servo.md §2.3 PWM 表 PWM10/PWM11 物理脚为“第 2/1 脚”，与实机 Pin5/Pin4 矛盾 | 备用通道接线时混淆 | 按 agent4_pcb 实机将表内物理脚改为第 5/4 脚，或在表中注明“以实机 pinmux 为准” |
| **P3** | 40cm MIPI FFC 信号完整性（长线 + 云台弯折） | 高分辨率下可能花屏/闪断 | 尽早用实物验证；排线远离舵机 PWM/5V 走线；留原装短线兜底；必要时降分辨率/帧率 |