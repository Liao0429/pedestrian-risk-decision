// =============================================================================
// pan_tilt.h — 双舵机云台跟踪控制器
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的“摄像头云台自动跟踪”——检测/决策主循环给出目标
// 中心（原图像素坐标），PanTilt 换算为 PAN/TILT 角度并平滑输出。
// 设计：
//   - 依赖 hw::Servo 抽象（servo.h），板端传 SysfsPwm，测试传 FakePwm；
//   - 控制逻辑全部与硬件无关（死区/P 增益/机械限幅/角速度限幅/一阶低通），
//     可在 PC/CI 上单元测试；
//   - 无目标时保持不动；舵机不可用时 update 安全跳过。
//
// 集成示例（接入主循环，每帧调用一次）：
//   hw::SysfsPwm panPwm(0, 0), tiltPwm(0, 1);
//   hw::Servo pan(&panPwm), tilt(&tiltPwm);
//   hw::PanTilt gimbal(pan, tilt);
//   gimbal.init();
//   ...
//   gimbal.update(imgW, imgH, targetX, targetY, hasTarget);
// =============================================================================
#pragma once
#include <cmath>

#include "servo.h"

namespace hw {

class PanTilt {
public:
    PanTilt(Servo& pan, Servo& tilt)
        : m_pan(pan), m_tilt(tilt),
          m_ok(false),
          m_panAngle(90), m_tiltAngle(90),
          m_smoothPan(90), m_smoothTilt(90) {}

    bool init() {
        m_ok = m_pan.init() && m_tilt.init();
        if (m_ok) {
            m_pan.setAngle(90);
            m_tilt.setAngle(90);
        }
        return m_ok;
    }

    bool available() const { return m_ok; }

    // imgW/imgH: 图像宽高; tx,ty: 目标中心(原图像素坐标)
    // hasTarget: 本帧是否有有效目标（无目标保持不动）
    void update(int imgW, int imgH, float tx, float ty, bool hasTarget) {
        if (!hasTarget || !m_ok) return;   // 无目标或舵机不可用，保持不动

        const float cx = imgW / 2.0f, cy = imgH / 2.0f;
        float errX = tx - cx;
        float errY = ty - cy;

        // 死区
        const float DEAD = 15.0f;
        if (fabs(errX) < DEAD && fabs(errY) < DEAD) return;

        // P 增益
        const float KP_X = 0.125f;   // °/px
        const float KP_Y = 0.125f;

        float targetPan  = 90.0f + KP_X * errX;
        float targetTilt = 90.0f + KP_Y * errY;

        // 机械限幅
        if (targetPan  < 10) targetPan  = 10;
        if (targetPan  > 170) targetPan = 170;
        if (targetTilt < 10) targetTilt = 10;
        if (targetTilt > 170) targetTilt = 170;

        // 角速度限幅（每次最大 2°，约 40°/s @ 20Hz）
        const float MAX_STEP = 2.0f;
        float dPan  = clamp(targetPan  - m_panAngle,  -MAX_STEP,  MAX_STEP);
        float dTilt = clamp(targetTilt - m_tiltAngle, -MAX_STEP,  MAX_STEP);
        m_panAngle  += dPan;
        m_tiltAngle += dTilt;

        // 一阶低通平滑
        m_smoothPan  = 0.7f * m_panAngle  + 0.3f * m_smoothPan;
        m_smoothTilt = 0.7f * m_tiltAngle + 0.3f * m_smoothTilt;

        m_pan.setAngle(m_smoothPan);
        m_tilt.setAngle(m_smoothTilt);
    }

    // 查询当前目标角度（供其它线程使用）：由调用方在多线程时加锁
    void currentAngles(float& pan, float& tilt) const {
        pan = m_smoothPan; tilt = m_smoothTilt;
    }

private:
    static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    Servo& m_pan;
    Servo& m_tilt;
    bool   m_ok;
    float  m_panAngle, m_tiltAngle;
    float  m_smoothPan, m_smoothTilt;
};

}  // namespace hw
