# OLED 显示屏（SSD1306 I2C）+ 物理开关模块设计方案

> 项目：《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统设计》
> 目标板：Luckfox Pico Max（RV1106G3，256MB DDR3L，NPU 1 TOPS，单核 Cortex-A7 1.2GHz）
> 适用源码：`src/luckfox_rtsp_yolov5/`（RKNN YOLOv5 行人检测 + 复杂网络风险决策 + RTSP 推流）
> 文档日期：2026-10-25

---

## 1. 需求与目标

在现有视觉检测/风险决策系统基础上，为设备增加**人机交互外设**：

1. **OLED 显示屏**：实时显示
 - 当前检测到的行人数（`n`）
 - 拥挤风险等级（SAFE / WARNING / DANGER）
 - 推理帧率（FPS）
 - 本机 IP 地址
2. **物理开关**：
 - 电源开关（切断整机供电）
 - 模式切换开关（如 自动 / 手动调参 两种模式）
 - 静音开关（抑制告警蜂鸣器/声音输出）
 - 复位开关（硬件复位 / 软件复位风险状态）

设计约束：**不能拖慢现有推理与 RTSP 推流链路**，OLED 与开关读取必须放在低优先级、低频次的独立线程中（单核 A7 上以 2~5Hz 刷新即可）。

---

## 2. OLED 选型与硬件接口

### 2.1 选型：0.96 寸 SSD1306 I2C 128×64

| 参数 | 值 | 说明 |
|------|----|----|
| 驱动 IC | SSD1306 | 单色 OLED 最主流驱动，资料极多 |
| 分辨率 | 128×64 | 8 页 × 128 列，每字节 8 个纵向像素 |
| 接口 | I2C（4 线：VCC/GND/SDA/SCL） | 只占 2 个 GPIO，比 SPI 省线 |
| 供电 | 3.3V（板载稳压） | 与 Luckfox 的 3V3 电平兼容 |
| 从机地址 | 0x3C（SA0 接地，默认） | 改 SA0 接 VCC 则 0x3D |
| 市场价 | 约 ¥8~15 | 淘宝搜 “OLED 0.96 SSD1306 I2C” |
| 字体/图形 | 需要库（u8g2）或自绘字库 | 见第 3 节 |

**推荐型号**：带 4 脚（VCC、GND、SCL、SDA）的 0.96 寸 I2C 版本。若希望分辨率更高可换 1.3 寸 SH1106（引脚兼容，仅初始化略有差异），但本项目 128×64 已足够显示四行信息。

### 2.2 Luckfox Pico Max 的 I2C 接口与引脚

Luckfox Pico Max 的 40P 排针布局参考 Raspberry Pi Pico。官方出厂镜像**默认使能 SPI0 与 I2C3**，I2C3 可直接使用（本系统用 I2C3）。

**关键引脚定义**（依据 Luckfox 官方 GPIO/原理图）：

| 物理引脚 | 引脚名 | 复用功能 | GPIO 编号 |
|---------|--------|---------|-----------|
| Pin 29 | GPIO2_A6_d | **I2C3_SCL_M0**（时钟） | gpio 70 |
| Pin 34 | GPIO2_A7_d | **I2C3_SDA_M0**（数据） | gpio 71 |
| Pin 36 | 3V3(OUT) | 3.3V 电源输出 | - |
| Pin 38 | GND | 地 | - |
| Pin 30 | RESET | 复位（低有效） | - |

> GPIO 编号计算：`pin = bank*32 + (group*8 + X)`，其中 group：A=0、B=1、C=2、D=3。
> 例：GPIO2_A6 = 2*32 + (0*8+6) = 70；GPIO2_A7 = 2*32 + 7 = 71。

I2C 总线设备节点为 **`/dev/i2c-3`**（内核按适配器序号编号）。系统已有 `i2c-4` 被 PMIC（0x30）占用，**外部 I2C 设备请接 I2C3**。

### 2.3 OLED 接线表

| OLED 引脚 | 接开发板 | 说明 |
|-----------|---------|------|
| VCC | Pin 36（3V3） | 3.3V 电源 |
| GND | Pin 38（GND） | 地 |
| SCL | Pin 29（GPIO2_A6 / I2C3_SCL） | I2C 时钟 |
| SDA | Pin 34（GPIO2_A7 / I2C3_SDA） | I2C 数据 |

**上拉电阻**：SSD1306 模组 PCB 上通常已带 4.7kΩ 上拉。若 `i2cdetect` 扫不到设备，在 SDA/SCL 各接一只 4.7kΩ 到 3V3（见注意事项）。

---

## 3. C++ 驱动 SSD1306 方案

### 3.1 方案对比

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| **A. 移植 u8g2（推荐）** | 成熟、支持中文/多字体、绘图原语（线/框/圆）、跨平台、C/C++ 接口、资料多 | 需把库源码加入工程；需写一个 Linux I2C 的字节回调（约 40 行） | **首选**，可引用开源库并写明移植方法 |
| B. 移植 u8glib（旧版） | 与 u8g2 同源 | 已停止维护，建议直接用 u8g2 | 不推荐 |
| C. 手写 SSD1306 I2C 驱动 | 代码少、完全自主可控、**时更能体现“自己实现”**；可作“外设驱动设计”小节 | 需自备字库（ASCII/中文）、无绘图原语 | 可作为**方案 A 的补充**（素材）或备选 |

**推荐：方案 A（u8g2）+ 自研约 40 行 Linux I2C 回调**。理由：
- u8g2 是 SSD1306 事实标准库，含 `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` 现成构造器与多款字体；
- 我们只需实现 `u8x8_byte_linux_i2c`（基于 `/dev/i2c-N` + `ioctl`），不依赖任何第三方 HAL，交叉编译零额外依赖；
- 显示中文可加载 `u8g2_font_wqy12_t_gb2312` 等字库，但会显著增大固件体积，**本方案推荐用英文标签**。

同时，第 3.3 节给出一个**完整可运行的“手写最小驱动”示例**（含 5×7 字库），既可作为备选，也可直接用于源码/演示。

### 3.2 方案 A：移植 u8g2 + Linux I2C 回调（推荐）

#### 3.2.1 获取源码

把官方 u8g2 克隆进工程（只需 `csrc/` 与 `cppsrc/`）：

```bash
git clone https://github.com/olikraus/u8g2.git
# 或直接拷贝 csrc/ 与 cppsrc/ 到 src/luckfox_rtsp_yolov5/3rdparty/u8g2/
```

#### 3.2.2 自研 Linux I2C 字节回调（新增 `u8x8_linux_i2c.h/.c`）

u8g2 通过 `u8x8_byte_cb` 回调收发字节。我们实现一个基于 `/dev/i2c-N` 的回调，把整帧缓冲通过一次 `write` 发出（SSD1306 支持连续数据块）。

```c
/* u8x8_linux_i2c.h */
#ifndef U8X8_LINUX_I2C_H
#define U8X8_LINUX_I2C_H
#include "u8x8.h"
uint8_t u8x8_byte_linux_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay_linux(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
#endif
```

```c
/* u8x8_linux_i2c.c */
#include "u8x8_linux_i2c.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_BUS_PATH "/dev/i2c-3" /* Luckfox Pico Max 的 I2C3 */

static int g_i2c_fd = -1;
static uint8_t g_buf[1024]; /* 128x64 全帧缓冲 */
static int g_buf_len = 0;

uint8_t u8x8_byte_linux_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
 switch (msg) {
 case U8X8_MSG_BYTE_INIT: /* 打开 /dev/i2c-N 并设置从机地址 */
 if (g_i2c_fd < 0) {
 g_i2c_fd = open(I2C_BUS_PATH, O_RDWR);
 if (g_i2c_fd < 0) { perror("open /dev/i2c-3"); return 0; }
 if (ioctl(g_i2c_fd, I2C_SLAVE, 0x3C) < 0) { perror("I2C_SLAVE"); return 0; }
 }
 return 1;
 case U8X8_MSG_BYTE_SEND: /* 收集本帧数据 */
 memcpy(g_buf + g_buf_len, arg_ptr, arg_int);
 g_buf_len += arg_int;
 return 1;
 case U8X8_MSG_BYTE_START_TRANSFER:/* 新事务开始 */
 g_buf_len = 0;
 return 1;
 case U8X8_MSG_BYTE_END_TRANSFER: /* 一次性写整帧 */
 if (write(g_i2c_fd, g_buf, g_buf_len) != g_buf_len) return 0;
 return 1;
 default:
 return 0;
 }
}

/* 用不到 GPIO/延时，直接返回成功即可（HW I2C 不依赖软件时序） */
uint8_t u8x8_gpio_and_delay_linux(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
 return 1;
}
```

#### 3.2.3 C++ 用法（`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`）

由于我们自研回调，不使用 u8g2 预置的 `HW_I2C` 构造器（那面向 MCU HAL），而是用 `u8g2_Setup_ssd1306_i2c_128x64_noname_f` 显式指定我们的回调：

```cpp
// oled.h —— OLED 封装（C++）
#pragma once
#include <U8g2lib.h>

class Oled {
public:
 bool begin {
 u8g2_Setup_ssd1306_i2c_128x64_noname_f(
 &u8g2, U8G2_R0,
 u8x8_byte_linux_i2c, /* 自研 Linux I2C 回调 */
 u8x8_gpio_and_delay_linux);
 u8g2_InitDisplay(&u8g2);
 u8g2_SetPowerSave(&u8g2, 0); /* 0 = 点亮 */
 u8g2_ClearBuffer(&u8g2);
 return true;
 }

 // 每帧数据更新入口（约 2~5Hz 调用）
 void update(int personCount, const char* level,
 int fps, const char* ip, const char* mode, bool mute) {
 u8g2_ClearBuffer(&u8g2);
 u8g2_SetFont(&u8g2, u8g2_font_6x13_tr);

 u8g2_DrawStr(&u8g2, 0, 12, "Crowd:");
 char buf[8]; snprintf(buf, sizeof(buf), "%d", personCount);
 u8g2_DrawStr(&u8g2, 48, 12, buf);

 // 等级：用反色块突出
 u8g2_DrawBox(&u8g2, 0, 18, 128, 15);
 u8g2_SetDrawColor(&u8g2, 0); /* 反色文字 */
 u8g2_DrawStr(&u8g2, 4, 30, level); /* SAFE/WARNING/DANGER */
 u8g2_SetDrawColor(&u8g2, 1);

 u8g2_DrawStr(&u8g2, 0, 45, "FPS:");
 snprintf(buf, sizeof(buf), "%d %s", fps, mode);
 u8g2_DrawStr(&u8g2, 36, 45, buf);

 u8g2_DrawStr(&u8g2, 0, 60, ip); /* IP:192.168.x.x */
 if (mute) u8g2_DrawStr(&u8g2, 100, 60, "MUT");

 u8g2_SendBuffer(&u8g2); /* 整帧刷屏 */
 }
private:
 U8G2 u8g2;
};
```

> 说明：`u8g2_Setup_ssd1306_i2c_128x64_noname_f` 的 `_f` 表示 full buffer（内部 1KB 缓冲，`SendBuffer` 一次发完），`_1`/`_2` 为分页缓冲、省内存但要多刷几次。256MB 内存下用 `_f` 最简单。

### 3.3 方案 C：手写最小 SSD1306 驱动（简单示例，可运行）

若不引库，可直接用 Linux I2C 字符设备驱动 SSD1306。SSD1306 I2C 协议极简：
- 每字节前跟一个**控制字节**：`0x00` 表示后续为命令流，`0x40` 表示数据流。
- 初始化只需发送少量命令（关闭显示→设置→清屏→开启显示）。

#### 3.3.1 驱动类（`ssd1306.h/.cpp`）

```cpp
// ssd1306.h
#pragma once
#include <cstdint>
#include <cstddef>

class SSD1306 {
public:
 explicit SSD1306(int bus = 3, uint8_t addr = 0x3C);
 ~SSD1306;
 bool begin; // 打开 /dev/i2c-N，发送初始化序列
 void clear; // 清空内部缓冲
 void setPixel(int x, int y, bool on); // 写一像素到缓冲
 void drawChar(char c, int x, int y); // 画 5x7 字符（左上角坐标）
 void drawText(const char* s, int x, int y);
 void display; // 整帧上屏
private:
 bool writeCmd(uint8_t cmd);
 bool writeData(const uint8_t* data, size_t len);
 int fd_;
 int bus_;
 uint8_t addr_;
 uint8_t fb_[128 * 64 / 8]; // 1KB 帧缓冲
};
```

```cpp
// ssd1306.cpp
#include "ssd1306.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// 5x7 字库（仅收录仪表盘用到的字符，完整字库可抄 Adafruit GFX font5x7）
static const uint8_t font5x7[][5] = {
 {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
 {0x00,0x08,0x08,0x08,0x00}, /* '-' */
 {0x00,0x60,0x60,0x00,0x00}, /* '.' */
 {0x00,0x20,0x10,0x08,0x04}, /* '/' */
 {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
 {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
 {0x42,0x61,0x51,0x49,0x46}, /* '2' */
 {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
 {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
 {0x27,0x45,0x45,0x45,0x39}, /* '5' */
 {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
 {0x01,0x71,0x09,0x05,0x03}, /* '7' */
 {0x36,0x49,0x49,0x49,0x36}, /* '8' */
 {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
 {0x00,0x36,0x36,0x00,0x00}, /* ':' */
 {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
 {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
 {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
 {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
 {0x3E,0x41,0x41,0x51,0x32}, /* 'G' */
 {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
 {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
 {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
 {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
 {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
 {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
 {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
 {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
 {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
 {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
 {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
};
#define FONT_FIRST ' ' /* 字库表从空格开始 */

SSD1306::SSD1306(int bus, uint8_t addr) : fd_(-1), bus_(bus), addr_(addr) {
 memset(fb_, 0, sizeof(fb_));
}

SSD1306::~SSD1306 { if (fd_ >= 0) close(fd_); }

bool SSD1306::begin {
 char path[32];
 snprintf(path, sizeof(path), "/dev/i2c-%d", bus_);
 fd_ = open(path, O_RDWR);
 if (fd_ < 0) { perror("open i2c"); return false; }
 if (ioctl(fd_, I2C_SLAVE, addr_) < 0) { perror("I2C_SLAVE"); return false; }

 static const uint8_t init[] = {
 0xAE, /* display off */
 0x20, 0x00, /* 水平寻址模式 */
 0xB0, /* 页地址 = 0 */
 0xC8, /* COM 扫描方向（正常） */
 0x00, 0x10, /* 列地址 0 */
 0x40, /* 起始行 = 0 */
 0x81, 0xCF, /* 对比度 = 0xCF */
 0xA1, /* 段重映射 */
 0xA6, /* 正常显示（非反显） */
 0xA8, 0x3F, /* 多路复用比 1/64 */
 0xD3, 0x00, /* 显示偏移 0 */
 0xD5, 0x80, /* 时钟分频 */
 0xD9, 0xF1, /* 预充电周期 */
 0xDA, 0x12, /* COM 引脚配置 */
 0xDB, 0x40, /* VCOMH 电平 */
 0x8D, 0x14, /* 电荷泵使能 */
 0xAF /* display on */
 };
 for (size_t i = 0; i < sizeof(init); i++)
 if (!writeCmd(init[i])) return false;
 clear;
 display;
 return true;
}

void SSD1306::clear { memset(fb_, 0, sizeof(fb_)); }

void SSD1306::setPixel(int x, int y, bool on) {
 if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
 int byte = y / 8, bit = y % 8;
 if (on) fb_[byte * 128 + x] |= (1 << bit);
 else fb_[byte * 128 + x] &= ~(1 << bit);
}

void SSD1306::drawChar(char c, int x, int y) {
 int idx = c - FONT_FIRST;
 if (idx < 0 || idx >= (int)(sizeof(font5x7)/sizeof(font5x7[0]))) return;
 for (int row = 0; row < 7; row++)
 for (int col = 0; col < 5; col++)
 if (font5x7[idx][col] & (1 << row))
 setPixel(x + col, y + row, true);
}

void SSD1306::drawText(const char* s, int x, int y) {
 while (*s) { drawChar(*s++, x, y); x += 6; } /* 5 像素 + 1 间距 */
}

void SSD1306::display {
 for (int page = 0; page < 8; page++) {
 writeCmd(0xB0 + page); /* 页地址 */
 writeCmd(0x00); writeCmd(0x10); /* 列地址 0 */
 writeData(fb_ + page * 128, 128);
 }
}

bool SSD1306::writeCmd(uint8_t cmd) {
 uint8_t buf[2] = { 0x00, cmd }; /* 控制字节 0x00 = 命令 */
 return write(fd_, buf, 2) == 2;
}

bool SSD1306::writeData(const uint8_t* data, size_t len) {
 uint8_t hdr = 0x40; /* 控制字节 0x40 = 数据 */
 // 简单实现：逐字节发；也可先发 0x40 再 write 整块（更高效）
 for (size_t i = 0; i < len; i++) {
 uint8_t buf[2] = { hdr, data[i] };
 if (write(fd_, buf, 2) != 2) return false;
 }
 return true;
}
```

> 上述 5×7 字库只覆盖仪表盘所需字符（` -./0-9:ADEFGIKMNOPRSTUW`）。要显示完整 ASCII，直接替换为 Adafruit GFX 的 `font5x7` 全表（公开 MIT 许可）。

#### 3.3.3 使用示例

```cpp
#include "ssd1306.h"
int main {
 SSD1306 oled(3, 0x3C); // /dev/i2c-3, 地址 0x3C
 if (!oled.begin) return -1;
 oled.drawText("N:12", 0, 0);
 oled.drawText("DANGER", 0, 10);
 oled.drawText("FPS:18", 0, 24);
 oled.drawText("192.168.0.136", 0, 38);
 oled.display;
 return 0;
}
```

---

## 4. OLED 显示内容布局

### 4.1 分辨率与字体约束

- 分辨率 128×64，即 **16 个 8×8 字符 × 8 行**。
- 用 u8g2 的 `u8g2_font_6x13_tr`（6×13 像素）：每行约 **20 字符**，64/13 ≈ **4 行**。
- 用 `u8g2_font_10x20_tr`（10×20）：每行 12 字符，3 行——适合做等级大字。
- 中文（`u8g2_font_wqy12_t_gb2312`）每个字 12×12，会显著增大固件并减少单行字数，**建议用英文**。

### 4.2 一屏布局（推荐）

```
┌────────────────────────────────┐
│ Crowd:12 ← 第 1 行 人数 │
│ ████████████████████ │ ← 等级反色横条（2 行高）
│ ██ DANGER ████████████ │ ← 反白大字等级
│ FPS:18 AUTO │ ← 帧率 + 模式
│ IP:192.168.0.136 MUT │ ← IP + 静音标记
└────────────────────────────────┘
```

- **等级配色（伪色）**：单色 OLED 无法真正显示红/黄/绿，用**反色块 + 文字**表达：
 - SAFE：普通白字；
 - WARNING：等级文字反白 + 细边框；
 - DANGER：整条**反色横条**（黑底白字/白底黑字反相），视觉最醒目。
- **刷新频率**：2~5Hz（每 200~500ms 刷一帧）。不要在推理主循环里每帧刷屏。

### 4.3 数据来源

| 显示项 | 数据来源 | 获取方式 |
|-------|---------|---------|
| 人数 `n` | `riskEngine.features.n` | 决策线程/显示线程读取共享结构 |
| 等级 | `crowd`（`CrowdLevel`） | `riskEngine.levelName` 返回 `"SAFE"/"WARNING"/"DANGER"` |
| FPS | 推理帧计数 | 主循环每处理一帧 `g_frameCount++`（原子变量），显示线程按 1 秒窗口差分 |
| IP | `eth0` 接口 | `ioctl(SIOCGIFADDR)` |
| 模式 | 开关状态 | 读模式切换 GPIO |
| 静音 | 开关状态 | 读静音 GPIO，显示 `MUT` |

**获取本机 IP 的辅助函数**：

```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <cstring>
#include <string>

std::string getIpAddress(const char* ifname = "eth0") {
 int fd = socket(AF_INET, SOCK_DGRAM, 0);
 struct ifreq ifr;
 memset(&ifr, 0, sizeof(ifr));
 strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
 if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) { close(fd); return "0.0.0.0"; }
 close(fd);
 return inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}
```

**FPS 计数**（配合显示线程）：

```cpp
static std::atomic<uint64_t> g_frameCount{0}; // 主循环每帧 ++
// 显示线程中每 1 秒：
static uint64_t last = g_frameCount.load;
int fps = (int)(g_frameCount.load - last); // 若按 1s 窗口
last = g_frameCount.load;
```

---

## 5. 开关模块

### 5.1 开关类型与功能定义

| 开关 | 类型 | 功能 | 建议器件 |
|------|------|------|---------|
| 电源开关 | SPST 滑动/船型 | 切断整机 5V 供电 | SS22D07 滑动开关或船型开关 |
| 模式切换 | SPDT 滑动 | 自动模式 / 手动调参模式 | SS12D00 三脚滑动开关 |
| 静音开关 | SPDT 滑动 | 抑制告警蜂鸣器/声音 | SS12D00 三脚滑动开关 |
| 复位 | 轻触按键 | 硬件复位整板（低有效） | 6×6 轻触按键 |

### 5.2 GPIO 选型与接线

选择排针上**空闲、未复用**的 GPIO：

| 功能 | 引脚 | GPIO 名 | GPIO 编号 | 接线 |
|------|------|---------|-----------|------|
| 模式切换 | Pin 26 | GPIO2_A2_d | 66 | 开关一端接 3V3，一端接 GPIO；GPIO 对 GND 接 10kΩ 下拉 |
| 静音开关 | Pin 27 | GPIO2_A3_d | 67 | 同上 |
| 复位（软件） | Pin 17 | GPIO2_B0_d | 72 | 轻触键一端接 GPIO，一端接 GND；GPIO 对 3V3 接 10kΩ 上拉 |
| 复位（硬件） | Pin 30 | RESET | - | 轻触键一端接 RESET，一端接 GND（下拉即复位） |

**接线逻辑**：
- **模式/静音开关**：拨到 3V3 → GPIO 读到高（`1`）；拨到悬空/断开 → 下拉电阻使 GPIO 读低（`0`）。模式开关高=手动调参、低=自动；静音开关高=静音、低=正常告警。
- **软件复位按键**：按键按下 → GPIO 读到低（`0`），触发程序重置 RiskEngine 状态（清跟踪/滞回/计数器，重载配置）；默认上拉为高（`1`）。

> 若想省两个下拉电阻，可用开发板内部的**上/下拉**：Luckfox sysfs 支持通过 `device` 节点配置，但最简单可靠的做法仍是外接 10kΩ 电阻。

### 5.3 C++ 读取 GPIO（sysfs 轮询 + 软件消抖）

Linux sysfs GPIO 在 kernel 5.10 上可用。封装一个 `GpioIn` 类：导出 → 设输入 → 读值；并实现**多重采样消抖**（连续 N 次采样一致才更新状态）。

```cpp
// gpio_in.h —— 轮询 + 消抖的输入 GPIO 封装
#pragma once
#include <cstdint>
#include <string>
#include <cstdio>

class GpioIn {
public:
 GpioIn : fd_(-1), last_(0), stable_(0), value_(0) {}
 ~GpioIn { close; }

 bool openPin(unsigned gpio, bool activeLow = false) {
 // 导出
 FILE* f = fopen("/sys/class/gpio/export", "w");
 if (!f) return false;
 fprintf(f, "%u", gpio); fclose(f);

 std::string dir = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/direction";
 f = fopen(dir.c_str, "w");
 if (!f) return false;
 fprintf(f, "in"); fclose(f);

 std::string val = "/sys/class/gpio/gpio" + std::to_string(gpio) + "/value";
 fd_ = open(val.c_str, O_RDONLY); // 需要 #include <fcntl.h> <unistd.h>
 activeLow_ = activeLow;
 return fd_ >= 0;
 }

 void close { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

 // 轮询一次；返回消抖后的稳定电平（0/1）。采样间隔由调用方控制（建议 10~20ms）
 int sample {
 if (fd_ < 0) return -1;
 char c = 0;
 if (lseek(fd_, 0, SEEK_SET) < 0) return -1;
 if (read(fd_, &c, 1) != 1) return -1;
 int raw = (c == '1') ? 1 : 0;
 if (activeLow_) raw = 1 - raw;

 if (raw == last_) {
 if (++stable_ >= 3) value_ = raw; /* 连续 3 次一致才更新（约 30~60ms 消抖） */
 } else {
 last_ = raw;
 stable_ = 0;
 }
 return value_;
 }
private:
 int fd_;
 int last_, stable_, value_;
 bool activeLow_;
};
```

**轮询线程**（低频，约 20ms 一次）：

```cpp
// 在显示/外设线程中：
GpioIn modeGpio, muteGpio, resetGpio;
modeGpio.openPin(66); // 模式切换
muteGpio.openPin(67); // 静音
resetGpio.openPin(72); // 软件复位

while (running) {
 int mode = modeGpio.sample;
 int mute = muteGpio.sample;
 if (resetGpio.sample == 0) riskEngine.reset; // 软件复位

 oled.update(personCount, levelName, fps, ip.c_str,
 mode ? "MANUAL" : "AUTO", mute == 1);
 usleep(200 * 1000); // 5Hz
}
```

**中断（edge）方式（可选）**：若不想轮询，可对 GPIO 设 `echo both > /sys/class/gpio/gpioN/edge`，再用 `poll` 监听 `value` 文件描述符的可读事件，实现“边沿中断”。但对 3 个开关、5Hz 刷新场景，轮询已足够且更简单，单核 A7 上 CPU 开销可忽略。

### 5.4 电源开关

Luckfox Pico Max 通过 **USB Type-C（5V）供电**，排针没有 5V 直通脚。最简单可靠的方案：

1. **串联滑动开关**：在 USB 电源线的 **5V（红线）** 上串一只 SPST 滑动/船型开关。开关断开即整机断电。可用成品“USB 电源开关线”或自己焊接。
2. **软开关（进阶）**：若想做成“按键开机/长按关机”，需加 PMOS 自锁电路（类似树莓派电源管理），复杂度高，不必须。

> 注意：OLED 由 3V3 供电，**随主板一起断电**，无需单独电源开关。物理开关只控制整机供电。

---

## 6. 软件架构集成（线程）

现有主循环是单线程流水线（取帧→推理→风险→画框→编码→RTSP）。为不影响帧率，**新增一个低频“外设线程”**：

```
采集线程 ──► 推理线程 ──► 风险决策(1~5Hz) ──► 外设线程(2~5Hz) ──► OLED + 开关
 │
 读 GPIO（轮询+消抖）
```

- 主循环每处理一帧 `g_frameCount++`（`std::atomic`），并把 `personCount / crowd / levelName` 写入共享结构（或直接用原子/互斥量保护）。
- **外设线程**每 200~500ms：
 1. 读三个开关（消抖）；
 2. 计算 FPS（按 1 秒窗口差分）；
 3. 取 IP（启动时查一次即可）；
 4. 刷新 OLED；
 5. 根据静音/模式开关控制告警输出（蜂鸣器/LED/串口）。
- **线程安全**：`g_frameCount` 用原子；`RiskEngine` 状态若被软件复位按键修改，需加锁或仅在决策线程空闲时处理。

> 设计文档原写“线程≤4”，这里新增的外设线程绝大部分时间在 `usleep`，对单核几乎无影响。若担心，可把开关/OLED 并入现有“决策线程”中（决策线程本就是 1~5Hz）。

---

## 7. 接线汇总、成本与注意事项

### 7.1 完整接线表

| 外设 | 信号 | 接开发板引脚 | 说明 |
|------|------|-------------|------|
| OLED | VCC | Pin 36 (3V3) | |
| OLED | GND | Pin 38 | |
| OLED | SCL | Pin 29 (GPIO2_A6/I2C3_SCL) | 若扫不到加 4.7k 上拉到 3V3 |
| OLED | SDA | Pin 34 (GPIO2_A7/I2C3_SDA) | 同上 |
| 模式开关 | 公共端 | Pin 36 (3V3) | SPDT 滑动开关 |
| 模式开关 | 输出 | Pin 26 (GPIO2_A2=66) | 加 10k 下拉到 GND |
| 静音开关 | 公共端 | Pin 36 (3V3) | SPDT 滑动开关 |
| 静音开关 | 输出 | Pin 27 (GPIO2_A3=67) | 加 10k 下拉到 GND |
| 软件复位键 | 一端 | Pin 17 (GPIO2_B0=72) | 加 10k 上拉到 3V3 |
| 软件复位键 | 另一端 | Pin 38 (GND) | 按下为低 |
| 硬件复位键 | 一端 | Pin 30 (RESET) | 按下整板复位 |
| 硬件复位键 | 另一端 | Pin 38 (GND) | |
| 电源开关 | 串联 | USB 5V 红线 | 断开即断电 |

### 7.2 成本估算

| 器件 | 单价 | 数量 | 小计 |
|------|------|------|------|
| 0.96 寸 OLED SSD1306 I2C | ¥10 | 1 | ¥10 |
| SPDT 滑动开关（模式/静音） | ¥0.8 | 2 | ¥1.6 |
| SPST 滑动开关（电源） | ¥1.5 | 1 | ¥1.5 |
| 6×6 轻触按键（复位） | ¥0.3 | 1 | ¥0.3 |
| 10kΩ 电阻、4.7kΩ 电阻 | ¥0.1 | 若干 | ¥1 |
| 面包板 + 杜邦线 | ¥5 | 1套 | ¥5 |
| **合计** | | | **约 ¥20** |

### 7.3 注意事项

1. **I2C 总线确认**：不同镜像中 I2C3 的 `/dev/i2c-*` 序号可能不同。上电后先执行
 ```bash
 ls /dev/i2c-*
 i2cdetect -y -r 3 # 应看到 0x3c
 ```
 若 0x3c 未出现，多半是上拉电阻缺失或引脚被复用，给 SDA/SCL 各加 4.7kΩ 到 3V3。
2. **电平兼容**：OLED 与所有开关都工作在 3.3V，**不要接 5V**，否则可能损坏 GPIO。
3. **SSD1306 是 3.3V 器件**：I2C 上拉电阻必须接到 3V3，不能接到 5V。
4. **不要每帧刷 OLED**：I2C 在 100kHz 时传 1KB 帧约 90ms，会严重拖慢推理。务必 2~5Hz 刷新。
5. **模式/静音开关的悬空问题**：GPIO 输入悬空会随机抖动，**必须**用下拉/上拉电阻（或开内部上/下拉）并配合软件消抖。
6. **软件复位与硬件复位区分**：硬件复位（Pin 30）会重启整个 Linux 系统；软件复位（GPIO2_B0）只重置 RiskEngine 状态，用于现场调参演示。
7. **接线顺序**：先接 GND 再接电源，避免带电插拔损坏 OLED；全部接好后再上电测试。
8. **fps 计算**：推理本身会掉帧，FPS 显示的是“主循环实际处理帧率”，与 `v4l2` 采集帧率不同，中需说明口径。

---

## 8. 实施步骤与验收

### 实施步骤

1. **硬件搭建**：按第 7.1 节接线 OLED 与三个开关。
2. **验证 I2C**：`i2cdetect -y -r 3` 看到 `0x3c`。
3. **点亮 OLED**：先跑第 3.3 节手写示例（`SSD1306` 类），显示固定文本。
4. **集成 u8g2（可选）**：把 `csrc/cppsrc` 加入 CMake，用第 3.2 节封装 `Oled` 类。
5. **开关读取**：实现 `GpioIn` 类，串口打印开关状态验证消抖。
6. **数据接入**：在 `main.cc` 增加 `g_frameCount` 原子计数，启动外设线程刷新 OLED。
7. **联调**：真人入镜，观察 OLED 人数/等级/FPS 与 RTSP 画面一致；拨动开关验证模式/静音/复位。
8. **素材**：OLED 实拍照片、I2C 波形/`i2cdetect` 截图、开关消抖前后对比。

### 验收标准

- [ ] `i2cdetect` 稳定识别 OLED 0x3c，长时间运行不花屏。
- [ ] OLED 以 2~5Hz 显示人数、等级、FPS、IP，与画面检测结果一致。
- [ ] 模式开关切换后 OLED 显示 `AUTO`/`MANUAL`，行为相应变化。
- [ ] 静音开关开启后，告警蜂鸣器/LED/声音被抑制。
- [ ] 软件复位键按下后 RiskEngine 状态清零（人数归零、等级回 SAFE）。
- [ ] 硬件复位键按下后整板重启。
- [ ] 电源开关断开后整机断电，OLED 熄灭。
- [ ] 接入外设后推理帧率下降不超过 1~2 FPS（OLED 低频刷新前提下）。

---

## 附录：关键参考资料

- Luckfox Wiki：GPIO / I2C / OLED（Pico Pro/Max）：https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/
- Luckfox Pico Pro/Max 引脚定义（GPIO 编号计算）：`pin = bank*32 + group*8 + X`
- u8g2 官方仓库：https://github.com/olikraus/u8g2
- u8g2 字体与设备构造器：https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
- armlabs/ssd1306_linux（Linux I2C 手写驱动参考）：https://github.com/armlabs/ssd1306_linux
- Adafruit GFX font5x7（公开字体）：https://github.com/adafruit/Adafruit-GFX-Library