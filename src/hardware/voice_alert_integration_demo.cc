// voice_alert_integration_demo.cc
// 来自 docs/hardware/ 方案文档，供 Luckfox 开发板硬件扩展集成参考。
// 实际集成时请按板面丝印和 /sys/class/pwm、/dev/ttyS* 实际节点核对。

#include "voice_alert.h"
// ...
VoiceAlert voiceAlert;
voiceAlert.init("/dev/ttyS4", 30);   // 打开 UART4，音量 30

while(1) {
    // ... 原有检测 ...
    riskEngine.computeFeatures();
    CrowdLevel crowd = riskEngine.evaluate();
    voiceAlert.update(crowd, (uint64_t)(TEST_COMM_GetNowUs() / 1000));  // 传当前毫秒
    // ... 其余绘图/推流 ...
}
