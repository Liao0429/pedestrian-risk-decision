// =============================================================================
// ili9341.h — ILI9341 2.8 寸 SPI 触摸屏显示驱动（用户态，主机可测试）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的 2.8 寸 ILI9341 屏（320x240 RGB565）显示后端。采用
//       spidev 用户态驱动路径（见 docs/hardware/lvgl_board_port.md 方案 B）：
//       命令/数据字节经 SpiBus 发送，DC/RESET/背光由 LcdPins 抽象控制。
//
// 设计：
//   - SpiBus 抽象：板端实现 SysfsSpiBus（/dev/spidev0.0），测试用 FakeSpi；
//   - LcdPins 抽象：板端实现 SysfsLcdPins（sysfs GPIO），测试用 FakeLcdPins；
//   - 帧缓冲：320x240 uint16_t（RGB565，150KB），fillRect/flush 分批写入 SPI
//     （单次 transfer 上限 kMaxChunkPixels，兼容 spidev 单次长度限制）；
//   - 命令序列与窗口/像素协议按 ILI9341 数据手册实现，测试校验字节流；
//   - readId() 支持自检项：读 0xD3 返回 00 93 41 00（ILI9341 版本 ID）。
//
// 集成方式（真机阶段，见 docs/hardware/lvgl_board_port.md 路线图步骤 4）：
//   hw::SysfsGpio dc(dcPin), rst(rstPin), bl(blPin);  // 编号须按 pinmux 实测重选
//   SysfsSpiBus spi("/dev/spidev0.0");
//   SysfsLcdPins pins(&dc, &rst, &bl);
//   hw::Ili9341 lcd(&spi, &pins);
//   lcd.setDelayFn(板端延时回调);   // 例如 [](uint32_t ms){ usleep(ms*1000); }
//   lcd.setRotation(0);             // ★ 必调：默认 MADCTL=0x08 只能竖屏（见下方说明）
//   lcd.begin();
//   lcd.fillScreen(hw::rgb565(0, 0, 0));
//   ... 主循环：lcd.flush() 或直接 fillRect 局部刷新
//
// 2026-10-30 板上实测（Luckfox Pico Max，证据见 docs/hardware/pin_map_verified.md）：
//   - SPI0 复用实测在 GPIO1_C0..C3 = sysfs 48/49/50/51（cs0/clk/mosi/miso）；
//     设备节点只有 /dev/spidev0.0（SPI1 disabled、无 spidev0.1）；
//   - PCB 排针 J11 只有 8 脚（5V/GND/SCLK/MOSI/MISO/CS/DC/RST），**没有背光脚**：
//     背光通常直接并到 3.3V/5V 常亮，此时 LcdPins::backlight 用哑实现返回 true 即可；
//   - 厂商 fbtft 参考节点把 LCD RESET 定义成 GPIO1_C3 = sysfs 51，与 SPI0_MISO 冲突，
//     不要照抄；DC/RST 请另选未被复用占用的空闲 GPIO。
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace hw {

// -----------------------------------------------------------------------------
// ILI9341 常用命令（数据手册 8-bit 命令集）
// -----------------------------------------------------------------------------
enum : uint8_t {
    ILI9341_SWRESET = 0x01,   // 软件复位
    ILI9341_SLPOUT  = 0x11,   // 退出睡眠
    ILI9341_NORON   = 0x13,   // 正常显示模式开
    ILI9341_INVOFF  = 0x20,   // 颜色反转关
    ILI9341_INVON   = 0x21,   // 颜色反转开
    ILI9341_DISPOFF = 0x28,   // 显示关
    ILI9341_DISPON  = 0x29,   // 显示开
    ILI9341_CASET   = 0x2A,   // 列地址窗口
    ILI9341_PASET   = 0x2B,   // 行地址窗口
    ILI9341_RAMWR   = 0x2C,   // 写显存
    ILI9341_RAMRD   = 0x2E,   // 读显存
    ILI9341_MADCTL  = 0x36,   // 存储访问控制（旋转/镜像/RGB 序）
    ILI9341_COLMOD  = 0x3A,   // 像素格式
    ILI9341_RDID4   = 0xD3,   // 读版本 ID（返回 4 字节：00 93 41 00）
};

// -----------------------------------------------------------------------------
// MADCTL（0x36）常用值：旋转位 + BGR 位
//   ⚠ ILI9341 面板原生寻址是 240(列)×320(行)。本驱动按 320×240 横向使用，
//     MADCTL 必须带 MV（行/列交换，0x20），否则 CASET 写 0..319 会溢出/回绕，
//     表现为花屏或每行末 80 像素折到下一行。
//     0x08 只有 BGR 位、没有 MV → 仅适用 240×320 竖屏，**不能**用于本驱动。
//   BGR 位（0x08）是绝大多数 2.8 寸 ILI9341 模组的正确取值；若整屏红蓝互换，
//   把 0x08 清掉即可。
// -----------------------------------------------------------------------------
enum : uint8_t {
    ILI9341_MADCTL_LANDSCAPE   = 0x28,  // MV|BGR：320×240（推荐真机值）
    ILI9341_MADCTL_LANDSCAPE_R = 0xE8,  // MX|MY|MV|BGR：320×240（旋转 180°）
    ILI9341_MADCTL_PORTRAIT    = 0x48,  // MX|BGR：240×320（竖屏；本驱动不可用）
};

// -----------------------------------------------------------------------------
// SPI 总线抽象
// -----------------------------------------------------------------------------
class SpiBus {
public:
    virtual ~SpiBus() {}
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // 全双工 transfer：tx 发送 len 字节（可 NULL=纯读），rx 接收 len 字节（可
    // NULL=纯写）。返回 false 表示传输失败。
    virtual bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) = 0;

    // 片选会话（可选）：默认空实现，主机 FakeSpi 无需理会。
    // 真机 SysfsSpiBus 若配置了手工片选 GPIO（见 sysfs_spi.h setCsGpio），
    // beginCs() 会拉低片选、endCs() 释放，使「命令 + 读取」跨多次 transfer()
    // 时片选保持有效（XPT2046/ILI9341 读寄存器必须如此；spidev 默认每条
    // ioctl 消息结束就释放硬件片选）。
    virtual bool beginCs() { return true; }
    virtual bool endCs() { return true; }
};

// -----------------------------------------------------------------------------
// LCD 控制引脚抽象（DC=数据/命令选择，RESET=复位低有效，BL=背光）
// -----------------------------------------------------------------------------
class LcdPins {
public:
    virtual ~LcdPins() {}
    virtual bool dcCommand() = 0;   // DC 拉低 = 命令模式
    virtual bool dcData() = 0;      // DC 拉高 = 数据模式
    virtual bool resetLow() = 0;    // 复位拉低（低有效）
    virtual bool resetHigh() = 0;   // 复位释放
    virtual bool backlight(bool on) = 0;
};

// -----------------------------------------------------------------------------
// 颜色工具：8bit RGB -> RGB565
// -----------------------------------------------------------------------------
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// -----------------------------------------------------------------------------
// ILI9341 驱动
// -----------------------------------------------------------------------------
class Ili9341 {
public:
    static const int kWidth = 320;
    static const int kHeight = 240;
    // 单次 SPI transfer 的最大像素数（spidev 常见单次长度上限 4096 字节，
    // 2KB 一次留足余量；同时避免一次分配 150KB 栈/堆缓冲）。
    static const size_t kMaxChunkPixels = 1024;

    typedef void (*DelayFn)(uint32_t ms);

    Ili9341(SpiBus* spi, LcdPins* pins)
        : spi_(spi), pins_(pins) {}

    ~Ili9341() { close(); }

    // 注入延时回调（板端用 usleep；测试注入计数回调）。不设置则跳过延时。
    void setDelayFn(DelayFn fn) { delayFn_ = fn; }

    // 设置 MADCTL 原始值（默认 0x08，仅竖屏）。真机请用 setRotation 或命名常量。
    void setMadctl(uint8_t v) { madctl_ = v; }
    uint8_t madctl() const { return madctl_; }

    // 320×240 横向的两种方向：0=MV|BGR(0x28) 推荐；1=MX|MY|MV|BGR(0xE8) 转 180°。
    // 本驱动固定 320×240，竖屏值（0x08/0x48）会因 CASET 溢出而花屏。
    void setRotation(int r) {
        if (r == 0)      madctl_ = ILI9341_MADCTL_LANDSCAPE;
        else if (r == 1) madctl_ = ILI9341_MADCTL_LANDSCAPE_R;
    }

    // 打开 SPI 并分配帧缓冲。可重复调用（已打开则直接返回 true）。
    bool open() {
        if (!spi_ || !pins_) return false;
        if (open_) return true;
        if (!spi_->open()) return false;
        open_ = true;
        fb_.assign(static_cast<size_t>(kWidth) * kHeight, 0);
        return true;
    }

    void close() {
        if (spi_ && open_) spi_->close();
        open_ = false;
        fb_.clear();
    }

    bool isOpen() const { return open_; }

    // 上电初始化：复位脉冲 -> 初始化命令序列 -> 开背光。
    bool begin() {
        if (!open()) return false;

        // 复位脉冲：RESET 低 120ms -> 高 120ms（IL-9341 手册要求 tRES >= 5us，
        // 取 120ms 保证 sysfs GPIO 切换与电源稳定余量）。
        if (!pins_->resetLow()) return false;
        delay(120);
        if (!pins_->resetHigh()) return false;
        delay(120);

        // 标准初始化序列（最小可用集：16bit 像素格式 + 屏幕方向 + 显示开）：
        //   SWRESET -> SLPOUT -> COLMOD=0x55(16bit) -> MADCTL -> DISPON
        // madctl_ 默认 0x08（保持与既有主机测试的字节流一致），真机务必先
        // setRotation(0)/setMadctl(ILI9341_MADCTL_LANDSCAPE=0x28) 再 begin()。
        uint8_t colmod_data[1] = { 0x55 };
        uint8_t madctl_data[1] = { madctl_ };
        struct InitStep {
            uint8_t cmd;
            const uint8_t* data;
            uint8_t len;
        };
        const InitStep init[] = {
            { ILI9341_SWRESET, nullptr,      0 },
            { ILI9341_SLPOUT,  nullptr,      0 },
            { ILI9341_COLMOD,  colmod_data,  1 },
            { ILI9341_MADCTL,  madctl_data,  1 },
            { ILI9341_DISPON,  nullptr,      0 },
        };
        for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
            if (!writeCommand(init[i].cmd)) return false;
            if (init[i].len > 0 && !writeData(init[i].data, init[i].len)) {
                return false;
            }
            // 数据手册时序：SWRESET 后 ≥120ms、SLPOUT 后 ≥120ms 才能发下一条命令。
            // delayFn_ 未注入时 delay() 为空操作（主机测试不受影响）。
            if (init[i].cmd == ILI9341_SWRESET || init[i].cmd == ILI9341_SLPOUT) {
                delay(120);
            }
        }
        delay(10);   // DISPON 后留出上屏时间
        if (!pins_->backlight(true)) return false;
        return true;
    }

    // 写命令：DC=命令模式，发送 1 字节。
    bool writeCommand(uint8_t cmd) {
        if (!open_) return false;
        if (!pins_->dcCommand()) return false;
        return spi_->transfer(&cmd, nullptr, 1);
    }

    // 写数据：DC=数据模式，发送 len 字节。
    bool writeData(const uint8_t* data, size_t len) {
        if (!open_ || !data) return false;
        if (!pins_->dcData()) return false;
        return spi_->transfer(data, nullptr, len);
    }

    // 设置显示窗口（列/行地址范围，含端点）。坐标自动裁剪到有效区。
    bool setWindow(int x0, int y0, int x1, int y1) {
        if (!open_) return false;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= kWidth) x1 = kWidth - 1;
        if (y1 >= kHeight) y1 = kHeight - 1;
        if (x0 > x1 || y0 > y1) return false;

        uint8_t d[4];
        d[0] = static_cast<uint8_t>(x0 >> 8);
        d[1] = static_cast<uint8_t>(x0 & 0xFF);
        d[2] = static_cast<uint8_t>(x1 >> 8);
        d[3] = static_cast<uint8_t>(x1 & 0xFF);
        if (!writeCommand(ILI9341_CASET)) return false;
        if (!writeData(d, 4)) return false;

        d[0] = static_cast<uint8_t>(y0 >> 8);
        d[1] = static_cast<uint8_t>(y0 & 0xFF);
        d[2] = static_cast<uint8_t>(y1 >> 8);
        d[3] = static_cast<uint8_t>(y1 & 0xFF);
        if (!writeCommand(ILI9341_PASET)) return false;
        if (!writeData(d, 4)) return false;
        return true;
    }

    // 填充矩形（裁剪到屏幕范围）。color 为 RGB565。
    bool fillRect(int x, int y, int w, int h, uint16_t color) {
        if (!open_ || w <= 0 || h <= 0) return false;
        int x0 = x < 0 ? 0 : x;
        int y0 = y < 0 ? 0 : y;
        int x1 = x + w - 1;
        int y1 = y + h - 1;
        if (x1 >= kWidth) x1 = kWidth - 1;
        if (y1 >= kHeight) y1 = kHeight - 1;
        if (x1 < x0 || y1 < y0) return false;  // 完全在屏幕外

        if (!setWindow(x0, y0, x1, y1)) return false;
        if (!writeCommand(ILI9341_RAMWR)) return false;

        const size_t total = static_cast<size_t>(x1 - x0 + 1) *
                             static_cast<size_t>(y1 - y0 + 1);
        const uint8_t hi = static_cast<uint8_t>(color >> 8);
        const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
        uint8_t chunk[kMaxChunkPixels * 2];
        size_t remain = total;
        while (remain > 0) {
            const size_t n = remain > kMaxChunkPixels ? kMaxChunkPixels : remain;
            for (size_t i = 0; i < n; ++i) {
                chunk[i * 2] = hi;
                chunk[i * 2 + 1] = lo;
            }
            if (!writeData(chunk, n * 2)) return false;
            remain -= n;
        }
        return true;
    }

    // 整屏填充。
    bool fillScreen(uint16_t color) {
        return fillRect(0, 0, kWidth, kHeight, color);
    }

    // 单像素（越界返回 false 且不发送）。
    bool drawPixel(int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return false;
        return fillRect(x, y, 1, 1, color);
    }

    // ---- 帧缓冲（供 LVGL flush_cb 或菜单绘制读取）----
    const std::vector<uint16_t>& framebuffer() const { return fb_; }

    // 直接写帧缓冲（不触发 SPI 传输）。
    bool setPixel(int x, int y, uint16_t color) {
        if (!open_ || x < 0 || y < 0 || x >= kWidth || y >= kHeight) return false;
        fb_[static_cast<size_t>(y) * kWidth + x] = color;
        return true;
    }

    uint16_t pixelAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return 0;
        return fb_[static_cast<size_t>(y) * kWidth + x];
    }

    // 清空帧缓冲为指定颜色（默认黑）。
    void clear(uint16_t color = 0) {
        for (size_t i = 0; i < fb_.size(); ++i) fb_[i] = color;
    }

    // 整帧推送到屏幕（全窗口 + RAMWR + 全部像素，分批发送）。
    bool flush() {
        if (!open_) return false;
        if (!setWindow(0, 0, kWidth - 1, kHeight - 1)) return false;
        if (!writeCommand(ILI9341_RAMWR)) return false;

        uint8_t chunk[kMaxChunkPixels * 2];
        size_t remain = fb_.size();
        size_t idx = 0;
        while (remain > 0) {
            const size_t n = remain > kMaxChunkPixels ? kMaxChunkPixels : remain;
            for (size_t i = 0; i < n; ++i) {
                chunk[i * 2] = static_cast<uint8_t>(fb_[idx] >> 8);
                chunk[i * 2 + 1] = static_cast<uint8_t>(fb_[idx] & 0xFF);
                ++idx;
            }
            if (!writeData(chunk, n * 2)) return false;
            remain -= n;
        }
        return true;
    }

    // 读 ILI9341 版本 ID（自检用）：发送 0xD3 后读 4 字节，
    // 正常返回 {0x00, 0x93, 0x41, 0x00}，即 ID 0x9341。
    // 两段传输之间片选必须保持有效（spidev 每条消息结束会释放硬件片选，
    // 分两次裸 transfer 在真机上常读不到 ID）→ 用 beginCs/endCs 包住。
    bool readId(uint8_t out[4]) {
        if (!open_ || !out) return false;
        static const uint8_t cmd = ILI9341_RDID4;
        if (!spi_->beginCs()) return false;
        bool ok = pins_->dcCommand() && spi_->transfer(&cmd, nullptr, 1) &&
                  pins_->dcData();
        uint8_t rx[4] = { 0, 0, 0, 0 };
        if (ok) ok = spi_->transfer(nullptr, rx, 4);
        spi_->endCs();
        if (!ok) return false;
        out[0] = rx[0];
        out[1] = rx[1];
        out[2] = rx[2];
        out[3] = rx[3];
        return true;
    }

private:
    void delay(uint32_t ms) {
        if (delayFn_) delayFn_(ms);
    }

    SpiBus* spi_;
    LcdPins* pins_;
    bool open_ = false;
    DelayFn delayFn_ = nullptr;
    // 0x08 = 仅 BGR（竖屏），保留为默认值以兼容既有主机测试的字节流断言；
    // 真机必须 setRotation(0)->0x28 或 setMadctl(ILI9341_MADCTL_LANDSCAPE)。
    uint8_t madctl_ = 0x08;
    std::vector<uint16_t> fb_;
};

}  // namespace hw