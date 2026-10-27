// =============================================================================
// test_lcd_menu.cc — 触摸屏菜单状态机框架主机单元测试
// -----------------------------------------------------------------------------
// 验证点（全部使用 FakeMenuBackend 内存替身，无需 LVGL/ILI9341 硬件）：
//   1. 工具函数：风险等级文本/颜色映射、页面/参数/主菜单项名称；
//   2. begin()：初始化后端 + 渲染主菜单 + 初始参数推送；
//   3. 主菜单导航：UP/DOWN 循环移动高亮、OK 进入对应页面；
//   4. 页面切换：DETECTION/SETTINGS/ABOUT 的 OK/BACK 返回主菜单；
//   5. updateRisk：等级跳变立即推送（文本/人数/颜色），人数变化单独推送，
//      无变化不推送；
//   6. updateStats + tick 节流：500ms 内不推送，到期推送，参数格式化正确；
//   7. 等级跳变解除节流：dirty + lastPushMs=-1 后 tick 立即全量刷新；
//   8. NULL 后端容错：所有操作安全忽略不崩溃。
//
// 编译（无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I../../hardware test_lcd_menu.cc -o test_lcd_menu
// =============================================================================

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

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

// 测试后端：内存记录所有调用
class FakeMenuBackend : public hw::MenuBackend {
public:
    int  beginCount = 0;
    int  drawCount  = 0;
    int  riskUpdates = 0;
    int  cursorUpdates = 0;
    hw::MenuPage lastPage = hw::MenuPage::COUNT;
    std::map<hw::MenuParam, std::string> params;
    int    lastRiskLevel = -1;
    hw::MenuColor lastColor = hw::MenuColor::COUNT;
    int    lastCursor = -1;

    void begin() override { ++beginCount; }
    void drawPage(hw::MenuPage page) override {
        ++drawCount;
        lastPage = page;
    }
    void setParam(hw::MenuParam id, const char* value) override {
        params[id] = value ? value : "";
    }
    void setRiskLevel(int level, hw::MenuColor color) override {
        ++riskUpdates;
        lastRiskLevel = level;
        lastColor = color;
    }
    void setMenuCursor(int selected) override {
        ++cursorUpdates;
        lastCursor = selected;
    }

    // 便捷断言辅助
    bool paramIs(hw::MenuParam id, const char* expect) const {
        std::map<hw::MenuParam, std::string>::const_iterator it = params.find(id);
        return it != params.end() && it->second == expect;
    }
};

// ---------------------------------------------------------------------------
// 1. 工具函数
// ---------------------------------------------------------------------------
void testHelpers() {
    CHECK(std::strcmp(hw::riskLevelText(0), "SAFE") == 0);
    CHECK(std::strcmp(hw::riskLevelText(1), "WARNING") == 0);
    CHECK(std::strcmp(hw::riskLevelText(2), "DANGER") == 0);
    CHECK(std::strcmp(hw::riskLevelText(9), "DANGER") == 0);   // 越界兜底
    CHECK(std::strcmp(hw::riskLevelText(-1), "SAFE") == 0);

    CHECK(hw::riskColor(0) == hw::MenuColor::GREEN);
    CHECK(hw::riskColor(1) == hw::MenuColor::YELLOW);
    CHECK(hw::riskColor(2) == hw::MenuColor::RED);
    CHECK(hw::riskColor(5) == hw::MenuColor::RED);

    CHECK(std::strcmp(hw::menuPageName(hw::MenuPage::MAIN), "MAIN") == 0);
    CHECK(std::strcmp(hw::menuPageName(hw::MenuPage::DETECTION), "DETECTION") == 0);
    CHECK(std::strcmp(hw::menuPageName(hw::MenuPage::SETTINGS), "SETTINGS") == 0);
    CHECK(std::strcmp(hw::menuPageName(hw::MenuPage::ABOUT), "ABOUT") == 0);
    CHECK(std::strcmp(hw::menuPageName((hw::MenuPage)99), "UNKNOWN") == 0);

    CHECK(std::strcmp(hw::menuParamName(hw::MenuParam::IP), "IP") == 0);
    CHECK(std::strcmp(hw::menuParamName(hw::MenuParam::RISK_TEXT), "RISK_TEXT") == 0);
    CHECK(std::strcmp(hw::menuParamName((hw::MenuParam)99), "UNKNOWN") == 0);

    CHECK(std::strcmp(hw::mainItemName(hw::MainItem::START_DETECTION), "START_DETECTION") == 0);
    CHECK(std::strcmp(hw::mainItemName(hw::MainItem::SETTINGS), "SETTINGS") == 0);
    CHECK(std::strcmp(hw::mainItemName(hw::MainItem::ABOUT), "ABOUT") == 0);
    CHECK(std::strcmp(hw::mainItemName((hw::MainItem)99), "UNKNOWN") == 0);
}

// ---------------------------------------------------------------------------
// 2. begin()：初始化 + 渲染主菜单 + 初始参数推送
// ---------------------------------------------------------------------------
void testBegin() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    CHECK(fb.beginCount == 1);
    CHECK(fb.drawCount == 1);
    CHECK(fb.lastPage == hw::MenuPage::MAIN);
    CHECK(fb.cursorUpdates == 1);
    CHECK(fb.lastCursor == (int)hw::MainItem::START_DETECTION);

    // 初始参数推送
    CHECK(fb.paramIs(hw::MenuParam::IP, "N/A"));
    CHECK(fb.paramIs(hw::MenuParam::CPU_PERCENT, "0%"));
    CHECK(fb.paramIs(hw::MenuParam::MEM_PERCENT, "0%"));
    CHECK(fb.paramIs(hw::MenuParam::FPS, "0.0"));
    CHECK(fb.paramIs(hw::MenuParam::PERSON_COUNT, "0"));
    CHECK(fb.paramIs(hw::MenuParam::RISK_TEXT, "SAFE"));
    CHECK(fb.riskUpdates == 1);
    CHECK(fb.lastRiskLevel == 0);
    CHECK(fb.lastColor == hw::MenuColor::GREEN);

    CHECK(menu.page() == hw::MenuPage::MAIN);
    CHECK(menu.selected() == (int)hw::MainItem::START_DETECTION);
}

// ---------------------------------------------------------------------------
// 3. 主菜单导航：UP/DOWN 循环 + OK 进入对应页面
// ---------------------------------------------------------------------------
void testMainNavigation() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    // DOWN: 0 -> 1 -> 2 -> 0（循环）
    menu.onKey(hw::MenuKey::DOWN);
    CHECK(menu.selected() == (int)hw::MainItem::SETTINGS);
    CHECK(fb.lastCursor == (int)hw::MainItem::SETTINGS);
    menu.onKey(hw::MenuKey::DOWN);
    CHECK(menu.selected() == (int)hw::MainItem::ABOUT);
    CHECK(fb.lastCursor == (int)hw::MainItem::ABOUT);
    menu.onKey(hw::MenuKey::DOWN);
    CHECK(menu.selected() == (int)hw::MainItem::START_DETECTION);
    CHECK(fb.lastCursor == (int)hw::MainItem::START_DETECTION);

    // UP: 0 -> 2 -> 1 -> 0
    menu.onKey(hw::MenuKey::UP);
    CHECK(menu.selected() == (int)hw::MainItem::ABOUT);
    menu.onKey(hw::MenuKey::UP);
    CHECK(menu.selected() == (int)hw::MainItem::SETTINGS);
    menu.onKey(hw::MenuKey::UP);
    CHECK(menu.selected() == (int)hw::MainItem::START_DETECTION);

    // BACK 在主菜单无操作
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // OK(START_DETECTION) -> DETECTION
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::DETECTION);
    CHECK(fb.lastPage == hw::MenuPage::DETECTION);

    // 返回主菜单，OK(SETTINGS) -> SETTINGS
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);
    menu.onKey(hw::MenuKey::DOWN);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::SETTINGS);
    CHECK(fb.lastPage == hw::MenuPage::SETTINGS);

    // 返回主菜单，OK(ABOUT) -> ABOUT（BACK 后 selected 保持 SETTINGS=1）
    menu.onKey(hw::MenuKey::BACK);
    menu.onKey(hw::MenuKey::DOWN);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::ABOUT);
    CHECK(fb.lastPage == hw::MenuPage::ABOUT);
}

// ---------------------------------------------------------------------------
// 4. 子页面返回逻辑
// ---------------------------------------------------------------------------
void testSubPageBack() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    // 进入 DETECTION，OK 返回 MAIN
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::DETECTION);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // 进入 DETECTION，BACK 返回 MAIN
    menu.onKey(hw::MenuKey::OK);
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // 进入 SETTINGS，BACK 返回 MAIN；OK 不响应
    menu.onKey(hw::MenuKey::DOWN);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::SETTINGS);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::SETTINGS);  // OK 无效
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);

    // 进入 ABOUT，BACK 返回 MAIN（BACK 后 selected 保持 SETTINGS=1）
    menu.onKey(hw::MenuKey::BACK);
    menu.onKey(hw::MenuKey::DOWN);
    menu.onKey(hw::MenuKey::OK);
    CHECK(menu.page() == hw::MenuPage::ABOUT);
    menu.onKey(hw::MenuKey::BACK);
    CHECK(menu.page() == hw::MenuPage::MAIN);
}

// ---------------------------------------------------------------------------
// 5. updateRisk：等级跳变立即推送
// ---------------------------------------------------------------------------
void testRiskUpdate() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    // 等级跳变：SAFE(0) -> WARNING(1)
    menu.updateRisk(1, 3);
    CHECK(fb.riskUpdates == 2);                    // begin 1 次 + 跳变 1 次
    CHECK(fb.lastRiskLevel == 1);
    CHECK(fb.lastColor == hw::MenuColor::YELLOW);
    CHECK(fb.paramIs(hw::MenuParam::RISK_TEXT, "WARNING"));
    CHECK(fb.paramIs(hw::MenuParam::PERSON_COUNT, "3"));

    // 等级跳变：WARNING -> DANGER
    menu.updateRisk(2, 5);
    CHECK(fb.riskUpdates == 3);
    CHECK(fb.lastRiskLevel == 2);
    CHECK(fb.lastColor == hw::MenuColor::RED);
    CHECK(fb.paramIs(hw::MenuParam::RISK_TEXT, "DANGER"));
    CHECK(fb.paramIs(hw::MenuParam::PERSON_COUNT, "5"));

    // 等级不变、人数变化：只推人数，不推等级
    const int riskUpdatesBefore = fb.riskUpdates;
    menu.updateRisk(2, 8);
    CHECK(fb.riskUpdates == riskUpdatesBefore);    // 无等级推送
    CHECK(fb.paramIs(hw::MenuParam::PERSON_COUNT, "8"));

    // 等级不变、人数不变：无任何推送
    const int riskUpdatesBefore2 = fb.riskUpdates;
    const std::string personBefore = fb.params[hw::MenuParam::PERSON_COUNT];
    menu.updateRisk(2, 8);
    CHECK(fb.riskUpdates == riskUpdatesBefore2);
    CHECK(fb.paramIs(hw::MenuParam::PERSON_COUNT, personBefore.c_str()));

    // 降级：DANGER -> SAFE（颜色回绿）
    menu.updateRisk(0, 0);
    CHECK(fb.lastRiskLevel == 0);
    CHECK(fb.lastColor == hw::MenuColor::GREEN);
    CHECK(fb.paramIs(hw::MenuParam::RISK_TEXT, "SAFE"));
}

// ---------------------------------------------------------------------------
// 6. updateStats + tick 节流
// ---------------------------------------------------------------------------
void testStatsThrottle() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    // 更新统计参数，tick(0) 立即推送（上次推送时间为 -1）
    menu.updateStats(7.46f, "192.168.0.136", 33.0f, 12.6f);
    CHECK(menu.dirty());
    menu.tick(0);
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.5"));
    CHECK(fb.paramIs(hw::MenuParam::IP, "192.168.0.136"));
    CHECK(fb.paramIs(hw::MenuParam::CPU_PERCENT, "33%"));
    CHECK(fb.paramIs(hw::MenuParam::MEM_PERCENT, "13%"));

    // 500ms 内不推送
    menu.updateStats(7.40f, "192.168.0.136", 34.0f, 13.0f);
    menu.tick(300);
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.5"));     // 仍是旧值

    // 满 500ms 推送
    menu.tick(500);
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.4"));
    CHECK(fb.paramIs(hw::MenuParam::CPU_PERCENT, "34%"));

    // dirty=false 时 tick 不推送
    const int riskBefore = fb.riskUpdates;
    menu.tick(900);
    CHECK(fb.riskUpdates == riskBefore);

    // 页面切换触发全量推送（lastPushMs 重置）
    menu.onKey(hw::MenuKey::OK);   // 进入 DETECTION
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.4"));
    CHECK(menu.page() == hw::MenuPage::DETECTION);
}

// ---------------------------------------------------------------------------
// 7. 等级跳变解除节流：tick 立即全量刷新
// ---------------------------------------------------------------------------
void testRiskBypassThrottle() {
    FakeMenuBackend fb;
    hw::LcdMenu menu(&fb);
    menu.begin();

    // 先推送一次，建立节流时间基准
    menu.updateStats(7.0f, "10.0.0.1", 20.0f, 10.0f);
    menu.tick(1000);
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.0"));

    // 100ms 后等级跳变 -> 立即推送等级，且 dirty=true + lastPushMs=-1
    menu.updateRisk(2, 9);
    CHECK(fb.paramIs(hw::MenuParam::RISK_TEXT, "DANGER"));
    CHECK(menu.dirty());

    // 下一 tick（即使距上次不到 500ms）立即全量刷新
    menu.updateStats(7.2f, "10.0.0.1", 21.0f, 11.0f);
    menu.tick(1100);
    CHECK(fb.paramIs(hw::MenuParam::FPS, "7.2"));
    CHECK(fb.paramIs(hw::MenuParam::CPU_PERCENT, "21%"));
    CHECK(menu.dirty() == false);
}

// ---------------------------------------------------------------------------
// 8. NULL 后端容错
// ---------------------------------------------------------------------------
void testNullBackend() {
    hw::LcdMenu menu(NULL);
    menu.begin();
    menu.onKey(hw::MenuKey::UP);
    menu.onKey(hw::MenuKey::DOWN);
    menu.onKey(hw::MenuKey::OK);
    menu.onKey(hw::MenuKey::BACK);
    menu.updateRisk(2, 10);
    menu.updateStats(7.5f, "192.168.0.136", 30.0f, 10.0f);
    menu.tick(0);
    menu.tick(1000);
    CHECK(menu.page() == hw::MenuPage::MAIN);
    CHECK(menu.riskLevel() == 2);
    CHECK(menu.personCount() == 10);
}

}  // namespace

int main() {
    testHelpers();
    testBegin();
    testMainNavigation();
    testSubPageBack();
    testRiskUpdate();
    testStatsThrottle();
    testRiskBypassThrottle();
    testNullBackend();

    std::printf("\n===== lcd_menu 测试结果: %d passed, %d failed =====\n",
                g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}