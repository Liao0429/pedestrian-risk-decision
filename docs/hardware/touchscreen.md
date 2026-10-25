# 彩色触控大屏（淘晶驰 USART HMI 串口屏）+ 物理开关模块设计方案

> **本方案替代原 OLED(SSD1306) 显示方案**。原 `docs/hardware/oled_switch.md` 仅作历史/备用参考，其中**物理开关（GPIO）部分继续沿用**，显示模块改为彩色触控串口屏。

> 项目：《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》
> 目标板：Luckfox Pico Max（RV1106G3，256MB DDR3L，NPU 1 TOPS，单核 Cortex-A7 1.2GHz）
> 适用源码：`src/luckfox_rtsp_yolov5/`（RKNN YOLOv5 行人检测 + 复杂网络风险决策 + RTSP 推流）
> 文档日期：2026-10-25

---

## 1. 需求变更与方案选型

### 1.1 为什么弃用 OLED，改用彩色触控大屏

原方案采用 0.96 寸 SSD1306 单色 OLED（128×64），能显示四行文本，但存在明显局限：

| 局限 | 影响 |
|------|------|
| 单色、分辨率低（128×64） | 无法用红/黄/绿直观表达 SAFE/WARNING/DANGER 等级 |
| 无触摸 | 无法现场调参、切换页面 |
| 需要自绘字库/u8g2 中文字库 | 中文显示麻烦、字库占用 Flash |
| 屏太小 | 答辩/演示时远处看不清 |

**彩色触控大屏**（串口屏）用**红/黄/绿大字号**显示风险等级、**触摸按钮**切换页面、**彩色大字**显示人数/FPS/IP，演示效果好得多，答辩加分明显。

### 1.2 为什么选"串口屏"（USART HMI）

本项目是**边缘端 C++ 系统**，不是裸机 MCU 开发，核心是 RKNN 推理 + 风险决策。因此显示方案必须：
- **不占用主核太多资源**：串口屏只需 1 个 UART + 简单字符串指令，CPU 开销可忽略；
- **界面开发快**：用厂商上位机（USART HMI）图形化拖拽控件，生成工程烧录到屏，**C++ 端只需发字符串指令**改文本/颜色/页面，无需在开发板上做 GUI；
- **触摸交互简单**：屏幕按钮按下后由串口回传数据，C++ 端解析即可。

淘晶驰（TJC，USART HMI）是国内串口屏事实标准，资料多、上手快、价位低，是毕设彩色触控方案的最优选择。

---

## 2. 串口屏选型对比与成本控制说明

### 2.1 选型对比表

| 尺寸 | 分辨率 | 典型型号 | 触摸类型 | 接口 | 参考价 | 定位 |
|------|--------|----------|----------|------|--------|------|
| **4.3 寸** | **480×272** | TJC4827X343_011X（电阻）/ TJC4827C043_011X（电容） | 电阻 / 电容 | UART TTL | **约 ¥70~90** | **够用档（推荐）** |
| 5 寸 | 800×480 | TJC8048C050_011X | 电阻 / 电容 | UART TTL | 约 ¥110~140 | 高配档 |
| 7 寸 | 800×480 / 1024×600 | TJC8048C070_011X | 电容 | UART TTL | 约 ¥160~220 | 高配档 |

> 注：同一尺寸电阻触摸比电容便宜；电容手感好、支持多点，但对毕设非必需。以上为淘宝常见价。

### 2.2 成本控制说明（够用档 vs 高配档）

**结论：推荐「4.3 寸 480×272 电阻触摸」串口屏（约 ¥80）作为默认方案。**

理由（结合毕设展示与性价比）：

1. **显示内容完全够用**：主页只需要
   - 行人数（`Crowd: 12`）
   - 风险等级大字（`DANGER`，配红/黄/绿背景）
   - `FPS: 18`
   - `IP: 192.168.0.136`
   
   480×272 分辨率下，用 24~32px 大字号可排 4~5 行，远看清晰；OLED 那 128×64 根本做不到。

2. **触摸交互只需几个按钮**：副页放"自动/手动切换、静音、云台使能"等按钮，4.3 寸足够排布。

3. **价格差 40~100 元**：4.3 寸约 ¥80，5 寸约 ¥120，7 寸约 ¥180。多花 40~100 元换来的更大尺寸/更高分辨率，对毕设答辩的边际收益很低——评审主要看**功能与数据实时性**，不是屏幕大小。

4. **电阻触摸耐用且便宜**：毕设演示用触摸笔/手指点按即可，无需电容的多点手势。

**两档定位：**

| 档位 | 型号 | 价格 | 适用 |
|------|------|------|------|
| **够用档（推荐）** | 4.3 寸 480×272 电阻 | ~¥80 | 毕设展示主体方案：主页大字显示 + 副页触摸调参 |
| 高配档 | 5 寸 800×480 电阻/电容（~¥120）或 7 寸 800×480 电容（~¥180） | ¥120~180 | 预算充足、想要更大屏幕/更清晰文字、展示更多图表曲线 |

> 若预算实在紧张，也可用淘晶驰 3.5 寸（320×480，约 ¥50~60），但 4.3 寸性价比和显示效果更均衡，推荐 4.3 寸为底线。

---

## 3. UART 引脚分配（串口屏）

### 3.1 现状：语音模块 JQ8900 已占用 UART4

语音模块 JQ8900 已经占用 **UART4**（TX/RX 两根线）。因此串口屏**不能再用 UART4**，需使用另一个空闲 UART。

### 3.2 串口屏使用 UART3（推荐）——具体引脚

Luckfox Pico Max 排针上，**UART3** 是理想选择（与 I2C/SPI/其它外设不冲突）：

| 信号 | 排针引脚 | 引脚名 | 复用功能 | 连接方向 |
|------|---------|--------|----------|----------|
| UART3 TX | **Pin 19** | GPIO1_D0 | **UART3_TX_M1** | 开发板 TX → 串口屏 RX |
| UART3 RX | **Pin 20** | GPIO1_D1 | **UART3_RX_M1** | 开发板 RX ← 串口屏 TX |

**备用方案（若 UART3 被占用/使能异常）**：用 UART1
- Pin 21 = GPIO2_A4 = **UART1_TX_M1**（板 TX → 屏 RX）
- Pin 22 = GPIO2_A5 = **UART1_RX_M1**（板 RX ← 屏 TX）

**重要：交叉连接**——开发板 TX 必须接串口屏 RX，开发板 RX 必须接串口屏 TX，接反则无法通信。

设备节点：**`/dev/ttyS3`**（UART3 对应 `/dev/ttyS3`）。

### 3.3 UART 使能检查与配置

**先检查 UART3 是否已使能：**

```bash
ls /dev/ttyS*
# 期望看到 /dev/ttyS3
stty -F /dev/ttyS3
```

**若 `/dev/ttyS3` 不存在**，说明 UART3 未在设备树中使能（Luckfox 部分镜像默认只开调试串口 UART2，GitHub issue #303 也报告过 UART3/4 被禁用的问题）。解决办法：

1. **首选**：在 Luckfox 提供的系统里用 `luckfox-config` 工具开启 UART3（交互式菜单勾选 UART3）。
2. **或改设备树**：编辑内核设备树 `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`，找到 `uart3` 节点，将 `status = "disabled"` 改为 `"okay"`，并确认 `pinctrl-0` 指向 UART3 的引脚复用（`uart3m1_xfer` 对应 GPIO1_D0/D1），重新编译内核/设备树烧入。
3. **Ubuntu 系统**：若用板厂 Ubuntu 镜像，确认镜像已使能 UART3；未使能则需按 Luckfox 文档更新设备树。

> 排障命令：`dmesg | grep -i uart`、`ls /dev/ttyS*`、`cat /sys/kernel/debug/pinctrl/*/pinmux-pins`（查看引脚复用）。

---

## 4. C++ 集成方式（串口指令控制界面）

淘晶驰串口屏协议极简：**所有指令是 ASCII 字符串，以 `0xFF 0xFF 0xFF`（三个 0xFF）结尾**。C++ 端只需往 `/dev/ttyS3` 写格式化字符串即可控制界面；触摸按钮回传的数据从同一串口读取解析。

### 4.1 串口通信基础（termios 配置 115200 8N1）

```cpp
// touchscreen.h —— 串口屏封装（C++）
#pragma once
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

// HMI 颜色代号（RGB565 16 位值）
enum HmiColor {
    HMI_BLACK  = 0x0000,
    HMI_BLUE   = 0x001F,
    HMI_GREEN  = 0x07E0,   // 2016
    HMI_RED    = 0xF800,   // 63488
    HMI_YELLOW = 0xFFE0,   // 65504
    HMI_WHITE  = 0xFFFF,
    HMI_GRAY   = 0x8410,
};

class TouchScreen {
public:
    explicit TouchScreen(const char* dev = "/dev/ttyS3", int baud = 115200)
        : fd_(-1) {
        fd_ = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) { perror("open ttyS3"); return; }
        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        tcgetattr(fd_, &tty);
        cfsetospeed(&tty, B115200);
        cfsetispeed(&tty, B115200);
        tty.c_cflag &= ~PARENB;   // 无校验
        tty.c_cflag &= ~CSTOPB;   // 1 停止位
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |=  CS8;       // 8 数据位
        tty.c_cflag |=  (CLOCAL | CREAD);
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_oflag &= ~OPOST;
        tcsetattr(fd_, TCSANOW, &tty);
    }
    ~TouchScreen() { if (fd_ >= 0) close(fd_); }
    bool ok() const { return fd_ >= 0; }

    // 发送一条指令（自动追加 0xFF 0xFF 0xFF 结束符）
    void send(const char* fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        size_t n = strlen(buf);
        buf[n++] = 0xFF;
        buf[n++] = 0xFF;
        buf[n++] = 0xFF;
        write(fd_, buf, n);
    }

    // 控件文本：t0.txt="123"
    void setText(const char* ctrl, const char* text) {
        send("%s.txt=\"%s\"", ctrl, text);
    }
    // 数字控件：n0.val=123
    void setVal(const char* ctrl, int v) {
        send("%s.val=%d", ctrl, v);
    }
    // 页面跳转：page 1 / page main
    void setPage(const char* page) {
        send("page %s", page);
    }
    // 控件背景色/字体色：b0.bco=63488
    void setBco(const char* ctrl, int color) { send("%s.bco=%d", ctrl, color); }
    void setPco(const char* ctrl, int color) { send("%s.pco=%d", ctrl, color); }
    // 隐藏/显示：vis b0,0 / vis b0,1
    void setVis(const char* ctrl, int state) { send("vis %s,%d", ctrl, state); }
    // 触摸使能：tsw b0,1
    void setTouch(const char* ctrl, int state) { send("tsw %s,%d", ctrl, state); }

    // 读取并解析一帧触摸返回数据。
    // 返回 true 且 (page/ctrl/event) 有效时，表示串口屏回传了按键事件。
    // 返回 -1 表示串口暂时无数据。
    int readEvent(int* page, int* ctrl, int* event);

private:
    int fd_;
};
```

> 说明：`send()` 用 `vsnprintf` 生成指令体，再追加 `0xFF 0xFF 0xFF` 一次性写入串口，就是淘晶驰文档推荐的做法。

### 4.2 发送指令：改文本 / 背景色 / 页面 / 按钮状态

假设在上位机（USART HMI）里做了如下工程：

- **主页 page0**：文本控件 `t0`（等级大字）、`t1`（人数）、`t2`（FPS）、`t3`（IP）；按钮 `b0`（切到设置页）。
- **设置页 page1**：按钮 `b1`（自动/手动切换）、`b2`（静音）、`b3`（云台使能）、`b4`（返回主页）。

C++ 端控制：

```cpp
TouchScreen scr;
if (!scr.ok()) { /* 串口屏未就绪 */ }

// 1) 主页大字显示风险等级（红色背景、白字）
scr.setBco("t0", HMI_RED);       // DANGER 用红色
scr.setPco("t0", HMI_WHITE);
scr.setText("t0", "DANGER");

// 2) 人数 / FPS / IP
scr.setText("t1", "12");
scr.setText("t2", "18");
scr.setText("t3", "192.168.0.136");

// 3) 切到设置页
scr.setPage("1");                 // 等价 page 1

// 4) 设置按钮状态：静音按钮背景变黄表示已静音
scr.setBco("b2", HMI_YELLOW);
scr.setText("b2", "MUTE ON");

// 5) 隐藏/禁用某按钮
scr.setVis("b3", 0);            // 云台不可用时不显示
scr.setTouch("b3", 0);          // 禁用触摸
```

**等级颜色映射**（在决策线程里根据 `CrowdLevel` 选色）：

```cpp
void paintLevel(TouchScreen& scr, CrowdLevel lvl) {
    const char* name = RiskEngine::levelName(lvl);
    int color = HMI_GREEN;
    if (lvl == CROWD_WARNING) color = HMI_YELLOW;
    else if (lvl == CROWD_DANGER) color = HMI_RED;
    scr.setBco("t0", color);
    scr.setText("t0", name);
}
```

### 4.3 触摸回传解析（0x65 格式 / 自定义字符串）

**方式 A：解析官方 0x65 返回格式（推荐，通用）**

在上位机给每个按钮勾选 **“发送键值”**。触摸时串口屏返回：

```
0x65 + 页面ID + 按键ID + 触摸事件 + 0xFF 0xFF 0xFF
事件号：按下=0x01，弹起=0x00
```

例：`65 00 02 01 FF FF FF` = 页面 0、按钮 2、按下。

```cpp
// 触摸屏事件类型
enum { EVT_PRESS = 0x01, EVT_RELEASE = 0x00 };

// 读取并解析一帧 0x65 返回；无数据返回 -1
int TouchScreen::readEvent(int* page, int* ctrl, int* event) {
    uint8_t b;
    ssize_t r = read(fd_, &b, 1);
    if (r != 1) return -1;
    if (b != 0x65) return 0;             // 非按键事件，丢弃
    uint8_t buf[6];                        // 65 page ctrl event FF FF FF
    ssize_t got = 0;
    while (got < 6) {
        ssize_t n = read(fd_, buf + got, 6 - got);
        if (n <= 0) usleep(1000);
        else got += n;
    }
    *page  = buf[0];
    *ctrl  = buf[1];
    *event = buf[2];
    return 1;
}
```

**方式 B：自定义字符串回传（更可靠）**

在按钮“弹起事件”里写屏幕端脚本，用 `prints` 发送自定义标识：

```
// 按钮 b1（自动/手动）弹起事件代码：
prints "BTN_MODE",0

// 按钮 b2（静音）弹起事件：
prints "BTN_MUTE",0
```

C++ 端读串口，按字符串匹配（收到 `BTN_MODE` 则切换模式）。这种方式不依赖 0x65 的固件版本差异，推荐在实际开发中使用。

**主循环/线程中处理触摸事件：**

```cpp
int page, ctrl, evt;
if (scr.readEvent(&page, &ctrl, &evt) == 1 && evt == EVT_PRESS) {
    if (page == 0 && ctrl == 0) {          // 主页按钮 b0：进入设置页
        scr.setPage("1");
    } else if (page == 1) {
        if (ctrl == 1) toggleMode();      // 自动/手动
        else if (ctrl == 2) toggleMute(); // 静音
        else if (ctrl == 3) togglePtz();     // 云台使能
        else if (ctrl == 4) scr.setPage("0");
    }
}
```

### 4.4 显示内容布局（大字显示 + 触摸按钮翻页）

**主页 page0（480×272）**：

```
┌───────────────────────────────┐
│  Crowd: 12           (大字号)  │
│                              │
│  ██████ DANGER ██████        │  ← 风险等级大色块（红/黄/绿背景+白字）
│                              │
│  FPS: 18                     │  ← 帧率
│  IP: 192.168.0.136           │  ← IP 地址
│                              │
│        [ 设置 ]  (b0 按钮)      │
└───────────────────────────────┘
```

**设置页 page1**：

```
┌───────────────────────────────┐
│  模式: [自动]   (b1 按钮)     │
│  静音: [关]     (b2 按钮)      │
│  云台: [使能]   (b3 按钮)     │
│          [ 返回 ] (b4 按钮)     │
└───────────────────────────────┘
```

> 用上位机 USART HMI 排版：拖文本控件、按钮控件，导入大字库，把按钮“发送键值”勾上。背景色/字体色可通过第 4.2 节指令动态改。

---

## 5. 物理开关模块（保留）

物理开关继续沿用原 OLED 方案的 GPIO 方案，功能调整为 **模式切换 / 静音 / 云台使能** 三个。

### 5.1 开关功能与 GPIO 分配

| 开关 | 类型 | 功能 | 排针引脚 | GPIO 名 | GPIO 编号 | 接线 |
|------|------|------|---------|---------|-----------|------|
| 模式切换 | SPDT 滑动开关 | 自动 / 手动调参 | Pin 26 | GPIO2_A2 | 66 | 公共端接 3V3，输出接 GPIO；GPIO 对 GND 接 10kΩ 下拉 |
| 静音开关 | SPDT 滑动开关 | 抑制告警声音/蜂鸣器 | Pin 27 | GPIO2_A3 | 67 | 同上 |
| 云台使能 | SPDT 滑动开关 | 允许/禁止云台转动 | Pin 17 | GPIO2_B0 | 72 | 同上 |

- 拨到 3V3 → GPIO 读高（`1`）；断开 → 下拉电阻读低（`0`）。
- 模式开关：高=手动，低=自动；静音：高=静音；云台使能：高=允许云台。

### 5.2 C++ 读取 GPIO（轮询 + 消抖）

沿用原方案的 `GpioIn` 封装（sysfs + 软件消抖）：

```cpp
// gpio_in.h —— 轮询 + 多重采样消抖的输入 GPIO
#pragma once
#include <cstdio>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

class GpioIn {
public:
    GpioIn() : fd_(-1), last_(0), stable_(0), value_(0), activeLow_(false) {}
    ~GpioIn() { closePin(); }

    bool openPin(unsigned gpio, bool activeLow = false) {
        FILE* f = fopen("/sys/class/gpio/export", "w");
        if (!f) return false;
        fprintf(f, "%u", gpio); fclose(f);

        std::string dir = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction";
        f = fopen(dir.c_str(), "w");
        if (!f) return false;
        fprintf(f, "in"); fclose(f);

        std::string val = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/value";
        fd_ = open(val.c_str(), O_RDONLY);
        activeLow_ = activeLow;
        return fd_ >= 0;
    }
    void closePin() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

    // 采样一次；连续 3 次一致才更新（消抖）。调用间隔建议 10~20ms。
    int sample() {
        if (fd_ < 0) return -1;
        char c = 0;
        if (lseek(fd_, 0, SEEK_SET) < 0) return -1;
        if (read(fd_, &c, 1) != 1) return -1;
        int raw = (c == '1') ? 1 : 0;
        if (activeLow_) raw = 1 - raw;
        if (raw == last_) {
            if (++stable_ >= 3) value_ = raw;
        } else {
            last_ = raw;
            stable_ = 0;
        }
        return value_;
    }
private:
    int fd_, last_, stable_, value_;
    bool activeLow_;
};
```

**外设线程读取开关并联动串口屏**：

```cpp
// 外设线程（2~5Hz 刷新屏幕 + 读开关）
GpioIn modeGpio, muteGpio, ptzGpio;
modeGpio.openPin(66);
muteGpio.openPin(67);
ptzGpio.openPin(72);

TouchScreen scr;
while (running) {
    int mode = modeGpio.sample();
    int mute = muteGpio.sample();
    int ptz  = ptzGpio.sample();

    // 同步到串口屏设置页按钮状态
    scr.setBco("b1", mode ? HMI_YELLOW : HMI_GRAY);
    scr.setText("b1", mode ? "MANUAL" : "AUTO");
    scr.setBco("b2", mute ? HMI_YELLOW : HMI_GRAY);
    scr.setText("b2", mute ? "MUTE ON" : "MUTE OFF");
    scr.setBco("b3", ptz  ? HMI_GREEN  : HMI_GRAY);
    scr.setText("b3", ptz  ? "PTZ ON"  : "PTZ OFF");

    usleep(200 * 1000);   // 5Hz
}
```

> 若开关状态变化，可把 `mode/mute/ptz` 写入共享原子变量，供决策/输出线程读取（例如静音开关直接门控蜂鸣器）。

---

## 6. 接线图、成本与注意事项

### 6.1 完整接线表

| 外设 | 信号 | 接开发板 | 说明 |
|------|------|----------|------|
| 串口屏 VCC | 5V | 外部 5V 电源或 USB 5V 分线 | 串口屏一般 5V 供电（300~500mA），**不可接 3V3** |
| 串口屏 GND | GND | Pin 38（GND） | 与开发板**共地** |
| 串口屏 RX | UART3 TX | Pin 19（GPIO1_D0=UART3_TX_M1） | 板 TX → 屏 RX（**交叉**） |
| 串口屏 TX | UART3 RX | Pin 20（GPIO1_D1=UART3_RX_M1） | 板 RX ← 屏 TX（**交叉**） |
| 模式开关 | GPIO | Pin 26（GPIO2_A2=66） | 公共端 3V3，10kΩ 下拉 |
| 静音开关 | GPIO | Pin 27（GPIO2_A3=67） | 公共端 3V3，10kΩ 下拉 |
| 云台使能 | GPIO | Pin 17（GPIO2_B0=72） | 公共端 3V3，10kΩ 下拉 |
| 电源开关 | 串联 | USB 5V 红线 | 断开即整机断电（含串口屏） |

**串口屏 5V 供电**：开发板排针无 5V 输出，推荐
- 用一根 USB 线从 5V 电源/充电宝引出 5V，经电源开关后分给**开发板 Type-C** 和**串口屏 VCC**（共地）；
- 或给串口屏配独立 5V 电源适配器，GND 与开发板相连。

### 6.2 成本估算

| 器件 | 单价 | 数量 | 小计 |
|------|------|------|------|
| 4.3 寸 480×272 电阻串口屏（TJC4827X343 等） | ¥80 | 1 | ¥80 |
| SPDT 滑动开关（模式/静音/云台） | ¥0.8 | 3 | ¥2.4 |
| SPST 滑动开关（电源） | ¥1.5 | 1 | ¥1.5 |
| 10kΩ 电阻 | ¥0.1 | 3 | ¥0.3 |
| 5V USB 供电线/母座、杜邦线 | ¥8 | 1套 | ¥8 |
| **合计** | | | **约 ¥95** |

> 若换 5 寸约 ¥120、7 寸约 ¥180，总成本相应增加。**推荐 4.3 寸够用档，总预算控制在 ¥100 内。**

### 6.3 注意事项

1. **电平匹配**：串口屏 UART 逻辑电平多为 3.3V/5V TTL 兼容，Luckfox 的 UART 是 3.3V TTL。**务必确认所选型号支持 3.3V TTL 输入**，若仅支持 5V 需加电平转换模块（或串口屏选 3.3V 供电版本）。
2. **供电与共地**：串口屏 5V 供电，GND 必须与开发板相连，否则串口通信不稳定甚至损坏。
3. **UART 使能**：先确认 `/dev/ttyS3` 存在；不存在则按 3.3 节启用 UART3。
4. **波特率一致**：上位机工程（USART HMI 的 Program.s）里设 `bauds=115200`，与 C++ 端一致。
5. **编码一致**：淘晶驰默认 GB2312；若 C++ 发中文，需保证编码一致（推荐界面用英文/数字，避免乱码）。
6. **指令结束符**：每条指令必须以 `0xFF 0xFF 0xFF` 结尾，漏掉则屏幕不响应。
7. **不要在推理主循环里发串口指令**：主循环只做推理；屏幕刷新放到 2~5Hz 外设线程。
8. **回传解析**：优先用“发送键值”的 0x65 格式，或自定义 `prints` 字符串；注意按帧（以 3 个 0xFF 为边界）解析，避免半包。
9. **物理开关消抖**：GPIO 输入必须加下拉/上拉电阻 + 软件多重采样消抖，否则拨动瞬间抖动会导致误触发。

---

## 7. 实施步骤与验收

### 实施步骤

1. **选型下单**：按第 2 节买 4.3 寸电阻串口屏（够用档）。
2. **上位机建工程**：用 USART HMI 建工程，型号选对应屏；做 page0（主页大字）与 page1（设置页）；按钮勾选“发送键值”；`bauds=115200`；下载到屏。
3. **接线**：按第 6.1 节接 UART3 与 5V 供电。
4. **验证 UART3**：`ls /dev/ttyS*`、`stty -F /dev/ttyS3`；用串口助手/`echo` 先发一条 `page 1\xff\xff\xff` 测试屏幕跳页。
5. **C++ 集成**：实现 `TouchScreen` 类，在 `main.cc` 启动外设线程：读 GPIO 开关 + 组装人数/等级/FPS/IP → 发串口屏指令。
6. **触摸联动**：解析 0x65 回传，实现按钮切换页面/模式/静音/云台。
7. **联调**：真人入镜，屏幕实时显示人数/等级（红黄绿）/FPS/IP；触摸切页、拨动开关验证功能。
8. **论文素材**：串口屏实拍照片、USART HMI 工程截图、串口屏返回波形/日志、开关消抖前后对比。

### 验收标准

- [ ] 串口屏以 2~5Hz 刷新人数/等级/FPS/IP，与 RTSP 画面一致。
- [ ] 等级用红/黄/绿背景大字显示，切换正确。
- [ ] 触摸按钮可切换主页/设置页；设置页按钮可改模式/静音/云台状态。
- [ ] 物理开关（模式/静音/云台）状态能反映到屏幕按钮颜色与系统行为。
- [ ] 电源开关断开整机断电（含串口屏）。
- [ ] 接入外设后推理帧率下降 ≤ 1~2 FPS。

---

## 附录：参考资料

- 淘晶驰串口屏资料中心（指令集/控件/返回格式）：http://wiki.tjc1688.com/
- 淘晶驰官方：https://www.tjc1688.com/
- 淘晶驰指令集（命令速查）：`page`、`prints`、`printh`、`vis`、`tsw`、`click`、`get`、`sendme`；返回格式 `0x65/0x66/0x70/0x71`。
- Luckfox Wiki UART（Pico Pro/Max）：https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/UART/
- Luckfox UART 使能问题讨论：https://github.com/LuckfoxTECH/luckfox-pico/issues/303
- 原 OLED 方案（备用/历史参考）：`docs/hardware/oled_switch.md`