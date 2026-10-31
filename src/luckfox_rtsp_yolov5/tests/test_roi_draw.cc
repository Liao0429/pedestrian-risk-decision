// =============================================================================
// test_roi_draw.cc — 手动画 ROI 多边形状态机主机单元测试
// -----------------------------------------------------------------------------
// 验证点（使用内存替身：RoiFramebufferDisplay 帧缓冲 / FakeSpi+FakeLcdPins+
// Ili9341 真驱动对象，无需真实硬件）：
//   1. 默认配置与清洗（顶点半径>=8、maxVertices>=3、节流/防抖/压力阈值）；
//   2. 加顶点：点空白绘图区 -> VERTEX_ADDED，帧缓冲出现顶点圆点/线段/按钮；
//   3. 删除：点已有顶点 8px 半径内 -> VERTEX_REMOVED；
//   4. 清除/撤销按钮：CLEAR 清空、UNDO 弹出最后一个顶点；
//   5. 完成缩放：>=3 点后 ✓ -> DONE，zoneJson 输出 320x240 -> 720x480
//      等比例 letterbox 坐标（scale=2.0，X 居中 offset 40）；
//   6. 热区命中：底部三按钮边界（x=105/106、211/212、y=199/200）；
//   7. 去抖/节流/压力阈值：<50ms 防抖、<20ms 节流、pressure<threshold 忽略；
//   8. 越界顶点：标定后超出 0..screenW/H 的触摸被忽略；
//   9. 标定变换：swapXY/invertX/invertY/offset 三阶变换；
//  10. 输出映射可配置：letterbox / stretch / 自定义源区域；
//  11. NULL 显示后端容错；
//  12. 文字渲染（按钮标签有白色像素）；
//  13. Ili9341 窗口化填充适配器（SPI 命令 + 帧缓冲镜像）；
//  14. lcd_menu.h 集成：enterRoiDraw/leaveRoiDraw + OK/BACK 状态迁移。
//
// 编译（无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I../../hardware test_roi_draw.cc -o test_roi_draw
// =============================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "roi_draw.h"
#include "lcd_menu.h"

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
    int openCount = 0;
    int closeCount = 0;
    std::vector<uint8_t> writes;

    bool open() override {
        if (!openResult) return false;
        ++openCount;
        return true;
    }
    void close() override { ++closeCount; }
    bool isOpen() const override { return openCount > closeCount; }

    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        (void)rx;
        if (tx) writes.insert(writes.end(), tx, tx + len);
        return true;
    }

    bool wroteByte(uint8_t b) const {
        for (size_t i = 0; i < writes.size(); ++i) {
            if (writes[i] == b) return true;
        }
        return false;
    }
};

class FakeLcdPins : public hw::LcdPins {
public:
    bool dcCommand() override { return true; }
    bool dcData() override { return true; }
    bool resetLow() override { return true; }
    bool resetHigh() override { return true; }
    bool backlight(bool) override { return true; }
};

// 最小菜单后端替身（仅记录 drawPage，用于 lcd_menu 集成验证）
class FakeMenuBackend : public hw::MenuBackend {
public:
    int beginCount = 0;
    int drawCount = 0;
    hw::MenuPage lastPage = hw::MenuPage::COUNT;

    void begin() override { ++beginCount; }
    void drawPage(hw::MenuPage page) override {
        ++drawCount;
        lastPage = page;
    }
    void setParam(hw::MenuParam, const char*) override {}
    void setRiskLevel(int, hw::MenuColor) override {}
    void setMenuCursor(int) override {}
};

// 统计某个矩形区域内指定颜色的像素个数
int countColor(const hw::RoiFramebufferDisplay& d, int x0, int y0, int x1, int y1,
               uint16_t color) {
    int n = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (d.pixelAt(x, y) == color) ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// 1. 默认配置与清洗
// ---------------------------------------------------------------------------
void testDefaultsAndSanitize() {
    hw::RoiDraw roi(nullptr);
    CHECK(roi.config().screenW == 320);
    CHECK(roi.config().screenH == 240);
    CHECK(roi.config().vertexRadius >= 8);
    CHECK(roi.config().maxVertices >= 3);
    CHECK(roi.config().pollIntervalMs == 20);
    CHECK(roi.config().debounceMs == 50);
    CHECK(roi.config().pressureThreshold == 50);
    CHECK(roi.config().buttonHeight == 40);

    hw::RoiDrawConfig cfg;
    cfg.vertexRadius = 4;   // 应被清洗为 >=8
    cfg.maxVertices = 1;    // 应被清洗为 >=3
    cfg.buttonHeight = 500; // 应被清洗为 40
    roi.setConfig(cfg);
    CHECK(roi.config().vertexRadius >= 8);
    CHECK(roi.config().maxVertices >= 3);
    CHECK(roi.config().buttonHeight == 40);
}

// ---------------------------------------------------------------------------
// 2. 加顶点 + 帧缓冲渲染
// ---------------------------------------------------------------------------
void testAddVertexAndRender() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();
    CHECK(roi.state() == hw::RoiDrawState::DRAWING);
    CHECK(roi.vertexCount() == 0);

    CHECK(roi.feedTouch(100, 100, 100, 0) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 1);
    CHECK(roi.vertex(0).x == 100 && roi.vertex(0).y == 100);

    // 顶点大圆点（白色）已画入帧缓冲；背景为黑；按钮底色已画
    CHECK(d.pixelAt(100, 100) == hw::roiVertexColor());
    CHECK(d.pixelAt(0, 0) == hw::roiBgColor());
    CHECK(d.pixelAt(50, 220) == hw::roiDoneColor());

    // 第二条边：水平线段中点应为线段黄
    CHECK(roi.feedTouch(200, 100, 100, 100) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 2);
    CHECK(d.pixelAt(150, 100) == hw::roiLineColor());

    // 第三点：>=3 自动闭合预览，仍处于 DRAWING
    CHECK(roi.feedTouch(160, 160, 100, 200) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 3);
    CHECK(roi.state() == hw::RoiDrawState::DRAWING);
}

// ---------------------------------------------------------------------------
// 3. 点按已有顶点附近 = 删除该点
// ---------------------------------------------------------------------------
void testDeleteVertex() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();
    roi.feedTouch(100, 100, 100, 0);
    roi.feedTouch(200, 100, 100, 100);
    roi.feedTouch(160, 160, 100, 200);
    CHECK(roi.vertexCount() == 3);

    // (103,100) 距 (100,100) 仅 3px，应删除第 0 个顶点
    CHECK(roi.feedTouch(103, 100, 100, 300) == hw::RoiDrawEvent::VERTEX_REMOVED);
    CHECK(roi.vertexCount() == 2);
    CHECK(roi.vertex(0).x == 200 && roi.vertex(0).y == 100);
    CHECK(roi.vertex(1).x == 160 && roi.vertex(1).y == 160);

    // 半径边界：距离恰为 8 也命中删除（好按容差）
    roi.feedTouch(300, 100, 100, 400);  // 空白加顶点（距最近 100px）
    CHECK(roi.vertexCount() == 3);
    CHECK(roi.feedTouch(308, 100, 100, 500) == hw::RoiDrawEvent::VERTEX_REMOVED);
    CHECK(roi.vertexCount() == 2);
}

// ---------------------------------------------------------------------------
// 4. 清除 / 撤销
// ---------------------------------------------------------------------------
void testClearAndUndo() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();

    roi.feedTouch(100, 100, 100, 0);
    roi.feedTouch(200, 100, 100, 100);
    CHECK(roi.vertexCount() == 2);

    // ✕ CLEAR：x=160 落在第二个按钮（106..211），y=220 按钮区
    CHECK(roi.feedTouch(160, 220, 100, 200) == hw::RoiDrawEvent::CLEARED);
    CHECK(roi.vertexCount() == 0);
    CHECK(roi.state() == hw::RoiDrawState::DRAWING);

    roi.feedTouch(100, 100, 100, 300);
    roi.feedTouch(200, 100, 100, 400);
    CHECK(roi.vertexCount() == 2);

    // ↩ UNDO：x=280 落在第三个按钮（212..319）
    CHECK(roi.feedTouch(280, 220, 100, 500) == hw::RoiDrawEvent::UNDONE);
    CHECK(roi.vertexCount() == 1);
    CHECK(roi.vertex(0).x == 100);

    CHECK(roi.feedTouch(280, 220, 100, 600) == hw::RoiDrawEvent::UNDONE);
    CHECK(roi.vertexCount() == 0);
    // 空栈撤销：忽略
    CHECK(roi.feedTouch(280, 220, 100, 700) == hw::RoiDrawEvent::IGNORED);
}

// ---------------------------------------------------------------------------
// 5. 完成缩放 + roi_zone JSON
// ---------------------------------------------------------------------------
void testDoneScalingAndJson() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();

    roi.feedTouch(0, 0, 100, 0);
    roi.feedTouch(160, 120, 100, 100);
    roi.feedTouch(319, 199, 100, 200);
    CHECK(roi.vertexCount() == 3);

    // ✓ DONE：x=50 落在第一个按钮（0..105）
    CHECK(roi.feedTouch(50, 220, 100, 300) == hw::RoiDrawEvent::DONE);
    CHECK(roi.isDone());
    CHECK(roi.state() == hw::RoiDrawState::DONE);
    CHECK(roi.vertexCount() == 3);  // 完成后顶点保留，可供读取 JSON

    const std::string json = roi.zoneJson();
    CHECK(!json.empty());
    CHECK(json.find("\"roi_zone\"") != std::string::npos);
    CHECK(json.find("\"polygon\"") != std::string::npos);
    CHECK(json.find("[40, 0]") != std::string::npos);
    CHECK(json.find("[360, 240]") != std::string::npos);
    CHECK(json.find("[678, 398]") != std::string::npos);

    // DONE 后触摸不再处理
    CHECK(roi.feedTouch(100, 100, 100, 400) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 3);
}

// ---------------------------------------------------------------------------
// 5b. ✓ 完成需要至少 3 个顶点
// ---------------------------------------------------------------------------
void testDoneRequiresThree() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();
    roi.feedTouch(100, 100, 100, 0);
    roi.feedTouch(200, 100, 100, 100);
    CHECK(roi.feedTouch(50, 220, 100, 200) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.state() == hw::RoiDrawState::DRAWING);
    CHECK(roi.vertexCount() == 2);
    CHECK(roi.zoneJson().empty());
}

// ---------------------------------------------------------------------------
// 6. 底部热区命中与边界
// ---------------------------------------------------------------------------
void testButtonHitBoundary() {
    hw::RoiDraw roi(nullptr);

    hw::RoiPoint p;
    p.x = 105; p.y = 220;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::DONE);
    p.x = 106;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::CLEAR);
    p.x = 211;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::CLEAR);
    p.x = 212;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::UNDO);
    p.x = 319; p.y = 239;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::UNDO);

    // y 边界：199 是绘图区，200 是按钮区
    p.x = 50; p.y = 199;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::NONE);
    p.y = 200;
    CHECK(roi.hitTestButton(p) == hw::RoiDrawButton::DONE);
}

// ---------------------------------------------------------------------------
// 7. 去抖 + 节流
// ---------------------------------------------------------------------------
void testDebounceAndThrottle() {
    hw::RoiDraw roi(nullptr);
    roi.begin();

    CHECK(roi.feedTouch(100, 100, 100, 0) == hw::RoiDrawEvent::VERTEX_ADDED);
    // 30ms < 50ms 防抖窗口：忽略
    CHECK(roi.feedTouch(110, 100, 100, 30) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 1);
    // 60ms 距上次有效按下 >= 50ms：通过
    CHECK(roi.feedTouch(120, 100, 100, 60) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 2);
    // 70ms 距上次采样 60ms 仅 10ms < 20ms 节流：忽略
    CHECK(roi.feedTouch(130, 100, 100, 70) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 2);
    // 120ms 距上次有效按下 60ms 已超过 50ms 防抖窗口：通过
    CHECK(roi.feedTouch(140, 100, 100, 120) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 3);
}

// ---------------------------------------------------------------------------
// 8. 压力阈值
// ---------------------------------------------------------------------------
void testPressureThreshold() {
    hw::RoiDraw roi(nullptr);
    roi.begin();
    CHECK(roi.feedTouch(100, 100, 49, 0) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 0);
    CHECK(roi.feedTouch(100, 100, 50, 20) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.vertexCount() == 1);
}

// ---------------------------------------------------------------------------
// 9. 越界顶点
// ---------------------------------------------------------------------------
void testOutOfBounds() {
    hw::RoiDraw roi(nullptr);
    roi.begin();
    CHECK(roi.feedTouch(-5, -5, 100, 0) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 0);
    CHECK(roi.feedTouch(9999, 9999, 100, 100) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 0);
    CHECK(roi.feedTouch(320, 100, 100, 200) == hw::RoiDrawEvent::IGNORED);
    CHECK(roi.vertexCount() == 0);

    hw::RoiPoint out;
    CHECK(!roi.calibrate(-1, 0, out));
    CHECK(roi.calibrate(0, 0, out));
    CHECK(out.x == 0 && out.y == 0);
    CHECK(roi.calibrate(319, 239, out));
}

// ---------------------------------------------------------------------------
// 10. 标定变换（swapXY -> invert -> offset）
// ---------------------------------------------------------------------------
void testCalibrationTransform() {
    hw::RoiDraw roi(nullptr);

    // 仅 X 镜像：x' = 320-1-x
    roi.setCalibration(0, 0, false, true, false);
    hw::RoiPoint out;
    CHECK(roi.calibrate(10, 20, out));
    CHECK(out.x == 309 && out.y == 20);

    // swapXY + offset：raw(10,20) -> swap(20,10) -> offset(25,7)
    roi.setCalibration(5, -3, true, false, false);
    CHECK(roi.calibrate(10, 20, out));
    CHECK(out.x == 25 && out.y == 7);

    // Y 镜像 + 平移：y' = 240-1-y + offset
    roi.setCalibration(0, 0, false, false, true);
    CHECK(roi.calibrate(100, 50, out));
    CHECK(out.x == 100 && out.y == 189);

    // 标定参与实际触摸：offsetX=10，点 (100,100) 应落到 (110,100)
    roi.setCalibration(10, 0, false, false, false);
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi2(&d);
    roi2.setCalibration(10, 0, false, false, false);
    roi2.begin();
    CHECK(roi2.feedTouch(100, 100, 100, 0) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi2.vertex(0).x == 110 && roi2.vertex(0).y == 100);
}

// ---------------------------------------------------------------------------
// 11. 输出映射可配置
// ---------------------------------------------------------------------------
void testOutputMapping() {
    hw::RoiDraw roi(nullptr);

    // 默认：320x240 -> 720x480 等比例 letterbox（scale=2.0，X 居中 40）
    hw::RoiPoint o = roi.mapToOutput(hw::RoiPoint{0, 0});
    CHECK(o.x == 40 && o.y == 0);
    o = roi.mapToOutput(hw::RoiPoint{160, 120});
    CHECK(o.x == 360 && o.y == 240);
    o = roi.mapToOutput(hw::RoiPoint{319, 239});
    CHECK(o.x == 678 && o.y == 478);

    // stretch：按 X/Y 各自拉伸（preserveAspect=false）
    hw::RoiOutputMapping m;
    m.srcX = 0; m.srcY = 0; m.srcW = 320; m.srcH = 240;
    m.outW = 720; m.outH = 480;
    m.preserveAspect = false;
    roi.setOutputMapping(m);
    o = roi.mapToOutput(hw::RoiPoint{100, 100});
    CHECK(o.x == 225 && o.y == 200);

    // 自定义源区域：绘图区 320x200 -> 720x480 等比例（scale=2.25，Y 居中 15）
    hw::RoiOutputMapping m2;
    m2.srcX = 0; m2.srcY = 0; m2.srcW = 320; m2.srcH = 200;
    m2.outW = 720; m2.outH = 480;
    m2.preserveAspect = true;
    roi.setOutputMapping(m2);
    o = roi.mapToOutput(hw::RoiPoint{160, 100});
    CHECK(o.x == 360 && o.y == 240);
    o = roi.mapToOutput(hw::RoiPoint{0, 0});
    CHECK(o.x == 0 && o.y == 15);
}

// ---------------------------------------------------------------------------
// 12. NULL 显示后端容错
// ---------------------------------------------------------------------------
void testNullDisplay() {
    hw::RoiDraw roi(nullptr);
    roi.begin();
    CHECK(roi.state() == hw::RoiDrawState::DRAWING);
    CHECK(roi.feedTouch(100, 100, 100, 0) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.feedTouch(200, 100, 100, 100) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.feedTouch(160, 160, 100, 200) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(roi.feedTouch(50, 220, 100, 300) == hw::RoiDrawEvent::DONE);
    CHECK(!roi.zoneJson().empty());
}

// ---------------------------------------------------------------------------
// 13. 文字渲染（按钮标签有白色像素）
// ---------------------------------------------------------------------------
void testTextRendering() {
    hw::RoiFramebufferDisplay d(320, 240);
    hw::RoiDraw roi(&d);
    roi.begin();

    // 每个按钮区域内都应有文字白色像素（✓ OK / ✕ CLR / ↩ UNDO）
    CHECK(countColor(d, 0, 200, 105, 239, hw::roiTextColor()) > 0);
    CHECK(countColor(d, 106, 200, 211, 239, hw::roiTextColor()) > 0);
    CHECK(countColor(d, 212, 200, 319, 239, hw::roiTextColor()) > 0);
}

// ---------------------------------------------------------------------------
// 14. Ili9341 窗口化填充适配器（真实驱动对象 + 假 SPI）
// ---------------------------------------------------------------------------
void testIli9341Adapter() {
    FakeSpi spi;
    FakeLcdPins pins;
    hw::Ili9341 lcd(&spi, &pins);
    CHECK(lcd.open());
    lcd.setRotation(0);
    hw::Ili9341RoiDisplay display(&lcd);
    hw::RoiDraw roi(&display);
    roi.begin();

    // 按钮矩形已通过 lcd->fillRect（窗口化填充）写 SPI；镜像进帧缓冲
    CHECK(lcd.pixelAt(50, 220) == hw::roiDoneColor());
    CHECK(spi.wroteByte(0x2A));  // CASET
    CHECK(spi.wroteByte(0x2C));  // RAMWR

    // 加顶点后，顶点/线段写入帧缓冲并 flush 上屏
    CHECK(roi.feedTouch(100, 100, 100, 0) == hw::RoiDrawEvent::VERTEX_ADDED);
    CHECK(lcd.pixelAt(100, 100) == hw::roiVertexColor());
    CHECK(lcd.pixelAt(0, 0) == hw::roiBgColor());
}

// ---------------------------------------------------------------------------
// 15. lcd_menu.h 集成：进入/退出 ROI_DRAW
// ---------------------------------------------------------------------------
void testLcdMenuIntegration() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();
    CHECK(menu.page() == hw::MenuPage::MAIN);

    menu.enterRoiDraw();
    CHECK(menu.page() == hw::MenuPage::ROI_DRAW);
    CHECK(fb.lastPage == hw::MenuPage::ROI_DRAW);

    // BACK 取消返回
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // OK（完成）返回
    menu.enterRoiDraw();
    CHECK(menu.page() == hw::MenuPage::ROI_DRAW);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // 页面名与未知页兜底
    CHECK(std::strcmp(hw::menuPageName(hw::MenuPage::ROI_DRAW), "ROI_DRAW") == 0);
    CHECK(std::strcmp(hw::menuPageName((hw::MenuPage)99), "UNKNOWN") == 0);

    // NULL 后端：进入/离开均安全忽略
    hw::LcdMenu nullMenu(nullptr);
    nullMenu.enterRoiDraw();
    CHECK(nullMenu.page() == hw::MenuPage::MAIN);
    nullMenu.leaveRoiDraw();
    CHECK(nullMenu.page() == hw::MenuPage::MAIN);
}

}  // namespace

int main() {
    testDefaultsAndSanitize();
    testAddVertexAndRender();
    testDeleteVertex();
    testClearAndUndo();
    testDoneScalingAndJson();
    testDoneRequiresThree();
    testButtonHitBoundary();
    testDebounceAndThrottle();
    testPressureThreshold();
    testOutOfBounds();
    testCalibrationTransform();
    testOutputMapping();
    testNullDisplay();
    testTextRendering();
    testIli9341Adapter();
    testLcdMenuIntegration();

    std::printf("\n结果: %d 通过, %d 失败\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
