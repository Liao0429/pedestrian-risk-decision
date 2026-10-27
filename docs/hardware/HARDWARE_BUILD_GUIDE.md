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
| 彩色触控屏 | **串口屏 USART HMI 4.3寸** | ¥80~120 | 淘宝"淘晶驰 串口屏" |
| 电源适配器 | USB-A **5V/3A** | ¥10~25 | 淘宝/京东 |
| USB 一分二线 | 公→双母 | ¥5~10 | 淘宝 |
| 电容 | 1000µF/16V + 0.1µF | ¥2~3 | 电子店 |
| 降压模块(可选) | MP1584EN 3A | ¥3~8 | 淘宝 |
| 杜邦线/面包板 | 公母各若干 | ¥5~15 | 淘宝 |
| 外壳(可选) | 亚克力/3D打印 | ¥20~60 | 淘宝定制 |

## 二、接线要点（完整图见 docs/hardware/system_integration.md）

- **舵机**：Yaw 信号→PWM 脚，Pitch 信号→另一个 PWM 脚；红(VCC)接**独立5V轨**，棕(GND)共地。**绝不把舵机 VCC 接开发板 5V。**
- **语音 USB 喇叭**（推荐，用户 2026-10-27 决策）：免驱 USB 喇叭插开发板 USB Host 口，内核识别为 ALSA 声卡；预录 WAV 放系统 TF 卡 `/userdata/voice/`，C++ 用 `aplay` 播放。兜底方案：DFPlayer Mini（UART /dev/ttyS4）仍见 `docs/hardware/voice_module.md`。
- **串口屏**：单独 UART，RX/TX 交叉接开发板 TX/RX。
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
- 串口屏：实时显示 人数/拥挤等级/FPS/IP，触摸切换页面
- 整体：RTSP 推流 + 大屏显示 + 语音告警同时工作

## 五、C++ 驱动代码
- `src/hardware/servo.h`：SG90 PWM 驱动（hw::Pwm 抽象 + SysfsPwm 板端实现 + FakePwm 测试替身，角度→脉宽换算可测）
- `src/hardware/pan_tilt.h`：双舵机云台追踪（P控制+死区+机械/角速度限幅+一阶低通，逻辑可主机测试）
- `src/hardware/voice_alert.h` + `voice_alert.cc`：语音告警（USB 喇叭 + aplay，等级触发/抢占/防重复；含 FakeVoiceBackend 可测）
- 以上驱动均有主机单元测试（`make test-hardware`，共 118 项用例，无需真实硬件即可在 PC/CI 验证）
- `docs/hardware/voice_module.md`：语音方案对比（USB 喇叭推荐，DFPlayer 兜底）与播报策略
