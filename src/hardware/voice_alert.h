// =============================================================================
// voice_alert.h — 语音告警模块（USB 喇叭 + aplay 方案，用户 2026-10-27 决策）
// -----------------------------------------------------------------------------
// 背景：Luckfox Pico Max 未引出板载音频引脚（官方 Wiki 确认），因此采用
//   「USB 免驱喇叭 + 系统 TF 卡预录音频 + aplay 播放」方案（方案 E）：
//     - 音频文件存放在系统 TF 卡 /userdata/voice/（warning.wav / danger.wav）
//     - C++ 通过 system("aplay -q -D plughw:1,0 ... &") 后台播放，不阻塞主循环
//     - 无需 DFPlayer 模块、无需额外 TF 卡、无 UART 协议（DFPlayer 仅作兜底）
//
// 设计（与 gpio.h/led.h/button.h 一致的依赖注入风格）：
//   - VoiceBackend 抽象接口：板端 AplayVoiceBackend（voice_alert.cc），
//     测试 FakeVoiceBackend（内存替身，记录播放调用）；
//   - VoiceAlert 状态机（header-only）：等级跳变触发、DANGER 抢占、
//     防重复冷却、周期重播，全部基于时间戳（毫秒），可在 PC/CI 上单元测试；
//   - 后端不可用（无 aplay / 音频文件缺失）时 init 返回 false，仅禁用语音，
//     不影响主程序其余功能。
//
// 集成示例（接入 main.cc 主循环）：
//   hw::AplayVoiceBackend voiceBackend;            // 默认 plughw:1,0 + /userdata/voice/
//   hw::VoiceAlert voiceAlert(&voiceBackend);
//   voiceAlert.init();                              // 检查 aplay 与音频文件
//   ...
//   voiceAlert.update((hw::AlertLevel)crowdLevel, nowMs);  // 每帧调用
//
// 播报策略（可调参数）：
//   - SAFE   -> 停止当前播报；
//   - WARNING-> 等级跳变时播放 warning.wav；维持时每 20s 重播一次；
//   - DANGER -> 等级跳变时立即播放 danger.wav（抢占 WARNING）；维持时每 8s 重播；
//   - DANGER 降级到 WARNING 后 2s 冷却，避免立即重复播报。
// =============================================================================
#pragma once

#include <cstdint>

namespace hw {

// 风险等级（与 risk_engine.h 的 CrowdLevel 数值一致：0=安全 1=警告 2=危险）
enum class AlertLevel : uint8_t {
    SAFE    = 0,
    WARNING = 1,
    DANGER  = 2,
};

// =============================================================================
// VoiceBackend — 语音后端抽象接口（板端实现 + 测试替身共用）
// =============================================================================
class VoiceBackend {
public:
    virtual ~VoiceBackend() {}

    // 后端是否可用（aplay 存在、音频文件就绪、串口/设备打开成功等）
    virtual bool available() const = 0;

    // 播放警告语（WARNING）
    virtual void playWarning() = 0;

    // 播放危险语（DANGER）
    virtual void playDanger() = 0;

    // 停止当前播报
    virtual void stop() = 0;
};

// =============================================================================
// FakeVoiceBackend — 内存替身（单元测试用）
//   记录 playWarning / playDanger / stop 的调用次数，供状态机逻辑验证。
// =============================================================================
class FakeVoiceBackend : public VoiceBackend {
public:
    FakeVoiceBackend() : m_ok(true), m_warning(0), m_danger(0), m_stop(0) {}

    void setAvailable(bool ok) { m_ok = ok; }

    bool available() const override { return m_ok; }
    void playWarning() override { if (m_ok) ++m_warning; }
    void playDanger()  override { if (m_ok) ++m_danger; }
    void stop()        override { if (m_ok) ++m_stop; }

    int warningCount() const { return m_warning; }
    int dangerCount()  const { return m_danger; }
    int stopCount()    const { return m_stop; }
    int totalPlayCount() const { return m_warning + m_danger; }

private:
    bool m_ok;
    int  m_warning;
    int  m_danger;
    int  m_stop;
};

// =============================================================================
// VoiceAlert — 语音告警状态机（header-only）
//   构造参数可调，便于测试（周期/冷却用较小值）：
//     warnRepeatMs       WARNING 维持时的重播间隔（默认 20s）
//     dangerRepeatMs     DANGER 维持时的重播间隔（默认 8s）
//     escalateCooldownMs DANGER 降级到 WARNING 后的冷却（默认 2s）
// =============================================================================
class VoiceAlert {
public:
    VoiceAlert(VoiceBackend* backend,
               uint64_t warnRepeatMs = 20000,
               uint64_t dangerRepeatMs = 8000,
               uint64_t escalateCooldownMs = 2000)
        : m_backend(backend),
          m_warnRepeatMs(warnRepeatMs),
          m_dangerRepeatMs(dangerRepeatMs),
          m_escalateCooldownMs(escalateCooldownMs),
          m_lastLevel(AlertLevel::SAFE),
          m_playingLevel(AlertLevel::SAFE),
          m_lastPlayMs(0),
          m_levelChangeMs(0) {}

    // 后端可用性（后端为空或不可用则整体不可用）
    bool available() const { return m_backend && m_backend->available(); }

    // 主循环每帧调用：按当前风险等级与毫秒时间戳决定是否播报。
    void update(AlertLevel level, uint64_t nowMs) {
        if (!m_backend || !m_backend->available()) return;

        const bool changed = (level != m_lastLevel);
        m_lastLevel = level;

        // 回到安全：停止当前播报
        if (level == AlertLevel::SAFE) {
            if (m_playingLevel != AlertLevel::SAFE) {
                m_backend->stop();
                m_playingLevel = AlertLevel::SAFE;
            }
            return;
        }

        // 等级跳变：DANGER 立即抢占；WARNING 需满足冷却
        if (changed) {
            m_levelChangeMs = nowMs;
            if (level == AlertLevel::DANGER) {
                play(level, nowMs);
            } else if (level == AlertLevel::WARNING) {
                // 刚从 DANGER 降级时，避免立即重复播报
                if (nowMs - m_lastPlayMs >= m_escalateCooldownMs) {
                    play(level, nowMs);
                }
            }
            return;
        }

        // 等级未变：按冷却间隔周期重播，保证持续告警
        const uint64_t interval =
            (level == AlertLevel::DANGER) ? m_dangerRepeatMs : m_warnRepeatMs;
        if (nowMs - m_lastPlayMs >= interval) {
            play(level, nowMs);
        }
    }

    // 强制播报（手动触发，如按键测试）。nowMs>0 时同步更新上次播放时间，
    // 避免紧接着的周期重播逻辑误触发。
    void playWarning(uint64_t nowMs = 0) { play(AlertLevel::WARNING, nowMs); }
    void playDanger(uint64_t nowMs = 0)  { play(AlertLevel::DANGER, nowMs); }

    // 强制停止
    void stop() {
        if (m_backend) m_backend->stop();
        m_playingLevel = AlertLevel::SAFE;
    }

    // 状态查询（测试/诊断用）
    AlertLevel lastLevel() const    { return m_lastLevel; }
    AlertLevel playingLevel() const { return m_playingLevel; }
    uint64_t  lastPlayMs() const    { return m_lastPlayMs; }

private:
    void play(AlertLevel level, uint64_t nowMs) {
        if (level == AlertLevel::DANGER) {
            m_backend->playDanger();
            m_playingLevel = AlertLevel::DANGER;
        } else {
            m_backend->playWarning();
            m_playingLevel = AlertLevel::WARNING;
        }
        if (nowMs > 0) m_lastPlayMs = nowMs;
    }

    VoiceBackend* m_backend;
    uint64_t      m_warnRepeatMs;
    uint64_t      m_dangerRepeatMs;
    uint64_t      m_escalateCooldownMs;
    AlertLevel    m_lastLevel;
    AlertLevel    m_playingLevel;
    uint64_t      m_lastPlayMs;
    uint64_t      m_levelChangeMs;
};

}  // namespace hw