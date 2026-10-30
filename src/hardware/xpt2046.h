// =============================================================================
// xpt2046.h — XPT2046 电阻触摸控制器驱动（用户态，主机可测试）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示的 2.8 寸 ILI9341 触摸屏（板载 XPT2046）触摸输入。与
//       ili9341.h 同属 spidev 用户态路径（docs/hardware/lvgl_board_port.md
//       方案 B）：XPT2046 通过 SPI 读取 12 位 ADC 原始坐标/压力，经校准参数
//       映射到屏幕坐标，供 LVGL 的 indev 层或菜单状态机消费。
//
// 设计：
//   - 复用 hw::SpiBus 抽象（板端 SysfsSpiBus，测试 FakeSpi）。触摸芯片与
//     屏幕共用同一个 SPI 控制器；实测板上**只有 /dev/spidev0.0**（无 spidev0.1、
//     SPI0 仅一组 cs0），因此 XPT2046 必须用 GPIO 手工片选：
//     SysfsSpiBus("/dev/spidev0.0") + setCsGpio(&touchCs)，即与屏幕各用一个
//     SysfsSpiBus 实例、各自一个片选 GPIO；
//   - TouchPin 抽象：PENIRQ 引脚检测（低有效）。板端用 sysfs GPIO 输入，
//     测试用 FakeTouchPin；不接 PENIRQ 时可配置用压力阈值判定触摸；
//   - 命令字节兼容主流开源库（0xD0 读 X / 0x90 读 Y / 0xB0 读 Z1 / 0xC0 读
//     Z2，12 位单端模式，PD=00 掉电并使能 PENIRQ）；
//   - 数据提取：发送命令后连续读 2 字节，12 位结果默认位于高 12 位
//     (raw >> 4)。个别模块因转换延迟会右移 1 位，提供 dataShift 可调；
//   - 坐标校准：TouchCalibration（原始范围/屏幕尺寸/镜像/交换轴），
//     未校准时按 0..4095 全范围线性映射，坐标自动 clamp 到屏幕；
//   - 抗噪：采样 N 次取中值（默认 3 次），抑制电阻屏尖峰。
//
// 集成方式（真机阶段，见 docs/hardware/lvgl_board_port.md 路线图步骤 6）：
//   hw::SysfsGpio touchCs(touchCsPin);        // T_CS 接空闲 GPIO（非 Pin12 硬件 CS0）
//   SysfsSpiBus tSpi("/dev/spidev0.0", 2000000);   // 与 LCD 共用同一个 spidev
//   tSpi.setCsGpio(&touchCs);                  // ★ 手工片选（板上无 spidev0.1）
//   SysfsTouchPin pen(penGpio);               // PENIRQ 输入（低有效；J11 未引出）
//   hw::Xpt2046 tp(&tSpi, &pen);
//   tp.setCalibration({ .rawXMin=200, .rawXMax=3900, ... });  // 真机标定
//   tp.open();
//   int x, y;
//   if (tp.isTouched() && tp.readPoint(x, y)) { ... LVGL indev 上报 ... }
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ili9341.h"   // 复用 hw::SpiBus 抽象

namespace hw {

// -----------------------------------------------------------------------------
// XPT2046 常用命令（8 位控制字节：S=1 + A2A1A0 + MODE=0(12bit) + SER/DFR=0
// （单端，主流库兼容）+ PD=00（掉电并启用 PENIRQ））
// -----------------------------------------------------------------------------
enum : uint8_t {
    XPT2046_CMD_X  = 0xD0,  // A2A1A0=101：X 位置（X+ 输入）
    XPT2046_CMD_Y  = 0x90,  // A2A1A0=001：Y 位置（Y+ 输入）
    XPT2046_CMD_Z1 = 0xB0,  // A2A1A0=011：Z1 压力
    XPT2046_CMD_Z2 = 0xC0,  // A2A1A0=100：Z2 压力
};

// -----------------------------------------------------------------------------
// 触摸笔按下检测引脚抽象（PENIRQ，低有效）
// -----------------------------------------------------------------------------
class TouchPin {
public:
    virtual ~TouchPin() {}
    // 返回 true 表示触摸笔当前按下（PENIRQ 为低）。
    virtual bool isPressed() = 0;
};

// -----------------------------------------------------------------------------
// 触摸校准参数（真机标定后填入；未校准时按全范围线性映射）
// -----------------------------------------------------------------------------
struct TouchCalibration {
    uint16_t rawXMin = 0;     // X 轴 ADC 最小值（对应屏幕左/右边界，视接线）
    uint16_t rawXMax = 4095;  // X 轴 ADC 最大值
    uint16_t rawYMin = 0;
    uint16_t rawYMax = 4095;
    int screenW = 320;        // 屏幕宽度（ILI9341 = 320）
    int screenH = 240;        // 屏幕高度（ILI9341 = 240）
    bool swapXY = false;      // 交换 X/Y 轴（屏幕旋转 90° 时）
    bool invertX = false;     // X 轴镜像（电阻屏接线导致坐标反向时）
    bool invertY = false;     // Y 轴镜像
};

// -----------------------------------------------------------------------------
// XPT2046 触摸驱动
// -----------------------------------------------------------------------------
class Xpt2046 {
public:
    Xpt2046(SpiBus* spi, TouchPin* pen = nullptr)
        : spi_(spi), pen_(pen) {}

    ~Xpt2046() { close(); }

    // 打开 SPI。可重复调用（已打开则直接返回 true）。
    bool open() {
        if (!spi_) return false;
        if (open_) return true;
        if (!spi_->open()) return false;
        open_ = true;
        return true;
    }

    void close() {
        if (spi_ && open_) spi_->close();
        open_ = false;
    }

    bool isOpen() const { return open_; }

    // ---- 配置 ----
    void setCalibration(const TouchCalibration& cal) { cal_ = cal; }
    const TouchCalibration& calibration() const { return cal_; }

    // 12 位数据提取右移位数。默认 4（数据位于连续 16 位读回值的高 12 位）。
    // 真机若发现坐标整体偏移（转换延迟导致输出右移 1 位），调整为 3。
    void setDataShift(int bits) { dataShift_ = bits; }
    int dataShift() const { return dataShift_; }

    // 无 PENIRQ 时的压力阈值（Z1-Z2 绝对差），默认 50。
    void setPressureThreshold(uint16_t threshold) { pressureThreshold_ = threshold; }
    uint16_t pressureThreshold() const { return pressureThreshold_; }

    // 每次坐标读取代的采样次数（1..8，取中值抗噪），默认 3。
    void setSampleCount(int n) {
        sampleCount_ = n < 1 ? 1 : (n > 8 ? 8 : n);
    }
    int sampleCount() const { return sampleCount_; }

    // ---- 原始数据读取 ----
    // 发送控制字节并连续读 2 字节，返回 16 位原始值（未提取 12 位）。
    // 真机关键：命令字节与随后的 2 字节数据必须**同一个片选周期**内完成。
    // spidev 每条 ioctl 消息结束都会释放硬件片选；若裸调两次 transfer()，
    // XPT2046 会在片选抬高时中止转换，读回 0/噪声 → 用 beginCs/endCs 包住。
    bool readRaw(uint8_t cmd, uint16_t& raw) {
        if (!open_ || !spi_) return false;
        if (!spi_->beginCs()) return false;
        bool ok = spi_->transfer(&cmd, nullptr, 1);
        uint8_t rx[2] = { 0, 0 };
        if (ok) ok = spi_->transfer(nullptr, rx, 2);
        spi_->endCs();
        if (!ok) return false;
        raw = static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8) | rx[1]);
        return true;
    }

    // 从 16 位原始值提取 12 位 ADC 结果。
    uint16_t extract12(uint16_t raw) const {
        return static_cast<uint16_t>((raw >> dataShift_) & 0x0FFF);
    }

    bool readRawX(uint16_t& out) {
        uint16_t raw = 0;
        if (!readRaw(XPT2046_CMD_X, raw)) return false;
        out = extract12(raw);
        return true;
    }

    bool readRawY(uint16_t& out) {
        uint16_t raw = 0;
        if (!readRaw(XPT2046_CMD_Y, raw)) return false;
        out = extract12(raw);
        return true;
    }

    // 读压力 Z1/Z2（12 位原始值）。触摸时两者差值反映按压力度。
    bool readPressure(uint16_t& z1, uint16_t& z2) {
        uint16_t a = 0, b = 0;
        if (!readRaw(XPT2046_CMD_Z1, a)) return false;
        if (!readRaw(XPT2046_CMD_Z2, b)) return false;
        z1 = extract12(a);
        z2 = extract12(b);
        return true;
    }

    // ---- 带抗噪的坐标读取（N 次采样取中值）----
    bool readX(uint16_t& out) {
        return readFiltered(&Xpt2046::readRawX, out);
    }

    bool readY(uint16_t& out) {
        return readFiltered(&Xpt2046::readRawY, out);
    }

    // ---- 触摸判定 ----
    // 有 PENIRQ 时直接读引脚；否则用压力阈值（|Z1-Z2| >= threshold）。
    bool isTouched() {
        if (!open_) return false;
        if (pen_) return pen_->isPressed();
        uint16_t z1 = 0, z2 = 0;
        if (!readPressure(z1, z2)) return false;
        int32_t diff = static_cast<int32_t>(z1) - static_cast<int32_t>(z2);
        if (diff < 0) diff = -diff;
        return diff >= static_cast<int32_t>(pressureThreshold_);
    }

    // 读取一个有效触摸点（屏幕坐标）。仅当坐标转换后返回 true。
    bool readPoint(int& x, int& y) {
        if (!open_) return false;
        uint16_t rx = 0, ry = 0;
        if (!readX(rx) || !readY(ry)) return false;
        int sx = mapAxis(rx, cal_.rawXMin, cal_.rawXMax,
                         cal_.screenW, cal_.invertX);
        int sy = mapAxis(ry, cal_.rawYMin, cal_.rawYMax,
                         cal_.screenH, cal_.invertY);
        if (cal_.swapXY) {
            x = sy;
            y = sx;
        } else {
            x = sx;
            y = sy;
        }
        return true;
    }

private:
    typedef bool (Xpt2046::*RawReader)(uint16_t&);

    bool readFiltered(RawReader reader, uint16_t& out) {
        if (!open_) return false;
        if (sampleCount_ <= 1) return (this->*reader)(out);
        std::vector<uint16_t> samples;
        samples.reserve(static_cast<size_t>(sampleCount_));
        for (int i = 0; i < sampleCount_; ++i) {
            uint16_t v = 0;
            if (!(this->*reader)(v)) return false;
            samples.push_back(v);
        }
        std::sort(samples.begin(), samples.end());
        out = samples[samples.size() / 2];
        return true;
    }

    // 原始 ADC 值 -> 屏幕轴坐标（线性映射，越界 clamp）。
    static int mapAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax,
                       int size, bool invert) {
        if (rawMax <= rawMin) return 0;
        int v = (static_cast<int>(raw) - static_cast<int>(rawMin)) * size /
                (static_cast<int>(rawMax) - static_cast<int>(rawMin));
        if (invert) v = size - 1 - v;
        if (v < 0) v = 0;
        if (v >= size) v = size - 1;
        return v;
    }

    SpiBus* spi_;
    TouchPin* pen_;
    bool open_ = false;
    TouchCalibration cal_;
    int dataShift_ = 4;
    uint16_t pressureThreshold_ = 50;
    int sampleCount_ = 3;
};

}  // namespace hw