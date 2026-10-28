// =============================================================================
// servo.h — 舵机驱动（PWM 抽象层 + Linux sysfs 实现 + 主机测试替身）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的“双舵机云台跟踪”——PanTilt 通过 Servo 输出角度。
// 设计（与 gpio.h 同风格）：
//   1. Pwm 抽象接口：板端 SysfsPwm 走 /sys/class/pwm/pwmchipN/pwmM，
//      测试用 FakePwm 在内存中记录 duty/period/enable 写入；
//   2. Servo 负责“角度 -> 脉宽”换算（500~2500us @ 50Hz，中位 1.5ms = 90°），
//      对 PWM 不可用容错（init 失败仅禁用，不崩溃）；
//   3. 所有逻辑可在 PC/CI 上用 FakePwm 做单元测试，无需真实舵机。
//
// 引脚/节点说明（Luckfox Pico Max，以板面丝印与 /sys/class/pwm 实测为准）：
//   - 云台 PAN  舵机接 PWM 通道 0（pwmchip0/pwm0）
//   - 云台 TILT 舵机接 PWM 通道 1（pwmchip0/pwm1）
//   若节点不存在，需先在设备树中使能对应 PWM。详见
//   docs/hardware/tracking_servo.md 与 docs/hardware/HARDWARE_BUILD_GUIDE.md。
// =============================================================================
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace hw {

// PWM 抽象接口：板端 sysfs 实现与测试替身共用
class Pwm {
public:
    virtual ~Pwm() {}

    // 配置周期（ns）、极性并使能。成功返回 true。
    virtual bool configure(long periodNs) = 0;

    // 设置占空比（ns）。仅在已使能后有效。
    virtual bool setDutyCycle(long dutyNs) = 0;

    // 使能/禁用输出。
    virtual bool enable(bool on) = 0;
};

// =============================================================================
// SysfsPwm — Linux /sys/class/pwm/pwmchipN/pwmM 实现
// =============================================================================
class SysfsPwm : public Pwm {
public:
    SysfsPwm(int chip, int channel)
        : m_chip(chip), m_chan(channel), m_exported(false), m_enabled(false) {
        snprintf(m_base, sizeof(m_base),
                 "/sys/class/pwm/pwmchip%d/pwm%d", chip, channel);
    }

    // 导出 PWM 节点（幂等）。失败通常表示引脚被占用或设备树未使能。
    bool exportPin() {
        if (m_exported) return true;
        char p[64];
        snprintf(p, sizeof(p), "/sys/class/pwm/pwmchip%d/export", m_chip);
        FILE* f = fopen(p, "w");
        if (!f) return false;
        fprintf(f, "%d", m_chan);
        fclose(f);
        usleep(20000);  // 等待 sysfs 节点就绪
        m_exported = true;
        return true;
    }

    bool configure(long periodNs) override {
        if (!exportPin()) return false;
        if (!writeFile("period", periodNs)) return false;
        if (!writeFile("polarity", "normal")) return false;
        m_periodNs = periodNs;
        return true;
    }

    bool setDutyCycle(long dutyNs) override {
        if (!m_enabled) return false;
        return writeFile("duty_cycle", dutyNs);
    }

    bool enable(bool on) override {
        if (!exportPin()) return false;
        if (!writeFile("enable", on ? 1 : 0)) return false;
        m_enabled = on;
        return true;
    }

    int chip() const { return m_chip; }
    int channel() const { return m_chan; }

private:
    bool writeFile(const char* attr, long val) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE* f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%ld", val);
        fclose(f);
        return true;
    }
    bool writeFile(const char* attr, const char* val) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE* f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%s", val);
        fclose(f);
        return true;
    }

    int  m_chip, m_chan;
    char m_base[128];
    bool m_exported;
    bool m_enabled;
    long m_periodNs;
};

// =============================================================================
// FakePwm — 内存替身（单元测试用）
//   记录 configure/setDutyCycle/enable 的调用历史，便于断言角度换算与
//   云台跟踪逻辑（无需真实舵机/开发板）。
// =============================================================================
class FakePwm : public Pwm {
public:
    FakePwm() : m_period(0), m_duty(0), m_enabled(false),
                m_configureCount(0), m_dutyCount(0) {}

    bool configure(long periodNs) override {
        m_period = periodNs;
        ++m_configureCount;
        return true;
    }
    bool setDutyCycle(long dutyNs) override {
        if (!m_enabled) return false;
        m_duty = dutyNs;
        ++m_dutyCount;
        return true;
    }
    bool enable(bool on) override {
        m_enabled = on;
        return true;
    }

    long period() const { return m_period; }
    long duty() const { return m_duty; }
    bool enabled() const { return m_enabled; }
    int  configureCount() const { return m_configureCount; }
    int  dutyCount() const { return m_dutyCount; }

private:
    long m_period;
    long m_duty;
    bool m_enabled;
    int  m_configureCount;
    int  m_dutyCount;
};

// =============================================================================
// Servo — 角度 -> 脉宽换算（500~2500us @ 50Hz）
//   0°   = 0.5ms = 500000ns
//   90°  = 1.5ms = 1500000ns（中位）
//   180° = 2.5ms = 2500000ns
// =============================================================================
class Servo {
public:
    // 板端构造：传入 sysfs PWM 编号
    Servo(int chip, int channel)
        : m_pwm(NULL), m_chip(chip), m_chan(channel),
          m_ok(false), m_lastAngle(90.0f) {}

    // 测试/注入构造：传入 Pwm 实例（可为 FakePwm）
    explicit Servo(Pwm* pwm)
        : m_pwm(pwm), m_chip(-1), m_chan(-1),
          m_ok(false), m_lastAngle(90.0f) {}

    // 配置为 50Hz（20ms）并回到中位 90°，然后使能。
    // PWM 不可用时返回 false 但不会崩溃，调用方可选择禁用云台。
    bool init() {
        if (!m_pwm) return false;
        const long kPeriod = 20000000L;  // 20ms = 50Hz
        m_ok = m_pwm->configure(kPeriod);
        if (m_ok) {
            m_ok = m_pwm->enable(true);
        }
        if (m_ok) {
            setAngle(90.0f);  // 中位
        }
        return m_ok;
    }

    bool available() const { return m_ok; }

    // angle: 0~180，自动限幅；角度换算与硬件无关，可单元测试。
    void setAngle(float angle) {
        if (angle < 0)   angle = 0;
        if (angle > 180) angle = 180;
        m_lastAngle = angle;
        if (!m_pwm) return;
        long ns = 500000L + (long)((angle / 180.0f) * 2000000.0f);
        m_pwm->setDutyCycle(ns);
    }

    float lastAngle() const { return m_lastAngle; }

private:
    Pwm*  m_pwm;
    int   m_chip, m_chan;
    bool  m_ok;
    float m_lastAngle;
};

}  // namespace hw
