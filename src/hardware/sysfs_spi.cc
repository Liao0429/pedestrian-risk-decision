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
      fd_(-1),
      cs_(NULL),
      csActiveLow_(true),
      csSession_(false) {}

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

// 片选会话：拉低（低有效）或拉高（高有效）。无片选 GPIO 时是空操作，交给内核。
bool SysfsSpiBus::beginCs() {
    if (!cs_) return true;
    bool ok = cs_->setDirection(true) && cs_->write(csActiveLow_ ? false : true);
    csSession_ = ok;
    return ok;
}

bool SysfsSpiBus::endCs() {
    if (!cs_ || !csSession_) {
        csSession_ = false;
        return true;
    }
    csSession_ = false;
    return cs_->write(csActiveLow_ ? true : false);
}

bool SysfsSpiBus::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    if (fd_ < 0 || len == 0 || (tx == nullptr && rx == nullptr)) return false;

    // 未处于显式片选会话中时，按单次调用自动开关片选（保持旧行为）。
    const bool autoCs = (cs_ != NULL) && !csSession_;
    if (autoCs) beginCs();
    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    // tx_buf/rx_buf 为 __u64（内核 ABI），用 uintptr_t 中转保证 32/64 位一致。
    tr.tx_buf = static_cast<__u64>(reinterpret_cast<uintptr_t>(tx));
    tr.rx_buf = static_cast<__u64>(reinterpret_cast<uintptr_t>(rx));
    tr.len = static_cast<__u32>(len);
    tr.speed_hz = speedHz_;
    tr.bits_per_word = bits_;
    tr.delay_usecs = 0;

    const bool ok = ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >= 0;
    if (autoCs) endCs();
    return ok;
}

// -----------------------------------------------------------------------------
// SysfsLcdPins
// -----------------------------------------------------------------------------
SysfsLcdPins::SysfsLcdPins(Gpio* dc, Gpio* reset, Gpio* backlight)
    : dc_(dc), reset_(reset), bl_(backlight) {
    // 构造即把三路配置为输出。sysfs GPIO 导出后默认是输入，而 SysfsGpio::write()
    // 在未 setDirection(out) 时直接返回 false —— 2026-10-30 真机实跑就是这样让
    // Ili9341::begin() 在 resetLow() 一步返回 false（屏幕永远点不亮）。
    if (dc_) dc_->setDirection(true);
    if (reset_) reset_->setDirection(true);
    if (bl_) bl_->setDirection(true);
}

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
    : pen_(pen) {
    if (pen_) pen_->setDirection(false);   // PENIRQ 为输入
}

bool SysfsTouchPin::isPressed() {
    if (!pen_) return false;
    bool high = false;
    if (!pen_->read(&high)) return false;
    return !high;  // PENIRQ 低有效：读到低电平 = 按下
}

} // namespace hw