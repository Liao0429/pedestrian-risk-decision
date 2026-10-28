// =============================================================================
// sysfs_spi.h — Linux 板端适配层（spidev 总线 + sysfs GPIO 引脚）
// -----------------------------------------------------------------------------
// 用途：把主机侧已就绪的 ILI9341 显示驱动与 XPT2046 触摸驱动接到 Luckfox
//       Pico Max 的真实硬件上（docs/hardware/lvgl_board_port.md 方案 B，
//       路线图步骤 4/6）：
//
//   hw::SysfsSpiBus   —— /dev/spidevX.Y 用户态 SPI 总线（ioctl 全双工）。
//                        ILI9341 用 spidev0.0（板载 FPC/扩展排针 CS0），
//                        XPT2046 用独立片选（spidev0.1 或 GPIO CS）。
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
// 集成方式（真机阶段）：
//   hw::SysfsGpio dcGpio(69), resetGpio(70), blGpio(71);  // 按 PCB 原理图填
//   hw::SysfsLcdPins pins(&dcGpio, &resetGpio, &blGpio);
//   hw::SysfsSpiBus spi("/dev/spidev0.0", 50000000);
//   hw::Ili9341 lcd(&spi, &pins);
//   lcd.setDelayFn([](uint32_t ms){ usleep(ms * 1000); });
//   lcd.begin();
//
//   hw::SysfsSpiBus tSpi("/dev/spidev0.1", 2000000);
//   hw::SysfsTouchPin pen(&penGpio);
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

private:
    std::string devPath_;
    uint32_t speedHz_;
    uint8_t  mode_;
    uint8_t  bits_;
    int      fd_;
};

// -----------------------------------------------------------------------------
// SysfsLcdPins — ILI9341 控制引脚（DC/RESET/背光）的 sysfs GPIO 实现
// -----------------------------------------------------------------------------
class SysfsLcdPins : public LcdPins {
public:
    // 三个 Gpio 均为注入（非拥有）：真实板端传 hw::SysfsGpio 对象地址，
    // 测试传 hw::FakeGpio。允许 NULL（对应方法返回 false）。
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
    explicit SysfsTouchPin(Gpio* pen);

    // 返回 true 表示触摸笔当前按下（PENIRQ 读到低电平）。
    bool isPressed() override;

private:
    Gpio* pen_;
};

} // namespace hw