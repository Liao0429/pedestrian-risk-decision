# 语音告警模块设计方案

> 项目：基于 C++ 的边缘端轻量化行人感知与风险决策软件系统（Luckfox Pico Max / RV1106）
> 需求：当风险决策引擎判定为 **WARNING / DANGER** 时，通过语音播报提醒。
> 本文档对比可选硬件方案、给出推荐、接线、控制协议、C++ 集成方式与播报策略。

---

## 1. 需求与约束

- 触发条件：`RiskEngine::evaluate` 返回 `CROWD_WARNING(1)` 或 `CROWD_DANGER(2)` 时播报。
- 播报内容：固定短语即可（如“警告，前方行人密集”“危险，请立即疏散”），不需要实时动态文本。
- 控制方式：由主程序（C++，Linux 用户态）通过串口/GPIO 控制。
- 硬件平台约束：**Luckfox Pico Max 未引出 RV1106 内置音频（ACodec/I2S）引脚**，板上无音频接口、无耳机座，见 Luckfox 官方 Wiki《Audio》：*“Luckfox-Pico-Pro/Max 未引出音频引脚，该教程仅适用于 Luckfox-Pico Ultra 等”*。因此**不能直接使用板载音频输出**，必须外接语音模块。

---

## 2. 可选方案对比

### 方案 A：语音合成芯片（SYN6288 / XFS5152CE，UART/TTS）

- 原理：芯片内嵌中文 TTS 引擎，主控通过 UART 发送 **GB2312/GBK 文本**，芯片实时合成为语音并功放输出到喇叭。
- 代表型号：
 - **SYN6288**（北京宇音天下）：高性价比中文 TTS，默认 9600 8N1，支持音量/语速/背景音，模块自带功放（约 0.5W~1W）。
 - **XFS5152CE**（科大讯飞）：功能更强（中英混读 + MP3 解码），价格更高。
- 成本：模块约 **15~25 元**（XFS5152CE 约 25~40 元）。
- 接线难度：**低**。VCC(3.3~5V)、GND、UART_TX、UART_RX + 喇叭（2 线）。
- 音质/效果：**机械音，可接受但偏生硬**；最大优点是可**动态合成任意文本**。
- C++ 集成难度：**中**。只需向串口写帧，但文本必须是 **GB2312/GBK** 编码——Luckfox 上 C++ 字符串默认 UTF-8，需用 `iconv` 做 UTF-8→GB2312 转换，或用二进制写死 GB2312 短语。

### 方案 B：语音播放模块（DFPlayer Mini，UART 控制，播放 TF 卡预录音频）

- 原理：模块读取 TF 卡（microSD）中的预录音频（MP3/WAV），通过 UART 指令播放指定曲目；自带功放，直接驱动喇叭。
- 成本：模块约 **5~10 元**（含 TF 卡约 10~15 元）。
- 接线难度：**低**。VCC(3.2~5V)、GND、UART_TX、UART_RX + 喇叭（2 线）。
- 音质/效果：**最好**。预录音频可用 PC 端 TTS（如 edge-tts）生成，音质自然，且固定告警语足够。
- C++ 集成难度：**最低**。只发 10 字节固定帧（曲目号），**无文本编码问题**，指令帧固定、易调试。

### 方案 C：蜂鸣器 / 功放 + GPIO（简单告警音）

- 原理：GPIO 控制有源蜂鸣器（通电即响）或无源蜂鸣器（PWM 驱动），发出不同节奏的告警音。
- 成本：**1~3 元**。
- 接线难度：**最低**。GPIO + GND（有源蜂鸣器模块）或 GPIO + 三极管 + 蜂鸣器。
- 音质/效果：**仅有“哔哔”声，无语音内容**，区分度差。
- C++ 集成难度：**最低**。Linux sysfs GPIO `/sys/class/gpio` 写 0/1 或 PWM。

### 方案 D：板载音频输出（RV1106 ACodec / I2S）

- **不可行**。RV1106 SoC 内部虽有音频 Codec IP，但 **Luckfox Pico Pro/Max 硬件未引出音频引脚**（无 I2S/耳机座/功放输出），无法直接驱动喇叭。只有 Luckfox Pico Ultra 等型号引出了音频接口。
- 若强行使用，需飞线到 SoC 未焊接的音频焊盘（VCCIO 1.8V 域、需外接 I2S Codec 与功放），工作量与风险极大，不建议。

### 方案 E：USB 喇叭 + 系统 TF 卡（软件播放）——**推荐（用户 2026-10-27 决策）**

- **原理**：Luckfox Pico Max 有 USB Host 口。插入**免驱 USB 喇叭**（USB Audio Class，自带功放+扬声器），Linux 内核自动识别为 ALSA 声卡（如 `card 1`）。预录告警音频（WAV/MP3）直接存放在**系统 TF 卡**（`/userdata` 分区，238MB 可写，sudo 写入，重启保留），C++ 程序用 `aplay -D plughw:1,0 /userdata/voice/alert.wav` 一行命令播放。
- **成本**：USB 喇叭约 15~30 元；**无需额外 TF 卡**（复用系统 TF 卡）、无需 DFPlayer 模块。
- **接线难度**：**最低**（一个 USB 口，无杜邦线）。
- **C++ 集成难度**：**低**（`system()` 或 fork+exec 调用 aplay；播放状态可用 `aplay` 退出码/进程查）。
- **音质/效果**：**最佳**。音频文件在 PC 上用 edge-tts/真人录音生成 WAV（16kHz/16bit 单声道即可，文件小），固定告警语足够；可放多段音频。
- **优点**：
  1. **复用系统 TF 卡**——用户无需再买语音 TF 卡（用户明确要求）；
  2. 少一个模块、少一条 UART、少一套协议，硬件更简洁；
  3. 免驱即插即用，替换/升级只需换 USB 喇叭；
  4. 若 USB 喇叭带麦克风，还能顺带实现拾音。
- **注意**：USB 口占用——WiFi（RTL8723BU）+ USB 喇叭 + USB 麦克风 都插 USB Host，**需一个 4 口 USB Hub（约 10~20 元）**。
- **验证状态（2026-10-27 实测）**：板载 ACodec 已注册（`/proc/asound/cards` 有 `rvacodec`，`/dev/snd/pcmC0D0p` 存在），但 **Luckfox Pico Max 未引出音频引脚**（官方 Wiki 确认），板载声卡**无法直接接喇叭**；`aplay -l` 当前也识别不到（用户空间 ALSA 未完全就绪）。因此**必须用 USB 声卡/喇叭**——USB Audio 驱动是内核标配，插上即为独立声卡，绕开板载未引出问题。

> **推荐方案：方案 E —— USB 喇叭 + 系统 TF 卡软件播放。**
>
> 理由（用户 2026-10-27 决策：复用系统 TF 卡，不额外购卡/模块）：
>
> | 维度 | 方案 E（USB 喇叭） | 方案 B（DFPlayer） |
> |---|---|---|
> | 成本 | 15~30 元（USB 喇叭，或加 USB Hub 10~20 元） | 15~25 元（模块+TF 卡+小喇叭） |
> | 存储 | **复用系统 TF 卡（/userdata）**，零额外卡 | 需独立 microSD 卡（必须另买） |
> | 接线 | **无**（USB 即插即用） | 4 根信号/电源 + 2 根喇叭线 |
> | C++ 集成 | `system("aplay ...")` 一行 | 10 字节串口帧协议 |
> | 稳定性 | USB Audio 内核标配，免驱成熟 | 高，但多一个模块+卡 |
> | 扩展性 | 换音频文件=换文件；可接 USB Hub 扩多设备 | 曲目 0001~0099 限制 |
> | 风险点 | 需 USB Hub（WiFi/麦克风/喇叭共用 USB） | TF 卡接触不良、FAT 兼容 |
>
> **结论**：主方案用 **USB 喇叭**（复用系统 TF 卡）；若现场 USB 音频驱动出现意外，**兜底**再买 DFPlayer+TF 卡（约 20 元）按第 4~5 节接线切换（程序保留 UART 接口抽象）。

---

## 3. 结论与推荐

## 4. 推荐方案硬件接线（Luckfox Pico Max × DFPlayer Mini）

### 4.1 使用引脚

Luckfox Pico Pro/Max 采用 40 pin 排针（兼容树莓派位序），逻辑电平 **3.3V**。推荐使用 **UART4**（物理引脚 6/7），该串口默认固件已使能（设备节点 `/dev/ttyS4`），且紧邻 GND 与 5V 电源脚，走线最短。

| Luckfox Pico Max 引脚 | 信号 | 方向 | 接 DFPlayer Mini |
|---|---|---|---|
| **Pin 6** | `GPIO1_C5_d / UART4_TX_M1` | 输出 → 模块 RX | **RX** |
| **Pin 7** | `GP1_C4_d / UART4_RX_M1` | 输入 ← 模块 TX | **TX** |
| **Pin 8** | `GND` | 共地 | **GND** |
| **Pin 40** | `VBUS`（USB 5V，随板供电） | 电源 5V | **VCC** |
| — | 喇叭 | 输出 | DFPlayer **SPK_1 / SPK_2** |

> 说明：
> - DFPlayer Mini `VCC` 支持 **3.2~5V**，直接接板载 `VBUS(5V)`（Pin 40）即可；若板子由 USB-C 5V 供电，Pin 40 即为 5V。也可接 `VSYS`（Pin 39，约 4.7V）。
> - DFPlayer 串口为 TTL 3.3V 兼容，与 Luckfox UART（3.3V）**电平匹配，无需电平转换**。
> - 若担心电流余量，可给 DFPlayer 单独 5V 供电，仅与板子**共地**（GND 必须相连）。

### 4.2 DFPlayer Mini 引脚速查

| 引脚 | 说明 |
|---|---|
| VCC | 电源 3.2~5V（接 Pin 40） |
| GND | 地（接 Pin 8） |
| RX | 串口接收（接 Luckfox TX Pin 6） |
| TX | 串口发送（接 Luckfox RX Pin 7） |
| BUSY | 播放状态输出：低电平=播放中，高电平=空闲（可选接一个 GPIO 检测播完） |
| SPK_1 / SPK_2 | 喇叭输出（接 3W/8Ω 小喇叭） |

### 4.3 备用：SYN6288 接线（方案 A，如需动态文本）

| Luckfox Pico Max | SYN6288 模块 |
|---|---|
| Pin 6 `UART4_TX` | RX |
| Pin 7 `UART4_RX` | TX |
| Pin 40 `VBUS(5V)` | VCC（模块支持 3.3~5V） |
| Pin 8 `GND` | GND |
| — | SPK +/- 接喇叭 |

---

## 5. 控制协议

### 5.1 DFPlayer Mini 串口协议

- 波特率：**9600**，8 数据位，1 停止位，无校验（9600 8N1）。
- 命令帧（固定 10 字节）：

```
起始 版本 长度 命令 反馈 参数高 参数低 校验高 校验低 结束
0x7E 0xFF 0x06 CMD 0x00 P1 P2 CS_H CS_L 0xEF
```

- 校验和（**16 位取负（补码）**，大端存放）：
 ```
 sum = 0xFF + 0x06 + CMD + 0x00 + P1 + P2
 checksum = (uint16_t)(-sum) // 即 0x10000 - sum，取低 16 位
 CS_H = checksum >> 8
 CS_L = checksum & 0xFF
 ```

常用命令：

| CMD | 功能 | 参数 (P1,P2) |
|---|---|---|
| 0x06 | 设置音量（0~30） | 0x00, 音量值 |
| 0x09 | 指定播放设备 | 0x00, 0x02（SD 卡） |
| 0x0F | 播放指定文件夹中的指定曲目 | 0x0F：高字节=文件夹号(01~99)，低字节=曲目号(1~255) |
| 0x03 | 播放根目录指定曲目（按物理顺序，文件 0001.mp3...） | 0x00, 曲目号 |
| 0x16 | 停止播放 | 0x00, 0x00 |

**命令示例：**

```
// 初始化：选择 SD 卡设备
7E FF 06 09 00 00 02 FE F0 EF
// 设置音量 30
7E FF 06 06 00 00 1E FE D7 EF
// 播放文件夹01 曲目01（警告语）
7E FF 06 0F 00 01 01 FE EA EF
// 播放文件夹01 曲目02（危险语）
7E FF 06 0F 00 01 02 FE E9 EF
// 停止
7E FF 06 16 00 00 00 FE E5 EF
```

### 5.2 TF 卡预录音频准备

- TF 卡格式：**FAT16 / FAT32**。
- 目录结构（DFPlayer 约定）：在卡根目录建文件夹 `01`，把告警音频命名为 `0001.mp3`、`0002.mp3`：

```
TF 卡根目录/
└── 01/
 ├── 0001.mp3 # WARNING：警告，前方行人密集，请注意安全
 └── 0002.mp3 # DANGER：危险，行人过多，请立即疏散
```

- 音频生成：在 PC 上用离线 TTS（如 edge-tts、微软语音）或真人录音，导出 **44.1kHz / 16bit / 128kbps MP3**，时长 2~4 秒即可。
- 也可用方案 A 的 SYN6288 在 PC 端把文本合成 wav 后转成 mp3 放入卡中，效果更自然。

### 5.3 备用：SYN6288 协议（方案 A）

- 波特率：**9600 8N1**。
- 帧格式：

```
FD 数据长度(2B, 大端) 命令字 参数 文本数据(GB2312) 异或校验(1B)
FD LEN_H LEN_L 0x01 0x00 TEXT... XOR
```

- `数据长度 = 1(命令字) + 1(参数) + 文本字节数 + 1(校验) = 文本字节数 + 3`
- 校验 = 对“命令字、参数、全部文本字节”做**逐字节异或**。
- 参数低 3 位为编码格式：`0x00=GB2312`，`0x01=GBK`。
- **注意**：Luckfox 上 C++ 字符串为 UTF-8，发送前需 `iconv` 转成 GB2312/GBK，否则播报乱码。这是选择方案 B（DFPlayer）的重要原因之一。

---

## 6. C++ 集成

### 6.1 在现有工程中的位置

现有主循环（`src/luckfox_rtsp_yolov5/src/main.cc`）：

```cpp
riskEngine.computeFeatures;
CrowdLevel crowd = riskEngine.evaluate; // <-- 在此处拿到风险等级
```

集成方式：新增 `VoiceAlert` 类，在主循环每帧调用 `voiceAlert.update(crowd, nowMs)`；内部做等级跳变检测、冷却、防重复、优先级处理。

### 6.2 VoiceAlert 头文件 `include/voice_alert.h`

```cpp
#ifndef VOICE_ALERT_H
#define VOICE_ALERT_H

#include <stdint.h>
#include <unistd.h>
#include "risk_engine.h" // 复用 CrowdLevel 枚举

// 语音告警模块：DFPlayer Mini，UART4 (/dev/ttyS4)，9600 8N1
class VoiceAlert {
public:
 VoiceAlert;
 ~VoiceAlert;

 // 打开串口，初始化 DFPlayer（选 SD 卡、设音量）
 // 返回 true 表示初始化成功（串口打开失败不致命，仅禁用语音）
 bool init(const char* dev = "/dev/ttyS4", uint8_t volume = 30);

 // 主循环每帧调用：根据风险等级与当前时间(ms) 决定是否播报
 void update(CrowdLevel level, uint64_t nowMs);

 // 强制播报/停止（可手动触发）
 void playWarning;
 void playDanger;
 void stop;

 bool available const { return m_fd >= 0; }

private:
 void sendFrame(uint8_t cmd, uint8_t p1, uint8_t p2);
 bool isPlayingBusy; // 可选：读 BUSY GPIO

 int m_fd;
 uint8_t m_volume;
 CrowdLevel m_lastLevel; // 上一帧等级
 CrowdLevel m_playingLevel; // 正在播报/最近播报的等级
 uint64_t m_lastPlayMs; // 上次触发播报时间
 uint64_t m_levelChangeMs; // 等级变化时间
};

#endif
```

### 6.3 实现 `src/voice_alert.cc`

```cpp
#include "voice_alert.h"
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// 播报策略参数（可调）
#define WARN_REPEAT_MS 20000u // WARNING 重复播报间隔
#define DANGER_REPEAT_MS 8000u // DANGER 重复播报间隔
#define ESCALATE_COOLDOWN 2000u // 等级升级后的最小间隔

VoiceAlert::VoiceAlert
 : m_fd(-1), m_volume(30),
 m_lastLevel(CROWD_SAFE), m_playingLevel(CROWD_SAFE),
 m_lastPlayMs(0), m_levelChangeMs(0) {}

VoiceAlert::~VoiceAlert {
 if (m_fd >= 0) { stop; ::close(m_fd); m_fd = -1; }
}

bool VoiceAlert::init(const char* dev, uint8_t volume) {
 m_fd = ::open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
 if (m_fd < 0) { perror("voice: open uart"); return false; }

 struct termios tio;
 memset(&tio, 0, sizeof(tio));
 tio.c_iflag = 0; tio.c_oflag = 0; tio.c_lflag = 0;
 tio.c_cflag = CLOCAL | CREAD | CS8; // 8 位数据
 tio.c_cflag |= B9600; // 9600 波特
 tio.c_cc[VMIN] = 0;
 tio.c_cc[VTIME] = 1;
 if (tcsetattr(m_fd, TCSANOW, &tio) != 0) { perror("voice: tcsetattr"); ::close(m_fd); m_fd = -1; return false; }
 tcflush(m_fd, TCIOFLUSH);

 m_volume = volume;
 // 初始化 DFPlayer：选 SD 卡，设音量，停掉残留播放
 sendFrame(0x09, 0x00, 0x02); // 播放设备=SD 卡
 sendFrame(0x06, 0x00, m_volume);
 sendFrame(0x16, 0x00, 0x00); // 停止
 return true;
}

// 组帧并发送：7E FF 06 CMD 00 P1 P2 CS_H CS_L EF
void VoiceAlert::sendFrame(uint8_t cmd, uint8_t p1, uint8_t p2) {
 uint8_t frame[10];
 uint16_t sum = 0xFF + 0x06 + cmd + 0x00 + p1 + p2;
 uint16_t cs = (uint16_t)(-sum); // 补码 = 0x10000 - sum

 frame[0] = 0x7E; frame[1] = 0xFF; frame[2] = 0x06;
 frame[3] = cmd; frame[4] = 0x00;
 frame[5] = p1; frame[6] = p2;
 frame[7] = cs >> 8; frame[8] = cs & 0xFF;
 frame[9] = 0xEF;

 ssize_t n = ::write(m_fd, frame, sizeof(frame));
 if (n < 0) perror("voice: write");
}

void VoiceAlert::playWarning {
 sendFrame(0x0F, 0x01, 0x01); // 文件夹01 曲目01
 m_playingLevel = CROWD_WARNING;
}
void VoiceAlert::playDanger {
 sendFrame(0x0F, 0x01, 0x02); // 文件夹01 曲目02
 m_playingLevel = CROWD_DANGER;
}
void VoiceAlert::stop {
 sendFrame(0x16, 0x00, 0x00);
 m_playingLevel = CROWD_SAFE;
}

void VoiceAlert::update(CrowdLevel level, uint64_t nowMs) {
 if (m_fd < 0) return;

 bool changed = (level != m_lastLevel);
 m_lastLevel = level;

 if (level == CROWD_SAFE) {
 // 回到安全：若正在播报则停止（可选）
 if (m_playingLevel != CROWD_SAFE) stop;
 return;
 }

 if (changed) {
 m_levelChangeMs = nowMs;
 // 等级升级（DANGER 覆盖 WARNING，直接插播危险语）
 if (level == CROWD_DANGER) {
 playDanger;
 m_lastPlayMs = nowMs;
 } else if (level == CROWD_WARNING) {
 // 若刚从 DANGER 降级，避免立即重复播报
 if (nowMs - m_lastPlayMs >= ESCALATE_COOLDOWN) {
 playWarning;
 m_lastPlayMs = nowMs;
 }
 }
 return;
 }

 // 等级未变：周期性重复播报，保证持续告警
 uint64_t interval = (level == CROWD_DANGER) ? DANGER_REPEAT_MS : WARN_REPEAT_MS;
 if (nowMs - m_lastPlayMs >= interval) {
 if (level == CROWD_DANGER) playDanger;
 else playWarning;
 m_lastPlayMs = nowMs;
 }
}
```

### 6.4 主循环接入 `main.cc`

```cpp
#include "voice_alert.h"
// ...
VoiceAlert voiceAlert;
voiceAlert.init("/dev/ttyS4", 30); // 打开 UART4，音量 30

while(1) {
 // ... 原有检测 ...
 riskEngine.computeFeatures;
 CrowdLevel crowd = riskEngine.evaluate;
 voiceAlert.update(crowd, (uint64_t)(TEST_COMM_GetNowUs / 1000)); // 传当前毫秒
 // ... 其余绘图/推流 ...
}
```

> 说明：
> - `TEST_COMM_GetNowUs` 已有，返回微秒；除以 1000 得到毫秒。
> - 若串口打开失败（例如未使能 UART4），`voiceAlert.available` 为 false，系统其余功能不受影响。

### 6.5 若 UART4 未使能（设备树开启）

部分 Luckfox SDK 版本 UART3/4 默认未开启（官方 Wiki 与社区 Issue 有出入）。若 `ls /dev/ttyS*` 看不到 `ttyS4`，在 SDK 的 `config/dts_config` 中追加并重新编译：

```dts
&uart4 {
 status = "okay";
 pinctrl-names = "default";
 pinctrl-0 = <&uart4m1_xfer>; // 对应物理引脚 6/7（GPIO1_C5/C4）
};
```

> UART4 与 UART3 等不同串口可同时使用；但同一串口的 M0/M1 引脚组不能同时占用。

---

## 7. 播报策略

1. **按等级触发**：
 - `WARNING` → 播放 `01/0001.mp3`（警告语）。
 - `DANGER` → 播放 `01/0002.mp3`（危险语）。
 - `SAFE` → 停止播放（可选）。

2. **防重复播报**：
 - 仅在**等级跳变**时触发（`SAFE→WARNING`、`WARNING→DANGER`、`DANGER→SAFE` 等），不在每帧重复发。
 - 等级**维持不变**时按冷却间隔周期重播：DANGER 每 8s 一次，WARNING 每 20s 一次（可调）。
 - 可选：用 DFPlayer `BUSY` 引脚接一个 GPIO，读取到低电平（播放中）时不再发送新命令，避免指令堆积。

3. **优先级**：
 - `DANGER > WARNING`。当 WARNING 正在播报、等级升为 DANGER 时，直接下发 DANGER 曲目（DFPlayer 收到新播放命令会打断当前播放，天然实现抢占）。
 - 从 DANGER 降级到 WARNING 后，加 `ESCALATE_COOLDOWN`（2s）抑制立即重复播报。

4. **音量分级**（可选）：
 - 初始化时设统一音量（如 30）；如需分级，可在 `playDanger` 前把音量设到 30、`playWarning` 设 25，播完再恢复。

---

## 8. 成本清单

| 物料 | 数量 | 参考单价 | 合计 |
|---|---|---|---|
| DFPlayer Mini 模块 | 1 | 5~10 元 | 5~10 元 |
| microSD/TF 卡（含卡套） | 1 | 5~10 元 | 5~10 元 |
| 8Ω/3W 小喇叭 | 1 | 2~5 元 | 2~5 元 |
| 杜邦线/2.54 排母若干 | — | 2 元 | 2 元 |
| **合计** | | | **约 14~27 元** |

若升级 SYN6288：模块 15~25 元 + 喇叭 2~5 元，合计约 20~30 元。

---

## 9. 注意事项与坑

1. **TF 卡文件命名必须为 `0001.mp3` 这种 4 位数字**，放在 `01/` 文件夹下；否则 DFPlayer 找不到曲目。
2. TF 卡用 **FAT32**，首次使用建议格式化；文件名要**全小写** `.mp3`。
3. DFPlayer 上电初始化约需 100~300ms，`init` 后最好 `usleep(200000)` 再发第一条命令，否则可能丢帧。
4. 校验和是**16 位取负**（`-sum`），不是“取反”——二者差 1，务必用示例帧核对（可用串口助手验证）。
5. **SYN6288 必须 GB2312/GBK 编码**，UTF-8 会乱码；方案 B 无此问题。
6. DFPlayer 的 `VCC` 接 5V、`GND` 必须与板子共地；串口是 3.3V 逻辑，**不要**接 5V 的 TTL（Luckfox 是 3.3V，安全）。
7. Luckfox Pico Max 的 UART 是 **3.3V 电平**，接 DFPlayer 无需电平转换；但不要把模块 RX 直接接到其它 5V 逻辑设备。
8. 电源：DFPlayer 播放时峰值电流约 50~100mA，从板载 `VBUS(5V)` 取电没问题；若喇叭功率大（>3W），建议单独 5V 供电并共地，避免拉低板子电压导致系统重启。
9. 主循环帧率较高（每帧调用 `update`），播报逻辑必须**基于时间戳与状态机**，不要在每帧都发播放命令。
10. 若使用 BUSY 引脚，需把对应 GPIO 配成输入并做电平读取；本设计默认用时间冷却，无需 BUSY 也能正常工作。

---

## 10. 参考

- Luckfox Wiki：Audio（Luckfox Pico Ultra）——说明 Pro/Max 未引出音频引脚。
- Luckfox Wiki：GPIO / UART——引脚复用、`/dev/ttyS4`、设备树配置。
- DFRobot DFPlayer Mini 使用说明书（帧格式、命令表、校验和）。
- SYN6288 数据手册（宇音天下）；LibDriver/syn6288 驱动源码。
- Luckfox Pico Pro/Max 原理图（GitHub LuckfoxTECH / embeddedboys）——引脚信号映射。