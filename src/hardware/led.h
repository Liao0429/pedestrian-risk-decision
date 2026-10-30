// =============================================================================
// led.h — 三色 LED 驱动（红/绿/蓝三路 GPIO，高电平点亮）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的“风险等级灯”——绿色=安全、黄色=警告、红色=危险。
// 设计：
//   - 依赖 hw::Gpio 抽象（gpio.h），板端传 SysfsGpio，测试传 FakeGpio；
//   - init() 对 GPIO 不可用容错：返回 false 但不会崩溃，调用方可选择禁用；
//   - 引脚可在构造时指定（打样定版前以实测 /sys/class/gpio 为准）。
//
// 集成示例（接入主循环，每帧设置一次）：
//   hw::SysfsGpio red(0), green(0), blue(0);   // 引脚待定：56/57/58 实测已被
//   UART3_TX/RX（GPIO1_D0/D1）与 I2C3_SCL（GPIO1_D2）占用，切勿直接使用
//   hw::RgbLed led(&red, &green, &blue);
//   led.init();                                   // GPIO 不可用则禁用
//   ...
//   led.setLevel((int)crowdLevel);                // SAFE/WARNING/DANGER
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>

#include "gpio.h"

namespace hw {

// 三色 LED 可表示的颜色（红/绿/蓝三路组合）
enum class LedColor : uint8_t {
    OFF    = 0,
    RED    = 1,   // 仅红灯
    GREEN  = 2,   // 仅绿灯
    YELLOW = 3,   // 红 + 绿
    BLUE   = 4,   // 仅蓝灯
};

class RgbLed {
public:
    // 板端构造：传入三路 sysfs GPIO 编号
    RgbLed(int redGpio, int greenGpio, int blueGpio)
        : m_red(NULL), m_green(NULL), m_blue(NULL),
          m_redGpio(redGpio), m_greenGpio(greenGpio), m_blueGpio(blueGpio),
          m_ok(false) {}

    // 测试/注入构造：传入 Gpio 实例（可为 FakeGpio）
    RgbLed(Gpio* red, Gpio* green, Gpio* blue)
        : m_red(red), m_green(green), m_blue(blue),
          m_redGpio(-1), m_greenGpio(-1), m_blueGpio(-1),
          m_ok(false) {}

    // 配置三路为输出并默认全灭。任一 GPIO 失败则整模块标记不可用。
    bool init() {
        if (!m_red || !m_green || !m_blue) return false;
        bool ok = true;
        ok = m_red->setDirection(true) && ok;
        ok = m_green->setDirection(true) && ok;
        ok = m_blue->setDirection(true) && ok;
        m_ok = ok;
        setColor(LedColor::OFF);  // 上电默认全灭
        return m_ok;
    }

    bool available() const { return m_ok; }

    // 直接设置颜色
    void setColor(LedColor c) {
        switch (c) {
            case LedColor::RED:    writeRGB(true, false, false); break;
            case LedColor::GREEN:  writeRGB(false, true, false); break;
            case LedColor::BLUE:   writeRGB(false, false, true); break;
            case LedColor::YELLOW: writeRGB(true, true, false);  break;
            case LedColor::OFF:
            default:               writeRGB(false, false, false); break;
        }
    }

    // 按风险等级映射：
    //   0 = SAFE    -> 绿
    //   1 = WARNING -> 黄（红+绿）
    //   2 = DANGER  -> 红
    //   其他/未知    -> 灭
    void setLevel(int crowdLevel) {
        switch (crowdLevel) {
            case 0: setColor(LedColor::GREEN);  break;
            case 1: setColor(LedColor::YELLOW); break;
            case 2: setColor(LedColor::RED);    break;
            default: setColor(LedColor::OFF);   break;
        }
    }

private:
    void writeRGB(bool r, bool g, bool b) {
        if (m_red)   m_red->write(r);
        if (m_green) m_green->write(g);
        if (m_blue)  m_blue->write(b);
    }

    Gpio* m_red;
    Gpio* m_green;
    Gpio* m_blue;
    int   m_redGpio;
    int   m_greenGpio;
    int   m_blueGpio;
    bool  m_ok;
};

}  // namespace hw