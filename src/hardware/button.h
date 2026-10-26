// =============================================================================
// button.h — 按键驱动（GPIO 输入 + 软件防抖 + 边沿检测）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的“告警使能开关/功能按键”（GPIO1_C3_d，10kΩ 下拉，
// 按下为高）。也兼容低电平有效按键（activeHigh=false）。
//
// 防抖策略：连续 DEBOUNCE 次采样一致才确认电平切换，避免机械抖动误触发。
// 用法（主循环每帧调用一次 update）：
//   hw::SysfsGpio gpio(51);
//   hw::Button btn(&gpio, true /*按下为高*/, 5);
//   btn.init();
//   ...
//   btn.update();
//   if (btn.risingEdge())  { /* 按下：切换告警使能 */ }
//   if (btn.fallingEdge()) { /* 释放 */ }
// =============================================================================
#pragma once

#include <cstdint>

#include "gpio.h"

namespace hw {

class Button {
public:
    // gpio        : GPIO 输入引脚（Gpio 抽象，可为 SysfsGpio/FakeGpio）
    // activeHigh  : true=按下为高电平（默认），false=按下为低电平
    // debounce    : 连续采样一致次数（>=1，默认 5）
    Button(Gpio* gpio, bool activeHigh = true, int debounce = 5)
        : m_gpio(gpio),
          m_activeHigh(activeHigh),
          m_debounce(debounce > 0 ? debounce : 1),
          m_stable(false),
          m_prevStable(false),
          m_pending(false),
          m_pendingCount(0),
          m_ok(false) {}

    // 配置为输入并读取一次初始电平。失败返回 false（调用方可禁用按键）。
    bool init() {
        if (!m_gpio) return false;
        m_ok = m_gpio->setDirection(false);  // 输入
        if (m_ok) {
            bool v = false;
            if (m_gpio->read(&v)) {
                m_stable = v;
                m_prevStable = v;
                m_pending = v;
                m_pendingCount = m_debounce;
            }
        }
        return m_ok;
    }

    bool available() const { return m_ok; }

    // 主循环每帧调用：采样一次并更新防抖状态。
    void update() {
        if (!m_ok) return;
        bool v = false;
        if (!m_gpio->read(&v)) return;

        m_prevStable = m_stable;  // 保存上一轮稳定值（供边沿检测）
        if (v == m_pending) {
            ++m_pendingCount;
            if (m_pendingCount >= m_debounce) {
                m_stable = v;  // 候选电平连续一致达阈值，确认切换
            }
        } else {
            m_pending = v;
            m_pendingCount = 1;  // 出现抖动，重新累积
        }
    }

    // 原始稳定电平（未按 activeHigh 换算）
    bool rawLevel() const { return m_stable; }

    // 是否处于按下状态（已按 activeHigh 换算）
    bool isPressed() const {
        return m_activeHigh ? m_stable : !m_stable;
    }

    // 本次 update 后是否刚检测到“按下”边沿（稳定电平从释放->按下）
    bool risingEdge() const {
        bool prevPressed = m_activeHigh ? m_prevStable : !m_prevStable;
        return !prevPressed && isPressed();
    }

    // 本次 update 后是否刚检测到“释放”边沿（稳定电平从按下->释放）
    bool fallingEdge() const {
        bool prevPressed = m_activeHigh ? m_prevStable : !m_prevStable;
        return prevPressed && !isPressed();
    }

private:
    Gpio* m_gpio;
    bool  m_activeHigh;
    int   m_debounce;
    bool  m_stable;      // 防抖后的稳定原始电平
    bool  m_prevStable;  // 上一次稳定原始电平（边沿检测用）
    bool  m_pending;     // 当前累积的候选电平
    int   m_pendingCount;
    bool  m_ok;
};

}  // namespace hw