# 触摸屏菜单（LVGL + ILI9341）方案调研与框架实现

> 状态：**框架已完成（可主机测试），LVGL 板端适配待真机阶段**
> 对应代码：`src/hardware/lcd_menu.h`（菜单状态机，前端无关）+ `tests/test_lcd_menu.cc`（99 项主机测试）
> 方案依据：`docs/hardware/DISPLAY_MENU_SYSTEM_PLAN.md`（2.8 寸 ILI9341 + LVGL 彩色菜单）

---

## 1. LVGL 移植调研结论（2026-10-27）

### 1.1 官方支持情况（结论：可行）

- **Luckfox 官方提供 LVGL 指南**（wiki.luckfox.com/Luckfox-Pico-Plus-Mini/Luckfox-Pico-LVGL）：
  使用 Linux **Framebuffer（FBDEV）** 作为显示后端，配置设备树后即可通过 `/dev/fb0` 显示。
- 官方示例使用 Waveshare Pico-LCD-1.3（ST7789 屏）演示，流程为：
  1. 设备树启用 SPI + LCD 面板节点（定义 RES/DC/CS/背光 GPIO），编译烧录；
  2. `cat /dev/urandom > /dev/fb0` 可验证 framebuffer 生效；
  3. LVGL 配置 `USE_FBDEV 1`、`FBDEV_PATH "/dev/fb0"`，交叉编译 demo 到板端运行。
- **ILI9341（320×240）与 ST7789 同属 SPI 并口 LCD 族**，驱动方式一致（设备树 panel 或 fbtft），
  官方 FB 路径可复用；ILI9341 在 Luckfox 社区已有 SPI 驱动示例（spotpear 教程）。

### 1.2 版本选型：LVGL v8.x（推荐）

| 版本 | 结论 | 理由 |
|---|---|---|
| LVGL v8.3.x | ✅ 推荐 | 资料最多、Luckfox 示例基于 v8；`lv_conf.h` 显式配置简单；XPT2046/触摸驱动适配多 |
| LVGL v9.x | ⚠️ 可选 | API 变化大（显示驱动回调签名、配置方式改动），社区 Luckfox 资料少，踩坑成本高 |

### 1.3 资源预算（RV1106 256MB 完全够用）

- ILI9341 320×240 RGB565 帧缓冲 = `320×240×2 = 150 KB`；
- LVGL v8 软件渲染最小 RAM 需求（双缓冲 draw buffer 1/10 屏 ≈ 30 KB）可轻松满足；
- RV1106 为单核 Cortex-A7 1.2GHz，SPI 屏刷新受 SPI 时钟限制（几 FPS），
  菜单/大字/缩略图场景可接受；摄像头实时画面用 160×120 缩略图 + RGB565 上传。

### 1.4 中文显示

- LVGL 默认字体不含中文。方案：用 LVGL 字体转换工具（lv_font_conv）将
  **用到的少量中文字符**（开始检测/设置/关于/确定/返回等）子集化嵌入，
  Demo 页可先用英文（SAFE/WARNING/DANGER）保证展示稳定，中文子集作为增强项。

### 1.5 触摸（XPT2046）

- ILI9341 模块板载 XPT2046 电阻触摸，SPI 读取；
- LVGL v8 提供 `lv_indev` 注册方式，XPT2046 有现成参考驱动，适配工作量小。

---

## 2. 菜单框架架构（已完成）

### 2.1 设计原则

菜单逻辑与 LVGL 完全解耦，遵循项目既有硬件驱动模式（gpio/servo/selftest 同风格）：

```
┌─────────────────────────────┐        ┌──────────────────────────────┐
│  LcdMenu（状态机）           │        │  MenuBackend（抽象接口）       │
│  · 页面导航 MAIN/DETECTION/  │ ─────► │  · begin() 初始化屏幕          │
│    SETTINGS/ABOUT           │        │  · drawPage() 整页重绘        │
│  · 四键 UP/DOWN/OK/BACK     │        │  · setParam() 参数控件         │
│  · 风险等级颜色映射          │        │  · setRiskLevel() 大字+颜色    │
│  · 参数刷新节流 500ms        │        │  · setMenuCursor() 高亮       │
│  · 等级跳变立即推送          │        │                              │
└─────────────────────────────┘        └──────────────────────────────┘
        ▲                                        ▲
        │ 不依赖 LVGL（可主机测试）               │
┌───────┴─────────────┐        ┌───────────────┴──────────────┐
│ FakeMenuBackend（测试）│        │ LvglMenuBackend（板端，真机阶段）│
│ 内存记录调用，99 项用例│        │ LVGL label/image/color 映射   │
└───────────────────────┘        └──────────────────────────────┘
```

### 2.2 页面结构与导航

```
MAIN（主菜单：开始检测 / 设置 / 关于 + 状态栏）
  ├─ OK(开始检测) ──► DETECTION（检测画面：风险大字+人数+FPS+参数）
  │                    └─ OK / BACK ──► MAIN
  ├─ OK(设置)     ──► SETTINGS（预留：阈值/云台使能）
  │                    └─ BACK ──► MAIN
  └─ OK(关于)     ──► ABOUT（版本/项目信息）
                        └─ BACK ──► MAIN
```

- 主菜单 UP/DOWN **循环移动**高亮（0→1→2→0），OK 进入对应页面；
- 返回主菜单后高亮保持上次位置（演示时可快速回到检测页）；
- BACK 在主菜单无操作。

### 2.3 风险等级颜色映射（与三色 LED 一致）

| RiskLevel | 文本 | 颜色 |
|---|---|---|
| 0 (SAFE) | SAFE | 绿 GREEN |
| 1 (WARNING) | WARNING | 黄 YELLOW |
| 2 (DANGER) | DANGER | 红 RED |

### 2.4 参数刷新策略

- `updateStats(fps, ip, cpu, mem)`：**只缓存**，`tick(nowMs)` 每 **500ms** 节流推送一次；
- `updateRisk(level, personCount)`：**等级跳变立即推送**（不等节流），
  并重置节流计时（`lastPushMs=-1, dirty=true`），下一 tick 全量刷新，保证 DANGER 秒级上屏；
- 页面切换时立即全量推送当前缓存参数；
- 所有接口对 `NULL` 后端安全（板端未初始化 LVGL 前主循环可无脑调用）。

### 2.5 测试覆盖（99 项）

| 分组 | 覆盖点 | 用例数 |
|---|---|---|
| 工具函数 | 等级文本/颜色映射、页面/参数/主菜单项名称、越界兜底 | 19 |
| begin | 初始化/主菜单渲染/光标/初始参数推送 | 14 |
| 主菜单导航 | UP/DOWN 循环、OK 进入 DETECTION/SETTINGS/ABOUT、BACK 无操作 | 16 |
| 子页返回 | DETECTION OK/BACK 返回、SETTINGS/ABOUT 行为 | 13 |
| updateRisk | 等级跳变立即推送/颜色正确、人数单独推送、无变化不推送、降级回绿 | 14 |
| 统计节流 | tick(0) 立即推送、500ms 内不推、满 500ms 推送、参数格式化、页面切换全量刷新 | 12 |
| 节流旁路 | 等级跳变后 tick 立即全量刷新 | 6 |
| NULL 容错 | 全操作不崩溃、状态查询正常 | 5 |

编译运行：`cd src/luckfox_rtsp_yolov5 && make test-lcd-menu`（或 `make test` 全量）。

---

## 3. 板端 LVGL 适配路线图（待真机）

> 需采购 2.8 寸 ILI9341 触摸屏 + XPT2046（见《答辩硬件展示方案 v2》BOM）后实施。

1. **点亮屏幕**：优先采用 `docs/hardware/lvgl_board_port.md` 方案 B（spidev 用户态驱动，
   仅改 dts `spi@ff500000 status="okay"`，免重编内核）；ILI9341 显示驱动主机侧已实现
   （`src/hardware/ili9341.h`，123 项测试），真机补 `SysfsSpiBus`/`SysfsLcdPins` 适配即可点亮；
   备选官方路径：设备树启用 ILI9341 panel 验证 `/dev/fb0`（`cat /dev/zero > /dev/fb0` 清屏）；
2. **移植 LVGL v8**：交叉编译 LVGL 库，配置 `USE_FBDEV 1`，跑通官方 demo；
3. **实现 LvglMenuBackend**：继承 `hw::MenuBackend`，将
   `setParam/setRiskLevel/setMenuCursor/drawPage` 映射到 LVGL label/image 控件；
4. **接入主循环**：`menu.updateRisk(er.level, er.personCount)` +
   `menu.updateStats(fps, ip, cpu, mem)` + `menu.tick(nowMs)`，按键 GPIO 回调 `menu.onKey(...)`；
5. **中文字体子集**：用 lv_font_conv 子集化菜单用字；
6. **摄像头缩略图**：检测帧 BGR 缩放 160×120 → RGB565 → LVGL image 控件（SPI 带宽内几 FPS）；
7. **自检集成**：selftest 的 `SPI_LCD_ID` 项在 LVGL 点亮后改用真实 SPI 读 ID 校验 0x9341。

## 4. 相关文档

- 方案总览：`docs/hardware/DISPLAY_MENU_SYSTEM_PLAN.md`
- 硬件自检：`docs/hardware/selftest.md`
- 硬件构建指南：`docs/hardware/HARDWARE_BUILD_GUIDE.md`
- 官方 LVGL 指南：https://wiki.luckfox.com/Luckfox-Pico-Plus-Mini/Luckfox-Pico-LVGL