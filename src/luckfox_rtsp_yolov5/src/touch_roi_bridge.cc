// =============================================================================
// touch_roi_bridge.cc — BOARD_TOUCH_ROI 触摸画框 UI（ILI9341 + XPT2046 + RoiDraw）
// -----------------------------------------------------------------------------
// 只在 -DBOARD_TOUCH_ROI=1（CMake: -DBOARD_TOUCH_ROI=ON）时参与构建。
// 默认关闭时本文件不编译、main.cc 调用点被 #if 掉 → 对现有性能零影响。
//
// 硬件接线（docs/hardware/pin_map_verified.md 实测推荐值，接屏后按第 4 节标定）：
//   DC=GPIO1_D0(56)  RST=GPIO1_D1(57)  LCD_CS=GPIO2_B0(72)  LCD_BL=GPIO2_D4(92)
//   TOUCH_CS=GPIO1_C6(54)  TOUCH_IRQ=GPIO1_C7(55)
//   两个外设共用 /dev/spidev0.0，片选由 GPIO 手工控制（板上无 spidev0.1）。
//
// 纪律（防卡死）：
//   - 采样节流 20ms（<=50Hz），全部在主循环协作式执行，不建线程、不忙等；
//   - 只在"有事件"时 render（RoiDraw 内部按脏矩形/窗口化填充上屏），绝不每帧全屏刷；
//   - 任何硬件失败都静默降级（返回 false），绝不抛异常/退出主程序；
//   - 完成时只写 config 的 roi_zone 段，主程序 10 帧 mtime 热加载自动生效。
// =============================================================================
// 整文件受 BOARD_TOUCH_ROI 保护：某些构建系统（如 Luckfox SDK 的 CMake）会 glob
// src/*.cc，此时本文件仍会被编译——没有该宏时必须编译成空 TU，避免把硬件依赖
// （ILI9341/XPT2046/sysfs_spi）带进默认二进制。
#if BOARD_TOUCH_ROI

#include "touch_roi_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "ili9341.h"
#include "roi_draw.h"
#include "sysfs_spi.h"
#include "xpt2046.h"

namespace touch_roi {
namespace {

// ---- 接线常量（接屏/换线后改这里，或按 docs 第 4 节标定）----
const int kDcPin      = 56;
const int kRstPin     = 57;
const int kLcdCsPin   = 72;
const int kBlPin      = 92;
const int kTouchCsPin = 54;
const int kPenIrqPin  = 55;

const char* kConfigPath = "./config/default_config.json";

// ---- 全局状态（init 后有效；未 init 时全部 NULL → tick 直接返回）----
hw::SysfsGpio*      g_dc = NULL;
hw::SysfsGpio*      g_rst = NULL;
hw::SysfsGpio*      g_bl = NULL;
hw::SysfsGpio*      g_lcdCs = NULL;
hw::SysfsGpio*      g_touchCs = NULL;
hw::SysfsGpio*      g_pen = NULL;
hw::SysfsSpiBus*    g_lcdSpi = NULL;
hw::SysfsSpiBus*    g_touchSpi = NULL;
hw::SysfsLcdPins*   g_pins = NULL;
hw::Ili9341*        g_lcd = NULL;
hw::SysfsTouchPin*  g_penPin = NULL;
hw::Xpt2046*        g_touch = NULL;
hw::Ili9341RoiDisplay* g_display = NULL;
hw::RoiDraw*        g_roi = NULL;

bool     g_ready = false;
bool     g_active = false;
uint32_t g_lastPollMs = 0;
bool     g_wasTouched = false;

// 屏幕尺寸（ILI9341 320x240）
const int kScreenW = 320;
const int kScreenH = 240;

void delayMs(uint32_t ms) { usleep(ms * 1000u); }

// ---- 配置合并：把 zoneJson() 片段写回 config 的 roi_zone 段 ----
// zoneJson() 形如：  "roi_zone": {\n  "enabled": true,\n  "polygon": [...], ...\n}
// 做法：读全文 -> 定位 "roi_zone" 键 -> 找到其对象 { ... } 的结束位置（字符串感知的
// 花括号配平）-> 用新片段替换该键值段 -> 原子写回（先写 .tmp 再 rename）。
bool mergeRoiZoneIntoConfig(const std::string& fragment) {
    if (fragment.size() < 16) return false;
    FILE* f = fopen(kConfigPath, "rb");
    if (!f) {
        fprintf(stderr, "[TOUCH_ROI] 打不开 %s\n", kConfigPath);
        return false;
    }
    std::string text;
    {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    }
    fclose(f);

    const std::string key = "\"roi_zone\"";
    const size_t kpos = text.find(key);
    if (kpos == std::string::npos) {
        fprintf(stderr, "[TOUCH_ROI] config 里没有 roi_zone 键，放弃写回\n");
        return false;
    }
    const size_t open = text.find('{', kpos);
    if (open == std::string::npos) return false;

    // 字符串感知的括号配平
    int depth = 0;
    bool inStr = false;
    size_t end = std::string::npos;
    for (size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) { end = i; break; }
        }
    }
    if (end == std::string::npos) return false;

    std::string out = text.substr(0, kpos) + fragment + text.substr(end + 1);
    const std::string tmp = std::string(kConfigPath) + ".tmp";
    FILE* w = fopen(tmp.c_str(), "wb");
    if (!w) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), w) == out.size();
    fclose(w);
    if (!ok) {
        remove(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), kConfigPath) != 0) {
        remove(tmp.c_str());
        return false;
    }
    fprintf(stderr, "[TOUCH_ROI] roi_zone 已写回 config（%zu 字节），等待 10 帧热加载生效\n",
            out.size());
    return true;
}

// 入口/取消热区（屏幕右下角进入、右上角取消；按钮条本身由 RoiDraw 处理）
bool inEnterZone(int x, int y) { return x >= kScreenW - 80 && y >= kScreenH - 60; }
bool inCancelZone(int x, int y) { return x >= kScreenW - 60 && y < 50; }

void drawHint() {
    if (!g_display) return;
    g_display->fillScreen(hw::rgb565(0, 0, 0));
    g_display->drawText("ROI DRAW: touch to enter", 8, 8, hw::rgb565(255, 255, 255), 1);
    g_display->drawText("[bottom-right] enter", 8, 30, hw::rgb565(0, 255, 0), 1);
    g_display->drawText("[top-right] cancel", 8, 50, hw::rgb565(255, 80, 80), 1);
    g_display->flush();
}

}  // namespace

bool init() {
    if (g_ready) return true;
    g_dc = new hw::SysfsGpio(kDcPin);
    g_rst = new hw::SysfsGpio(kRstPin);
    g_bl = new hw::SysfsGpio(kBlPin);
    g_lcdCs = new hw::SysfsGpio(kLcdCsPin);
    g_touchCs = new hw::SysfsGpio(kTouchCsPin);
    g_pen = new hw::SysfsGpio(kPenIrqPin);

    g_lcdSpi = new hw::SysfsSpiBus("/dev/spidev0.0", 1000000, 0, 8);
    g_touchSpi = new hw::SysfsSpiBus("/dev/spidev0.0", 1000000, 0, 8);
    g_lcdSpi->setCsGpio(g_lcdCs);
    g_touchSpi->setCsGpio(g_touchCs);

    g_pins = new hw::SysfsLcdPins(g_dc, g_rst, g_bl);
    g_lcd = new hw::Ili9341(g_lcdSpi, g_pins);
    g_lcd->setDelayFn(&delayMs);
    if (!g_lcd->open() || !g_lcd->begin()) {
        fprintf(stderr, "[TOUCH_ROI] LCD 初始化失败（未接屏或 SPI 不可用）→ 关闭触摸画框\n");
        shutdown();
        return false;
    }

    g_penPin = new hw::SysfsTouchPin(g_pen);
    g_touch = new hw::Xpt2046(g_touchSpi, g_penPin);
    if (!g_touch->open()) {
        fprintf(stderr, "[TOUCH_ROI] XPT2046 初始化失败 → 关闭触摸画框\n");
        shutdown();
        return false;
    }

    g_display = new hw::Ili9341RoiDisplay(g_lcd);
    g_roi = new hw::RoiDraw(g_display);
    hw::RoiDrawConfig cfg;
    cfg.screenW = kScreenW;
    cfg.screenH = kScreenH;
    cfg.pollIntervalMs = 20;   // <=50Hz
    g_roi->setConfig(cfg);
    // 默认输出映射即 320x240 -> 720x480 等比例 letterbox；如接屏后需要排除按钮条，
    // 在此调用 g_roi->setOutputMapping(...)（docs 第 5 节预设 2）。
    drawHint();
    g_ready = true;
    fprintf(stderr, "[TOUCH_ROI] 就绪（ILI9341 320x240 + XPT2046，采样 <=50Hz）\n");
    return true;
}

void tick(uint32_t now_ms) {
    if (!g_ready || !g_roi || !g_touch) return;
    // 节流：<=50Hz（20ms）
    if (g_lastPollMs != 0 && now_ms - g_lastPollMs < 20) return;
    g_lastPollMs = now_ms;

    int x = 0, y = 0;
    bool touched = g_touch->isTouched();
    if (touched) {
        if (!g_touch->readPoint(x, y)) touched = false;
    }

    if (!g_active) {
        // 待命：右下角热区按下 -> 进入画框
        if (touched && !g_wasTouched && inEnterZone(x, y)) {
            g_roi->begin();
            g_active = true;
            g_roi->render();
            fprintf(stderr, "[TOUCH_ROI] 进入 ROI_DRAW\n");
        }
    } else {
        // 画框中：右上角热区 -> 取消
        if (touched && !g_wasTouched && inCancelZone(x, y)) {
            g_roi->cancel();
            g_active = false;
            drawHint();
            fprintf(stderr, "[TOUCH_ROI] 已取消 ROI_DRAW\n");
        } else if (touched) {
            const int pressure = 200;   // 已由 isTouched() 判定按下，给一个高于阈值的压力值
            const hw::RoiDrawEvent ev = g_roi->feedTouch(x, y, pressure, now_ms);
            if (ev != hw::RoiDrawEvent::IGNORED) g_roi->render();   // 只在有事件时刷
        }
        if (g_roi->isDone()) {
            const std::string zone = g_roi->zoneJson();
            if (!zone.empty()) mergeRoiZoneIntoConfig(zone);
            g_active = false;
            drawHint();
            fprintf(stderr, "[TOUCH_ROI] ROI_DRAW 完成，已写回配置\n");
        } else if (g_roi->isCancelled()) {
            g_active = false;
            drawHint();
        }
    }
    g_wasTouched = touched;
}

bool active() { return g_active; }

void shutdown() {
    delete g_roi;          g_roi = NULL;
    delete g_display;      g_display = NULL;
    delete g_touch;        g_touch = NULL;
    delete g_penPin;       g_penPin = NULL;
    delete g_lcd;          g_lcd = NULL;
    delete g_pins;         g_pins = NULL;
    delete g_lcdSpi;       g_lcdSpi = NULL;
    delete g_touchSpi;     g_touchSpi = NULL;
    delete g_dc;           g_dc = NULL;
    delete g_rst;          g_rst = NULL;
    delete g_bl;           g_bl = NULL;
    delete g_lcdCs;        g_lcdCs = NULL;
    delete g_touchCs;      g_touchCs = NULL;
    delete g_pen;          g_pen = NULL;
    g_ready = false;
    g_active = false;
}

}  // namespace touch_roi

#endif  // BOARD_TOUCH_ROI
