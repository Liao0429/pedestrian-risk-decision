# 硬件引脚 / 总线实测对照表（唯一事实源）

> 生成：2026-10-30 · 作者：hardware-hw（T3 硬件-PCB-结构-代码交叉验证）
> 依据三路证据：
> ① **Luckfox Pico Max 真机只读实测**（192.168.0.136，root 读取 `/sys/kernel/debug/gpio`、`/sys/kernel/debug/pinctrl/*/pinmux-pins`、`/sys/class/pwm`、`/proc/device-tree`、`/proc/asound/cards`、`/sys/class/udc`、`/proc/tty/drivers`、`ls /dev`）；
> ② **PCB Gerber v1.7**（`G:\毕设论文\02_答辩准备\PCB_Gerber\`，RS-274X 文本 + `design.json` + `generate_pcb.py` 网络核算，全程只读）；
> ③ **3D 外壳 V2**（`generate_enclosure_v2.py` 参数 + `装配验证报告.md` 尺寸）。
> 标注规则：**已验证** = 有板上实测或文件解析证据；**推断** = 由已验证事实按数据手册/几何推算；**未验证** = 需实物或官方原理图确认。
> ⚠ 本文取代 `system_integration.md` §3、`tracking_servo.md` §2.3、`led_button_driver.md` §2、`lvgl_board_port.md` §3.5 中的引脚表——那些表与实测冲突，详见 §3。

---

## 0. 结论摘要

| # | 结论 | 严重度 | 处置 |
|---|---|---|---|
| 1 | 板上 PWM 只有 **pwmchip8 / pwmchip9，且各自 npwm=1**；代码所有示例用的 `pwmchip0` / `pwm0+pwm1` 都不存在 | P0 | 改代码（已改） |
| 2 | `selftest_main.cc` 默认 `-b 92` 实为 **GPIO2_D4**，与按键 GPIO1_C3 / 排针 Pin27 都无关 | P0 | 改代码（已改 67） |
| 3 | 代码/文档当按键的 **GPIO1_C3(51) 实测是 SPI0_MISO**，与 ILI9341 读 ID 冲突 | P0 | 改代码+改文档（已改） |
| 4 | **只有 /dev/spidev0.0，无 spidev0.1**；SPI0 仅一组 cs0 → 触摸屏必须 GPIO 手工片选 | P0 | 改代码（已加） |
| 5 | ILI9341 **MADCTL=0x08 缺 MV 位**，320×240 窗口寻址越界 → 花屏 | P0 | 改代码（已加 setRotation） |
| 6 | PCB 上 **5V_SERVO 是孤立网络**，与 5V 无任何连接 → 舵机轨没有电源来源 | P0 | 改硬件/改文档 |
| 7 | PCB **J11 只有 8 脚，没有背光脚、没有触摸 T_CS/T_IRQ** | P0 | 改硬件（飞线+GPIO CS） |
| 8 | 外壳 4 按键孔 + 3 LED 孔与 PCB 前边缘差 **13~14mm**，且 PCB 上无按键/LED 焊盘；J8 开关**无开孔** | P0 | 改结构 |
| 9 | USB 口实测 **dr_mode=peripheral**（DT 原文）+ 无 host 控制器绑定 + 被 rockchip gadget 占用 → **USB 喇叭/WiFi 方案当前不成立**；但**板载 card0(rv-acodec) 已验证可播放**（aplay -D plughw:0,0 rc=0） | P0 | 改代码+改文档；语音改走板载 codec 或改 dtb 开 host |
| 10 | 无 `/dev/ttyS*`、无 `/dev/i2c-2` → J6 串口屏、UART4 语音、OLED(I2C2) 三条方案在现镜像上都不可用 | P1 | 改文档（标注预留不可用） |

---

## 1. SoC 侧实测事实（**已验证**，2026-10-30，Luckfox Pico Max / RV1106G3 / Linux 5.10.160 armv7l）

### 1.1 GPIO 编号规则

```
gpiochip0: base=0    ngpio=32   (gpio0, ff380000.gpio)
gpiochip1: base=32   ngpio=32   (gpio1, ff530000.gpio)
gpiochip2: base=64   ngpio=32   (gpio2, ff540000.gpio)
gpiochip3: base=96   ngpio=32   (gpio3, ff550000.gpio)
gpiochip4: base=128  ngpio=24   (gpio4, ff560000.gpio)
```

→ sysfs 编号 = `bank*32 + group*8 + pin`（A=0,B=1,C=2,D=3），**实测成立**。常用换算：

| GPIO 名 | sysfs | 备注（实测占用） |
|---|---|---|
| GPIO1_C0 | 48 | SPI0_CS0（spi0m0-cs0） |
| GPIO1_C1 | 49 | SPI0_SCLK |
| GPIO1_C2 | 50 | SPI0_MOSI |
| **GPIO1_C3** | **51** | **SPI0_MISO** ← 不是按键、也不能当 LCD RESET |
| GPIO1_C4 | 52 | PWM8_M1（未 claim，PAN 舵机信号） |
| GPIO1_C5 | 53 | PWM9_M1（未 claim，TILT 舵机信号） |
| GPIO1_D0 / D1 | 56 / 57 | 未 claim；v3 手册标为 UART3_TX/RX（UART3 当前未启用） |
| GPIO1_D2 / D3 | 58 / 59 | **I2C3 SCL / SDA**（i2c3m1-xfer） |
| GPIO2_A3 | **67** | 未 claim ← 按键/J8 应落这里（v3 手册 Pin27） |
| GPIO2_B0 / B1 | 72 / 73 | 厂商 fbtft 参考节点的 LCD 背光 / DC |
| GPIO2_D4 | 92 | 未 claim ← `selftest_main -b 92` 实际导出的就是它 |

### 1.2 PWM（**已验证**）

```
ls /sys/class/pwm/  →  pwmchip8 (ff490000.pwm, npwm=1)   pwmchip9 (ff490010.pwm, npwm=1)
其它 pwm@ff3500xx / ff3600xx / ff49002x / ff49003x 在 DTB 中全部 status=disabled
/sys/kernel/debug/pwm → 两个 chip 各 1 个 pwm，均为 (null) owner、period=0（未被占用）
```

→ **PAN(Yaw) = PWM8_M1 = GPIO1_C4 = pwmchip8/pwm0；TILT(Pitch) = PWM9_M1 = GPIO1_C5 = pwmchip9/pwm0。**
→ **每 chip 只有 1 通道（npwm=1）**，`pwmchipN/pwm1` 在该 SoC 上不可能存在。
→ 设备树已 okay，无需重编 dtb；但写 `/sys/class/pwm/.../export` 需要 root。

### 1.3 SPI（**已验证**）

```
/dev/spidev0.0       存在（crw-rw-rw-）
/sys/bus/spi/devices 只有 spi0.0(spidev) 与 spi2.0(spi-nand)
spi@ff510000 (SPI1)   status=disabled   → 没有 spidev0.1
pinctrl: pin48/49/50/51 = spi0m0-cs0 / clk / mosi / miso   ← SPI0 只在 GPIO1_C0..C3 上
```

→ **只有一个 spidev 节点、SPI0 只有一组片选**。ILI9341 与 XPT2046 若都挂在 SPI0 上，只能有一个用硬件 CS0、另一个用 GPIO 手工片选；两者都不建议用 CS0（见 §3）。

### 1.4 I2C / UART / 音频 / USB（**已验证**）

| 项 | 实测 | 影响 |
|---|---|---|
| I2C | 只有 `/dev/i2c-3`（i2c3m1-xfer = GPIO1_D2/D3）与 `/dev/i2c-4`；**无 /dev/i2c-2** | `oled_switch.md` / `system_integration.md` 的 OLED(I2C2) 方案不可用 |
| UART | `ls /dev/ttyS*` → 不存在；`/proc/tty/drivers` 有 serial 驱动但无设备节点；console = `/dev/ttyFIQ0` | J6 串口屏（UART3）、UART4 语音模块都不可用 |
| 音频 | `/proc/asound/cards` → 只有 `card 0: rvacodec`（pcmC0D0c/p） | `voice_alert.cc` 默认 `plughw:1,0` 必然失败 |
| USB | `/sys/class/udc` = `ffb00000.usb`；**`dr_mode` = "peripheral"**（`/proc/device-tree/usbdrd/usb@ffb00000/dr_mode`）；`xhci-hcd` 驱动存在但无设备绑定；`/sys/bus/usb/devices` 为空；configfs gadget `rockchip` 已绑 UDC 且功能含 rndis/adb/uvc/**uac1/uac2** | USB 喇叭 / USB WiFi / USB 麦 / USB Hub 当前全部不可用（详见 §10.3） |
| 显示 | `/sys/class/graphics` 只有 `fbcon`，无 fb0 | 与 `lvgl_board_port.md` 结论一致（spidev 用户态驱动路线） |
| 热 | `thermal_zone0 = 66.6 °C`（当时 load 11.4） | 见 §6 / §5.4 |

### 1.5 厂商 fbtft 参考节点（**已验证**，status = disabled）

读 `/proc/device-tree/spi@ff500000/fbtft@0/*` 原始字节：

| 属性 | 原始值 | 解析 | 文档里的写法 |
|---|---|---|---|
| `led-gpios` | `<&gpio2 8 0>` | **GPIO2_B0 = sysfs 72** | ❌ `lvgl_board_port.md` 写 GPIO2_C0 |
| `dc-gpios` | `<&gpio2 9 0>` | **GPIO2_B1 = sysfs 73** | ❌ 该文档写 GPIO2_C1 |
| `reset-gpios` | `<&gpio1 19 1>` | **GPIO1_C3 = sysfs 51（低有效）** = SPI0_MISO | ❌ 与 MISO 冲突，不能照抄 |

→ 该参考节点的 RESET 与 SPI0_MISO 同脚，**不能直接复用到 ILI9341**（读 ID 需要 MISO）。DC/RST 必须另选空闲 GPIO。

---

## 2. 40 pin 排针 ↔ GPIO 推导表（**推断**：两个独立来源交叉）

推导依据：
- v3 手册（`答辩硬件展示方案_v3_BOM与PCB手册.md`）给出 GND = Pin **3/8/13/18/23/28/33/38**（间隔 5），并给出 Pin6/7=PWM9/PWM8、Pin12/14/15/16=SPI0、Pin19/20=UART3、Pin21/22=UART1、Pin27=GPIO2_A3、Pin36=3V3、Pin39=VSYS、Pin40=VBUS；
- 板端 pinmux 实测给出 SPI0 与 PWM 的真实 GPIO；
- 两者拼起来，排针是「每 5 脚一块：[GP0, GP1, GND, GP2, GP3]」的重复结构，能同时解释 GND 间隔与所有已声明功能。

| 排针脚 | 块内位置 | GPIO（推断） | 依据 |
|---|---|---|---|
| 1 | GP0 | （未确认，可能 3V3/5V） | 未验证 |
| 2 | GP1 | （未确认） | 未验证 |
| 3 | GND | GND | v3 GND 列表 |
| 4 | GP2 | （未确认） | 未验证 |
| 5 | GP3 | （未确认） | 未验证 |
| 6 | GP0 | **GPIO1_C5 = 53（PWM9_M1）** | v3 + pwmchip9 实测 |
| 7 | GP1 | **GPIO1_C4 = 52（PWM8_M1）** | v3 + pwmchip8 实测 |
| 8 | GND | GND | v3 |
| 12 | GP0 | **GPIO1_C0 = 48（SPI0_CS0）** | v3 + pinmux 实测 spi0m0-cs0 |
| 13 | GND | GND | v3 |
| 14 | GP1 | **GPIO1_C1 = 49（SPI0_SCLK）** | v3 + pinmux |
| 15 | GP2 | **GPIO1_C2 = 50（SPI0_MOSI）** | v3 + pinmux |
| 16 | GP3 | **GPIO1_C3 = 51（SPI0_MISO）** | v3 + pinmux |
| 18 | GND | GND | v3 |
| 19 | GP2 | GPIO1_D0 = 56（UART3_TX_M1） | v3（UART3 当前未启用） |
| 20 | GP3 | GPIO1_D1 = 57（UART3_RX_M1） | v3 |
| 21 | GP0 | GPIO2_A4 = 68（UART1_TX_M1） | v3 |
| 22 | GP1 | GPIO2_A5 = 69（UART1_RX_M1） | v3 |
| 23 | GND | GND | v3 |
| 27 | GP1 | **GPIO2_A3 = 67** | v3 + PCB J8 设计目标 |
| 28 | GND | GND | v3 |
| 36 | GP0 | 3V3 | v3 |
| 38 | GND | GND | v3 |
| 39 | GP2 | VSYS | v3 |
| 40 | GP3 | VBUS(5V) | v3 |

> **这套表只能靠「排针↔GPIO 通断实测（万用表）或官方原理图」最终确认**，属**推断**。但本报告用到的关键结论（PWM8=GPIO1_C4、PWM9=GPIO1_C5、SPI0=GPIO1_C0..C3、按键候选 GPIO2_A3）都有**板端 pinmux 实测**支撑，不依赖排针编号。

---

## 3. 代码常量 → 实测（逐条对照）

| # | 位置 | 代码/文档写的 | 实测应改 | 判定 | 处置 |
|---|---|---|---|---|---|
| 1 | `servo.h` 注释、`pan_tilt.h` 示例、`selftest.h` 示例、`selftest_main.cc` | PWM 通道 0/1，`pwmchip0/pwm0`、`pwmchip0/pwm1`，`-c 0` | `SysfsPwm(8,0)`=PAN、`SysfsPwm(9,0)`=TILT，`-c 8` | **代码错** | ✅ 已改 |
| 2 | `selftest_main.cc` / `selftest.h` 注释 | 按键 `-b 92`、"GPIO1_C3_d Pin 92" | sysfs **67**（GPIO2_A3，PCB J8→Pin27） | **代码错** | ✅ 已改 |
| 3 | `gpio.h` / `button.h` / `led_button_driver.md` | 按键 GPIO1_C3 = 51 | 51 实测 = SPI0_MISO，冲突 → 改 67 | **代码+文档错** | ✅ 已改 |
| 4 | `led.h` / `led_button_driver.md` | LED R/G/B = 56/57/58 | 56/57 = UART3_TX/RX 复用组、58 = **I2C3_SCL** | **未定版且有冲突** | ✅ 已改注释 |
| 5 | `sysfs_spi.h` / `xpt2046.h` | 触摸用 `/dev/spidev0.1` 独立片选 | **不存在 spidev0.1**；SPI0 只一组 cs0 | **设计不成立** | ✅ 已加 `setCsGpio()` |
| 6 | `ili9341.h` | `MADCTL=0x08`（无 MV） | 320×240 须带 MV → `0x28` | **代码错** | ✅ 已加 `setRotation()`（默认值保留兼容主机测试，**真机必须调用**） |
| 7 | `ili9341.h` | SWRESET/SLPOUT 后无延时 | 手册要求各 ≥120ms | **代码错** | ✅ 已加 delay |
| 8 | `ili9341.h` / `xpt2046.h` | "命令" "读取" 两次裸 transfer | spidev 每条消息结束释放片选 → 真机读不到 | **代码错** | ✅ 已加 `beginCs()/endCs()` |
| 9 | `selftest.h` 默认 SPI 探测命令 | compatible 含 "9341" | 实测 = `rockchip,spidev`，**永远不匹配** | **代码错（该项永远 FAIL）** | ✅ 已加 `setLcd(&lcd)` 真读 0xD3 |
| 10 | `voice_alert.cc` | 默认 `plughw:1,0` | 板上只有 card0=rv-acodec，无 USB 声卡 | **默认值错** | ✅ 已加 card 检测 + 回退 `default` |
| 11 | `lcd_menu.h` | "扩展底板 4 个轻触按键" | PCB 无按键焊盘；外壳有 4 个 7×7 孔 | **跨件不一致** | 改硬件/结构（见 §5） |

---

## 4. PCB Gerber v1.7 网络实况（**已验证**，`design.json` 解析 + `generate_pcb.py` 核算）

板框 100×80 mm，双层，4×φ3.2 安装孔 @ (5,5)(95,5)(5,75)(95,75)，98 焊盘 / 20 走线 / 2 过孔。

### 4.1 电源网络实况

| 网络 | 焊盘集合 | 走线 | 判定 |
|---|---|---|---|
| VIN | J1-1, SW1-1 | 1 | ✅ |
| **5V** | SW1-2, J2a-1, C2-1, **J11-5V**, U1-40(VBUS) | 4 | ✅（含 5V→VBUS 反灌） |
| **5V_SERVO** | ~~J2b-1, C1+, J3-VCC, J4-VCC, J5-VCC, J6-VCC~~（v1.8.1 已并入 5V 单轨，经 SJ1 0.3mm 细线默认连通、可割断） | 4 | ❌→✅（v1.8.1 已修复） |
| GND | 13 个焊盘 | 11 | ✅（但只有 0.8mm 走线，无铺铜） |
| SW_ALARM | R1-1, J8-1, J10A-SW_ALARM | 0 | ✅（跳线设计） |

**P0-硬件（v1.7 结论；v1.8.1 已修复）**：`5V` 与 `5V_SERVO` 是两个互不连接的网，`5V_SERVO` 的全部 6 个焊盘都在负载侧，**没有任何电源输入**。

> ✅ **v1.8.1 修复**：5V_SERVO 各焊盘已并入 5V 单轨，`SJ1` 改为 **0.3mm 细走线默认连通**（丝印「CUT=断开舵机供电」，割断后可接第二路 5V）；`SJ2` 为 5V↔VBUS 焊盘桥，**默认断开防反灌**。
→ 三份文档都写"舵机 VCC/GND 板上已接好"（v3 §4.4 步骤4、`嘉立创下单说明.md` 步骤5、`PCB_BOARD_DESIGN.md` §四），**与 Gerber 不符**。

### 4.2 单焊盘（"跳线"）网络清单

设计意图是"电源底板 + 信号跳线"，下列网络只有 1 个焊盘、0 条走线，**必须靠杜邦线外接**：

`SPI_SCLK / SPI_MOSI / SPI_MISO / SPI_CS / SPI_DC / SPI_RST / LCD_BL / TOUCH_CS / TOUCH_IRQ`（J11 侧 9 个信号，v1.8.1 由 8 脚扩为 11 脚）、`SPK_P / SPK_N`（J7，v1.8.1 已删除）、`NC`（J5-6）、`U1_1..U1_40`（排母 40 脚）、`U1_PAN_SIG` 等 8 个 J10B 焊盘。

含义 / 后果：

- J11 的 6 个信号**不是断线缺陷**，而是设计如此（用杜邦线从开发板排针接到 J11 排针）。**v1.8.1 已补 LCD_BL / TOUCH_CS / TOUCH_IRQ 三脚（J11 共 11 脚）**；推荐 GPIO 分配见文末 §11。
- `SPK_P/SPK_N` 孤立 → J7 喇叭端子无法出声（当前语音走 USB，可接受，但文档说"喇叭接 J7"是错的）。
- 每个 U1 焊盘都是单焊盘网络 → 用户必须"从排母对应脚引线"到 J10B，**共 8 根飞线**；U1 焊盘在**底层（layers="D"）**，插上开发板后需从板底焊线。

### 4.3 制造/可读性问题（**已验证**）

| 项 | 实测 | 影响 |
|---|---|---|
| 覆铜 | `generate_pcb.py` 只画走线，**没有任何 polygon/region 填充**（`add_fill` 定义但从未调用） | 文档称"顶层铺 GND 铜"不符；大电流回路只有 0.8mm 线 |
| GND 回流 | 0.8mm 走线 + 仅 2 个 φ0.6 过孔 | 2A 级电流回流裕量小（估算） |
| 丝印 J10A | 焊盘步进 **2.54mm**，丝印标注步进 **5.08mm** | 标注与焊盘**错位**，极易接错 |
| J11 排针 | 焊盘为单排 2.54mm 直插 | ILI9341 模块多自带**公头**，PCB 应焊**排母**或改用杜邦线 |

---

## 5. 结构（外壳 V2）vs PCB vs 代码

### 5.1 已对齐（**已验证**）

| 项 | 外壳 V2 | PCB | 结论 |
|---|---|---|---|
| 板子外形 | 底座内腔 136×106，PCB 居中 | 100×80 | ✅ 放得下 |
| 安装柱 | 4×φ7 柱，φ3.2 孔，间距 **90×70** | 4×φ3.2 孔，间距 **90×70** | ✅ 完全对得上 |
| 底座高度 | 内腔 27.5mm | 最高件（排母+开发板 ≈12~15mm） | ✅ |
| 屏幕排线 | 面板 10×8 过槽贯通 + 背板 12×10 穿线孔 | — | ✅ |

### 5.2 未对齐（**已验证/推断**）

| # | 问题 | 证据 | 严重度 | 处置 |
|---|---|---|---|---|
| 1 | 4 个按键孔（7×7，z=13）在**前壁 y≈109**；PCB 前边缘在 **y=95** → 相距 **13~14mm**；PCB 上**无按键焊盘** | `lower.difference(box((7,4,7),(cx,109,13)))` + PCB 板框 | P0 | 改结构+改硬件 |
| 2 | 3 个 LED 孔（φ5.2，z=21.0）；PCB 顶面 z≈8.5，5mm LED 顶端 ≈17.1 → 高度差 1.3mm、深度差 12.5mm；PCB 无 LED/限流电阻焊盘 | 同上 | P0 | 改结构+改硬件 |
| 3 | **J8 告警开关无任何开孔** | 脚本全量 `difference` 列表 | P1 | 改结构 |
| 4 | 面板背后与背板只有 **0.1mm 间隙**；面板上**没有屏幕模块的 4×M2.5 螺柱/压框**（只有 2 个"面板↔背板"固定长圆孔） | `装配验证报告.md` §四 + 脚本 | P1 | 改结构 |
| 5 | 窗口 53×79 与 2.8" 模块 PCB（≈50×86，未购）：宽 53>50 会漏边、高 79<86 塞不进 | **推断** | P2 | 实测后收口 |
| 6 | 摄像头 FFC 需 40cm（云台 z=190，PCB z≈10） | v3 BOM #16 | P2 | 采购项 |

### 5.3 三方（代码 / PCB / 外壳）交互件一致性

| 功能 | 代码 | PCB | 外壳 | 一致？ |
|---|---|---|---|---|
| 三色灯 | 3×GPIO（led.h） | 无焊盘、无限流电阻 | 3×φ5.2 孔 | ❌ |
| 4 键菜单 | lcd_menu.h | 无焊盘 | 4×7×7 孔 | ❌ |
| 告警开关 | button.h（1×GPIO） | J8 2P + R1 下拉 ✅ | **无开孔** | ❌ |
| 舵机云台 | servo.h/pan_tilt.h | J3/J4 ✅ 但**无 5V 源** | 云台平台 z=190 ✅ | ⚠ |
| 触摸屏 | ili9341.h/xpt2046.h | J11 11P ✅（v1.8.1 补 BL/T_CS/T_IRQ） | 窗口+过槽 ✅ | ⚠ |
| 语音 | voice_alert.cc（USB aplay） | J5/J7 预留 | 喇叭孔阵在右壁 | ⚠（缺 USB host） |

### 5.4 散热（**已验证数据点**）

实测 `soc-thermal = 66.6~67.2 °C`（当时 load 11.4、内存 211MB/free 21MB）。外壳 V2 是 **PLA 封闭腔 + 顶部全开**、无风扇。长期演示建议：SoC 贴 15×15×5mm 铝散热片 + 导热硅胶，并把顶部开口作烟囱出口。**PLA 玻璃化温度 ~60-65°C**，腔体长期 67°C 对结构件有蠕变风险（推断，需测腔内温度）。

---

## 6. 供电 / 功耗核算

### 6.1 供电链路（**已验证**）

```
J1(5V/3A) ─► SW1 ─► 5V ─┬─► U1-40 (VBUS，给开发板)
                        ├─► J11-1 (屏 VCC)
                        ├─► C2 0.1µF
                        └─► J2a-1  ┊需人工飞线┊
     5V_SERVO ◄─ J2b-1 ──┴─ C1 1000µF / J3,J4(舵机VCC) / J5,J6(预留)
```

- **无 DC-DC / LDO**（BOM 明确"MP1584 无此位号"）；"两路 5V"实际是**同一个 5V 源**，仅靠 1000µF 缓冲，**没有真正隔离**。
- **无 3.3V 轨**：LED/按键逻辑取自开发板 3V3（Pin36）或直接用 GPIO 灌电流。

### 6.2 电流预算（标注可信度）

| 负载 | 电压 | 典型 | 峰值 | 可信度 |
|---|---|---|---|---|
| Luckfox Pico Max + SC3336 | 5V(VBUS) | 0.3~0.5A | 0.8A | 估算 |
| SG90 ×2 | 5V_SERVO | 0.2~0.4A | **1.2~1.4A**（双堵转） | 手册（单舵机堵转 500~700mA） |
| ILI9341 2.8" 模块（含背光） | 5V | 0.05~0.1A | 0.1A | 估算（模块未购） |
| 三色 LED ×3 | 3V3(GPIO) | 0.012A | 0.012A | 计算 (3.3-2.0)/330Ω |
| USB 喇叭（若改 host 模式） | 5V(USB) | 0.1~0.3A | 0.5A | 估算（USB 端口上限） |
| USB WiFi RTL8723BU（若启用） | 5V(USB) | 0.15A | 0.3A | 估算 |
| **合计（现状：无 USB 主机）** | — | **0.5~1.0A** | **≈2.3A** | — |
| **合计（USB host + Hub 三件）** | — | 0.8~1.5A | **≈3.1A** | 超 3A 适配上限 |

结论：
1. 5V/3A 在现状配置下够用，但双舵机堵转+屏+开发板峰值已接近 2.3A，且全部走 1.0/0.8mm 走线、无铺铜 → 压降与地弹风险偏高（**估算**）。
2. 加上 USB Hub+WiFi+喇叭+麦后峰值顶到 3A，且 USB 主干仍在开发板 VBUS、与舵机共地共源 → **必须**给舵机独立电源（第二路 5V 或 12V+MP1584），这正是原文档主张但 **Gerber 未实现**。
3. J11 屏 VCC 取 5V：模块自带 3.3V 稳压则可；**若买到无稳压裸模组，5V 会烧**（**未验证**）。

---

## 7. 坐标系 / 极性

### 7.1 ILI9341 RGB/BGR 与旋转（**改代码**）

- `COLMOD=0x55` = 16bit RGB565 ✅。
- `MADCTL` 位：D7 MY / D6 MX / D5 MV / D4 ML / D3 BGR / D2 MH。
- 现驱动 `MADCTL=0x08` = **只有 BGR，没有 MV**：面板按 **240 列×320 行**寻址，而驱动按 320×240 写窗口（CASET 写到 0x013F）→ 列地址仅 8 位，会截断/回绕 → **花屏或折行**（**推断，依据数据手册语义，强烈）**。
- 正确值：`0x28 = MV|BGR`（320×240）或 `0xE8 = MX|MY|MV|BGR`（180°）。已加 `setRotation(0/1)` 与 `setMadctl()`。
- BGR 位 0x08 是绝大多数 2.8" 模组的正确取值；若整屏红蓝互换，清掉 0x08（**未验证**）。

### 7.2 触摸坐标 vs 显示坐标（**改代码/标定**）

- 显示改成 **MV（行列交换）后的 320×240 横向**，XPT2046 的 raw X/Y 与屏幕 X/Y 不再是同一轴向。
- `xpt2046.h` 的 `TouchCalibration::swapXY` **默认 false** → 不改则触摸坐标整体旋转 90°。
- 处置：真机标定必须 `cal.swapXY = true`（或按实测），并四点标定 `rawXMin/Max/rawYMin/Max + invertX/invertY`；建议把 swapXY 与 `setRotation()` 绑成同一个配置项，避免"显示转 180°、触摸没转"。

### 7.3 LVGL 移植配置（**文档缺失**）

`lvgl_board_port.md` 与 `lvgl_menu.md` **都没有写** `lv_conf.h` 里的 `LV_COLOR_16_SWAP`、旋转、`hor_res/ver_res`、flush 方向。板端 320×240 横向 + RGB565 的建议组合（**推断，需实机验证**）：

| 项 | 建议值 | 说明 |
|---|---|---|
| `LV_COLOR_DEPTH` | 16 | RGB565 |
| `LV_COLOR_16_SWAP` | **1** | LVGL 16bit 按 little-endian，SPI 发字节序是 big-endian（`flush()` 先发高字节）→ 通常必须 SWAP=1，否则颜色错乱。**必须实测** |
| `hor_res / ver_res` | 320 / 240 | 与 `Ili9341::kWidth/kHeight` 一致 |
| MADCTL | `0x28`（`setRotation(0)`） | 与 hor/ver 对应 |
| indev | XPT2046，`swapXY=true` + 四点标定 | 与显示旋转一致 |

---

## 8. 需要实物 / 用户确认的未知项

1. **40 pin 排针 ↔ GPIO 物理通断**（万用表）——§2 属推断，关键项有 pinmux 支撑。
2. ILI9341 模块实物：是否自带 3.3V 稳压、背光电流、外形尺寸、排针性别、T_IRQ 是否引出。
3. XPT2046 片选/PENIRQ 最终接哪两个空闲 GPIO。
4. 舵机电源方案：单路 5V 飞线 vs 独立第二路 5V（后者才满足"隔离"主张）。
5. ~~USB 主机模式是否可开~~ → **已实测**：`dr_mode=peripheral`，无 host 控制器绑定，唯一 USB 口被 rockchip gadget 占用 → **当前不可用**（§10.3）。
5b. **新增未验证项**：RV1106 板载 ACODEC 输出引脚是否引到排针 / 有喇叭功放（card0 软件层已验证可播放，能否真出声取决于硬件）——需官方原理图或实物。
6. 外壳实际腔温（67°C SoC 附近 PLA 蠕变）。
7. LED 三引脚最终定版（56/57/58 有复用冲突）。

---

## 9. 本轮改动与验证记录

改动文件（全部在 `src/hardware/`，C++11 + 仅标准库）：

| 文件 | 改动 |
|---|---|
| `servo.h` | 引脚/节点注释改实测值；说明每 chip 1 通道 |
| `pan_tilt.h` | 示例改 `SysfsPwm(8,0)/(9,0)` |
| `selftest.h` | 示例/默认改 67、pwmchip8；新增 `setLcd(Ili9341*)`，SPI_LCD_ID 用真实 0xD3 读 ID；include ili9341.h |
| `selftest_main.cc` | 默认 `-g 56 -b 67 -c 8 -p 0`；usage 与注释同步 |
| `gpio.h` / `button.h` / `led.h` | 引脚注释改实测事实（51=SPI0_MISO、92=GPIO2_D4、56/57/58 不可用） |
| `ili9341.h` | 新增 MADCTL 常量 + `setMadctl()/setRotation()`；SWRESET/SLPOUT 后加 120ms 延时；`readId()` 用片选会话；`SpiBus` 新增 `beginCs()/endCs()` |
| `xpt2046.h` | `readRaw()` 用片选会话；接线说明改「同一 spidev0.0 + GPIO 片选」 |
| `sysfs_spi.h/.cc` | 新增 `setCsGpio()/beginCs()/endCs()` 手工片选（板上无 spidev0.1 的唯一可行路径）；注释按实测重写 |
| `voice_alert.cc/.h` | init() 检测 card 是否存在并回退 ALSA `default`，打印实际设备；背景注释按实测修正 |

验证（全部**已验证**）：

```
Windows/MinGW（x86_64-w64-mingw32-g++, -std=c++11 -O2 -Wall -Wextra）
  test_hardware_modules  118 passed / 0 failed
  test_selftest           80 passed / 0 failed
  test_lcd_menu           99 passed / 0 failed
  test_ili9341           123 passed / 0 failed
  test_xpt2046            98 passed / 0 failed
  hardware_selftest        编译通过
  tmp/hw_check/min_check  19 passed / 0 failed（新 API：setRotation→MADCTL 0x28、片选会话计数、延时累加）
Linux 构建机（g++ 11.4, x86-64）
  test_sysfs_spi          40 passed / 0 failed（含新增手工片选代码）
  sysfs_spi.cc / voice_alert.cc / selftest_main.cc  编译 0 警告
ARM 交叉编译（arm-linux-gnueabihf-g++ 11.4, armhf）
  hardware_selftest       → ELF 32-bit ARM, /lib/ld-linux-armhf.so.3 ✅
  smoke（10 个 header-only 模块一起 include）→ ELF 32-bit ARM ✅，0 警告
```

未改动（不在本 scope）：`config/default_config.json`（`conf_threshold=0.35` 与 `tests/test_hardware_config_consistency.py` 期望的 0.60 不符，属 algo-py scope）。


---

## 10. 真机实跑验证（2026-10-31，Luckfox Pico Max）

> 部署：Windows → 构建机(192.168.0.63) 用 `arm-linux-gnueabihf-g++ 11.4` 交叉编译 → 回传 Windows → paramiko helper 上传到板卡 `/tmp`。
> 二进制：`hardware_selftest`（`src/hardware/selftest_main.cc`）、`arm_hw_probe`（本轮新增最小真机探针，源码 `tmp/hw_check/arm_hw_probe.cc`）。
> 运行：`echo <pwd> | sudo -S /tmp/...`（写 /sys/class/gpio、/sys/class/pwm 需 root）；**运行前后均记录并恢复 GPIO/PWM/pinmux 原值**（未干扰 `pedestrian-risk.service`）。

### 10.1 hardware_selftest 真机原始输出

~~~
=== hardware selftest ===
GPIO loopback=56 button=67 PWM=pwmchip8/pwm0
[PASS] GPIO_LOOPBACK  GPIO 输出写读回 OK
[PASS] PWM_NODE       PWM 节点导出/配置/写 duty OK
[FAIL] SPI_LCD_ID     LCD ID 不匹配 0x9341
[FAIL] USB_AUDIO      未检测到 USB 声卡
[FAIL] WIFI_LINK      wlan0 不存在或未就绪
[PASS] BUTTON_LEVEL   按键电平读取 OK（当前 LOW）
=== result: 3 PASS / 3 FAIL ===
rc=1
~~~

**实测结论**：
- **PWM_NODE PASS** 证实 §3 的 PWM 修正正确：`pwmchip8/pwm0` 真机可 export / 20 ms / 1.5 ms duty。
- **BUTTON_LEVEL PASS** 证实 **GPIO2_A3 = sysfs 67** 真实可导出可读（旧值 92 已被否）。
- **GPIO_LOOPBACK PASS**（gpio 56 写读回）。
- 3 项 FAIL 均为**预期**：没有屏幕（且默认探测命令只读 `rockchip,spidev`，见 §3 第 9 条）、没有 USB 声卡、wlan0 不存在。
- 运行后 `/sys/class/gpio` 无残留 → 本轮新增的 `unexportPin()` 生效（修复前实测残留 `gpio56/gpio67`）。

### 10.2 arm_hw_probe 真机原始输出

~~~
PWM8: configure=1 period=20000000 duty=1500000 enable=1 polarity=normal
[PASS] pwmchip8/pwm0 可 export/20ms/1.5ms/使能，且 sysfs 回读一致
[PASS] /dev/spidev0.0 可打开（mode0/8bit/1MHz）
[PASS] 片选 GPIO 已注册（跨 transfer 保持）
CS 回读: 会话中=0(read_ok=1)  会话后=1(read_ok=1)
[PASS] beginCs 拉低 / endCs 拉高，可回读验证
spidev 全双工单字节: ok=1 rx=0xD3（无外设，读值无意义）
[PASS] spidev 全双工 transfer 在真机执行成功
XPT2046 readRawX: ok=1 raw=0x0000（无触摸模组，值无意义）
[PASS] XPT2046 readRaw 走片选会话，真机不崩
ILI9341 begin=1 madctl=0x28（无屏幕，只验证命令序列发出）
[PASS] MADCTL 运行时值 = 0x28（MV|BGR，横向 320x240）
ILI9341 readId=1 id=00 00 00 00（无屏幕，预期非 9341）
[PASS] 无屏幕时 ID 不是 0x9341（负向验证）
=== arm_hw_probe: 8 passed, 0 failed ===
~~~

**已"实测"**：PWM8 通道；`/dev/spidev0.0` 打开（mode0/8bit/1MHz）；手工片选会话（拉低/拉高由 GPIO 回读证实）；spidev 全双工传输执行；XPT2046 读路径不崩；MADCTL 运行时=0x28；`Ili9341::begin()` 完整走通（返回 1）。
**仍"未验证"**：屏幕实际显示内容（无屏）、触摸坐标（无触摸模组）、BGR/红蓝是否正确（无屏）。

### 10.3 USB host 与语音方案判定（**实测结论**）

| 证据 | 值 |
|---|---|
| DT `/proc/device-tree/usbdrd/usb@ffb00000/dr_mode` | **`peripheral`** |
| `/sys/class/udc` | `ffb00000.usb`，state=`not attached` |
| configfs gadget | `/sys/kernel/config/usb_gadget/rockchip`，UDC=`ffb00000.usb`，功能 `ffs.adb ffs.ntb hid.usb0 rndis.gs0 uac1.gs0 uac2.gs0 uvc.gs6` |
| `usb0` | 172.32.0.70/16（RNDIS gadget 虚拟网卡） |
| `xhci-hcd` 驱动 | 存在，但**无任何平台设备绑定** |
| `/sys/bus/usb/devices` | **空**（host 栈未激活） |
| 板载声卡 | `card 0: rvacodec`；`sudo aplay -l` 可枚举；`sudo aplay -D plughw:0,0 /tmp/silence.wav` → **rc=0 正常播放** |
| 权限 | `/dev/snd/*` = root:audio 0660，`pico` 不在 audio 组；`pedestrian-risk.service` 是 **User=root**，故 demo 内 aplay 可用 |

**结论**：
1. **USB 喇叭方案（文档方案 E）在当前镜像不可落地**：DT 把 USB 固定为 `dr_mode=peripheral`，无 host 控制器绑定，唯一端口被 rockchip gadget（rndis/adb/uvc/uac）占用。要落地须改 DTB `dr_mode`（otg/host）+ 拆除 gadget（会丢 usb0 网络与 adb），属内核/overlay 级改动，且需确认 Type-C 的 OTG/VBUS 硬件支持。
2. **可行替代**：板载 **rv-acodec（card0）软件层已实测可播放**（`aplay -D plughw:0,0` rc=0）。`voice_alert` 现在会把 `plughw:1,0` 自动回退到 ALSA `default`（即 card0）。**唯一未验证项是 ACODEC 输出引脚是否引出/有功放**——若引出，语音今天就能上，不需要 USB。
3. 顺带修正：`/dev/snd/*` 权限导致**非 root** 跑 `aplay -l` 会报 "no soundcards found"（假 FAIL）。自检 USB_AUDIO 的默认探测命令已从 `aplay -l` 改为 world-readable 的 `cat /proc/asound/cards`。

### 10.4 真机实跑新发现并修复的缺陷

| # | 缺陷 | 现象 | 处置 |
|---|---|---|---|
| 1 | `SysfsLcdPins` 构造时**不配置 GPIO 方向** | `Ili9341::begin()` 在 `resetLow()` 一步返回 false（sysfs GPIO 默认输入，`write()` 直接 false）→ **屏幕永远点不亮**；修复后同一探针 `begin=1` | ✅ 构造内对 DC/RESET/背光 `setDirection(true)`（`SysfsTouchPin` 同理设输入） |
| 2 | 自检跑完**留下导出的 GPIO/PWM** | 实测残留 `gpio56/gpio67` 占用 | ✅ 新增 `SysfsGpio::unexportPin()` / `SysfsPwm::unexportPin()`，`selftest_main` 退出前调用 |
| 3 | USB_AUDIO 自检项非 root 下**假 FAIL** | `aplay -l` 报 "no soundcards found"（权限），即使有声卡 | ✅ 默认探测命令改 `cat /proc/asound/cards` |

### 10.5 状态变更汇总（推断 → 实测 / 仍未验证）

| 条目 | 原状态 | 现状态 |
|---|---|---|
| PWM 芯片/通道 = 8/9 且通道 0 | 实测（列目录） | **实测（真机 export + 20ms + 1.5ms + 回读）** |
| 按键 GPIO = GPIO2_A3(67) 可用 | 实测（pinmux） | **实测（真机导出 + 读电平 PASS）** |
| 手工片选会话可用 | 推断（代码语义） | **实测（GPIO 回读 assert/deassert + transfer）** |
| MADCTL 运行时 = 0x28 | 代码逻辑 | **实测（运行时值 + begin() 走通）**；实际显示效果**仍未验证（无屏）** |
| 触摸坐标 / 校准 | 推断 | **未验证（无触摸模组）** |
| USB host 模式 | 未验证 | **实测：不可用（dr_mode=peripheral）** |
| 板载 codec 可播放 | 未验证 | **实测可播放（rc=0）**；能否真出声 **未验证（引脚/功放）** |
| spidev0.0 可打开 | 实测（节点存在） | **实测（mode0/8bit/1MHz 配置成功 + 传输执行）** |
| 排针 ↔ GPIO 物理通断 | 推断 | **仍未验证（需万用表/原理图）** |


---

## 11. v1.8.1 定稿更新（2026-10-31，PCB 交付前最后一轮）

> 对应实物：`G:\毕设论文\02_答辩准备\PCB_Gerber\`（generate_pcb.py v1.8.1 + design.json 133 焊盘 / 52 走线 / 2 过孔，不含 4 个安装孔）。

1. **供电**：SJ1 = **0.3mm 细走线，默认连通**（5V → 舵机轨，丝印「CUT=断开舵机供电」）；SJ2 = **5V↔VBUS 焊盘桥，默认断开**（防反灌）。两种供电方案：A（推荐）USB-C 供开发板 + 底板 J1 只供外设，SJ2 保持断开；B 底板 J1 单电源 → 短接 SJ2，开发板经 U1-40 得电，此时不要插 USB-C 供电。
2. **J11 = 11 脚**：5V / GND / SCLK / MOSI / MISO / CS / DC / RST / **LCD_BL / TOUCH_CS / TOUCH_IRQ**。CS 一律 **GPIO 手工片选**（不接排针 Pin12 硬件 CS0；板上只有 /dev/spidev0.0，无 spidev0.1）。
3. **J11 六个 GPIO 推荐分配（接线表定稿）**：LCD 与触摸**必须两个独立 CS**（同一 SPI 总线共享 MISO，源码 `setCsGpio(&lcdCs)` / `setCsGpio(&touchCs)` 分别片选），否则数据打架。

| 信号 | GPIO 名 | sysfs | 排针（推断） | 说明 |
|---|---|---|---|---|
| DC | GPIO1_D0 | 56 | Pin19 | UART3_TX 复用组，当前未启用 |
| RST | GPIO1_D1 | 57 | Pin20 | UART3_RX 复用组，当前未启用 |
| **LCD_CS** | **GPIO2_B0** | **72** | （未确认） | fbtft 参考节点已 disabled，实测未占用；与 TOUCH_CS 独立片选 |
| LCD_BL | GPIO2_D4 | 92 | （未确认） | 无复用占用 |
| TOUCH_CS | GPIO1_C6 | 54 | （未确认） | PWM10_M1，已 disabled |
| TOUCH_IRQ | GPIO1_C7 | 55 | （未确认） | PWM11_IR_M1，已 disabled |

   排针编号除 Pin19/20 外为推断（需万用表/官方原理图确认）；sysfs 编号为板上实测（pinmux）。
3b. **四个侧按按键 S1-S4 的 GPIO（v1.8.1 新增，接线表定稿）**：四键经 J12(5P) 引出，**内部上拉 + 按下为低**（源码 `Button(&gpio, false /*activeHigh*/)`）；J8 告警开关仍为 GPIO2_A3=67（10kΩ 下拉、按下为高），两者独立。

| 按键 | GPIO 名 | sysfs | 排针（推断） | 实测状态（2026-10-31 pinmux 探针） |
|---|---|---|---|---|
| S1 | GPIO2_A4 | **68** | Pin21 | MUX/GPIO 均 UNCLAIMED |
| S2 | GPIO2_A5 | **69** | Pin22 | MUX/GPIO 均 UNCLAIMED |
| S3 | GPIO2_A6 | **70** | Pin23（推断相邻） | MUX/GPIO 均 UNCLAIMED |
| S4 | GPIO2_A7 | **71** | Pin24（推断相邻） | MUX/GPIO 均 UNCLAIMED |

   > 证据：2026-10-31 板上 `cat /sys/kernel/debug/pinctrl/*/pinmux-pins` 实测 pin 68/69/70/71 全部 `(MUX UNCLAIMED) (GPIO UNCLAIMED)`；`/sys/kernel/debug/gpio` 仅 gpio-33/117/118 被驱动占用。备用空闲：GPIO2_B1=73、GPIO1_D4..D7=60..63、GPIO2_C0..C7=80..87（同批实测 UNCLAIMED）。
4. **J10A ↔ J10B**：v1.8.1 起 J10B 与 J10A 为**同一网络名**（PAN_SIG 等 8 个），但 PCB 上没有铜走线直连（横跨 U1 无布线通道）→ **用户必须杜邦线短接 J10A-x ↔ J10B-x**，丝印已标注。
5. 焊盘数 **133**（不含 4 个 φ3.2 安装孔，安装孔在 Edge.Cuts/drl 里）。
