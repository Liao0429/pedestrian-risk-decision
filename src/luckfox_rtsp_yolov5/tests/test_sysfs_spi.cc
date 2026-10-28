// =============================================================================
// test_sysfs_spi.cc — Linux 板端适配层单元测试（仅 Linux 编译运行）
// -----------------------------------------------------------------------------
// 验证点：
//   1. SysfsSpiBus 生命周期：初始未打开、close 幂等、重复 open（失败路径）；
//   2. SysfsSpiBus 失败路径：NULL 路径、不存在的 /dev/spidev 节点、未打开时
//      transfer、len==0、tx/rx 同时为 NULL；
//   3. SysfsSpiBus 真实设备（可选）：若 /dev/spidev0.0 存在且可打开，做一次
//      纯写传输回环并关闭（CI 无设备时自动跳过，不算失败）；
//   4. SysfsLcdPins：FakeGpio 注入验证 DC/RESET/背光电平与方向；NULL 容错；
//   5. SysfsTouchPin：FakeGpio 输入低电平=PENIRQ 按下，高电平=释放；NULL 容错。
//
// 编译（Linux，无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I../../hardware
//       test_sysfs_spi.cc ../../hardware/sysfs_spi.cc -o test_sysfs_spi
// =============================================================================

#include <cstdio>
#include <cstring>

#include "sysfs_spi.h"

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
// 1/2/3. SysfsSpiBus 生命周期与失败路径
// -----------------------------------------------------------------------------
void test_spi_bus_lifecycle() {
    std::printf("[SysfsSpiBus] 生命周期与失败路径\n");

    // NULL 路径：open 必须失败，且 isOpen 保持 false
    {
        hw::SysfsSpiBus bus(nullptr);
        CHECK(!bus.isOpen());
        CHECK(!bus.open());
        CHECK(!bus.isOpen());
        CHECK(!bus.transfer(nullptr, nullptr, 1));   // 未打开
    }

    // 空串路径
    {
        hw::SysfsSpiBus bus("");
        CHECK(!bus.open());
    }

    // 不存在的设备节点：open 失败、isOpen false、transfer 安全失败
    {
        hw::SysfsSpiBus bus("/dev/spidev999.9");
        CHECK(!bus.isOpen());
        CHECK(!bus.open());
        CHECK(!bus.isOpen());
        CHECK(!bus.transfer(nullptr, nullptr, 4));
        bus.close();  // 未打开时 close 必须幂等（不崩溃）
    }

    // transfer 参数校验（未打开时）
    {
        hw::SysfsSpiBus bus("/dev/spidev0.0");
        uint8_t tx[2] = {0xAA, 0xBB};
        uint8_t rx[2] = {0, 0};
        CHECK(!bus.transfer(tx, rx, 0));        // len==0
        CHECK(!bus.transfer(nullptr, nullptr, 2)); // tx/rx 双 NULL
        CHECK(!bus.transfer(tx, nullptr, 2));   // 未打开
        CHECK(!bus.transfer(nullptr, rx, 2));   // 未打开
    }

    // 真实设备（可选）：若 /dev/spidev0.0 可打开，做纯写传输并关闭。
    // CI/无硬件环境自动跳过（不增加失败计数）。
    {
        hw::SysfsSpiBus bus("/dev/spidev0.0", 1000000);
        if (bus.open()) {
            CHECK(bus.isOpen());
            CHECK(bus.path() != nullptr && std::strcmp(bus.path(), "/dev/spidev0.0") == 0);
            CHECK(bus.speedHz() == 1000000);
            uint8_t tx[2] = {0x00, 0x00};
            CHECK(bus.transfer(tx, nullptr, sizeof(tx)));  // 纯写
            bus.close();
            CHECK(!bus.isOpen());
            // 关闭后再次 close 幂等
            bus.close();
            std::printf("  [info] /dev/spidev0.0 存在，真实设备路径已通过\n");
        } else {
            std::printf("  [skip] /dev/spidev0.0 不可用（无硬件环境），跳过真实设备用例\n");
        }
    }
}

// -----------------------------------------------------------------------------
// 4. SysfsLcdPins（FakeGpio 注入）
// -----------------------------------------------------------------------------
void test_lcd_pins() {
    std::printf("[SysfsLcdPins] DC/RESET/背光逻辑\n");

    hw::FakeGpio dc, reset, bl;
    dc.setDirection(true);
    reset.setDirection(true);
    bl.setDirection(true);

    hw::SysfsLcdPins pins(&dc, &reset, &bl);

    // DC：命令模式 = 低，数据模式 = 高
    CHECK(pins.dcCommand());
    CHECK(dc.lastWritten() == false);
    CHECK(pins.dcData());
    CHECK(dc.lastWritten() == true);

    // RESET：低有效
    CHECK(pins.resetLow());
    CHECK(reset.lastWritten() == false);
    CHECK(pins.resetHigh());
    CHECK(reset.lastWritten() == true);

    // 背光开关
    CHECK(pins.backlight(true));
    CHECK(bl.lastWritten() == true);
    CHECK(pins.backlight(false));
    CHECK(bl.lastWritten() == false);

    // 写入计数：dc 2 次、reset 2 次、bl 2 次
    CHECK(dc.writeCount() == 2);
    CHECK(reset.writeCount() == 2);
    CHECK(bl.writeCount() == 2);
}

void test_lcd_pins_null() {
    std::printf("[SysfsLcdPins] NULL 容错\n");

    hw::SysfsLcdPins allNull(nullptr, nullptr, nullptr);
    CHECK(!allNull.dcCommand());
    CHECK(!allNull.dcData());
    CHECK(!allNull.resetLow());
    CHECK(!allNull.resetHigh());
    CHECK(!allNull.backlight(true));

    hw::FakeGpio dc;
    dc.setDirection(true);
    hw::SysfsLcdPins partialNull(&dc, nullptr, nullptr);
    CHECK(partialNull.dcCommand());
    CHECK(!partialNull.resetLow());
    CHECK(!partialNull.backlight(true));
}

// -----------------------------------------------------------------------------
// 5. SysfsTouchPin（FakeGpio 输入注入）
// -----------------------------------------------------------------------------
void test_touch_pin() {
    std::printf("[SysfsTouchPin] PENIRQ 按下/释放\n");

    hw::FakeGpio pen;
    pen.setDirection(false);  // 输入
    hw::SysfsTouchPin tp(&pen);

    // 默认输入电平 false（低）=> PENIRQ 有效 => 按下
    CHECK(tp.isPressed());

    // 高电平 = 释放
    pen.setInputLevel(true);
    CHECK(!tp.isPressed());

    // 回到低电平 = 再次按下
    pen.setInputLevel(false);
    CHECK(tp.isPressed());

    // NULL 容错
    hw::SysfsTouchPin nullTp(nullptr);
    CHECK(!nullTp.isPressed());
}

} // namespace

int main() {
    test_spi_bus_lifecycle();
    test_lcd_pins();
    test_lcd_pins_null();
    test_touch_pin();

    std::printf("\n结果: %d 通过, %d 失败\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}