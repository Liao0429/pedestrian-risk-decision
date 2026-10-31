// =============================================================================
// sysfs_spi.h — Linux 板端适配层（spidev 总线 + sysfs GPIO 引脚）
// -----------------------------------------------------------------------------
// 用途：把主机侧已就绪的 ILI9341 显示驱动与 XPT2046 触摸驱动接到 Luckfox
//       Pico Max 的真实硬件上（docs/hardware/lvgl_board_port.md 方案 B，
//       路线图步骤 4/6）：
//
//   hw::SysfsSpiBus   —— /dev/spidevX.Y 用户态 SPI 总线（ioctl 全双工）。
//
// 片选（2026-10-30 于 Luckfox Pico Max 实测，证据见 docs/hardware/pin_map_verified.md）：
//   - 板上只有 /dev/spidev0.0（spi0 status=okay）；SPI1 disabled，**不存在
//     /dev/spidev0.1**；SPI0 复用只有一组 spi0m0-cs0（GPIO1_C0 = sysfs 48）。
//   - 所以 ILI9341 与 XPT2046 不能各占一个 spidev 片选。正确做法：
//     ① 两者共用同一个 /dev/spidev0.0；
//     ② 片选改由 GPIO 手工控制：new 两个 SysfsSpiBus("/dev/spidev0.0")，分别
//        setCsGpio(&lcdCs) / setCsGpio(&touchCs)，两个外设即可互不干扰；
//     ③ J11 的 SPI_CS 与触摸 T_CS 都接空闲 GPIO（不要接排针 Pin12 的硬件 CS0）。
//   hw::SysfsLcdPins  —— ILI9341 的 DC / RESET / 背光引脚（sysfs GPIO）。
//   hw::SysfsTouchPin —— XPT2046 的 PENIRQ 输入（低有效）。
//
// 设计：
//   - SysfsSpiBus 的 open() 设置 SPI mode / bits / max speed，transfer()
//     构造单个 spi_ioc_transfer 全双工交换；tx/rx 均允许 NULL（纯读/纯写）；
//   - SysfsLcdPins / SysfsTouchPin 通过 Gpio* 注入（非拥有），真实板端传
//     hw::SysfsGpio，单元测试传 hw::FakeGpio（逻辑可主机/CI 验证）；
//   - 所有方法对 NULL/未打开状态安全返回 false，与现有硬件驱动风格一致。
//
// 集成方式（真机阶段，2026-10-30 实测校准）：
//   // 引脚一律以 pinmux 实测的空闲 GPIO 为准。v1.8.1 起 J11 为 11 脚，含 LCD_BL /
//   // TOUCH_CS / TOUCH_IRQ；推荐 GPIO 分配（见 docs/hardware/pin_map_verified.md）：
//   //   DC=GPIO1_D0(56)  RST=GPIO1_D1(57)  LCD_CS=GPIO2_B0(72)  LCD_BL=GPIO2_D4(92)
//   //   TOUCH_CS=GPIO1_C6(54)  TOUCH_IRQ=GPIO1_C7(55)  （两个 CS 都走 GPIO，不接排针 Pin12 硬件 CS0）
//   hw::SysfsGpio dcGpio(dcPin), resetGpio(rstPin), blGpio;
//   hw::SysfsGpio lcdCs(lcdCsPin), touchCs(touchCsPin);
//   hw::SysfsLcdPins pins(&dcGpio, &resetGpio, &blGpio);
//   hw::SysfsSpiBus spi("/dev/spidev0.0", 50000000);
//   spi.setCsGpio(&lcdCs);              // ★ 手工片选（板上无 spidev0.1）
//   hw::Ili9341 lcd(&spi, &pins);
//   lcd.setDelayFn([](uint32_t ms){ usleep(ms * 1000); });
//   lcd.setRotation(0);                 // MADCTL=0x28，横向 320×240
//   lcd.begin();
//
//   hw::SysfsSpiBus tSpi("/dev/spidev0.0", 2000000);   // 同一个 spidev
//   tSpi.setCsGpio(&touchCs);                          // 触摸自己的片选 GPIO
//   hw::SysfsTouchPin pen(&penGpio);                   // PENIRQ 输入（低有效）
//   hw::Xpt2046 tp(&tSpi, &pen);
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "gpio.h"       // hw::Gpio
#include "ili9341.h"    // hw::SpiBus / hw::LcdPins
#include "xpt2046.h"    // hw::TouchPin

namespace hw {

// -----------------------------------------------------------------------------
// SysfsSpiBus — Linux spidev 用户态 SPI 总线
// -----------------------------------------------------------------------------
class SysfsSpiBus : public SpiBus {
public:
    // devPath：/dev/spidevX.Y（NULL/空串视为非法，open 返回 false）
    // speedHz：SPI 时钟（ILI9341 常用 50MHz=50000000，XPT2046 常用 1~2MHz）
    // mode/bits：SPI 模式（默认 Mode 0）与字宽（默认 8）
    explicit SysfsSpiBus(const char* devPath,
                         uint32_t speedHz = 50000000,
                         uint8_t mode = 0,
                         uint8_t bits = 8);
    ~SysfsSpiBus() override;

    bool open() override;
    void close() override;
    bool isOpen() const override { return fd_ >= 0; }
    // 全双工 transfer：tx 可 NULL（纯读），rx 可 NULL（纯写）。
    // 未打开 / len==0 / tx 与 rx 同时为 NULL 时返回 false。
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override;

    const char* path() const { return devPath_.c_str(); }
    uint32_t speedHz() const { return speedHz_; }

    // ---- 手工片选（真机必需，原因见文件顶部「片选」说明）----
    // cs：片选 GPIO（注入，非拥有）；activeLow：低有效（默认，绝大多数 LCD/触摸模组）。
    // 设置后每次 transfer() 自动 assert/deassert；beginCs()/endCs() 用于把
    // 「命令 + 读取」两段传输保持在同一个片选周期内。
    void setCsGpio(Gpio* cs, bool activeLow = true) {
        cs_ = cs;
        csActiveLow_ = activeLow;
        csSession_ = false;
        if (cs_) cs_->setDirection(true);
    }
    bool hasCsGpio() const { return cs_ != NULL; }
    // 只有配置了片选 GPIO 才能跨 transfer 保持片选（硬件 CS0 由内核按消息管理）。
    bool holdsCsAcrossTransfers() const { return cs_ != NULL; }

    bool beginCs() override;
    bool endCs() override;

private:
    std::string devPath_;
    uint32_t speedHz_;
    uint8_t  mode_;
    uint8_t  bits_;
    int      fd_;
    Gpio*    cs_;
    bool     csActiveLow_;
    bool     csSession_;   // beginCs()..endCs() 会话中（transfer 不再自动开关）
};

// -----------------------------------------------------------------------------
// SysfsLcdPins — ILI9341 控制引脚（DC/RESET/背光）的 sysfs GPIO 实现
// -----------------------------------------------------------------------------
class SysfsLcdPins : public LcdPins {
public:
    // 三个 Gpio 均为注入（非拥有）：真实板端传 hw::SysfsGpio 对象地址，
    // 测试传 hw::FakeGpio。允许 NULL（对应方法返回 false）。
    // 构造函数会自行把 DC/RESET/背光配置为输出（sysfs GPIO 默认输入，不配置
    // 则 write() 全部失败——2026-10-30 真机实跑踩到，见 sysfs_spi.cc 注释）。
    SysfsLcdPins(Gpio* dc, Gpio* reset, Gpio* backlight);

    bool dcCommand() override;   // DC 拉低 = 命令模式
    bool dcData() override;      // DC 拉高 = 数据模式
    bool resetLow() override;    // 复位拉低（低有效）
    bool resetHigh() override;   // 复位释放
    bool backlight(bool on) override;

private:
    Gpio* dc_;
    Gpio* reset_;
    Gpio* bl_;
};

// -----------------------------------------------------------------------------
// SysfsTouchPin — XPT2046 PENIRQ 触摸按下检测（低有效）
// -----------------------------------------------------------------------------
class SysfsTouchPin : public TouchPin {
public:
    // pen：PENIRQ 引脚对应的 Gpio 输入（注入，非拥有）。允许 NULL。
    // 构造函数会把它配置为输入。
    explicit SysfsTouchPin(Gpio* pen);

    // 返回 true 表示触摸笔当前按下（PENIRQ 读到低电平）。
    bool isPressed() override;

private:
    Gpio* pen_;
};

} // namespace hw