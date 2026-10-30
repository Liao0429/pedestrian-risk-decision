# A4 PCB 硬件专家审查报告

> 审查对象：`docs/hardware/PCB_BOARD_DESIGN.md`（v1.0）与 `G:/毕设论文/02_答辩准备/答辩硬件展示方案_v2_BOM与PCB手册.md`（BOM v2）
> 审查方式：官方资料查证（Luckfox 官方 40pin 引脚图）+ **开发板实机验证**（SSH 到运行中的 Luckfox Pico Max，读取内核 pinmux/GPIO/设备树）
> 结论：**存在 3 个 P0 级引脚映射错误**（照 BOM v2 打样接线会导致舵机不工作、5V 接到 GPIO 有烧板风险、DFPlayer 语音与舵机 PWM 物理冲突）。**打样前必须按本报告修正后再下单。**

---

## 一、官方 40pin 引脚定义（本次审查实测确认）

数据来源：① Luckfox 官方 Pico Pro/Max 40-pin 引脚图（逐区域 OCR 识别）；② 开发板实机 `pinmux-pins` / `gpio` / 设备树交叉验证（板子当前固件 5.10.160，`/proc/device-tree/model = Luckfox Pico Max`）。两处完全吻合。

| Pin | GPIO | 复用功能（官方图） | 实机 pinmux 状态 | 备注 |
|---|---|---|---|---|
| 1 | GPIO1_B2_d | UART2_TX_M1 | UNCLAIMED | 可作 GPIO |
| 2 | GPIO1_B3_u | UART2_RX_M1 | UNCLAIMED | 可作 GPIO |
| 3 | GND | — | — | |
| 4 | GPIO1_C7_d | **PWM11_IR_M1** / UART4_CTS_M1 | UNCLAIMED | PWM 控制器未使能 |
| 5 | GPIO1_C6_d | **PWM10_M1** / UART4_RTS_M1 | UNCLAIMED | PWM 控制器未使能 |
| 6 | GPIO1_C5_d | **PWM9_M1** / **UART4_TX_M1** | UNCLAIMED | ⚠️ PWM 与 UART4 同脚 |
| 7 | GPIO1_C4_d | **PWM8_M1** / **UART4_RX_M1** | UNCLAIMED | ⚠️ PWM 与 UART4 同脚 |
| 8 | GND | — | — | |
| 9 | GPIO1_D2_d | PWM0_M1 / I2C3_SDA_M1 / SPI0_CS1_M0 | **i2c3m1-xfer** | 已被 I2C3 占用 |
| 10 | GPIO1_D3_d | PWM11_IR_M2 / I2C3_SCL_M1 / SPI0_CS0_M0 | **i2c3m1-xfer** | 已被 I2C3 占用 |
| 11 | GPIO2_B1_d | PWM6_M1 / I2C1_SDA_M1 / UART0_TX_M1 | UNCLAIMED | 可作 GPIO |
| 12 | GPIO1_C0_d | PWM2_M2 / **SPI0_CS0_M0** | **spi0m0-cs0** | 屏幕 CS 候选 |
| 13 | GND | — | — | |
| 14 | GPIO1_C1_d | PWM4_M2 / **SPI0_CLK_M0** | **spi0m0-clk** | 屏幕 SCLK |
| 15 | GPIO1_C2_d | PWM5_M2 / **SPI0_MOSI_M0** | **spi0m0-mosi** | 屏幕 MOSI |
| 16 | GPIO1_C3_d | PWM6_M2 / **SPI0_MISO_M0** | **spi0m0-miso** | 触摸数据回读 |
| 17 | GPIO2_B0_d | PWM5_M1 / I2C1_SCL_M1 / UART0_RX_M1 | UNCLAIMED | 可作 GPIO（DC） |
| 18 | GND | — | — | |
| 19 | GPIO1_D0_d | PWM3_IR_M2 / UART3_TX_M1 | UNCLAIMED | 可作 GPIO（RST） |
| 20 | GPIO1_D1_d | PWM10_M2 / UART3_RX_M1 | UNCLAIMED | 可作 GPIO（TP_CS） |
| 21 | GPIO2_A4_d | UART1_TX_M1 | UNCLAIMED | 可作 GPIO |
| 22 | GPIO2_A5_d | UART1_RX_M1 | UNCLAIMED | 可作 GPIO |
| 23 | GND | — | — | |
| 24 | GPIO2_A0_d | UART1_CTS_M1 / I2C4_SDA_M0 | UNCLAIMED | 可作 GPIO |
| 25 | GPIO2_A1_d | UART1_RTS_M1 / I2C4_SCL_M0 | UNCLAIMED | 可作 GPIO |
| 26 | GPIO2_A2_d | — | UNCLAIMED | 可作 GPIO |
| 27 | GPIO2_A3_d | — | UNCLAIMED | 可作 GPIO |
| 28 | GND | — | — | |
| 29 | GPIO2_A6_d | UART0_RTS_M1 / I2C3_SCL_M0 | UNCLAIMED | 可作 GPIO |
| 30 | **RESET** | — | — | ⚠️ 复位脚，不可作 GPIO |
| 31 | GPIO4_C0_z | SARADC_IN0 | — | ADC，1.8V 域 |
| 32 | GPIO4_C1_z | SARADC_IN1 | — | ADC，1.8V 域 |
| 33 | GND | — | — | |
| 34 | GPIO2_A7_d | UART0_CTS_M1 / I2C3_SDA_M0 | UNCLAIMED | 可作 GPIO |
| 35 | **NC** | — | — | 未连接，不可用 |
| 36 | **3V3(OUT)** | — | — | 3.3V 输出 |
| 37 | **3V3_EN** | — | — | 3.3V 使能控制脚 |
| 38 | GND | — | — | |
| 39 | **VSYS** | — | — | 系统电源输入 |
| 40 | **VBUS** | — | — | ⚠️ **5V 输入脚，不是 Pin20** |

**核心结论（与 BOM v2 / PCB v1.0 直接冲突的 3 项）：**
- `PWM8_M1 = Pin7`，不是 Pin5；`PWM9_M1 = Pin6`，不是 Pin4。
- `VBUS = Pin40`，不是 Pin20。
- `UART4_TX_M1 = Pin6`、`UART4_RX_M1 = Pin7`——与 `PWM9/PWM8` **完全同脚复用**，舵机 PWM 与 UART4 串口语音**物理上不可共存**。

---

## 二、逐项核对结果

### a) 接口选型：串口屏 → SPI 触摸屏（已确认）

PCB v1.0 全篇基于「串口屏 USART HMI」（J6 四针：VCC/GND/RX/TX，行 29/57-58）设计，但最终形态已是 **2.8 寸 ILI9341 SPI 触摸屏（XPT2046）**（BOM v2 行 97-98）。v1.0 与最终硬件不匹配，**PCB 文档必须升级 v2**。J6 应改为 SPI 排针（≥8P）。

### b) 语音：USB 喇叭为主，DFPlayer 兜底方案必须删除

- 板载声卡为 `rv-acodec`（`/proc/asound/cards` 实测存在，C0D0p/C0D0c），USB 喇叭插入后作为 UAC 声卡（需 `aplay -l` 实测确认设备号，不一定是 `plughw:1,0`）。
- BOM v2 行 99 的「语音 UART：开发板 UART4（/dev/ttyS4）→ J5 TX/RX」**在当前硬件形态下不可行**：
  1. 当前固件**未使能 uart4**——开发板实测 `/dev/` 下**没有 `/dev/ttyS4`**（只有 `/dev/ttyFIQ0` 调试串口）。
  2. 即使重新编译固件使能 UART4，其 TX/RX 位于 Pin6/Pin7，与舵机 `PWM9_M1/PWM8_M1` 完全冲突。
- **建议**：删除 J5（DFPlayer 排针）与 J7（喇叭端子），语音统一走 USB 喇叭 + USB 麦克风（USB Hub 扩展）。DFPlayer 若想保留为可选扩展，应改成 I2C 或单独 GPIO 串口（如 UART3 Pin19/20，但与屏幕 RST/TP_CS 冲突），**不建议在 v2 底板保留**。

### c) 电源：VBUS 引脚错误（P0）

- BOM v2 行 91：`VBUS_5V = J1(+5V) → U1 排母 VBUS 脚（Pin20）→ U2 输入`。
- 实测 **Pin20 = GPIO1_D1_d（UART3_RX_M1 / PWM10_M2）**，是普通 GPIO；**VBUS 在 Pin40**。
- 按 BOM v2 的「Pin20」布线，5V 将直接加到 GPIO1_D1，**有烧坏开发板 IO 的实打风险**。
- 修正：`J1(+5V) → U1 排母 Pin40（VBUS）→ U2 输入`。U1-40 焊盘在排 2（蛇形 40pin 的最后一脚），PCB 画图时注意编号方向。

### d) 舵机 PWM：引脚映射错误（P0）+ 固件未使能 PWM（P1）

- BOM v2 行 95/96：`PWM_PAN = Pin5（PWM8_M1）`、`PWM_TILT = Pin4（PWM9_M1）`。
- 实测 **Pin5 = PWM10_M1、Pin4 = PWM11_IR_M1**；正确映射：**PWM_PAN = Pin7（PWM8_M1）、PWM_TILT = Pin6（PWM9_M1）**。
- 按 BOM 接线，舵机接到 PWM10/PWM11 通道，且 PWM 控制器默认未使能，舵机完全不动。
- **额外前置条件**：实机设备树中 12 个 PWM 控制器节点**全部 `status=disabled`**（`/proc/device-tree/pwm@*` 实测）。要用舵机 PWM，必须修改 Luckfox SDK 设备树（使能对应 `pwm` 节点 + 配置 `pinctrl` 为 `pwm8m1`/`pwm9m1`）并重新编译烧录固件。**这是软件侧前置工作，文档需写明。**

### e) 屏幕与触摸：引脚未定义（P1）

- BOM v2 行 97/98 只写「SPI0（MOSI/MISO/SCLK/CS/DC/RST/BL）→ J6 对应脚」，没有给具体引脚号，无法画原理图。
- 依据实测给出**推荐映射**（见下节），其中注意：
  - `SPI0_CS0_M0 = Pin12`、`SPI0_CLK_M0 = Pin14`、`SPI0_MOSI_M0 = Pin15`、`SPI0_MISO_M0 = Pin16`（实机 pinmux 已确认 `ff500000.spi`）。
  - **Pin9/Pin10 已被 I2C3（`i2c3m1-xfer`）占用**，不能用作 SPI CS1 或 DC。
  - XPT2046 需要**独立的 CS**（不能与 LCD_CS 共用 Pin12），建议用 GPIO 模拟：TP_CS = Pin20。

### f) 软件集成清单：与代码现状脱节（P1）

- BOM v2 行 136-139 把 `servo.h/.cc`、`led.h/.cc`、`button.h/.cc`、`lvgl_menu/` 标为「需实现」，但 `src/hardware/` 下 LED/按键/语音告警**已实现**，`servo.cc`、`lvgl_menu/` 实际**不存在**。文档引用不存在的文件会误导答辩准备。
- BOM v2 行 143 提到 RTSP 端口相关（8080）——实际推流端口为 1984（go2rtc），需统一。

---

## 三、问题清单

### P0（必须修复后才能打样/接线）

| # | 位置 | 问题 | 影响 | 修复建议 |
|---|---|---|---|---|
| 1 | BOM v2 行 95/96；PCB v1.0 行 53/54 | PWM_PAN/PWM_TILT 引脚写错：Pin5/Pin4 实际是 PWM10_M1/PWM11_IR_M1，不是 PWM8_M1/PWM9_M1 | 舵机接错通道，云台不工作；若误初始化 PWM10/11 还可能误驱动 | 改为 PWM_PAN=Pin7（GPIO1_C4_d/PWM8_M1）、PWM_TILT=Pin6（GPIO1_C5_d/PWM9_M1） |
| 2 | BOM v2 行 91 | VBUS 写成 Pin20（实际 Pin40；Pin20=GPIO1_D1_d） | 5V 直通普通 GPIO，有烧板风险；开发板无法从底板取电 | J1(+5V) → U1 排母 Pin40（VBUS）→ U2 输入；画图时核对 U1-40 焊盘在排 2 |
| 3 | BOM v2 行 99、126；PCB v1.0 J5/J7 | DFPlayer UART4 语音方案与舵机 PWM8/PWM9 同脚冲突（UART4_TX=Pin6、RX=Pin7），且当前固件无 /dev/ttyS4 | 语音与云台不可共存；按 v1.0 打样的 J5/J7 无法使用 | 删除 J5/J7，语音统一 USB 喇叭+USB 麦克风（走 USB Hub）；文档同步删掉 DFPlayer 相关内容 |

### P1（不修会导致现场演示失败或返工）

| # | 位置 | 问题 | 影响 | 修复建议 |
|---|---|---|---|---|
| 4 | BOM v2 全局（舵机） | 固件设备树 12 个 PWM 节点全部 disabled，未使能 pwm8/pwm9 的 pinctrl | 即使引脚接对，舵机仍无 PWM 输出 | 在 Luckfox SDK 设备树使能 PWM8/PWM9 节点与 pinctrl，重新编译烧录；文档补充该步骤 |
| 5 | BOM v2 行 97-98 | 屏幕 SPI 具体引脚未定义（DC/RST/BL/TP_CS 未指定） | 无法画原理图/排针定义 | 按下节推荐映射固定 J6 引脚定义（8P 排针：VCC/GND/SCLK/MOSI/MISO/LCD_CS/LCD_DC/LCD_RST/BL/TP_CS） |
| 6 | BOM v2 行 140 | `aplay -D plughw:1,0` 硬编码声卡号，但板载 rv-acodec 为 card0，USB 喇叭插入后编号需实测 | 语音播报可能失败（设备号不对） | 启动脚本用 `aplay -l` 动态探测 USB 声卡；文档注明 |
| 7 | BOM v2 行 136-143 | 软件集成清单引用不存在的 `servo.h/.cc`、`lvgl_menu/`，已实现模块标为「需实现」；端口 8080 与 1984 不一致 | 文档与代码脱节，误导答辩准备 | 按 `src/hardware/` 实际代码重写该表；端口统一 1984 |
| 8 | PCB v1.0 全文 | v1.0 基于串口屏+DFPlayer 旧方案，与最终硬件形态（SPI 屏+USB 语音）不符 | 按 v1.0 打样的板子无法使用 ILI9341 屏 | PCB_BOARD_DESIGN.md 升级 v2：替换 J6 为 SPI 屏排针、删除 J5/J7、修正引脚映射 |

### P2（建议优化）

| # | 位置 | 问题 | 建议 |
|---|---|---|---|
| 9 | BOM v2 行 98 | XPT2046 触摸与屏幕共用 SPI 总线，CS 必须独立 | TP_CS 用 Pin20（GPIO1_D1_d）GPIO 模拟；IRQ 可选接 Pin34 或轮询 |
| 10 | BOM v2 行 103-108 | 40pin 排母 U1-40 在排 2（蛇形），PCB 布局/丝印易错 | 丝印标注 U1-39/40 位置，DRC 时核对网络名 |
| 11 | BOM v2 行 92 | 舵机 5V 独立轨用 C1 1000μF 合理 | C1 尽量靠近 J3/J4；若现场舵机抖动，在舵机端并联 100μF |
| 12 | BOM v2 行 100-101 | LED/按键 GPIO 未指定 | 建议 LED_R/G/B=Pin21/22/24（GPIO2_A4/A5/A0），按键 SW1~4=Pin25/26/27/29（GPIO2_A1/A2/A3/A6），避开 I2C3/SPI0/UART4 冲突脚 |
| 13 | 开发板供电 | 底板 5V 经 Pin40 VBUS 供开发板时，USB-C 与排针同时供电的电流路径 | 上电顺序：先接底板电源再插 USB-C（或二选一）；文档注明 |

---

## 四、修正后的 PCB 引脚映射（BOM v2 升级直接用）

> 依据官方引脚图 + 开发板实机 pinmux 双重确认。打样前按此表接线。

### 电源网络

| 网络 | 连接 | 修正 |
|---|---|---|
| VBUS_5V | J1(+5V) → **U1-40（VBUS）** → U2 输入 | ✅ 原 Pin20 改为 Pin40 |
| 5V_SERVO | J1(+5V) → J3/J4 VCC（经 C1 滤波） | 不变 |
| 3V3_LOGIC | U2 输出 → 屏幕逻辑/按键上拉/LED 正极 | 不变；可用 Pin36 3V3(OUT) 作参考 |
| GND | J1 GND → U1-38/33/28/23/18/13/8/3（任选多个） | 不变（注意 U1-38 在排 2） |

### 信号网络

| 网络 | 开发板引脚 | 说明 | 修正 |
|---|---|---|---|
| PWM_PAN（Yaw） | **Pin7** = GPIO1_C4_d（PWM8_M1） | J3 SIG | ✅ 原 Pin5 改 Pin7 |
| PWM_TILT（Pitch） | **Pin6** = GPIO1_C5_d（PWM9_M1） | J4 SIG | ✅ 原 Pin4 改 Pin6 |
| LCD_SCLK | Pin14 = GPIO1_C1_d（SPI0_CLK_M0） | J6 | 固定 |
| LCD_MOSI | Pin15 = GPIO1_C2_d（SPI0_MOSI_M0） | J6 | 固定 |
| LCD_MISO | Pin16 = GPIO1_C3_d（SPI0_MISO_M0） | J6，XPT2046 回读共用 | 固定 |
| LCD_CS | Pin12 = GPIO1_C0_d（SPI0_CS0_M0） | J6 | 固定 |
| LCD_DC | Pin17 = GPIO2_B0_d | J6 | 新定义 |
| LCD_RST | Pin19 = GPIO1_D0_d | J6 | 新定义 |
| LCD_BL | Pin11 = GPIO2_B1_d | J6 | 新定义 |
| TP_CS | Pin20 = GPIO1_D1_d（GPIO 模拟 CS） | J6 | 新定义（独立 CS） |
| LED_R/G/B | Pin21/22/24 = GPIO2_A4/A5/A0 | J9~J11 | 新定义 |
| 按键 SW1~4 | Pin25/26/27/29 = GPIO2_A1/A2/A3/A6 | J12~J15 | 新定义 |
| 语音 | USB 喇叭 + USB 麦克风（USB Hub） | 不占排针 | ✅ 删除 J5/J7 |

---

## 五、给总负责人的最高价值发现（Top 5）

1. **[P0] 舵机引脚整体错位**：BOM v2 的 PWM_PAN=Pin5/PWM_TILT=Pin4 是错的，PWM8_M1 在 **Pin7**、PWM9_M1 在 **Pin6**。照旧接线云台必不工作。
2. **[P0] VBUS 不是 Pin20 而是 Pin40**：按 BOM 把 5V 接到 Pin20（GPIO1_D1）有烧板风险。
3. **[P0] DFPlayer/UART4 方案必须废弃**：UART4_TX/RX 与 PWM9/PWM8 同脚（Pin6/Pin7），且当前固件没有 /dev/ttyS4；语音只能走 USB。
4. **[P1] 固件 PWM 全部 disabled**：12 个 PWM 控制器节点 status=disabled，必须改 Luckfox SDK 设备树并重新烧录，舵机才有 PWM 输出。
5. **[P1] 屏幕 J6 引脚未定义**：已给出完整可用的 8 脚 SPI 屏映射（SCLK=14/MOSI=15/MISO=16/LCD_CS=12/DC=17/RST=19/BL=11/TP_CS=20），可直接进 BOM v2 和 PCB v2。

---
*报告人：A4 PCB 硬件专家　|　日期：2026-10-29　|　证据：Luckfox 官方 40pin 引脚图 OCR + 开发板实机 pinmux/GPIO/设备树核对*

---

## 六、实际 Gerber（v1.5）核对 —— 补充审查（2026-10-29 晚）

> 在 R4 初稿后，进一步核对了 `G:/毕设论文/02_答辩准备/PCB_Gerber/` 下的**实际可打样文件**（generate_pcb.py + design.json + 7 个 Gerber + 下单说明）。结论：**实际 Gerber 已正确，BOM v2 手册是唯一错误源，必须修正手册。**

### 6.1 实际文件清单（已存在，非待办）

| 文件 | 内容 | 状态 |
|---|---|---|
| `generate_pcb.py` | Gerber 生成脚本（`V2 = False`，当前输出 v1 跳线版） | 可运行 |
| `design.json` | 设计数据：98 焊盘 / 18 走线 / 12 过孔 | 电源网络完整 |
| `PedestrianRisk_F.Cu.gtl` 等 7 文件 | 完整 Gerber 打样包（顶层/底层/阻焊/丝印/板框/钻孔） | 已通过软件 DRC |
| `嘉立创下单说明.md` | 下单步骤 + 焊接接线清单 + v2 映射表 | 信息准确 |

### 6.2 v1.5 电源直连（正确 ✅）

- design.json 确认：**U1-40 → 5V**（VBUS），**U1-38 → GND**。开发板插入排母即供电，无需跳线。
- 走线 `5V: (66,8)→(66,40)→(58.26,40)→(58.26,36.54)` 直达 U1 Pin40；`GND: 右竖干→(53.18,36.54)` 直达 U1 Pin38。
- **此版无烧板风险**——与官方引脚图完全一致。

### 6.3 信号连接：v1 跳线版（设计选择，非缺陷）

- 当前可打样版为 **v1 电源底板 + 信号跳线**：外设信号从 J3/J4/J5/J6/J8 引出到 J10A 排针，开发板对应引脚经杜邦线接到 J10B（`U1_PAN_SIG` 等网络焊盘）。
- 下单说明 §焊接顺序/接线清单已给出正确引脚：**舵机 J3-SIG→Pin7、J4-SIG→Pin6；语音 J5-RX→Pin21/J5-TX→Pin22；串口屏 J6-RX→Pin19/J6-TX→Pin20；告警 J8-1→Pin27**。全部与官方定义一致 ✅
- 这是**刻意设计**（电源绝对安全 + 信号灵活可调），适合第一次打样。

### 6.4 v2 信号直连（脚本已备，未生成 Gerber）

- `generate_pcb.py` L103-124 已内置 v2 信号直连走线（`V2=True` 时启用）：PAN_SIG→Pin7、TILT_SIG→Pin6、VOICE_TX→Pin21、VOICE_RX→Pin22、SCREEN_TX→Pin19、SCREEN_RX→Pin20、SW_ALARM→Pin27。
- **但 v2 走线从未运行过**：design.json/Gerber 仍是 v1（`V2=False` 产物）。
- 若想免跳线打样：运行 `python generate_pcb.py`（改 `V2=True`）→ 软件 DRC → 检查 SVG → 重新导出 Gerber。**未经验证前不要直接打 v2。**

### 6.5 遗留问题（修正后的真实 P0/P1 清单）

| # | 严重度 | 问题 | 位置 | 修复 |
|---|---|---|---|---|
| 1 | **P0** | BOM v2 手册正文引脚表仍写 `PWM8=Pin5/PWM9=Pin4/VBUS=Pin20`，照手册接线必错 | `答辩硬件展示方案_v2_BOM与PCB手册.md` | 按 §6.3 映射修正手册（同步 §3.2 表格与接线清单） |
| 2 | **P0** | BOM v2 手册出现 **DFPlayer Mini**（UART 语音模块），而实际方案是 USB 麦克风+喇叭；J5/J6 串口外设排针与现方案矛盾 | BOM v2 + PCB v1.5 | 二选一：要么补 DFPlayer 模块（J5 有用），要么删除 J5/J6 相关描述改 USB Hub |
| 3 | P1 | J11 SPI 触摸屏排针信号未直连 U1（v1 版需飞线到 SPI0：Pin12/14/15/16 + DC/RST 任意 GPIO） | PCB v1.5 | v2 版补 SPI 走线；v1 版下单说明已写明跳线 |
| 4 | P1 | 舵机 PWM8/PWM9 与 UART4_TX/RX 同脚（Pin6/Pin7），若同时启用 UART4 会冲突 | Luckfox 硬件 | 本项目不用 UART4，无实际冲突；文档注明即可 |
| 5 | P1 | BOM v2 中屏幕排针方向/引脚序未与实物（ILI9341 模块）核对 | BOM v2 | 到货后用万用表核对 J11 引脚序再冻结 |
| 6 | P2 | PCB_BOARD_DESIGN.md 仍是 v1.0（未同步 v1.5 电源直连与 v2 映射） | docs/hardware/ | 评审后统一更新 |

### 6.6 打样建议（最终结论）

1. **当前 v1 Gerber 可直接打样**（电源正确、DRC 已过、信号跳线安全）——但**先修 BOM v2 手册**，避免答辩时按错误手册接线。
2. 打样前**逐项核对 J11 屏幕排针**（到货实测）。
3. 若时间允许再出 v2 免跳线版，并跑通 DRC + 3D 预览后再下单。
