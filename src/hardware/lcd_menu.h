// =============================================================================
// lcd_menu.h — 触摸屏菜单状态机框架（LVGL 前端无关逻辑）
// -----------------------------------------------------------------------------
// 用途：2.8 寸 ILI9341 触摸屏动画菜单（答辩硬件展示核心）的前端无关逻辑——
//       页面导航、四键处理（上/下/确定/返回）、风险等级颜色映射、参数刷新
//       节流。LVGL/ILI9341 驱动通过 MenuBackend 抽象接入；本框架不依赖
//       LVGL 头文件，可在 PC/CI 上全量测试（FakeMenuBackend）。
//
// 页面结构：
//   MAIN（主菜单：开始检测 / 设置 / 关于 + 状态栏）
//     ├─ OK(开始检测) -> DETECTION（检测画面：风险大字 + 人数 + FPS + 参数）
//     │      └─ OK / BACK -> MAIN
//     ├─ OK(设置)     -> SETTINGS（预留：阈值 / 云台使能）
//     └─ OK(关于)       -> ABOUT（版本 / 项目信息）
//   任何子页 BACK -> MAIN
//
// 风险等级颜色映射（与 led.h 三色灯一致）：
//   SAFE(0)    -> 绿  MenuColor::GREEN
//   WARNING(1) -> 黄  MenuColor::YELLOW
//   DANGER(2)  -> 红  MenuColor::RED
//
// 刷新策略：
//   - updateStats() 只缓存参数，tick(nowMs) 每 500ms 节流推送一次到后端；
//   - updateRisk() 检测到等级跳变立即推送（不等节流），保证 DANGER 秒级上屏。
//
// 集成示例（板端 LVGL 适配，真机阶段）：
//   class LvglBackend : public hw::MenuBackend { ... lv_label_set_text(...) ... };
//   hw::LcdMenu menu(&lvglBackend);
//   menu.begin();
//   主循环每帧： menu.updateRisk(er.level, er.personCount);
//               menu.updateStats(fps, ip, cpu, mem);
//               menu.tick(nowMs);
//               按键回调：menu.onKey(hw::MenuKey::UP);
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace hw {

// -----------------------------------------------------------------------------
// 菜单页面
// -----------------------------------------------------------------------------
enum class MenuPage : uint8_t {
    MAIN = 0,        // 主菜单
    DETECTION,       // 检测画面
    SETTINGS,        // 设置（预留）
    ABOUT,           // 关于
    COUNT            // 总页数（保持最后）
};

inline const char* menuPageName(MenuPage page) {
    switch (page) {
        case MenuPage::MAIN:      return "MAIN";
        case MenuPage::DETECTION: return "DETECTION";
        case MenuPage::SETTINGS:   return "SETTINGS";
        case MenuPage::ABOUT:      return "ABOUT";
        default:                  return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// 按键定义（对应扩展底板 4 个轻触按键）
// -----------------------------------------------------------------------------
enum class MenuKey : uint8_t {
    UP = 0,          // 上
    DOWN,            // 下
    OK,             // 确定
    BACK,           // 返回
    COUNT
};

// -----------------------------------------------------------------------------
// 前端可渲染颜色（具体 RGB565 值由 LVGL 适配层映射）
// -----------------------------------------------------------------------------
enum class MenuColor : uint8_t {
    BLACK = 0,
    WHITE,
    RED,
    YELLOW,
    GREEN,
    BLUE,
    GRAY,
    COUNT
};

// -----------------------------------------------------------------------------
// 参数槽位（后端按 id 更新对应控件文本）
// -----------------------------------------------------------------------------
enum class MenuParam : uint8_t {
    IP = 0,           // IP 地址
    CPU_PERCENT,     // CPU 占用率（%）
    MEM_PERCENT,     // 内存占用率（%）
    FPS,             // 帧率
    PERSON_COUNT,    // 检测人数
    RISK_TEXT,       // 风险等级文本（SAFE/WARNING/DANGER）
    COUNT
};

inline const char* menuParamName(MenuParam p) {
    switch (p) {
        case MenuParam::IP:           return "IP";
        case MenuParam::CPU_PERCENT:  return "CPU";
        case MenuParam::MEM_PERCENT: return "MEM";
        case MenuParam::FPS:         return "FPS";
        case MenuParam::PERSON_COUNT: return "PERSON_COUNT";
        case MenuParam::RISK_TEXT:    return "RISK_TEXT";
        default:                     return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// 主菜单条目
// -----------------------------------------------------------------------------
enum class MainItem : uint8_t {
    START_DETECTION = 0,   // 开始检测
    SETTINGS,             // 设置
    ABOUT,                // 关于
    COUNT
};

inline const char* mainItemName(MainItem item) {
    switch (item) {
        case MainItem::START_DETECTION: return "START_DETECTION";
        case MainItem::SETTINGS:         return "SETTINGS";
        case MainItem::ABOUT:            return "ABOUT";
        default:                        return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// 风险等级 -> 文本 / 颜色（与 risk_fsm.h 的 RiskLevel 对齐：0/1/2）
// -----------------------------------------------------------------------------
inline const char* riskLevelText(int level) {
    if (level >= 2) return "DANGER";
    if (level == 1) return "WARNING";
    return "SAFE";
}

inline MenuColor riskColor(int level) {
    if (level >= 2) return MenuColor::RED;
    if (level == 1) return MenuColor::YELLOW;
    return MenuColor::GREEN;
}

// -----------------------------------------------------------------------------
// MenuBackend — 菜单渲染后端抽象
//   板端实现：LvglMenuBackend（LVGL + ILI9341，真机阶段编写）
//   测试实现：FakeMenuBackend（内存记录调用）
// -----------------------------------------------------------------------------
class MenuBackend {
public:
    virtual ~MenuBackend() {}

    // 初始化屏幕/字体/控件
    virtual void begin() = 0;

    // 整页重绘（页面切换时调用一次）
    virtual void drawPage(MenuPage page) = 0;

    // 更新参数控件文本
    virtual void setParam(MenuParam id, const char* value) = 0;

    // 更新风险等级大字与颜色
    virtual void setRiskLevel(int level, MenuColor color) = 0;

    // 主菜单高亮项（START_DETECTION/SETTINGS/ABOUT）
    virtual void setMenuCursor(int selected) = 0;
};

// -----------------------------------------------------------------------------
// LcdMenu — 菜单状态机
//   全部逻辑与时间/硬件无关；tick 由外部主循环驱动（传入毫秒时间戳）。
//   构造注入 MenuBackend*，允许为 NULL（所有操作安全忽略）。
// -----------------------------------------------------------------------------
class LcdMenu {
public:
    static const int kRefreshIntervalMs = 500;  // 参数节流刷新周期

    explicit LcdMenu(MenuBackend* backend)
        : m_backend(backend),
          m_page(MenuPage::MAIN),
          m_selected((int)MainItem::START_DETECTION),
          m_riskLevel(0),
          m_personCount(0),
          m_lastPersonCount(0),
          m_fps(0.0f),
          m_cpu(0.0f),
          m_mem(0.0f),
          m_dirty(false),
          m_lastPushMs(-1) {
        m_ip[0] = '\0';
    }

    // 初始化：调用后端 begin() 并渲染初始主菜单
    void begin() {
        if (!m_backend) return;
        m_backend->begin();
        renderCurrentPage();
    }

    // 按键处理（导航状态机）
    void onKey(MenuKey key) {
        if (!m_backend) return;
        switch (m_page) {
            case MenuPage::MAIN:
                onMainKey(key);
                break;
            case MenuPage::DETECTION:
                // 检测画面：确定/返回都回到主菜单
                if (key == MenuKey::OK || key == MenuKey::BACK) {
                    switchPage(MenuPage::MAIN);
                }
                break;
            case MenuPage::SETTINGS:
            case MenuPage::ABOUT:
                if (key == MenuKey::BACK) {
                    switchPage(MenuPage::MAIN);
                }
                break;
            default:
                break;
        }
    }

    // 风险等级/人数更新：等级跳变立即推送（不等节流）
    void updateRisk(int level, int personCount) {
        m_personCount = personCount;
        const bool riskChanged = (level != m_riskLevel);
        m_riskLevel = level;
        if (!m_backend) return;
        if (riskChanged) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", personCount);
            m_backend->setParam(MenuParam::PERSON_COUNT, buf);
            m_backend->setParam(MenuParam::RISK_TEXT, riskLevelText(level));
            m_backend->setRiskLevel(level, riskColor(level));
            m_lastPushMs = -1;  // 重置节流计时，下次 tick 全量刷新
            m_dirty = true;
        } else if (personCount != m_lastPersonCount) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", personCount);
            m_backend->setParam(MenuParam::PERSON_COUNT, buf);
            m_dirty = true;
        }
        m_lastPersonCount = personCount;
    }

    // 统计参数更新：仅缓存，tick 节流推送
    void updateStats(float fps, const char* ip, float cpu, float mem) {
        m_fps = fps;
        m_cpu = cpu;
        m_mem = mem;
        if (ip) {
            std::snprintf(m_ip, sizeof(m_ip), "%s", ip);
        }
        m_dirty = true;
    }

    // 周期驱动：nowMs 毫秒时间戳；距上次推送 >= kRefreshIntervalMs 时刷新
    void tick(int64_t nowMs) {
        if (!m_backend || !m_dirty) return;
        if (m_lastPushMs >= 0 && (nowMs - m_lastPushMs) < kRefreshIntervalMs) {
            return;
        }
        pushParams();
        m_lastPushMs = nowMs;
        m_dirty = false;
    }

    // ---- 状态查询（测试/调试用） ----
    MenuPage page() const { return m_page; }
    int      selected() const { return m_selected; }
    int      riskLevel() const { return m_riskLevel; }
    int      personCount() const { return m_personCount; }
    bool     dirty() const { return m_dirty; }

private:
    void onMainKey(MenuKey key) {
        const int itemCount = (int)MainItem::COUNT;
        if (key == MenuKey::UP) {
            m_selected = (m_selected + itemCount - 1) % itemCount;
            m_backend->setMenuCursor(m_selected);
        } else if (key == MenuKey::DOWN) {
            m_selected = (m_selected + 1) % itemCount;
            m_backend->setMenuCursor(m_selected);
        } else if (key == MenuKey::OK) {
            switch (static_cast<MainItem>(m_selected)) {
                case MainItem::START_DETECTION:
                    switchPage(MenuPage::DETECTION);
                    break;
                case MainItem::SETTINGS:
                    switchPage(MenuPage::SETTINGS);
                    break;
                case MainItem::ABOUT:
                    switchPage(MenuPage::ABOUT);
                    break;
                default:
                    break;
            }
        }
        // BACK 在主菜单无操作
    }

    void switchPage(MenuPage page) {
        m_page = page;
        renderCurrentPage();
    }

    void renderCurrentPage() {
        if (!m_backend) return;
        m_backend->drawPage(m_page);
        if (m_page == MenuPage::MAIN) {
            m_backend->setMenuCursor(m_selected);
        }
        // 页面切换后立即推送当前缓存参数（风险等级大字 + 参数栏）
        pushParams();
        m_lastPushMs = -1;
        m_dirty = false;
    }

    void pushParams() {
        if (!m_backend) return;
        char buf[64];

        std::snprintf(buf, sizeof(buf), "%s", m_ip[0] ? m_ip : "N/A");
        m_backend->setParam(MenuParam::IP, buf);

        std::snprintf(buf, sizeof(buf), "%.0f%%", m_cpu);
        m_backend->setParam(MenuParam::CPU_PERCENT, buf);

        std::snprintf(buf, sizeof(buf), "%.0f%%", m_mem);
        m_backend->setParam(MenuParam::MEM_PERCENT, buf);

        std::snprintf(buf, sizeof(buf), "%.1f", m_fps);
        m_backend->setParam(MenuParam::FPS, buf);

        std::snprintf(buf, sizeof(buf), "%d", m_personCount);
        m_backend->setParam(MenuParam::PERSON_COUNT, buf);

        m_backend->setParam(MenuParam::RISK_TEXT, riskLevelText(m_riskLevel));
        m_backend->setRiskLevel(m_riskLevel, riskColor(m_riskLevel));
    }

    MenuBackend* m_backend;
    MenuPage     m_page;
    int          m_selected;
    int          m_riskLevel;
    int          m_personCount;
    int          m_lastPersonCount;
    float        m_fps;
    float        m_cpu;
    float        m_mem;
    char         m_ip[64];
    bool         m_dirty;
    int64_t      m_lastPushMs;
};

}  // namespace hw