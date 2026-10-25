// voice_alert.h
// 来自 docs/hardware/ 方案文档，供 Luckfox 开发板硬件扩展集成参考。
// 实际集成时请按板面丝印和 /sys/class/pwm、/dev/ttyS* 实际节点核对。

#ifndef VOICE_ALERT_H
#define VOICE_ALERT_H

#include <stdint.h>
#include <unistd.h>
#include "risk_engine.h"   // 复用 CrowdLevel 枚举

// 语音告警模块：DFPlayer Mini，UART4 (/dev/ttyS4)，9600 8N1
class VoiceAlert {
public:
    VoiceAlert();
    ~VoiceAlert();

    // 打开串口，初始化 DFPlayer（选 SD 卡、设音量）
    // 返回 true 表示初始化成功（串口打开失败不致命，仅禁用语音）
    bool init(const char* dev = "/dev/ttyS4", uint8_t volume = 30);

    // 主循环每帧调用：根据风险等级与当前时间(ms) 决定是否播报
    void update(CrowdLevel level, uint64_t nowMs);

    // 强制播报/停止（可手动触发）
    void playWarning();
    void playDanger();
    void stop();

    bool available() const { return m_fd >= 0; }

private:
    void sendFrame(uint8_t cmd, uint8_t p1, uint8_t p2);
    bool isPlayingBusy();                 // 可选：读 BUSY GPIO

    int      m_fd;
    uint8_t  m_volume;
    CrowdLevel m_lastLevel;                 // 上一帧等级
    CrowdLevel m_playingLevel;            // 正在播报/最近播报的等级
    uint64_t m_lastPlayMs;              // 上次触发播报时间
    uint64_t m_levelChangeMs;            // 等级变化时间
};

#endif
