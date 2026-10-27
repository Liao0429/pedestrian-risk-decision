// =============================================================================
// test_hardware_modules.cc — 硬件驱动模块主机单元测试
// -----------------------------------------------------------------------------
// 验证点（全部使用 FakeGpio 内存替身，无需真实硬件）：
//   1. FakeGpio 基本行为（方向/写记录/读注入）；
//   2. RgbLed 风险等级颜色映射：SAFE=绿、WARNING=黄、DANGER=红、未知=灭；
//   3. RgbLed 直接颜色控制（红/绿/蓝/黄/灭）；
//   4. Button 防抖：<5 次抖动不触发，连续 5 次一致才确认按下；
//   5. Button 边沿检测：risingEdge / fallingEdge；
//   6. Button 低电平有效（activeHigh=false）场景。
//
// 编译（无需第三方依赖）：
//   g++ -std=c++11 -O2 -I../../hardware test_hardware_modules.cc -o hw_test
// =============================================================================

#include <cstdio>

#include "button.h"
#include "gpio.h"
#include "led.h"
#include "voice_alert.h"

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failed;                                                      \
        } else {                                                             \
            ++g_passed;                                                      \
        }                                                                    \
    } while (0)

// 运行 n 次按键采样
void runUpdates(hw::Button* btn, int n) {
    for (int i = 0; i < n; ++i) btn->update();
}

// ---------------------------------------------------------------------------
// 1. FakeGpio 基本行为
// ---------------------------------------------------------------------------
void testFakeGpio() {
    hw::FakeGpio gpio;
    CHECK(gpio.setDirection(true));
    CHECK(gpio.isOutput());
    CHECK(gpio.write(true));
    CHECK(gpio.lastWritten());
    CHECK(gpio.write(false));
    CHECK(!gpio.lastWritten());
    CHECK(gpio.writeCount() == 2);

    // 输出模式下不可读
    bool v = true;
    CHECK(!gpio.read(&v));

    // 输入模式
    hw::FakeGpio in(false);
    CHECK(in.setDirection(false));
    CHECK(in.read(&v));
    CHECK(!v);
    in.setInputLevel(true);
    CHECK(in.read(&v));
    CHECK(v);
    // 输入模式下不可写
    CHECK(!in.write(true));
}

// ---------------------------------------------------------------------------
// 2/3. RgbLed 颜色映射与控制
// ---------------------------------------------------------------------------
void testRgbLed() {
    hw::FakeGpio red(false), green(false), blue(false);
    hw::RgbLed led(&red, &green, &blue);
    CHECK(led.init());
    CHECK(led.available());
    // 上电默认全灭
    CHECK(!red.lastWritten() && !green.lastWritten() && !blue.lastWritten());

    // SAFE -> 绿
    led.setLevel(0);
    CHECK(!red.lastWritten() && green.lastWritten() && !blue.lastWritten());

    // WARNING -> 黄（红+绿）
    led.setLevel(1);
    CHECK(red.lastWritten() && green.lastWritten() && !blue.lastWritten());

    // DANGER -> 红
    led.setLevel(2);
    CHECK(red.lastWritten() && !green.lastWritten() && !blue.lastWritten());

    // 未知等级 -> 灭
    led.setLevel(99);
    CHECK(!red.lastWritten() && !green.lastWritten() && !blue.lastWritten());

    // 直接颜色控制
    led.setColor(hw::LedColor::BLUE);
    CHECK(!red.lastWritten() && !green.lastWritten() && blue.lastWritten());
    led.setColor(hw::LedColor::OFF);
    CHECK(!red.lastWritten() && !green.lastWritten() && !blue.lastWritten());
    led.setColor(hw::LedColor::YELLOW);
    CHECK(red.lastWritten() && green.lastWritten() && !blue.lastWritten());

    // 不可用模块：传入 NULL 时 init 失败但可安全调用
    hw::RgbLed bad((hw::Gpio*)0, (hw::Gpio*)0, (hw::Gpio*)0);
    CHECK(!bad.init());
    CHECK(!bad.available());
    bad.setLevel(2);  // 不应崩溃
}

// ---------------------------------------------------------------------------
// 4/5. Button 防抖与边沿检测（高电平有效）
// ---------------------------------------------------------------------------
void testButtonActiveHigh() {
    hw::FakeGpio gpio(false);
    hw::Button btn(&gpio, true, 5);
    CHECK(btn.init());
    CHECK(!btn.isPressed());

    // 抖动：不足 5 次连续一致，不触发
    gpio.setInputLevel(true);
    runUpdates(&btn, 2);   // 累积 2 次
    CHECK(!btn.isPressed());
    CHECK(!btn.risingEdge());
    gpio.setInputLevel(false);
    runUpdates(&btn, 1);   // 打断
    gpio.setInputLevel(true);
    runUpdates(&btn, 1);   // 重新累积 1 次
    CHECK(!btn.isPressed());
    gpio.setInputLevel(false);
    runUpdates(&btn, 3);   // 回到低电平
    CHECK(!btn.isPressed());
    CHECK(!btn.risingEdge());

    // 持续按下 5 次 -> 确认按下并产生上升沿
    gpio.setInputLevel(true);
    runUpdates(&btn, 5);
    CHECK(btn.isPressed());
    CHECK(btn.risingEdge());
    CHECK(!btn.fallingEdge());

    // 保持按下：不再产生新边沿
    runUpdates(&btn, 3);
    CHECK(btn.isPressed());
    CHECK(!btn.risingEdge());

    // 释放：连续 5 次低 -> 确认释放并产生下降沿
    gpio.setInputLevel(false);
    runUpdates(&btn, 5);
    CHECK(!btn.isPressed());
    CHECK(btn.fallingEdge());
    CHECK(!btn.risingEdge());

    // 保持释放：无新边沿
    runUpdates(&btn, 3);
    CHECK(!btn.isPressed());
    CHECK(!btn.fallingEdge());
}

// ---------------------------------------------------------------------------
// 6. Button 低电平有效
// ---------------------------------------------------------------------------
void testButtonActiveLow() {
    hw::FakeGpio gpio(true);  // 初始高 = 未按下
    hw::Button btn(&gpio, false, 5);
    CHECK(btn.init());
    CHECK(!btn.isPressed());

    gpio.setInputLevel(false);  // 按下（低电平）
    runUpdates(&btn, 5);
    CHECK(btn.isPressed());
    CHECK(btn.risingEdge());

    gpio.setInputLevel(true);   // 释放
    runUpdates(&btn, 5);
    CHECK(!btn.isPressed());
    CHECK(btn.fallingEdge());
}

// ---------------------------------------------------------------------------
// 7. VoiceAlert 语音告警状态机（FakeVoiceBackend 内存替身）
// ---------------------------------------------------------------------------
void testVoiceAlert() {
    using hw::AlertLevel;
    using hw::FakeVoiceBackend;
    using hw::VoiceAlert;

    // 7.1 FakeVoiceBackend 基本行为
    {
        FakeVoiceBackend fb;
        CHECK(fb.available());
        CHECK(fb.warningCount() == 0 && fb.dangerCount() == 0);
        fb.playWarning();
        fb.playDanger();
        CHECK(fb.warningCount() == 1 && fb.dangerCount() == 1);
        CHECK(fb.totalPlayCount() == 2);
        fb.setAvailable(false);
        fb.playWarning();
        CHECK(fb.warningCount() == 1);  // 不可用时不计数
        CHECK(!fb.available());
    }

    // 7.2 SAFE 不触发播报；SAFE→WARNING 跳变触发警告
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);  // 短周期便于测试
        va.update(AlertLevel::SAFE, 0);
        CHECK(fb.totalPlayCount() == 0);
        va.update(AlertLevel::WARNING, 1000);
        CHECK(fb.warningCount() == 1);
        CHECK(va.playingLevel() == AlertLevel::WARNING);
    }

    // 7.3 WARNING→DANGER 升级抢占（播危险语，不再播警告）
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);
        va.update(AlertLevel::WARNING, 1000);
        CHECK(fb.warningCount() == 1);
        va.update(AlertLevel::DANGER, 1100);
        CHECK(fb.dangerCount() == 1);
        CHECK(fb.warningCount() == 1);  // 未重复播警告
        CHECK(va.playingLevel() == AlertLevel::DANGER);
    }

    // 7.4 DANGER 维持时按周期重播（100ms）
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);
        va.update(AlertLevel::DANGER, 1000);
        CHECK(fb.dangerCount() == 1);
        va.update(AlertLevel::DANGER, 1050);  // 未到周期
        CHECK(fb.dangerCount() == 1);
        va.update(AlertLevel::DANGER, 1100);  // 到周期（100ms）
        CHECK(fb.dangerCount() == 2);
    }

    // 7.5 WARNING 维持时按周期重播（100ms）
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);
        va.update(AlertLevel::WARNING, 1000);
        CHECK(fb.warningCount() == 1);
        va.update(AlertLevel::WARNING, 1099);  // 差 1ms 不到
        CHECK(fb.warningCount() == 1);
        va.update(AlertLevel::WARNING, 1100);  // 到周期
        CHECK(fb.warningCount() == 2);
    }

    // 7.6 DANGER→WARNING 降级：冷却 200ms 内不立即播报，冷却后播报
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);
        va.update(AlertLevel::DANGER, 1000);
        CHECK(fb.dangerCount() == 1);
        va.update(AlertLevel::WARNING, 1100);   // 降级，距上次播放仅 100ms < 200ms
        CHECK(fb.warningCount() == 0);          // 冷却抑制
        CHECK(va.lastLevel() == AlertLevel::WARNING);
        va.update(AlertLevel::WARNING, 1300);   // 距上次播放 300ms >= 200ms 冷却
        CHECK(fb.warningCount() == 1);
    }

    // 7.7 SAFE 停止播报
    {
        FakeVoiceBackend fb;
        VoiceAlert va(&fb, 100, 100, 200);
        va.update(AlertLevel::DANGER, 1000);
        CHECK(fb.dangerCount() == 1);
        va.update(AlertLevel::SAFE, 1100);
        CHECK(fb.stopCount() == 1);
        CHECK(va.playingLevel() == AlertLevel::SAFE);
        // 继续 SAFE 不重复 stop
        va.update(AlertLevel::SAFE, 1200);
        CHECK(fb.stopCount() == 1);
    }

    // 7.8 后端不可用：update 不播报、不崩溃
    {
        FakeVoiceBackend fb;
        fb.setAvailable(false);
        VoiceAlert va(&fb, 100, 100, 200);
        CHECK(!va.available());
        va.update(AlertLevel::DANGER, 1000);
        va.update(AlertLevel::WARNING, 1100);
        va.update(AlertLevel::SAFE, 1200);
        CHECK(fb.totalPlayCount() == 0);
        CHECK(fb.stopCount() == 0);
    }

    // 7.9 NULL 后端：构造/update 安全
    {
        VoiceAlert va((hw::VoiceBackend*)0, 100, 100, 200);
        CHECK(!va.available());
        va.update(AlertLevel::DANGER, 1000);
        va.playWarning();
        va.stop();
        CHECK(va.playingLevel() == AlertLevel::SAFE);
    }
}

}  // namespace

int main() {
    std::printf("=== test_hardware_modules ===\n");
    testFakeGpio();
    testRgbLed();
    testButtonActiveHigh();
    testButtonActiveLow();
    testVoiceAlert();
    std::printf("passed=%d failed=%d\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}