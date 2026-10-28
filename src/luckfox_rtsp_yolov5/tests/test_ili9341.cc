// =============================================================================
// test_ili9341.cc — ILI9341 SPI 显示驱动主机单元测试
// -----------------------------------------------------------------------------
// 验证点（全部使用 FakeSpi / FakeLcdPins 内存替身，无需真实硬件）：
//   1. rgb565 颜色转换（红/绿/蓝/白/黑及边界值）；
//   2. 生命周期：NULL 容错、open/close、重复 begin、close 后重新初始化；
//   3. begin() 复位时序（resetLow 先于 resetHigh）、延时回调、背光开启；
//   4. 初始化命令序列（SWRESET->SLPOUT->COLMOD 0x55->MADCTL 0x08->DISPON）
//      与命令/数据阶段切换（DC 引脚计数）；
//   5. setWindow：CASET/PASET 窗口参数字节序（大端）；
//   6. fillRect：像素数、颜色字节序（高字节在前）、边界裁剪、完全越界拒绝；
//   7. drawPixel / fillScreen / 帧缓冲 setPixel/pixelAt/clear；
//   8. flush()：整帧 153600 字节分批推送，首尾像素正确；
//   9. readId()：0xD3 读回 00 93 41 00（ILI9341 ID，供硬件自检）；
//  10. 未 open 时所有操作安全返回 false。
//
// 编译（无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I../../hardware test_ili9341.cc -o test_ili9341
// =============================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ili9341.h"

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

// -----------------------------------------------------------------------------
// 测试替身
// -----------------------------------------------------------------------------
class FakeSpi : public hw::SpiBus {
public:
    bool openResult = true;
    bool failTransfer = false;
    int openCount = 0;
    int closeCount = 0;
    std::vector<uint8_t> writes;
    std::vector<uint8_t> reads;   // 读回放数据
    size_t readIdx = 0;

    bool open() override {
        if (!openResult) return false;
        ++openCount;
        return true;
    }
    void close() override { ++closeCount; }
    bool isOpen() const override { return openCount > closeCount; }

    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        if (failTransfer) return false;
        if (tx) writes.insert(writes.end(), tx, tx + len);
        if (rx) {
            for (size_t i = 0; i < len; ++i) {
                rx[i] = readIdx < reads.size() ? reads[readIdx++] : 0;
            }
        }
        return true;
    }

    size_t writeSize() const { return writes.size(); }
};

class FakeLcdPins : public hw::LcdPins {
public:
    bool failDc = false;
    bool failReset = false;
    bool failBl = false;
    int dcCmdCount = 0;
    int dcDataCount = 0;
    int resetLowCount = 0;
    int resetHighCount = 0;
    int blOnCount = 0;
    int blOffCount = 0;
    // 阶段序列：C=命令 DC 低，D=数据 DC 高，L=复位低，H=复位高，B=背光开
    std::string seq;

    bool dcCommand() override {
        if (failDc) return false;
        ++dcCmdCount;
        seq += 'C';
        return true;
    }
    bool dcData() override {
        if (failDc) return false;
        ++dcDataCount;
        seq += 'D';
        return true;
    }
    bool resetLow() override {
        if (failReset) return false;
        ++resetLowCount;
        seq += 'L';
        return true;
    }
    bool resetHigh() override {
        if (failReset) return false;
        ++resetHighCount;
        seq += 'H';
        return true;
    }
    bool backlight(bool on) override {
        if (failBl) return false;
        if (on) {
            ++blOnCount;
            seq += 'B';
        } else {
            ++blOffCount;
            seq += 'b';
        }
        return true;
    }
};

// 延时回调：累计毫秒数
uint32_t g_delayMs = 0;
void fakeDelay(uint32_t ms) { g_delayMs += ms; }

// 在 v 中从 start 开始查找子序列 pat
bool hasSub(const std::vector<uint8_t>& v, const std::vector<uint8_t>& pat,
            size_t start = 0) {
    if (pat.empty() || pat.size() > v.size()) return false;
    for (size_t i = start; i + pat.size() <= v.size(); ++i) {
        size_t j = 0;
        for (; j < pat.size(); ++j) {
            if (v[i + j] != pat[j]) break;
        }
        if (j == pat.size()) return true;
    }
    return false;
}

// 返回最后一个字节 b 的位置，找不到返回 SIZE_MAX
size_t lastOf(const std::vector<uint8_t>& v, uint8_t b) {
    for (size_t i = v.size(); i > 0; --i) {
        if (v[i - 1] == b) return i - 1;
    }
    return SIZE_MAX;
}

// -----------------------------------------------------------------------------
// 用例
// -----------------------------------------------------------------------------
void testRgb565() {
    CHECK(hw::rgb565(255, 0, 0) == 0xF800);    // 红
    CHECK(hw::rgb565(0, 255, 0) == 0x07E0);    // 绿
    CHECK(hw::rgb565(0, 0, 255) == 0x001F);    // 蓝
    CHECK(hw::rgb565(255, 255, 255) == 0xFFFF); // 白
    CHECK(hw::rgb565(0, 0, 0) == 0x0000);      // 黑
    CHECK(hw::rgb565(128, 128, 128) == 0x8410); // 中灰（RGB565 舍入）
}

void testNullTolerance() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 nullBoth(nullptr, nullptr);
    CHECK(nullBoth.begin() == false);
    CHECK(nullBoth.isOpen() == false);

    hw::Ili9341 nullSpi(nullptr, &pins);
    CHECK(nullSpi.begin() == false);

    hw::Ili9341 nullPins(&spi, nullptr);
    CHECK(nullPins.begin() == false);
    // 未打开时 SPI 不应被 open
    CHECK(spi.openCount == 0);
}

void testOpenClose() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);

    CHECK(lcd.isOpen() == false);
    CHECK(lcd.open() == true);
    CHECK(lcd.isOpen() == true);
    CHECK(spi.openCount == 1);
    CHECK(lcd.framebuffer().size() == 320u * 240u);

    // 重复 open 不重复打开 SPI
    CHECK(lcd.open() == true);
    CHECK(spi.openCount == 1);

    lcd.close();
    CHECK(lcd.isOpen() == false);
    CHECK(spi.closeCount == 1);
    CHECK(lcd.framebuffer().empty());

    // SPI open 失败
    FakeSpi badSpi;
    badSpi.openResult = false;
    hw::Ili9341 badLcd(&badSpi, &pins);
    CHECK(badLcd.open() == false);
    CHECK(badLcd.isOpen() == false);
}

void testBeginResetAndInit() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    lcd.setDelayFn(fakeDelay);
    g_delayMs = 0;

    CHECK(lcd.begin() == true);

    // 复位脉冲：复位低先于复位高
    CHECK(pins.resetLowCount >= 1);
    CHECK(pins.resetHighCount >= 1);
    size_t posL = pins.seq.find('L');
    size_t posH = pins.seq.find('H');
    CHECK(posL != std::string::npos && posH != std::string::npos);
    CHECK(posL < posH);

    // 延时：复位低 120ms + 复位高 120ms
    CHECK(g_delayMs >= 240);

    // 背光开启
    CHECK(pins.blOnCount >= 1);

    // 初始化命令序列（命令与数据字节连续流）：
    //   SWRESET(01) SLPOUT(11) COLMOD(3A)->55 MADCTL(36)->08 DISPON(29)
    static const std::vector<uint8_t> expect = {
        0x01,
        0x11,
        0x3A, 0x55,
        0x36, 0x08,
        0x29,
    };
    CHECK(hasSub(spi.writes, expect));
    // 数据阶段确实发生过（DC 高）
    CHECK(pins.dcDataCount >= 2);
    CHECK(pins.dcCmdCount >= 5);

    // 帧缓冲已分配
    CHECK(lcd.framebuffer().size() == 320u * 240u);
}

void testBeginFailures() {
    // 复位失败
    {
        FakeSpi spi;
        FakeLcdPins pins;
        pins.failReset = true;
        hw::Ili9341 lcd(&spi, &pins);
        CHECK(lcd.begin() == false);
    }
    // DC 失败（初始化命令阶段）
    {
        FakeSpi spi;
        FakeLcdPins pins;
        pins.failDc = true;
        hw::Ili9341 lcd(&spi, &pins);
        CHECK(lcd.begin() == false);
    }
    // 背光失败
    {
        FakeSpi spi;
        FakeLcdPins pins;
        pins.failBl = true;
        hw::Ili9341 lcd(&spi, &pins);
        CHECK(lcd.begin() == false);
    }
    // SPI 传输失败
    {
        FakeSpi spi;
        FakeLcdPins pins;
        spi.failTransfer = true;
        hw::Ili9341 lcd(&spi, &pins);
        CHECK(lcd.begin() == false);
    }
}

void testSetWindow() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    const size_t base = spi.writeSize();
    CHECK(lcd.setWindow(10, 20, 30, 40) == true);

    // CASET: 0x2A, data 00 0A 00 1E（x0=10, x1=30 大端）
    static const std::vector<uint8_t> caset = { 0x2A, 0x00, 0x0A, 0x00, 0x1E };
    // PASET: 0x2B, data 00 14 00 28（y0=20, y1=40 大端）
    static const std::vector<uint8_t> paset = { 0x2B, 0x00, 0x14, 0x00, 0x28 };
    CHECK(hasSub(spi.writes, caset, base));
    CHECK(hasSub(spi.writes, paset, base));

    // 越界窗口（x0>x1）拒绝
    const size_t before = spi.writeSize();
    CHECK(lcd.setWindow(50, 0, 10, 10) == false);
    CHECK(spi.writeSize() == before);
}

void testFillRect() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    // fillRect(10,20,30,40)：21x21 = 441 像素 = 882 字节，颜色 0x8410
    const uint16_t color = hw::rgb565(128, 128, 128);  // 0x8410
    const size_t base = spi.writeSize();
    CHECK(lcd.fillRect(10, 20, 21, 21, color) == true);

    size_t pos2c = lastOf(spi.writes, 0x2C);
    CHECK(pos2c != SIZE_MAX);
    CHECK(pos2c >= base);
    const size_t dataLen = spi.writeSize() - pos2c - 1;
    CHECK(dataLen == 441u * 2u);
    // 颜色字节序：高字节在前
    CHECK(spi.writes[pos2c + 1] == static_cast<uint8_t>(color >> 8));
    CHECK(spi.writes[pos2c + 2] == static_cast<uint8_t>(color & 0xFF));
    // 最后一个像素也是同色
    CHECK(spi.writes.back() == static_cast<uint8_t>(color & 0xFF));

    // 裁剪：fillRect(-5,-5,100,100) -> x0=0,y0=0,x1=94,y1=94 -> 95x95=9025
    const size_t base2 = spi.writeSize();
    CHECK(lcd.fillRect(-5, -5, 100, 100, 0x0000) == true);
    static const std::vector<uint8_t> casetClipped = { 0x2A, 0x00, 0x00, 0x00, 0x5E };
    static const std::vector<uint8_t> pasetClipped = { 0x2B, 0x00, 0x00, 0x00, 0x5E };
    CHECK(hasSub(spi.writes, casetClipped, base2));
    CHECK(hasSub(spi.writes, pasetClipped, base2));
    size_t pos2c2 = lastOf(spi.writes, 0x2C);
    CHECK(spi.writeSize() - pos2c2 - 1 == 9025u * 2u);

    // 裁剪：fillRect(300,200,100,100) -> x0=300,y0=200,x1=319,y1=239 -> 20x40=800
    const size_t base3 = spi.writeSize();
    CHECK(lcd.fillRect(300, 200, 100, 100, 0xFFFF) == true);
    static const std::vector<uint8_t> casetRight = { 0x2A, 0x01, 0x2C, 0x01, 0x3F };
    static const std::vector<uint8_t> pasetBottom = { 0x2B, 0x00, 0xC8, 0x00, 0xEF };
    CHECK(hasSub(spi.writes, casetRight, base3));
    CHECK(hasSub(spi.writes, pasetBottom, base3));
    size_t pos2c3 = lastOf(spi.writes, 0x2C);
    CHECK(spi.writeSize() - pos2c3 - 1 == 800u * 2u);

    // 完全越界：fillRect(320,0,10,10) 与 fillRect(0,240,10,10)
    const size_t before = spi.writeSize();
    CHECK(lcd.fillRect(320, 0, 10, 10, 0x0000) == false);
    CHECK(lcd.fillRect(0, 240, 10, 10, 0x0000) == false);
    CHECK(lcd.fillRect(10, 10, 0, 5, 0x0000) == false);
    CHECK(lcd.fillRect(10, 10, 5, -1, 0x0000) == false);
    CHECK(spi.writeSize() == before);

    // 未 open 时拒绝
    FakeSpi spi2;
    FakeLcdPins pins2;
    hw::Ili9341 closed(&spi2, &pins2);
    CHECK(closed.fillRect(0, 0, 10, 10, 0) == false);
    CHECK(spi2.writeSize() == 0);
}

void testDrawPixel() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    CHECK(lcd.drawPixel(100, 50, 0xF800) == true);
    size_t pos2c = lastOf(spi.writes, 0x2C);
    CHECK(pos2c != SIZE_MAX);
    CHECK(spi.writeSize() - pos2c - 1 == 2u);   // 单像素 2 字节

    // 越界拒绝
    const size_t before = spi.writeSize();
    CHECK(lcd.drawPixel(-1, 0, 0) == false);
    CHECK(lcd.drawPixel(0, 240, 0) == false);
    CHECK(lcd.drawPixel(320, 0, 0) == false);
    CHECK(spi.writeSize() == before);
}

void testFillScreen() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    const uint16_t color = 0x07E0;  // 绿
    CHECK(lcd.fillScreen(color) == true);
    size_t pos2c = lastOf(spi.writes, 0x2C);
    CHECK(pos2c != SIZE_MAX);
    const size_t totalPixels = 320u * 240u;
    CHECK(spi.writeSize() - pos2c - 1 == totalPixels * 2u);
    // 首像素与末像素颜色一致
    CHECK(spi.writes[pos2c + 1] == 0x07);
    CHECK(spi.writes[pos2c + 2] == 0xE0);
    CHECK(spi.writes.back() == 0xE0);
}

void testFramebuffer() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    // 初始全 0
    CHECK(lcd.pixelAt(0, 0) == 0);
    CHECK(lcd.pixelAt(319, 239) == 0);

    // setPixel / pixelAt
    CHECK(lcd.setPixel(10, 20, 0xF800) == true);
    CHECK(lcd.pixelAt(10, 20) == 0xF800);
    CHECK(lcd.pixelAt(11, 20) == 0);
    CHECK(lcd.setPixel(319, 239, 0xFFFF) == true);
    CHECK(lcd.pixelAt(319, 239) == 0xFFFF);

    // 越界 setPixel 拒绝
    CHECK(lcd.setPixel(-1, 0, 1) == false);
    CHECK(lcd.setPixel(0, 240, 1) == false);
    CHECK(lcd.pixelAt(-1, 0) == 0);
    CHECK(lcd.pixelAt(320, 0) == 0);

    // clear
    lcd.clear(0x8410);
    CHECK(lcd.pixelAt(0, 0) == 0x8410);
    CHECK(lcd.pixelAt(319, 239) == 0x8410);
    CHECK(lcd.framebuffer().size() == 76800u);
    lcd.clear();
    CHECK(lcd.pixelAt(0, 0) == 0);
}

void testFlush() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    // 帧缓冲写几个像素后整帧推送
    lcd.setPixel(0, 0, 0xF800);
    lcd.setPixel(1, 0, 0x07E0);
    lcd.setPixel(319, 239, 0x001F);
    const size_t base = spi.writeSize();
    CHECK(lcd.flush() == true);

    size_t pos2c = lastOf(spi.writes, 0x2C);
    CHECK(pos2c >= base);
    CHECK(spi.writeSize() - pos2c - 1 == 76800u * 2u);
    // 首像素 = fb[0] = 0xF800（高字节在前）
    CHECK(spi.writes[pos2c + 1] == 0xF8);
    CHECK(spi.writes[pos2c + 2] == 0x00);
    // 第二个像素 = fb[1] = 0x07E0
    CHECK(spi.writes[pos2c + 3] == 0x07);
    CHECK(spi.writes[pos2c + 4] == 0xE0);
    // 末像素 = fb[last] = 0x001F
    CHECK(spi.writes.back() == 0x1F);

    // 未 open 拒绝
    FakeSpi spi2;
    FakeLcdPins pins2;
    hw::Ili9341 closed(&spi2, &pins2);
    CHECK(closed.flush() == false);
    CHECK(spi2.writeSize() == 0);
}

void testReadId() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.begin() == true);

    // 设置读回数据：ILI9341 版本 ID 00 93 41 00
    spi.reads = { 0x00, 0x93, 0x41, 0x00 };
    uint8_t id[4] = { 0, 0, 0, 0 };
    CHECK(lcd.readId(id) == true);
    CHECK(id[0] == 0x00);
    CHECK(id[1] == 0x93);
    CHECK(id[2] == 0x41);
    CHECK(id[3] == 0x00);
    // 发送了 0xD3 命令
    CHECK(hasSub(spi.writes, std::vector<uint8_t>{ 0xD3 }));

    // 未 open 拒绝
    FakeSpi spi2;
    FakeLcdPins pins2;
    hw::Ili9341 closed(&spi2, &pins2);
    uint8_t id2[4] = { 0, 0, 0, 0 };
    CHECK(closed.readId(id2) == false);
    // NULL 输出指针
    CHECK(lcd.readId(nullptr) == false);
}

void testReopen() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);

    CHECK(lcd.begin() == true);
    const size_t firstLen = spi.writeSize();
    lcd.close();
    CHECK(lcd.begin() == true);
    CHECK(spi.openCount == 2);
    CHECK(spi.writeSize() > firstLen);
    // 第二次初始化同样包含完整命令序列
    static const std::vector<uint8_t> expect = {
        0x01, 0x11, 0x3A, 0x55, 0x36, 0x08, 0x29,
    };
    CHECK(hasSub(spi.writes, expect, firstLen));
}

}  // namespace

int main() {
    testRgb565();
    testNullTolerance();
    testOpenClose();
    testBeginResetAndInit();
    testBeginFailures();
    testSetWindow();
    testFillRect();
    testDrawPixel();
    testFillScreen();
    testFramebuffer();
    testFlush();
    testReadId();
    testReopen();

    std::printf("test_ili9341: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}