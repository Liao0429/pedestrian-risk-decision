// =============================================================================
// sysfs_spi.cc — Linux 板端适配层实现（仅 Linux 编译）
// -----------------------------------------------------------------------------
// 依赖 Linux 头文件 <linux/spi/spidev.h>（spidev 用户态接口）与
// <sys/ioctl.h>/<fcntl.h>/<unistd.h>。Windows 主机测试不编译本文件。
// =============================================================================

#include "sysfs_spi.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

namespace hw {

// -----------------------------------------------------------------------------
// SysfsSpiBus
// -----------------------------------------------------------------------------
SysfsSpiBus::SysfsSpiBus(const char* devPath, uint32_t speedHz, uint8_t mode, uint8_t bits)
    : devPath_(devPath ? devPath : ""),
      speedHz_(speedHz),
      mode_(mode),
      bits_(bits),
      fd_(-1) {}

SysfsSpiBus::~SysfsSpiBus() {
    close();
}

bool SysfsSpiBus::open() {
    if (fd_ >= 0) return true;          // 已打开（幂等）
    if (devPath_.empty()) return false; // NULL 路径容错

    fd_ = ::open(devPath_.c_str(), O_RDWR);
    if (fd_ < 0) return false;

    // 配置 SPI 模式 / 字宽 / 最大时钟；任一失败即关闭并返回 false。
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode_) < 0 ||
        ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits_) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speedHz_) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void SysfsSpiBus::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SysfsSpiBus::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    if (fd_ < 0 || len == 0 || (tx == nullptr && rx == nullptr)) return false;

    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    // tx_buf/rx_buf 为 __u64（内核 ABI），用 uintptr_t 中转保证 32/64 位一致。
    tr.tx_buf = static_cast<__u64>(reinterpret_cast<uintptr_t>(tx));
    tr.rx_buf = static_cast<__u64>(reinterpret_cast<uintptr_t>(rx));
    tr.len = static_cast<__u32>(len);
    tr.speed_hz = speedHz_;
    tr.bits_per_word = bits_;
    tr.delay_usecs = 0;

    return ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >= 0;
}

// -----------------------------------------------------------------------------
// SysfsLcdPins
// -----------------------------------------------------------------------------
SysfsLcdPins::SysfsLcdPins(Gpio* dc, Gpio* reset, Gpio* backlight)
    : dc_(dc), reset_(reset), bl_(backlight) {}

bool SysfsLcdPins::dcCommand() {
    return dc_ && dc_->write(false);
}

bool SysfsLcdPins::dcData() {
    return dc_ && dc_->write(true);
}

bool SysfsLcdPins::resetLow() {
    return reset_ && reset_->write(false);
}

bool SysfsLcdPins::resetHigh() {
    return reset_ && reset_->write(true);
}

bool SysfsLcdPins::backlight(bool on) {
    return bl_ && bl_->write(on);
}

// -----------------------------------------------------------------------------
// SysfsTouchPin
// -----------------------------------------------------------------------------
SysfsTouchPin::SysfsTouchPin(Gpio* pen)
    : pen_(pen) {}

bool SysfsTouchPin::isPressed() {
    if (!pen_) return false;
    bool high = false;
    if (!pen_->read(&high)) return false;
    return !high;  // PENIRQ 低有效：读到低电平 = 按下
}

} // namespace hw