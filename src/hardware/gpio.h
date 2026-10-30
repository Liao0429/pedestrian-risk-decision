// =============================================================================
// gpio.h — GPIO 抽象层（Luckfox 板端 sysfs 实现 + 主机测试替身）
// -----------------------------------------------------------------------------
// 设计目的：
//   1. SysfsGpio：Luckfox RV1106 / 通用 Linux 的 /sys/class/gpio 访问，
//      供三色 LED、按键、告警开关等硬件模块使用；
//   2. Gpio 抽象接口 + FakeGpio：让 LED/按键的“逻辑”（颜色映射、防抖、
//      边沿检测）可以在无硬件的 PC/CI 上做单元测试。
//
// 引脚编号说明（2026-10-30 Luckfox Pico Max 实机实测；sysfs 编号 = bank*32+group*8+pin）：
//   - 按键（告警使能/功能键）：PCB Gerber 的 J8(SW_ALARM)+R1 设计为跳线到排针
//     Pin27 = GPIO2_A3_d = sysfs 67，10kΩ 下拉到 GND，按下为高。
//     ⚠ 不要再写「Pin 92」/「GPIO1_C3」：
//       * sysfs 92 = GPIO2_D4（gpiochip2 base=64 实测），既不是 GPIO1_C3，也不对应排针 Pin；
//       * GPIO1_C3 = sysfs 51 实测被 SPI0 复用为 MISO（pin 51 = spi0m0-miso），
//         拿它做按键会与 ILI9341 读版本 ID 冲突。
//   - 三色 LED 的 R/G/B 引脚尚未定版；注意 56/57/58 不是空闲脚：
//     GPIO1_D0/D1（56/57）是 UART3_TX/RX（串口屏 J6 预留），GPIO1_D2（58）实测是 I2C3_SCL。
//     选脚前先 cat /sys/kernel/debug/gpio 与 /sys/kernel/debug/pinctrl/*/pinmux-pins。
// 若 /sys/class/gpio 中找不到目标节点，需先在设备树中使能对应 GPIO（并确认未被复用占用）。
// =============================================================================
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace hw {

// GPIO 抽象接口：板端 sysfs 实现与测试替身共用
class Gpio {
public:
    virtual ~Gpio() {}

    // 配置方向：true=输出，false=输入。返回是否成功。
    virtual bool setDirection(bool output) = 0;

    // 输出高/低电平（仅在输出模式有效）。
    virtual bool write(bool high) = 0;

    // 读取电平（输入模式），成功返回 true 并写入 *high。
    virtual bool read(bool* high) = 0;
};

// =============================================================================
// SysfsGpio — Linux /sys/class/gpio 实现
// =============================================================================
class SysfsGpio : public Gpio {
public:
    explicit SysfsGpio(int gpio) : m_gpio(gpio), m_exported(false), m_isOutput(false) {}

    // 导出 GPIO 节点（幂等）。失败通常表示引脚被占用或设备树未使能。
    bool exportPin() {
        if (m_exported) return true;
        FILE* f = fopen("/sys/class/gpio/export", "w");
        if (!f) return false;
        fprintf(f, "%d", m_gpio);
        fclose(f);
        usleep(20000);  // 等待 sysfs 节点就绪
        m_exported = true;
        return true;
    }

    bool setDirection(bool output) override {
        if (!exportPin()) return false;
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", m_gpio);
        FILE* f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%s", output ? "out" : "in");
        fclose(f);
        m_isOutput = output;
        return true;
    }

    bool write(bool high) override {
        if (!m_isOutput) return false;
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", m_gpio);
        FILE* f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%d", high ? 1 : 0);
        fclose(f);
        return true;
    }

    bool read(bool* high) override {
        if (!high) return false;
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", m_gpio);
        FILE* f = fopen(path, "r");
        if (!f) return false;
        char buf[8] = {0};
        if (!fgets(buf, sizeof(buf), f)) {
            fclose(f);
            return false;
        }
        fclose(f);
        *high = (buf[0] == '1');
        return true;
    }

    int gpio() const { return m_gpio; }

    // 取消导出（自检/工具跑完清理现场用；幂等）。
    // 2026-10-30 真机实跑：hardware_selftest 会把 -g/-b 的 GPIO 一直留在
    // /sys/class/gpio 里占用（实测残留 gpio56/gpio67），CLI 退出前应调用本方法。
    bool unexportPin() {
        if (!m_exported) return true;
        FILE* f = fopen("/sys/class/gpio/unexport", "w");
        if (!f) return false;
        fprintf(f, "%d", m_gpio);
        fclose(f);
        usleep(20000);
        m_exported = false;
        return true;
    }

private:
    int  m_gpio;
    bool m_exported;
    bool m_isOutput;
};

// =============================================================================
// FakeGpio — 内存替身（单元测试用）
//   输出模式：记录最近一次写入电平与写入次数；默认不可读，启用
//   setOutputReadback(true) 后可读回最近写入值（模拟 sysfs 输出电平读回，
//   供硬件自检 GPIO_LOOPBACK 用例使用）；
//   输入模式：电平由外部 setInputLevel() 注入，模拟按键/开关。
// =============================================================================
class FakeGpio : public Gpio {
public:
    explicit FakeGpio(bool initialInputLevel = false)
        : m_output(false),
          m_inputLevel(initialInputLevel),
          m_lastWritten(false),
          m_writeCount(0),
          m_outputReadback(false) {}

    bool setDirection(bool output) override {
        m_output = output;
        return true;
    }

    bool write(bool high) override {
        if (!m_output) return false;
        m_lastWritten = high;
        ++m_writeCount;
        return true;
    }

    bool read(bool* high) override {
        if (m_output) {
            // 输出模式默认不可读（与真实 sysfs 只写语义一致）；
            // 启用读回后返回最近写入值，模拟 sysfs GPIO 输出电平读回。
            if (!m_outputReadback) return false;
            if (high) *high = m_lastWritten;
            return true;
        }
        if (high) *high = m_inputLevel;
        return true;
    }

    bool  lastWritten() const { return m_lastWritten; }
    int   writeCount() const { return m_writeCount; }
    bool  isOutput() const { return m_output; }
    void  setInputLevel(bool high) { m_inputLevel = high; }
    // 启用/禁用输出模式读回（默认禁用，硬件自检用例需启用）
    void  setOutputReadback(bool en) { m_outputReadback = en; }

private:
    bool m_output;
    bool m_inputLevel;
    bool m_lastWritten;
    int  m_writeCount;
    bool m_outputReadback;
};

}  // namespace hw