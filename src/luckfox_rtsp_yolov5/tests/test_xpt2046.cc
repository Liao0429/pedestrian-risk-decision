// =============================================================================
// test_xpt2046.cc — XPT2046 电阻触摸控制器驱动主机单元测试
// -----------------------------------------------------------------------------
// 验证点（全部使用 FakeSpi / FakeTouchPin 内存替身，无需真实硬件）：
//   1. 命令字节：读 X 发送 0xD0、读 Y 发送 0x90、读压力发送 0xB0/0xC0；
//   2. 数据提取：16 位读回值 -> 12 位 ADC（默认 >>4），dataShift 可调；
//   3. 生命周期：NULL 容错、open/close、重复 open、close 后重新初始化；
//   4. 触摸判定：PENIRQ 按下/释放；无 PENIRQ 时压力阈值（|Z1-Z2|）；
//   5. 压力读取：Z1/Z2 命令序列与回读值；
//   6. 坐标转换：线性映射、边界 clamp、镜像（invertX/invertY）、轴交换
//      （swapXY）、未校准默认全范围、屏幕尺寸；
//   7. 抗噪：N 次采样取中值（sampleCount 可调，异常值被过滤）；
//   8. 故障注入：SPI 传输失败、采样中途失败、PENIRQ 异常；
//   9. 未 open 时所有操作安全返回 false。
//
// 编译（无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I../../hardware test_xpt2046.cc -o test_xpt2046
// =============================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "xpt2046.h"

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

class FakeTouchPin : public hw::TouchPin {
public:
    bool pressed = false;
    int readCount = 0;
    bool failRead = false;

    bool isPressed() override {
        ++readCount;
        if (failRead) return false;
        return pressed;
    }
};

// -----------------------------------------------------------------------------
// 1. 命令字节（读数据时 tx=NULL 纯读，FakeSpi 只记录命令字节）
// -----------------------------------------------------------------------------
void testCommandBytes() {
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();

    uint16_t v = 0;
    CHECK(tp.readRawX(v));
    CHECK(spi.writeSize() == 1);            // 仅 1 字节命令写入
    CHECK(spi.writes[0] == hw::XPT2046_CMD_X);

    spi.writes.clear();
    CHECK(tp.readRawY(v));
    CHECK(spi.writes.size() == 1);
    CHECK(spi.writes[0] == hw::XPT2046_CMD_Y);

    spi.writes.clear();
    uint16_t z1 = 0, z2 = 0;
    CHECK(tp.readPressure(z1, z2));
    CHECK(spi.writes.size() == 2);          // Z1 + Z2 两个命令
    CHECK(spi.writes[0] == hw::XPT2046_CMD_Z1);
    CHECK(spi.writes[1] == hw::XPT2046_CMD_Z2);
}

// -----------------------------------------------------------------------------
// 2. 数据提取
// -----------------------------------------------------------------------------
void testDataExtraction() {
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();

    // 回放：0xAB 0xCD -> 16 位 0xABCD，默认 >>4 = 0xABC
    spi.reads = { 0xAB, 0xC0 };
    spi.readIdx = 0;
    uint16_t x = 0;
    CHECK(tp.readRawX(x));
    CHECK(x == 0xABC);

    // extract12 直接验证
    CHECK(tp.extract12(0xABCD) == 0xABC);
    CHECK(tp.extract12(0xFFFF) == 0xFFF);
    CHECK(tp.extract12(0x0000) == 0x000);
    CHECK(tp.extract12(0x1234) == 0x123);

    // dataShift 可调（模拟转换延迟导致数据右移 1 位）
    tp.setDataShift(3);
    CHECK(tp.extract12(0xABCD) == 0x579);   // (0xABCD >> 3) & 0x0FFF = 0x579
    tp.setDataShift(0);
    CHECK(tp.extract12(0xABCD) == 0xBCD);    // 低 12 位
    tp.setDataShift(4);                      // 恢复默认
    CHECK(tp.extract12(0xABCD) == 0xABC);

    // 位序验证（高位字节在前）
    spi.reads = { 0x0F, 0xF0 };
    spi.readIdx = 0;
    CHECK(tp.readRawX(x));
    CHECK(x == 0x0FF);
}

// -----------------------------------------------------------------------------
// 3. 生命周期与 NULL 容错
// -----------------------------------------------------------------------------
void testOpenClose() {
    // NULL SPI
    hw::Xpt2046 tpNull(nullptr);
    CHECK(tpNull.open() == false);
    CHECK(tpNull.isOpen() == false);
    uint16_t v = 0;
    CHECK(tpNull.readRawX(v) == false);
    CHECK(tpNull.readX(v) == false);
    int px = 0, py = 0;
    CHECK(tpNull.readPoint(px, py) == false);
    CHECK(tpNull.isTouched() == false);

    // 正常 open/close
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    CHECK(tp.open());
    CHECK(tp.isOpen());
    CHECK(spi.openCount == 1);

    // 重复 open 不重复打开 SPI
    CHECK(tp.open());
    CHECK(spi.openCount == 1);

    tp.close();
    CHECK(!tp.isOpen());
    CHECK(spi.closeCount == 1);

    // 关闭后操作返回 false
    CHECK(tp.readRawX(v) == false);

    // 重新打开可再次工作
    CHECK(tp.open());
    CHECK(spi.openCount == 2);
    spi.reads = { 0x12, 0x30 };
    spi.readIdx = 0;
    CHECK(tp.readRawX(v));
    CHECK(v == 0x123);
}

// -----------------------------------------------------------------------------
// 4. 触摸判定（PENIRQ 与压力阈值）
// -----------------------------------------------------------------------------
void testTouchDetection() {
    // PENIRQ 路径
    FakeSpi spi;
    FakeTouchPin pen;
    hw::Xpt2046 tp(&spi, &pen);
    tp.open();

    pen.pressed = false;
    CHECK(tp.isTouched() == false);
    CHECK(pen.readCount >= 1);

    pen.pressed = true;
    CHECK(tp.isTouched() == true);

    // 无 PENIRQ 时走压力阈值
    hw::Xpt2046 tpNoPen(&spi);
    tpNoPen.open();
    // Z1=0x300, Z2=0x100 -> 差 0x200 = 512 >= 50 -> 触摸
    spi.reads = { 0x30, 0x00, 0x10, 0x00 };
    spi.readIdx = 0;
    CHECK(tpNoPen.isTouched() == true);

    // 差值小于阈值 -> 未触摸
    spi.reads = { 0x10, 0x00, 0x0F, 0xF0 };   // Z1=0x100, Z2=0x0FF, 差=1
    spi.readIdx = 0;
    CHECK(tpNoPen.isTouched() == false);

    // 阈值可调
    tpNoPen.setPressureThreshold(10);
    spi.reads = { 0x10, 0x00, 0x0F, 0xF0 };   // 差=1 < 10
    spi.readIdx = 0;
    CHECK(tpNoPen.isTouched() == false);
    tpNoPen.setPressureThreshold(1);
    spi.reads = { 0x10, 0x00, 0x0F, 0xF0 };   // 差=1 >= 1
    spi.readIdx = 0;
    CHECK(tpNoPen.isTouched() == true);

    // 压力读取失败 -> 判定为未触摸
    spi.failTransfer = true;
    CHECK(tpNoPen.isTouched() == false);
    spi.failTransfer = false;
}

// -----------------------------------------------------------------------------
// 5. 压力读取
// -----------------------------------------------------------------------------
void testPressure() {
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();

    // Z1=0xABC, Z2=0x321
    spi.reads = { 0xAB, 0xCD, 0x32, 0x10 };
    spi.readIdx = 0;
    uint16_t z1 = 0, z2 = 0;
    CHECK(tp.readPressure(z1, z2));
    CHECK(z1 == 0xABC);
    CHECK(z2 == 0x321);

    // 采样模式不影响压力读取（直接原始读）
    tp.setSampleCount(1);
    spi.reads = { 0x0F, 0xFF, 0x0A, 0x00 };
    spi.readIdx = 0;
    CHECK(tp.readPressure(z1, z2));
    CHECK(z1 == 0x0FF);
    CHECK(z2 == 0x0A0);
}

// -----------------------------------------------------------------------------
// 6. 坐标转换
// -----------------------------------------------------------------------------
void testCalibration() {
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();
    tp.setSampleCount(1);   // 坐标测试聚焦转换逻辑，关闭采样

    // 默认：全范围 0..4095 -> 320x240
    spi.reads = { 0xFF, 0xF0, 0x0F, 0xF0 };  // X=0xFFF, Y=0x0FF
    spi.readIdx = 0;
    int x = 0, y = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 319);                            // 4095*320/4095=320 -> clamp 319
    CHECK(y == 14);                            // 255*240/4095 = 14.94 -> 14

    // 边界 clamp：raw 超出范围仍安全
    spi.reads = { 0xFF, 0xF0, 0x00, 0x00 };   // X=0xFFF, Y=0x000
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 319);
    CHECK(y == 0);

    // 校准：rawX 200..3800 -> 0..319，rawY 150..3700 -> 0..239
    hw::TouchCalibration cal;
    cal.rawXMin = 200;
    cal.rawXMax = 3800;
    cal.rawYMin = 150;
    cal.rawYMax = 3700;
    tp.setCalibration(cal);

    // X 中点：2000 -> (2000-200)*320/3600 = 160
    spi.reads = { 0x7D, 0x00, 0x70, 0x00 };   // X=0x7D0=2000, Y=0x700=1792
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 160);
    // Y: (1792-150)*240/(3700-150) = 1642*240/3550 = 111.0 -> 111
    CHECK(y == 111);

    // 边界值：X=200 -> 0，Y=150 -> 0
    spi.reads = { 0x0C, 0x80, 0x09, 0x60 };   // X=200(0x0C80), Y=150(0x0960)
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 0);
    CHECK(y == 0);

    // 上边界：X=3792 -> clamp 319，Y=3700 -> 239
    spi.reads = { 0xED, 0x00, 0xE7, 0x40 };   // X=3792(0xED00), Y=3700(0xE740)
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 319);
    CHECK(y == 239);

    // 镜像：invertX/invertY
    cal.invertX = true;
    cal.invertY = true;
    tp.setCalibration(cal);
    spi.reads = { 0x0C, 0x80, 0x09, 0x60 };   // X=200, Y=150 -> 镜像到右下角
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 319);
    CHECK(y == 239);

    // 轴交换：swapXY
    cal.swapXY = true;
    cal.invertX = false;
    cal.invertY = false;
    tp.setCalibration(cal);
    spi.reads = { 0xED, 0x00, 0x09, 0x60 };   // X=3792->319, Y=150->0
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 0);                              // swap: x = sy = 0
    CHECK(y == 319);                            // swap: y = sx = 319

    // 未校准但 rawMax <= rawMin：mapAxis 返回 0（防御）
    hw::TouchCalibration bad;
    bad.rawXMax = 0;
    bad.rawYMax = 0;
    tp.setCalibration(bad);
    spi.reads = { 0x0F, 0xF0, 0x0F, 0xF0 };
    spi.readIdx = 0;
    CHECK(tp.readPoint(x, y));
    CHECK(x == 0);
    CHECK(y == 0);
}

// -----------------------------------------------------------------------------
// 7. 抗噪（中值采样）
// -----------------------------------------------------------------------------
void testFiltering() {
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();

    // 默认 3 次采样：0x100, 0x9FF(尖峰), 0x102 -> 中值 0x102
    tp.setSampleCount(3);
    spi.reads = {
        0x10, 0x00,   // 0x100
        0x9F, 0xF0,   // 0x9FF
        0x10, 0x20,   // 0x102
    };
    spi.readIdx = 0;
    uint16_t x = 0;
    CHECK(tp.readX(x));
    CHECK(x == 0x102);

    // 采样计数 = 2：0x100, 0x101 -> 排序取 index 1 = 0x101
    tp.setSampleCount(2);
    spi.reads = {
        0x10, 0x00,   // 0x100
        0x10, 0x10,   // 0x101
    };
    spi.readIdx = 0;
    CHECK(tp.readX(x));
    CHECK(x == 0x101);

    // sampleCount 边界：0 -> 强制 1（不采样）
    tp.setSampleCount(0);
    CHECK(tp.sampleCount() == 1);
    spi.reads = { 0x12, 0x30 };
    spi.readIdx = 0;
    CHECK(tp.readX(x));
    CHECK(x == 0x123);

    // sampleCount 上限 8
    tp.setSampleCount(99);
    CHECK(tp.sampleCount() == 8);

    // Y 轴同样滤波：0x200, 0x201, 0x2FF(尖峰) -> 中值 0x201
    tp.setSampleCount(3);
    spi.reads = {
        0x20, 0x00,   // 0x200
        0x20, 0x10,   // 0x201
        0x2F, 0xF0,   // 0x2FF 尖峰
    };
    spi.readIdx = 0;
    uint16_t y = 0;
    CHECK(tp.readY(y));
    CHECK(y == 0x201);
}

// -----------------------------------------------------------------------------
// 8. 故障注入
// -----------------------------------------------------------------------------
void testFailures() {
    // SPI 传输失败
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    tp.open();

    spi.failTransfer = true;
    uint16_t v = 0;
    CHECK(tp.readRawX(v) == false);
    CHECK(tp.readX(v) == false);
    CHECK(tp.readY(v) == false);
    uint16_t z1 = 0, z2 = 0;
    CHECK(tp.readPressure(z1, z2) == false);
    int x = 0, y = 0;
    CHECK(tp.readPoint(x, y) == false);
    spi.failTransfer = false;

    // 采样中途失败：正常读一次（3 次采样全 0x100），随后开启失败
    tp.setSampleCount(3);
    spi.reads = { 0x10, 0x00, 0x10, 0x00, 0x10, 0x00 };
    spi.readIdx = 0;
    uint16_t out = 0;
    CHECK(tp.readX(out));
    CHECK(out == 0x100);

    // 现在开启失败：下一次 readX 第一次采样即失败
    spi.failTransfer = true;
    CHECK(tp.readX(out) == false);
    spi.failTransfer = false;

    // PENIRQ 读失败（返回 false）
    FakeTouchPin pen;
    pen.failRead = true;
    hw::Xpt2046 tpPen(&spi, &pen);
    tpPen.open();
    CHECK(tpPen.isTouched() == false);
}

// -----------------------------------------------------------------------------
// 9. 与 ILI9341 共用 SpiBus 抽象（类型兼容性）
// -----------------------------------------------------------------------------
void testSpiBusReuse() {
    // 验证 xpt2046.h 通过 ili9341.h 引入的 hw::SpiBus 与 Ili9341 可共用
    FakeSpi spi;
    hw::Xpt2046 tp(&spi);
    hw::Ili9341 lcd(&spi, nullptr);   // 编译期类型兼容即可
    CHECK(tp.open());
    CHECK(lcd.open() == false);       // pins 为 NULL 时 open 失败，安全
}

}  // namespace

int main() {
    testCommandBytes();
    testDataExtraction();
    testOpenClose();
    testTouchDetection();
    testPressure();
    testCalibration();
    testFiltering();
    testFailures();
    testSpiBusReuse();

    std::printf("test_xpt2046: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
