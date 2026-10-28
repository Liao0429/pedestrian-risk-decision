# 硬件落地组装指南（Buy & Build）

> 本指南让方案真正变成可运行的成品。按清单采购 → 按接线图接线 → 按步骤组装 → 烧录驱动代码。

## 一、采购清单（性价比优先）

### 已有（无需购买）
| 器件 | 型号 | 数量 | 备注 |
|---|---|---|---|
| 开发板 | Luckfox Pico Max (RV1106) | 1 | 已刷 Ubuntu 系统，已跑通检测 |
| 摄像头 | SC3336 3MP | 1 | MIPI CSI |
| 舵机 | **SG90 9g** | 2 | 需购买 |

### 需新购（约 ¥70~130）
| 器件 | 推荐型号 | 参考价 | 购买渠道 |
|---|---|---|---|
| SG90 双轴云台支架套件 | 二自由度云台 | ¥12~25 | 淘宝"SG90 云台支架" |
| 语音模块 | **DFPlayer Mini** | ¥5~10 | 淘宝"DFPlayer Mini" |
| 功放喇叭 | 8Ω 3W 微型喇叭 | ¥3~8 | 淘宝/拼多多 |
| TF 卡 | 8GB Class10 | 手头有则免 | 存 3 段告警 MP3 |
| 彩色触控屏 | **2.8 寸 ILI9341 SPI 触摸屏（320×240，XPT2046）** | ¥25~35 | 淘宝"2.8寸 ILI9341 触摸屏 SPI" |
| 电源适配器 | USB-A **5V/3A** | ¥10~25 | 淘宝/京东 |
| USB 一分二线 | 公→双母 | ¥5~10 | 淘宝 |
| 电容 | 1000µF/16V + 0.1µF | ¥2~3 | 电子店 |
| 降压模块(可选) | MP1584EN 3A | ¥3~8 | 淘宝 |
| 杜邦线/面包板 | 公母各若干 | ¥5~15 | 淘宝 |
| 外壳(可选) | 亚克力/3D打印 | ¥20~60 | 淘宝定制 |

## 二、接线要点（完整图见 docs/hardware/system_integration.md）

- **舵机**：Yaw 信号→PWM 脚，Pitch 信号→另一个 PWM 脚；红(VCC)接**独立5V轨**，棕(GND)共地。**绝不把舵机 VCC 接开发板 5V。**
- **语音 USB 喇叭**（推荐，用户 2026-10-27 决策）：免驱 USB 喇叭插开发板 USB Host 口，内核识别为 ALSA 声卡；预录 WAV 放系统 TF 卡 `/userdata/voice/`，C++ 用 `aplay` 播放。兜底方案：DFPlayer Mini（UART /dev/ttyS4）仍见 `docs/hardware/voice_module.md`。
- **触摸屏菜单**（LVGL + ILI9341）：`lcd_menu.h` 菜单状态机框架已就绪（页面导航/四键/风险颜色映射/参数节流，99 项主机测试），`ili9341.h` SPI 显示驱动已就绪（复位/初始化命令序列/窗口/矩形填充/整帧 flush/读 ID 0x9341，123 项主机测试），`xpt2046.h` 电阻触摸驱动已就绪（SPI 12 位坐标/压力读取、PENIRQ+压力阈值触摸判定、校准坐标映射、中值抗噪，98 项主机测试），**板端适配层 `sysfs_spi.{h,cc}` 已就绪**（`SysfsSpiBus` 打开 `/dev/spidevX.Y` 全双工传输 + `SysfsLcdPins`/`SysfsTouchPin` sysfs GPIO 控制 DC/RESET/背光/PENIRQ，40 项 Linux 单元测试已在开发板 armv7l 真机编译运行通过）；真机阶段按 `docs/hardware/lvgl_board_port.md` 路线改设备树（spi0 status=okay）→ 验证 `/dev/spidev0.0` → 接线点亮 ILI9341 并校准触摸（方案 B，免重编内核）。
- **供电**：5V/3A 适配器 → USB 一分二 → 一路开发板 USB-C、一路面包板 5V 轨。面包板 +5V/GND 并 1000µF 电容。

## 三、组装步骤

1. 云台支架固定：Yaw 舵机装底座，Pitch 舵机装云台臂，摄像头轻质贴于臂端（FFC 排线留余量）。
2. 接线：按第二部接线表把所有模块接到开发板排针和面包板电源轨。
3. 供电：先插电源适配器，开发板启动，舵机/语音/屏幕上电。
4. 烧录驱动：把 src/hardware/ 的 servo.h / pan_tilt.h / voice_alert.h（+ voice_alert.cc）集成进交叉编译工程（老电脑编译 → scp 到板子运行）。
5. 调试：`/sys/class/pwm` 确认舵机 PWM、`aplay -l` 确认 USB 喇叭声卡、播放 `/userdata/voice/warning.wav` 测试语音、串口屏显示状态。

## 四、验证标准
- 舵机云台：摄像头能自动追踪画面中最大行人/人群质心（PanTilt 类）
- 语音：检测到 WARNING 播报警告音，DANGER 播报严重告警
- 触摸屏菜单：ILI9341 点亮后主菜单可按键导航，DETECTION 页大字显示 人数/风险等级/FPS/IP（`lcd_menu.h` 状态机逻辑已 99 项主机测试，`ili9341.h` 显示驱动已 123 项主机测试，`xpt2046.h` 触摸驱动已 98 项主机测试，板端适配层 `sysfs_spi.{h,cc}` 已 40 项测试在开发板 armv7l 真机通过；真机点亮待硬件采购）
- 整体：RTSP 推流 + 触摸屏显示 + 语音告警 + 三色灯同时工作

## 五、C++ 驱动代码
- `src/hardware/servo.h`：SG90 PWM 驱动（hw::Pwm 抽象 + SysfsPwm 板端实现 + FakePwm 测试替身，角度→脉宽换算可测）
- `src/hardware/pan_tilt.h`：双舵机云台追踪（P控制+死区+机械/角速度限幅+一阶低通，逻辑可主机测试）
- `src/hardware/voice_alert.h` + `voice_alert.cc`：语音告警（USB 喇叭 + aplay，等级触发/抢占/防重复；含 FakeVoiceBackend 可测）
- `src/hardware/selftest.h` + `selftest_main.cc`：**开机硬件自检**（GPIO 写读回/PWM 节点/SPI 屏幕 ID 0x9341/USB 声卡/WiFi/按键 6 项 PASS/FAIL；`make hardware-selftest` 生成板端 CLI，退出码=失败数）
- `src/hardware/lcd_menu.h`：**触摸屏菜单状态机**（页面导航/四键/风险颜色映射/参数节流/NULL 容错，LVGL 前端无关）
- `src/hardware/ili9341.h`：**ILI9341 SPI 显示驱动**（320×240 RGB565：复位/初始化命令序列/窗口/矩形填充裁剪/帧缓冲/整帧 flush/读 ID 0x9341；`SpiBus`+`LcdPins` 抽象，FakeSpi/FakeLcdPins 可主机测试）
- `src/hardware/xpt2046.h`：**XPT2046 电阻触摸驱动**（SPI 12 位 ADC 坐标/压力：0xD0/0x90 命令协议、数据提取、PENIRQ+压力阈值触摸判定、校准坐标映射（镜像/轴交换）、中值抗噪；`TouchPin` 抽象，FakeSpi/FakeTouchPin 可主机测试）
- `src/hardware/sysfs_spi.h` + `sysfs_spi.cc`：**Linux 板端适配层**（`SysfsSpiBus` 打开 `/dev/spidevX.Y` 并配置 mode/bits/speed、全双工 transfer；`SysfsLcdPins` 控制 DC/RESET/背光、`SysfsTouchPin` 读 PENIRQ，均经 FakeGpio 注入可测；仅 Linux 编译，`make test` 在 Linux/CI 自动纳入）
- 以上驱动均有主机单元测试（`make test-hardware` 118 项 + `make test-selftest` 80 项 + `make test-lcd-menu` 99 项 + `make test-ili9341` 123 项 + `make test-xpt2046` 98 项，无需真实硬件即可在 PC/CI 验证；`make test-sysfs-spi` 40 项 Linux 板端适配测试，已在开发板 armv7l 真机编译运行通过）
- `docs/hardware/voice_module.md`：语音方案对比（USB 喇叭推荐，DFPlayer 兜底）与播报策略
- `docs/hardware/selftest.md`：自检框架说明、systemd 开机自检部署、演示模式门禁
- `docs/hardware/lvgl_menu.md`：LVGL 移植调研、菜单框架说明、板端适配路线图
