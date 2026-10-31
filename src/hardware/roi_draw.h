// =============================================================================
// roi_draw.h — 触摸屏手动画 ROI 多边形状态机（前端无关，主机可测试）
// -----------------------------------------------------------------------------
// 用途：2.8 寸 ILI9341 触摸屏（320x240 RGB565）上手动圈选 ROI 危险区域，
//       输出 720x480 检测帧坐标下的 roi_zone polygon JSON 片段，供
//       risk_config.h / config/default_config.json 的 roi_zone 消费。
//
// 设计（与 lcd_menu.h 保持一致的前端无关风格）：
//   - RoiDrawDisplay 抽象：板端用 Ili9341RoiDisplay（窗口化填充 + 帧缓冲），
//     测试用 RoiFramebufferDisplay（内存帧缓冲替身）；
//   - RoiDraw 状态机只依赖 display->fillRect/fillScreen/drawPixel/drawText/
//     flush，不依赖 LVGL / Linux，可在 PC/CI 上全量测试；
//   - 触摸采样防卡死：主循环以 ≤50Hz（默认 20ms）非阻塞轮询 XPT2046，把
//     单次按下采样喂给 feedTouch()；RoiDraw 内部做 采样节流 + 压力阈值 +
//     去抖 + 越界过滤，绝不阻塞等待触摸；
//   - 标定：setCalibration(offsetX, offsetY, swapXY, invertX, invertY)，
//     对屏幕坐标做 轴交换 -> 镜像 -> 平移 三阶变换（详见 docs/hardware/
//     roi_draw_touch.md 标定步骤）；
//   - 缩放：mapToOutput() 把 320x240 屏幕坐标映射到 720x480（默认等比例
//     letterbox：min(720/320, 480/240)=2.0，水平居中 offsetX=40；映射参数
//     全部可配置）。
//
// 交互约定（320x240）：
//   - 绘图区 y < screenH-buttonHeight（默认 y<200）：
//       点空白处=加顶点；点已有顶点 8px 半径内=删除该点；
//   - 底部 3 个矩形热区按钮（默认 y=200..239）：
//       ✓ DONE  -> 顶点>=3 时完成并生成 roi_zone JSON，状态 DONE；
//       ✕ CLEAR -> 清空全部顶点；
//       ↩ UNDO  -> 撤销最后一个顶点；
//   - 取消（返回菜单）由 LcdMenu::onKey(BACK) 驱动，main.cc 同时调用
//     roi.cancel() 丢弃当前多边形。
//
// 板端集成（真机阶段，见 docs/hardware/roi_draw_touch.md）：
//   hw::Ili9341 lcd(&spi, &pins); lcd.open(); lcd.setRotation(0);
//   hw::Ili9341RoiDisplay display(&lcd);
//   hw::RoiDraw roi(&display);
//   roi.setCalibration(cal);   // 上板标定后填入
//   主循环每帧（非阻塞，节流 20ms）：
//     if (tp.isTouched() && tp.readPoint(x, y)) {
//         roi.feedTouch(x, y, pressure, nowMs);
//     }
//   ✓ 完成：roi.isDone() -> 取 roi.zoneJson() 落盘/合并进 config；
//   ✕/↩ 清除/撤销：roi 内部处理并重绘；
//   BACK 返回：menu.onKey(BACK) + roi.cancel()。
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ili9341.h"   // 复用 hw::rgb565 / hw::Ili9341

namespace hw {

// -----------------------------------------------------------------------------
// 基础类型与配置
// -----------------------------------------------------------------------------
struct RoiPoint {
    int x;
    int y;
};

// 触摸屏幕坐标微调标定（在 XPT2046 已映射到屏幕坐标之后再做一次三阶修正）
struct RoiCalibration {
    int  offsetX = 0;      // X 平移（像素）
    int  offsetY = 0;      // Y 平移（像素）
    bool swapXY  = false;  // 先交换 X/Y 轴
    bool invertX = false;  // 再镜像 X：x' = screenW-1-x
    bool invertY = false;  // 再镜像 Y：y' = screenH-1-y
};

// 输出映射（屏幕坐标 -> 检测帧坐标）。默认 320x240 -> 720x480 等比例 letterbox。
struct RoiOutputMapping {
    int  srcX = 0;           // 屏幕源区域左上角 X
    int  srcY = 0;           // 屏幕源区域左上角 Y
    int  srcW = 320;         // 屏幕源区域宽度
    int  srcH = 240;         // 屏幕源区域高度
    int  outW = 720;         // 输出帧宽度
    int  outH = 480;         // 输出帧高度
    bool preserveAspect = true;  // true=等比例 letterbox；false=拉伸填满
};

// 触摸采样与绘制参数
struct RoiDrawConfig {
    int      screenW = 320;          // 触摸屏宽
    int      screenH = 240;          // 触摸屏高
    int      vertexRadius = 8;       // 顶点圆半径（>=8，好按）
    int      maxVertices = 16;       // 顶点上限
    int      buttonHeight = 40;      // 底部按钮热区高度
    uint32_t pollIntervalMs = 20;    // 采样节流（<=50Hz）
    uint32_t debounceMs = 50;        // 按下防抖
    int      pressureThreshold = 50; // 压力阈值（|Z1-Z2| ADC 计数，与 XPT2046 默认一致）
};

// -----------------------------------------------------------------------------
// 状态与事件
// -----------------------------------------------------------------------------
enum class RoiDrawState : uint8_t {
    IDLE = 0,      // 未开始
    DRAWING,       // 正在画
    DONE,          // ✓ 完成（顶点>=3）
    CANCELLED      // 已取消（返回菜单）
};

enum class RoiDrawEvent : uint8_t {
    IGNORED = 0,   // 被节流/压力不足/越界/按钮无效等忽略
    VERTEX_ADDED,
    VERTEX_REMOVED,
    CLEARED,
    UNDONE,
    DONE
};

enum class RoiDrawButton : uint8_t {
    NONE = 0,
    DONE,
    CLEAR,
    UNDO
};

// -----------------------------------------------------------------------------
// 按钮标签：✓ OK / ✕ CLR / ↩ UNDO（中文标签见 docs/hardware/roi_draw_touch.md，
// 板端若走 LVGL 适配可用中文字库替换 drawText 文本）。
// -----------------------------------------------------------------------------
static const char kRoiDoneLabel[]  = "\xE2\x9C\x93 OK";   // U+2713 ✓
static const char kRoiClearLabel[] = "\xE2\x9C\x95 CLR";  // U+2715 ✕
static const char kRoiUndoLabel[]  = "\xE2\x86\xA9 UNDO"; // U+21A9 ↩

// -----------------------------------------------------------------------------
// 颜色（RGB565）。按钮底色与风险语义一致：完成=绿、清除=红、撤销=灰。
// -----------------------------------------------------------------------------
inline uint16_t roiBgColor()     { return rgb565(0, 0, 0); }        // 黑
inline uint16_t roiVertexColor() { return rgb565(255, 255, 255); }  // 白（顶点大圆点）
inline uint16_t roiLineColor()   { return rgb565(255, 255, 0); }    // 黄（线段/闭合预览）
inline uint16_t roiDoneColor()   { return rgb565(0, 140, 0); }      // 深绿
inline uint16_t roiClearColor()  { return rgb565(140, 0, 0); }      // 深红
inline uint16_t roiUndoColor()   { return rgb565(90, 90, 90); }     // 深灰
inline uint16_t roiTextColor()   { return rgb565(255, 255, 255); }  // 白

// -----------------------------------------------------------------------------
// RoiDrawDisplay — 绘制后端抽象
//   板端实现：Ili9341RoiDisplay（见下）
//   测试实现：RoiFramebufferDisplay（内存帧缓冲替身）
// -----------------------------------------------------------------------------
class RoiDrawDisplay {
public:
    virtual ~RoiDrawDisplay() {}

    virtual void fillScreen(uint16_t color) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
    virtual void drawPixel(int x, int y, uint16_t color) = 0;
    // 5x7 ASCII + ✓/✕/↩ 内置点阵，scale 为整数放大倍数
    virtual void drawText(const char* text, int x, int y, uint16_t color, uint8_t scale) = 0;
    virtual void flush() = 0;
    virtual int  width() const = 0;
    virtual int  height() const = 0;
};

namespace detail {

// -----------------------------------------------------------------------------
// 内置 5x7 ASCII 点阵（空格 + 0-9 + A-Z，列优先，bit0=顶部行）
// -----------------------------------------------------------------------------
static const uint8_t kFont5x7[37][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x7C, 0x12, 0x11, 0x12, 0x7C},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x01, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x3A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x30, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};

// 8x8 符号点阵（bit7=左侧像素）
static const uint8_t kGlyphCheck[8] = {
    0x00, 0x01, 0x03, 0x06, 0x0C, 0x18, 0x31, 0x63
};  // U+2713 ✓（近似）
static const uint8_t kGlyphCross[8] = {
    0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81
};  // U+2715 ✕
static const uint8_t kGlyphUndo[8] = {
    0x00, 0x1E, 0x20, 0x40, 0x40, 0x44, 0x5E, 0x00
};  // U+21A9 ↩（近似）

// 解码一个 UTF-8 字形。返回：
//   0..36  -> kFont5x7 下标（空格/0-9/A-Z）
//   100    -> ✓ 101 -> ✕ 102 -> ↩
//   -1     -> 未知（渲染为空，按 1 字节跳过）
inline int decodeGlyph(const char* s, int& bytes) {
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (c0 < 0x80) {
        bytes = 1;
        if (c0 == ' ') return 0;
        if (c0 >= '0' && c0 <= '9') return 1 + (c0 - '0');
        if (c0 >= 'A' && c0 <= 'Z') return 11 + (c0 - 'A');
        if (c0 >= 'a' && c0 <= 'z') return 11 + (c0 - 'a');
        return -1;
    }
    if (c0 == 0xE2 && static_cast<unsigned char>(s[1]) == 0x9C) {
        if (static_cast<unsigned char>(s[2]) == 0x93) { bytes = 3; return 100; }  // ✓
        if (static_cast<unsigned char>(s[2]) == 0x95) { bytes = 3; return 101; }  // ✕
    }
    if (c0 == 0xE2 && static_cast<unsigned char>(s[1]) == 0x86 &&
        static_cast<unsigned char>(s[2]) == 0xA9) {
        bytes = 3;
        return 102;  // ↩
    }
    bytes = 1;
    return -1;
}

inline int glyphWidthAtScale1(int glyph) {
    return glyph >= 100 ? 8 : 5;
}

inline int glyphAdvanceAtScale1(int glyph) {
    return glyph >= 100 ? 9 : 6;  // 字形宽 + 1px 间距
}

inline int textWidth(const char* text, uint8_t scale) {
    if (!text) return 0;
    int w = 0;
    int i = 0;
    while (text[i] != '\0') {
        int bytes = 1;
        const int gi = decodeGlyph(text + i, bytes);
        if (gi >= 0) {
            w += glyphAdvanceAtScale1(gi);
        } else {
            w += 6;  // 未知字形按一个空格宽度跳过
        }
        i += bytes;
    }
    if (w > 0) w -= 1;  // 去掉末尾多余间距
    return w * static_cast<int>(scale);
}

inline void renderText(RoiDrawDisplay* d, const char* text, int x, int y,
                       uint16_t color, uint8_t scale) {
    if (!d || !text) return;
    const int s = scale < 1 ? 1 : static_cast<int>(scale);
    int cx = x;
    int i = 0;
    while (text[i] != '\0') {
        int bytes = 1;
        const int gi = decodeGlyph(text + i, bytes);
        if (gi >= 100) {
            const uint8_t* g = (gi == 100) ? kGlyphCheck
                              : (gi == 101) ? kGlyphCross : kGlyphUndo;
            for (int row = 0; row < 8; ++row) {
                const uint8_t bits = g[row];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (0x80u >> col)) {
                        for (int sy = 0; sy < s; ++sy) {
                            for (int sx = 0; sx < s; ++sx) {
                                d->drawPixel(cx + col * s + sx, y + row * s + sy, color);
                            }
                        }
                    }
                }
            }
            cx += glyphAdvanceAtScale1(gi) * s;
        } else if (gi >= 0) {
            const uint8_t* g = kFont5x7[gi];
            for (int col = 0; col < 5; ++col) {
                const uint8_t bits = g[col];
                for (int row = 0; row < 7; ++row) {
                    if (bits & (1u << row)) {
                        for (int sy = 0; sy < s; ++sy) {
                            for (int sx = 0; sx < s; ++sx) {
                                d->drawPixel(cx + col * s + sx, y + row * s + sy, color);
                            }
                        }
                    }
                }
            }
            cx += glyphAdvanceAtScale1(gi) * s;
        } else {
            cx += 6 * s;
        }
        i += bytes;
    }
}

}  // namespace detail

// -----------------------------------------------------------------------------
// RoiFramebufferDisplay — 内存帧缓冲替身（主机测试 + 无 LVGL 板端可用）
// -----------------------------------------------------------------------------
class RoiFramebufferDisplay : public RoiDrawDisplay {
public:
    explicit RoiFramebufferDisplay(int w = 320, int h = 240)
        : w_(w > 0 ? w : 320),
          h_(h > 0 ? h : 240),
          fb_(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0) {}

    void fillScreen(uint16_t color) override {
        for (size_t i = 0; i < fb_.size(); ++i) fb_[i] = color;
    }

    void fillRect(int x, int y, int w, int h, uint16_t color) override {
        if (w <= 0 || h <= 0) return;
        const int x0 = x < 0 ? 0 : x;
        const int y0 = y < 0 ? 0 : y;
        const int x1 = x + w - 1 >= w_ ? w_ - 1 : x + w - 1;
        const int y1 = y + h - 1 >= h_ ? h_ - 1 : y + h - 1;
        if (x0 > x1 || y0 > y1) return;
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                fb_[static_cast<size_t>(yy) * w_ + xx] = color;
            }
        }
    }

    void drawPixel(int x, int y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        fb_[static_cast<size_t>(y) * w_ + x] = color;
    }

    void drawText(const char* text, int x, int y, uint16_t color, uint8_t scale) override {
        detail::renderText(this, text, x, y, color, scale);
    }

    void flush() override {}

    int width() const override  { return w_; }
    int height() const override { return h_; }

    uint16_t pixelAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
        return fb_[static_cast<size_t>(y) * w_ + x];
    }

    const std::vector<uint16_t>& framebuffer() const { return fb_; }

private:
    int w_;
    int h_;
    std::vector<uint16_t> fb_;
};

// -----------------------------------------------------------------------------
// Ili9341RoiDisplay — 板端适配：窗口化填充按钮 + 帧缓冲绘制线/文字
//   fillRect：先走 lcd->fillRect（CASET/PASET/RAMWR 窗口化填充，立即上屏），
//             再镜像进 lcd 帧缓冲，保证后续 flush 时画面一致；
//   drawPixel/drawText：只写帧缓冲，由 flush() 整帧推送；
//   renderFrame 的绘制顺序保证最终 flush 后按钮、文字、顶点、线段全部可见。
// -----------------------------------------------------------------------------
class Ili9341RoiDisplay : public RoiDrawDisplay {
public:
    explicit Ili9341RoiDisplay(Ili9341* lcd) : lcd_(lcd) {}

    void fillScreen(uint16_t color) override {
        if (lcd_) lcd_->clear(color);
    }

    void fillRect(int x, int y, int w, int h, uint16_t color) override {
        if (!lcd_ || w <= 0 || h <= 0) return;
        const int x0 = x < 0 ? 0 : x;
        const int y0 = y < 0 ? 0 : y;
        const int x1 = x + w - 1 >= Ili9341::kWidth  ? Ili9341::kWidth - 1  : x + w - 1;
        const int y1 = y + h - 1 >= Ili9341::kHeight ? Ili9341::kHeight - 1 : y + h - 1;
        if (x0 > x1 || y0 > y1) return;
        // 1) 窗口化填充（立即上屏）
        lcd_->fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, color);
        // 2) 镜像进帧缓冲，保证 flush 后与屏幕一致
        for (int yy = y0; yy <= y1; ++yy) {
            for (int xx = x0; xx <= x1; ++xx) {
                lcd_->setPixel(xx, yy, color);
            }
        }
    }

    void drawPixel(int x, int y, uint16_t color) override {
        if (lcd_) lcd_->setPixel(x, y, color);
    }

    void drawText(const char* text, int x, int y, uint16_t color, uint8_t scale) override {
        detail::renderText(this, text, x, y, color, scale);
    }

    void flush() override {
        if (lcd_) lcd_->flush();
    }

    int width() const override  { return lcd_ ? Ili9341::kWidth : 0; }
    int height() const override { return lcd_ ? Ili9341::kHeight : 0; }

private:
    Ili9341* lcd_;
};

// -----------------------------------------------------------------------------
// RoiDraw — 手动画 ROI 状态机
//   构造注入 RoiDrawDisplay*，允许为 NULL（逻辑可测试，渲染安全忽略）。
// -----------------------------------------------------------------------------
class RoiDraw {
public:
    explicit RoiDraw(RoiDrawDisplay* display)
        : display_(display),
          state_(RoiDrawState::IDLE),
          lastSampleMs_(-1),
          lastTapMs_(-1) {
        if (display_) {
            if (display_->width() > 0) cfg_.screenW = display_->width();
            if (display_->height() > 0) cfg_.screenH = display_->height();
        }
        sanitizeConfig();
        sanitizeMapping();
    }

    // ---- 配置 ----
    void setConfig(const RoiDrawConfig& cfg) {
        cfg_ = cfg;
        sanitizeConfig();
        sanitizeMapping();
    }
    const RoiDrawConfig& config() const { return cfg_; }

    void setCalibration(int offsetX, int offsetY, bool swapXY, bool invertX, bool invertY) {
        cal_.offsetX = offsetX;
        cal_.offsetY = offsetY;
        cal_.swapXY = swapXY;
        cal_.invertX = invertX;
        cal_.invertY = invertY;
    }
    void setCalibration(const RoiCalibration& cal) { cal_ = cal; }
    const RoiCalibration& calibration() const { return cal_; }

    void setOutputMapping(const RoiOutputMapping& m) {
        map_ = m;
        sanitizeMapping();
    }
    const RoiOutputMapping& outputMapping() const { return map_; }

    // ---- 生命周期 ----
    void begin() {
        vertices_.clear();
        state_ = RoiDrawState::DRAWING;
        lastSampleMs_ = -1;
        lastTapMs_ = -1;
        renderFrame();
    }

    // 取消（返回菜单）：丢弃当前多边形并清屏
    void cancel() {
        if (state_ == RoiDrawState::DONE) return;
        vertices_.clear();
        state_ = RoiDrawState::CANCELLED;
        renderFrame();
    }

    // 强制整帧重绘（屏幕旋转/页面重新进入时调用）
    void render() { renderFrame(); }

    RoiDrawState state() const { return state_; }
    bool isDone() const { return state_ == RoiDrawState::DONE; }
    bool isCancelled() const { return state_ == RoiDrawState::CANCELLED; }

    size_t vertexCount() const { return vertices_.size(); }
    const std::vector<RoiPoint>& vertices() const { return vertices_; }
    RoiPoint vertex(size_t i) const { return vertices_[i]; }

    // ---- 触摸采样（非阻塞，防卡死核心）----
    // 主循环在按下沿调用一次；本函数内部完成：
    //   ① 压力阈值过滤 ② ≤50Hz 采样节流 ③ 标定+越界过滤 ④ 按下防抖 ⑤ 命中处理
    RoiDrawEvent feedTouch(int rawX, int rawY, int pressure, uint32_t nowMs) {
        if (state_ != RoiDrawState::DRAWING) return RoiDrawEvent::IGNORED;
        const int64_t now = static_cast<int64_t>(nowMs);

        // ① 压力不足：非有效按压
        if (pressure < cfg_.pressureThreshold) return RoiDrawEvent::IGNORED;

        // ② 采样节流：主循环即便 >50Hz 喂入，也只处理 50Hz
        if (lastSampleMs_ >= 0 && now - lastSampleMs_ < static_cast<int64_t>(cfg_.pollIntervalMs)) {
            return RoiDrawEvent::IGNORED;
        }
        lastSampleMs_ = now;

        // ③ 标定（轴交换 -> 镜像 -> 平移），越界直接忽略
        RoiPoint p;
        if (!calibrate(rawX, rawY, p)) return RoiDrawEvent::IGNORED;

        // ④ 防抖：距上一次有效按下不足 debounceMs 忽略
        if (lastTapMs_ >= 0 && now - lastTapMs_ < static_cast<int64_t>(cfg_.debounceMs)) {
            return RoiDrawEvent::IGNORED;
        }
        lastTapMs_ = now;

        // ⑤ 命中处理
        return handleTap(p);
    }

    // ---- 坐标变换（公开便于测试）----
    // 标定：swapXY -> invertX/invertY -> offset，返回是否落在屏幕内（未 clamp）
    bool calibrate(int rawX, int rawY, RoiPoint& out) const {
        int x = rawX;
        int y = rawY;
        if (cal_.swapXY) {
            const int t = x;
            x = y;
            y = t;
        }
        if (cal_.invertX) x = cfg_.screenW - 1 - x;
        if (cal_.invertY) y = cfg_.screenH - 1 - y;
        x += cal_.offsetX;
        y += cal_.offsetY;
        out.x = x;
        out.y = y;
        return x >= 0 && x < cfg_.screenW && y >= 0 && y < cfg_.screenH;
    }

    // 屏幕坐标 -> 输出帧坐标（默认 320x240 -> 720x480 等比例 letterbox）
    RoiPoint mapToOutput(const RoiPoint& p) const {
        const RoiOutputMapping& m = map_;
        const int64_t x = static_cast<int64_t>(p.x) - m.srcX;
        const int64_t y = static_cast<int64_t>(p.y) - m.srcY;
        int ox = 0;
        int oy = 0;
        int64_t mx = 0;
        int64_t my = 0;
        if (m.preserveAspect) {
            if (static_cast<int64_t>(m.outW) * m.srcH <
                static_cast<int64_t>(m.outH) * m.srcW) {
                // 宽度受限：按 X 方向缩放，Y 居中
                const int64_t scaledH = static_cast<int64_t>(m.srcH) * m.outW / m.srcW;
                oy = static_cast<int>((m.outH - scaledH) / 2);
                mx = (x * m.outW + m.srcW / 2) / m.srcW;
                my = (y * m.outW + m.srcW / 2) / m.srcW;
            } else {
                // 高度受限：按 Y 方向缩放，X 居中（320x240->720x480 走这里：scale=2.0）
                const int64_t scaledW = static_cast<int64_t>(m.srcW) * m.outH / m.srcH;
                ox = static_cast<int>((m.outW - scaledW) / 2);
                mx = (x * m.outH + m.srcH / 2) / m.srcH;
                my = (y * m.outH + m.srcH / 2) / m.srcH;
            }
        } else {
            mx = (x * m.outW + m.srcW / 2) / m.srcW;
            my = (y * m.outH + m.srcH / 2) / m.srcH;
        }
        RoiPoint out;
        out.x = static_cast<int>(mx) + ox;
        out.y = static_cast<int>(my) + oy;
        return out;
    }

    // 生成 roi_zone polygon JSON 片段（顶点<3 时返回空串）
    std::string zoneJson() const {
        if (vertices_.size() < 3) return std::string();
        std::string s;
        s.reserve(512);
        s += "\"roi_zone\": {\n";
        s += "  \"enabled\": true,\n";
        s += "  \"name\": \"manual_roi\",\n";
        s += "  \"polygon\": [\n";
        for (size_t i = 0; i < vertices_.size(); ++i) {
            const RoiPoint o = mapToOutput(vertices_[i]);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "    [%d, %d]", o.x, o.y);
            s += buf;
            if (i + 1 < vertices_.size()) s += ",";
            s += "\n";
        }
        s += "  ],\n";
        s += "  \"color\": [0, 0, 255],\n";
        s += "  \"thickness\": 2\n";
        s += "}";
        return s;
    }

    // ---- 命中判定（公开便于测试）----
    RoiDrawButton hitTestButton(const RoiPoint& p) const {
        const int buttonY = cfg_.screenH - cfg_.buttonHeight;
        if (p.y < buttonY || p.y >= cfg_.screenH) return RoiDrawButton::NONE;
        if (p.x < 0 || p.x >= cfg_.screenW) return RoiDrawButton::NONE;
        const int bw = cfg_.screenW / 3;
        if (p.x < bw) return RoiDrawButton::DONE;
        if (p.x < bw * 2) return RoiDrawButton::CLEAR;
        return RoiDrawButton::UNDO;
    }

    // 返回与 p 距离 <= vertexRadius 的最近顶点下标；无命中返回 -1
    int hitVertex(const RoiPoint& p) const {
        const int r2 = cfg_.vertexRadius * cfg_.vertexRadius;
        int best = -1;
        int bestD2 = r2;
        for (size_t i = 0; i < vertices_.size(); ++i) {
            const int dx = vertices_[i].x - p.x;
            const int dy = vertices_[i].y - p.y;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) {
                bestD2 = d2;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

private:
    // ---- 状态机内部 ----
    RoiDrawEvent handleTap(const RoiPoint& p) {
        const RoiDrawButton btn = hitTestButton(p);
        if (btn != RoiDrawButton::NONE) {
            switch (btn) {
                case RoiDrawButton::DONE:
                    if (vertices_.size() >= 3) {
                        state_ = RoiDrawState::DONE;
                        return RoiDrawEvent::DONE;
                    }
                    return RoiDrawEvent::IGNORED;  // 顶点不足 3 个，完成无效
                case RoiDrawButton::CLEAR:
                    vertices_.clear();
                    renderFrame();
                    return RoiDrawEvent::CLEARED;
                case RoiDrawButton::UNDO:
                    if (!vertices_.empty()) {
                        vertices_.pop_back();
                        renderFrame();
                        return RoiDrawEvent::UNDONE;
                    }
                    return RoiDrawEvent::IGNORED;
                default:
                    return RoiDrawEvent::IGNORED;
            }
        }

        // 绘图区：命中顶点=删除；否则（未超上限）加顶点
        const int idx = hitVertex(p);
        if (idx >= 0) {
            vertices_.erase(vertices_.begin() + idx);
            renderFrame();
            return RoiDrawEvent::VERTEX_REMOVED;
        }
        if (static_cast<int>(vertices_.size()) >= cfg_.maxVertices) {
            return RoiDrawEvent::IGNORED;
        }
        vertices_.push_back(p);
        renderFrame();
        return RoiDrawEvent::VERTEX_ADDED;
    }

    // ---- 绘制 ----
    void renderFrame() {
        if (!display_) return;
        const int n = static_cast<int>(vertices_.size());

        display_->fillScreen(roiBgColor());

        // 线段连线（>=3 点自动闭合预览）
        for (int i = 0; i + 1 < n; ++i) {
            drawLine(vertices_[static_cast<size_t>(i)],
                     vertices_[static_cast<size_t>(i + 1)], roiLineColor());
        }
        if (n >= 3) {
            drawLine(vertices_[static_cast<size_t>(n - 1)], vertices_[0], roiLineColor());
        }

        // 顶点大圆点（半径>=8px）
        for (int i = 0; i < n; ++i) {
            fillCircle(vertices_[static_cast<size_t>(i)], cfg_.vertexRadius, roiVertexColor());
        }

        // 底部按钮：先填底色，再画文字，最后 flush 保证线/文字上屏
        const int buttonY = cfg_.screenH - cfg_.buttonHeight;
        drawButtonRects(buttonY);
        drawButtonTexts(buttonY);
        display_->flush();
    }

    void drawButtonRects(int buttonY) {
        const int bw = cfg_.screenW / 3;
        display_->fillRect(0, buttonY, bw, cfg_.buttonHeight, roiDoneColor());
        display_->fillRect(bw, buttonY, bw, cfg_.buttonHeight, roiClearColor());
        display_->fillRect(bw * 2, buttonY, cfg_.screenW - bw * 2,
                           cfg_.buttonHeight, roiUndoColor());
    }

    void drawButtonTexts(int buttonY) {
        const int bw = cfg_.screenW / 3;
        const uint8_t scale = 2;
        drawButtonText(0, bw, buttonY, kRoiDoneLabel, scale);
        drawButtonText(bw, bw, buttonY, kRoiClearLabel, scale);
        drawButtonText(bw * 2, cfg_.screenW - bw * 2, buttonY, kRoiUndoLabel, scale);
    }

    void drawButtonText(int x0, int w, int buttonY, const char* label, uint8_t scale) {
        const int tw = detail::textWidth(label, scale);
        const int th = 8 * static_cast<int>(scale);  // 取符号/ASCII 最大高度
        int tx = x0 + (w - tw) / 2;
        int ty = buttonY + (cfg_.buttonHeight - th) / 2;
        if (tx < x0 + 1) tx = x0 + 1;
        if (ty < buttonY + 1) ty = buttonY + 1;
        display_->drawText(label, tx, ty, roiTextColor(), scale);
    }

    void drawLine(const RoiPoint& a, const RoiPoint& b, uint16_t color) {
        int x0 = a.x;
        int y0 = a.y;
        const int x1 = b.x;
        const int y1 = b.y;
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            display_->drawPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void fillCircle(const RoiPoint& c, int r, uint16_t color) {
        const int r2 = r * r;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r2) {
                    display_->drawPixel(c.x + dx, c.y + dy, color);
                }
            }
        }
    }

    // ---- 配置清洗 ----
    void sanitizeConfig() {
        if (cfg_.screenW < 1) cfg_.screenW = 320;
        if (cfg_.screenH < 1) cfg_.screenH = 240;
        if (cfg_.vertexRadius < 8) cfg_.vertexRadius = 8;      // 要求半径>=8
        if (cfg_.maxVertices < 3) cfg_.maxVertices = 16;
        if (cfg_.buttonHeight < 1 || cfg_.buttonHeight > cfg_.screenH / 2) {
            cfg_.buttonHeight = 40;
        }
    }

    void sanitizeMapping() {
        if (map_.srcW < 1) map_.srcW = cfg_.screenW;
        if (map_.srcH < 1) map_.srcH = cfg_.screenH;
        if (map_.outW < 1) map_.outW = 720;
        if (map_.outH < 1) map_.outH = 480;
        if (map_.srcX < 0) map_.srcX = 0;
        if (map_.srcY < 0) map_.srcY = 0;
        if (map_.srcX + map_.srcW > cfg_.screenW) map_.srcW = cfg_.screenW - map_.srcX;
        if (map_.srcY + map_.srcH > cfg_.screenH) map_.srcH = cfg_.screenH - map_.srcY;
    }

    // ---- 成员 ----
    RoiDrawDisplay*       display_;
    RoiDrawConfig         cfg_;
    RoiCalibration        cal_;
    RoiOutputMapping      map_;
    RoiDrawState          state_;
    std::vector<RoiPoint> vertices_;
    int64_t               lastSampleMs_;
    int64_t               lastTapMs_;
};

}  // namespace hw
