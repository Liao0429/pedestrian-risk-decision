// pan_tilt.h
// 来自 docs/hardware/ 方案文档，供 Luckfox 开发板硬件扩展集成参考。
// 实际集成时请按板面丝印和 /sys/class/pwm、/dev/ttyS* 实际节点核对。

// pan_tilt.h
#pragma once
#include "servo.h"
#include <cmath>

class PanTilt {
public:
    PanTilt(Servo &pan, Servo &tilt)
        : m_pan(pan), m_tilt(tilt),
          m_panAngle(90), m_tiltAngle(90),
          m_smoothPan(90), m_smoothTilt(90) {}

    void init() {
        m_pan.init();  m_tilt.init();
        m_pan.setAngle(90);  m_tilt.setAngle(90);
    }

    // imgW/imgH: 图像宽高; tx,ty: 目标中心(原图像素坐标)
    // mode: 0 追踪最大行人, 1 追踪人群质心
    void update(int imgW, int imgH, float tx, float ty, bool hasTarget) {
        if (!hasTarget) return;   // 无目标保持不动

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

    // 查询当前目标中心（供其它线程使用）：由调用方在多线程时加锁
    void currentAngles(float &pan, float &tilt) const {
        pan = m_smoothPan; tilt = m_smoothTilt;
    }

private:
    static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    Servo &m_pan, &m_tilt;
    float m_panAngle, m_tiltAngle;
    float m_smoothPan, m_smoothTilt;
};
