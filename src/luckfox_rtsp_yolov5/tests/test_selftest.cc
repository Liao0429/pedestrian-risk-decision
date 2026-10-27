// =============================================================================
// test_selftest.cc — 硬件自检框架主机单元测试
// -----------------------------------------------------------------------------
// 验证点（全部使用 FakeGpio/FakePwm/FakeSelftestProbe 内存替身，无需硬件）：
//   1. 全部外设正常 -> 6 项全 PASS，allPassed=true；
//   2. GPIO 写读回失败（输出读回不可用）-> FAIL；
//   3. PWM 配置失败（BadPwm）-> FAIL；
//   4. SPI 设备节点缺失 / ID 不匹配 0x9341 -> FAIL；
//   5. USB 声卡未检测到（无 USB 字样）-> FAIL；
//   6. wlan0 无 IP -> FAIL；
//   7. 按键 GPIO 读取失败 -> FAIL；
//   8. NULL 注入容错：全部 FAIL 不崩溃；
//   9. 自定义 SPI 设备路径 / 探测命令 setter 生效；
//   10. printReport 输出不崩溃。
//
// 编译（无第三方依赖）：
//   g++ -std=c++11 -O2 -I../../hardware test_selftest.cc -o selftest_test
// =============================================================================

#include <cstdio>
#include <cstring>

#include "gpio.h"
#include "selftest.h"
#include "servo.h"

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

// 永远失败的 PWM（模拟 PWM 节点不可用）
class BadPwm : public hw::Pwm {
public:
    bool configure(long) override { return false; }
    bool setDutyCycle(long) override { return false; }
    bool enable(bool) override { return false; }
};

// 自定义探测命令（避免依赖默认字符串，同时验证 setter 生效）
const char* kSpiCmd   = "lcd_id_probe";
const char* kAudioCmd = "aplay -l";
const char* kWifiCmd  = "ip addr show wlan0";

// 构造一个全部外设正常的环境
hw::FakeSelftestProbe makeGoodProbe() {
    hw::FakeSelftestProbe p;
    p.setFile("/dev/spidev0.0", true);
    p.setCmdOutput(kSpiCmd, "ilitek,ili9341\n");
    p.setCmdOutput(kAudioCmd,
                   "card 1: USB Audio [USB Audio], device 0: USB Audio [USB Audio]\n");
    p.setCmdOutput(kWifiCmd,
                   "1: wlan0    inet 192.168.0.136/24 brd 192.168.0.255 scope global wlan0\n");
    return p;
}

// ---------------------------------------------------------------------------
// 0. FakeSelftestProbe 基本行为
// ---------------------------------------------------------------------------
void testFakeProbe() {
    hw::FakeSelftestProbe p;
    CHECK(!p.fileExists("/nonexistent"));
    char buf[8] = {0};
    CHECK(!p.runCmd("echo hi", buf, sizeof(buf)));  // 未注入 -> 失败
    p.setCmdOutput("echo hi", "hi\n");
    CHECK(p.runCmd("echo hi", buf, sizeof(buf)));
    CHECK(std::strcmp(buf, "hi\n") == 0);
    p.setFile("/dev/spidev0.0", true);
    CHECK(p.fileExists("/dev/spidev0.0"));
    CHECK(!p.fileExists("/dev/spidev1.0"));
}

// ---------------------------------------------------------------------------
// 1. 全部外设正常 -> 6 项全 PASS
// ---------------------------------------------------------------------------
void testAllPass() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);   // 启用输出读回
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(st.runAll());
    CHECK(st.allPassed());
    CHECK(st.passedCount() == 6);
    CHECK(st.failedCount() == 0);

    // GPIO 写读回确实执行了两次写
    CHECK(loop.writeCount() == 2);
    CHECK(loop.isOutput());

    // 各单项 PASS 且 detail 有内容
    for (int i = 0; i < (int)hw::SelftestItem::COUNT; ++i) {
        CHECK(st.result(i).pass);
        CHECK(st.result(i).detail[0] != '\0');
        CHECK(hw::selftestItemName(st.result(i).item) != NULL);
    }
    CHECK(std::strstr(st.result((int)hw::SelftestItem::SPI_LCD_ID).detail,
                         "0x9341") != NULL);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::USB_AUDIO).detail,
                         "USB") != NULL);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::WIFI_LINK).detail,
                         "wlan0") != NULL);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::BUTTON_LEVEL).detail,
                         "LOW") != NULL);
}

// ---------------------------------------------------------------------------
// 2. GPIO 写读回失败（输出模式未启用读回）
// ---------------------------------------------------------------------------
void testGpioLoopbackFail() {
    hw::FakeGpio loop(false);   // 未启用输出读回 -> 写高读回失败
    hw::FakeGpio btn(false);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.allPassed());
    CHECK(st.failedCount() == 1);
    CHECK(st.passedCount() == 5);
    CHECK(!st.result((int)hw::SelftestItem::GPIO_LOOPBACK).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::GPIO_LOOPBACK).detail,
                         "写高读回失败") != NULL);
}

// ---------------------------------------------------------------------------
// 3. PWM 配置失败
// ---------------------------------------------------------------------------
void testPwmFail() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    BadPwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(st.failedCount() == 1);
    CHECK(!st.result((int)hw::SelftestItem::PWM_NODE).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::PWM_NODE).detail,
                         "configure") != NULL);
}

// ---------------------------------------------------------------------------
// 4. SPI 设备节点缺失
// ---------------------------------------------------------------------------
void testSpiNodeMissing() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    probe.setFile("/dev/spidev0.0", false);   // 节点缺失
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.result((int)hw::SelftestItem::SPI_LCD_ID).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::SPI_LCD_ID).detail,
                         "未找到 SPI 设备节点") != NULL);
}

// ---------------------------------------------------------------------------
// 5. SPI ID 不匹配 0x9341
// ---------------------------------------------------------------------------
void testSpiIdMismatch() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    probe.setCmdOutput(kSpiCmd, "unknown lcd\n");   // 非 9341
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.result((int)hw::SelftestItem::SPI_LCD_ID).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::SPI_LCD_ID).detail,
                         "不匹配 0x9341") != NULL);
}

// ---------------------------------------------------------------------------
// 6. USB 声卡未检测到（无 USB 字样）
// ---------------------------------------------------------------------------
void testUsbAudioFail() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    probe.setCmdOutput(kAudioCmd,
                       "card 0: AK5370 [AK5370], device 0: AK5370 [AK5370]\n");
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.result((int)hw::SelftestItem::USB_AUDIO).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::USB_AUDIO).detail,
                         "未检测到 USB 声卡") != NULL);
}

// ---------------------------------------------------------------------------
// 7. wlan0 无 IP
// ---------------------------------------------------------------------------
void testWifiNoIp() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    probe.setCmdOutput(kWifiCmd, "2: wlan0    <NO-CARRIER> ...\n");  // 无 inet
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.result((int)hw::SelftestItem::WIFI_LINK).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::WIFI_LINK).detail,
                         "未获取 IP") != NULL);
}

// 按键读取失败专用替身：setDirection 成功但 read 永远失败
class FailReadGpio : public hw::Gpio {
public:
    bool setDirection(bool) override { return true; }
    bool write(bool) override { return true; }
    bool read(bool*) override { return false; }
};

// ---------------------------------------------------------------------------
// 8. 按键 GPIO 读取失败
// ---------------------------------------------------------------------------
void testButtonReadFail() {
    hw::FakeGpio loop(false);
    loop.setOutputReadback(true);
    FailReadGpio btn;   // read 永远失败
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(!st.runAll());
    CHECK(!st.result((int)hw::SelftestItem::BUTTON_LEVEL).pass);
    CHECK(std::strstr(st.result((int)hw::SelftestItem::BUTTON_LEVEL).detail,
                         "读取失败") != NULL);
}

// ---------------------------------------------------------------------------
// 9. NULL 注入容错：全部 FAIL 不崩溃
// ---------------------------------------------------------------------------
void testNullTolerance() {
    hw::HardwareSelfTest st((hw::Gpio*)0, (hw::Pwm*)0, (hw::SelftestProbe*)0);
    CHECK(!st.runAll());
    CHECK(!st.allPassed());
    CHECK(st.passedCount() == 0);
    CHECK(st.failedCount() == (int)hw::SelftestItem::COUNT);
    // 各单项 detail 非空
    for (int i = 0; i < (int)hw::SelftestItem::COUNT; ++i) {
        CHECK(!st.result(i).pass);
        CHECK(st.result(i).detail[0] != '\0');
    }
}

// ---------------------------------------------------------------------------
// 10. 自定义 SPI 设备路径生效
// ---------------------------------------------------------------------------
void testSpiDeviceCustom() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    probe.setFile("/dev/spidev1.0", true);   // 自定义路径存在
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiDevice("/dev/spidev1.0");
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);

    CHECK(st.runAll());
    CHECK(st.allPassed());
}

// ---------------------------------------------------------------------------
// 11. printReport 输出不崩溃
// ---------------------------------------------------------------------------
void testPrintReport() {
    hw::FakeGpio loop(false), btn(false);
    loop.setOutputReadback(true);
    hw::FakePwm pwm;
    hw::FakeSelftestProbe probe = makeGoodProbe();
    hw::HardwareSelfTest st(&loop, &pwm, &probe);
    st.setButtonGpio(&btn);
    st.setSpiIdCmd(kSpiCmd);
    st.setAudioProbeCmd(kAudioCmd);
    st.setWifiProbeCmd(kWifiCmd);
    CHECK(st.runAll());

    FILE* f = tmpfile();
    CHECK(f != NULL);
    st.printReport(f);
    std::rewind(f);
    char line[256];
    bool sawPass = false;
    while (fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "[PASS]", 6) == 0) sawPass = true;
    }
    fclose(f);
    CHECK(sawPass);   // 报告里至少有一行 PASS
}

}  // namespace

int main() {
    std::printf("=== test_selftest ===\n");
    testFakeProbe();
    testAllPass();
    testGpioLoopbackFail();
    testPwmFail();
    testSpiNodeMissing();
    testSpiIdMismatch();
    testUsbAudioFail();
    testWifiNoIp();
    testButtonReadFail();
    testNullTolerance();
    testSpiDeviceCustom();
    testPrintReport();
    std::printf("passed=%d failed=%d\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}