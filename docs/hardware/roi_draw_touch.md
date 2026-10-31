# 触摸屏手动画 ROI 多边形（ROI_DRAW 页面）

> 状态：**代码 + 主机测试已完成（可 make test），未接屏真机验证**
> 对应代码：`src/hardware/roi_draw.h`（状态机，前端无关）+ `src/hardware/lcd_menu.h`（ROI_DRAW 页面迁移）
> 主机测试：`src/luckfox_rtsp_yolov5/tests/test_roi_draw.cc`（131 项，全绿）
> 显示驱动复用：`src/hardware/ili9341.h`（320x240 RGB565，MADCTL=0x28 横向）
> 触摸驱动复用：`src/hardware/xpt2046.h`（SPI 12 位 ADC，GPIO 手工片选）

---

## 1. 状态机图

### 1.1 RoiDraw（触摸画多边形，roi_draw.h）

```text
                    begin()
  +-----------+  ----------->  +-----------+
  |   IDLE    |                |  DRAWING  |<-----------------------------+
  +-----------+                +-----------+                              |
        |                            |                                    |
        | cancel()               tap 空白 -> VERTEX_ADDED                  |
        v                            |  tap 顶点附近 -> VERTEX_REMOVED     |
  +-----------+                      |  ✕ CLEAR -> 清空全部顶点             |
  | CANCELLED |                      |  ↩ UNDO  -> 弹出最后一个顶点         |
  +-----------+                      |  ✓ DONE(顶点>=3)                   |
                                     v                                    |
                                +-----------+                            |
                                |   DONE    | -- 重新 begin() -----------+
                                +-----------+
                                     |
                                     v
                          zoneJson() 输出 roi_zone 片段
                          （顶点保留，可读；之后 begin() 复位）
```

- `IDLE`：构造后未开始。
- `DRAWING`：只处理 feedTouch()；`✓` 且顶点数 >= 3 -> `DONE`；否则忽略。
- `CANCELLED`：`cancel()`（BACK 取消）进入，清空顶点并清屏。
- `DONE`：不再处理触摸；`begin()` 复位回到 `DRAWING` 重新画。

### 1.2 LcdMenu 页面迁移（lcd_menu.h）

```text
  MAIN
   |  enterRoiDraw()                 （菜单入口：LVGL 触摸热区/外部调用）
   v
  ROI_DRAW  --------------------------+
   |                                   |
   | onKey(OK)   （✓ 完成后 main.cc 调用 leaveRoiDraw()）
   | onKey(BACK) （取消：main.cc 同时 roi.cancel()）
   v
  MAIN
```

- `MenuPage` 新增 `ROI_DRAW`（位于 `ABOUT` 之后、`COUNT` 之前），
  `menuPageName(ROI_DRAW) == "ROI_DRAW"`。
- `LcdMenu::enterRoiDraw()` 进入页面并触发后端 `drawPage(ROI_DRAW)`；
  `leaveRoiDraw()` 回到 `MAIN`。
- ROI_DRAW 页内 `renderCurrentPage()` 不推送菜单参数（避免覆盖 ROI 画面）。
- **未改动 MainItem**：4 键主菜单仍为 开始检测/设置/关于 三项，现有
  `test_lcd_menu.cc` 的 99 项导航/节流测试保持全绿。进入 ROI_DRAW 的
  推荐方式是 LVGL 后端在 MAIN 页画一个触摸热区按钮调用 `enterRoiDraw()`，
  或板端物理按键映射（见第 6 节清单）。

---

## 2. 屏幕布局与交互约定（320x240）

```text
(0,0)                          (319,0)
  +-------------------------------+
  |                               |
  |        绘图区                  |  y < 200
  |  · 空白点按 = 加顶点            |
  |  · 顶点 8px 半径内点按 = 删除  |
  |  · >=3 点自动画闭合预览         |
  |                               |
  +--------+--------+------------+
  | ✓ DONE | ✕ CLR  | ↩ UNDO     |  y=200..239（buttonHeight=40）
  +--------+--------+------------+
  0      106      212           320
```

按钮热区（默认 `buttonHeight=40`，三等分 `bw=screenW/3=106`）：

| 按钮 | x 范围 | 动作 | 顶点不足 3 时按 ✓ |
|---|---|---|---|
| ✓ DONE  | 0..105   | 完成并生成 roi_zone JSON | 忽略（保持 DRAWING） |
| ✕ CLEAR | 106..211 | 清空全部顶点 | 照常清空 |
| ↩ UNDO  | 212..319 | 撤销最后一个顶点 | 空栈时忽略 |

绘制颜色：背景黑、顶点白（半径 >=8px 大圆点）、线段/闭合预览黄、
按钮 绿/红/灰 底 + 白字。按钮标签当前为 `✓ OK / ✕ CLR / ↩ UNDO`，
板端若要中文（完成/清除/撤销）按 `docs/hardware/lvgl_menu.md` §1.4
嵌入中文字库后在 `drawText` 层替换即可（状态机逻辑不变）。

---

## 3. 触摸采样策略（防卡死关键）

主循环**非阻塞轮询** XPT2046，把"按下沿"喂给 RoiDraw：

```cpp
// 板端伪码（main.cc 最小改动清单见第 6 节）
bool prevPressed = false;
for (;;) {
    const uint32_t now = nowMs();
    const bool pressed = tp.isTouched();          // ① 非阻塞
    if (pressed && !prevPressed) {                // ② 只在按下沿取一次
        int x = 0, y = 0;
        uint16_t z1 = 0, z2 = 0;
        if (tp.readPoint(x, y) && tp.readPressure(z1, z2)) {
            roi.feedTouch(x, y, (int)(z1 > z2 ? z1 - z2 : z2 - z1), now);
        }
    }
    prevPressed = pressed;
    // ③ 其余渲染/检测循环照常执行，绝不阻塞等待触摸
}
```

`feedTouch()` 内部处理顺序（全部非阻塞）：

1. **压力阈值**：`pressure < config.pressureThreshold`（默认 50，与
   `Xpt2046::pressureThreshold` 默认一致，单位 |Z1-Z2| ADC 计数）-> 忽略；
2. **采样节流**：距上次采样 < `pollIntervalMs`（默认 20ms = 50Hz）-> 忽略，
   主循环即使喂得快也只按 50Hz 处理；
3. **标定 + 越界过滤**：`calibrate()` 三阶变换后不在
   `[0,screenW) x [0,screenH)` 内 -> 忽略（**不 clamp**，避免屏幕外误触按钮）；
4. **防抖**：距上次有效按下 < `debounceMs`（默认 50ms）-> 忽略；
5. **命中处理**：按钮 -> 完成/清除/撤销；绘图区 -> 命中顶点删除，否则加顶点。

`pollIntervalMs` / `debounceMs` / `pressureThreshold` 都可在
`RoiDrawConfig` 中配置（默认 20 / 50 / 50）。

---

## 4. 标定步骤（未接屏：参数默认值，上板后标定）

XPT2046 先把 ADC 原始值线性映射到 320x240 屏幕坐标（`xpt2046.h` 的
`TouchCalibration`）。RoiDraw 的 `setCalibration(offsetX, offsetY, swapXY,
invertX, invertY)` 是在屏幕坐标上再做的**三阶微调**，变换顺序固定为：

```text
  raw(screenX, screenY)
    -> ① swapXY  ?  (x,y) = (y,x)
    -> ② invertX ?  x = screenW-1-x
       invertY ?  y = screenH-1-y
    -> ③ x += offsetX ; y += offsetY
    -> 越界检查（不在 0..319 / 0..239 内则丢弃）
```

默认值：`(0, 0, false, false, false)`，即不做修正，直接使用
`xpt2046.h` 标定后的屏幕坐标。上板后按下面步骤标定：

1. 先把 `xpt2046.h` 的 `TouchCalibration` 按实测 ADC 极值填好
   （屏幕四角分别读 `readRaw(XPT2046_CMD_X/Y)`，取 min/max）；
2. 在 ROI_DRAW 页点一下**左上角**，若顶点落在屏幕外/明显偏右上，先试
   `setCalibration(0, 0, true, false, false)`（轴交换）；
3. 点**左上角**，若 X 反了（点左出右），开 `invertX`；点**上边**，若 Y
   反了（点上出下），开 `invertY`；
4. 最后量屏上落点与指尖的固定偏差，填入 `offsetX / offsetY`（像素）；
5. 四角复核：点四个角，确认顶点都落在对应角附近；偏差 >8px 回到第 3/4 步。

例：点 (10,10) 实际落在 (25,18)，则
`roi.setCalibration(10 - 25, 10 - 18, false, false, false)`
（offset 是"加在屏幕坐标上的修正量"，落点偏右下就取负 offset）。

---

## 5. 输出映射（320x240 -> 720x480）

`mapToOutput()` 默认**等比例 letterbox**（保留宽高比）：

```text
scale = min(720/320, 480/240) = 2.0
offsetX = (720 - 320*2.0) / 2 = 40   // 水平居中
offsetY = (480 - 240*2.0) / 2 = 0
  屏幕(160,120) -> (160*2+40, 120*2) = (360,240)
```

`RoiOutputMapping` 全部可配置：

| 字段 | 默认 | 说明 |
|---|---|---|
| srcX/srcY/srcW/srcH | 0/0/320/240 | 屏幕源区域 |
| outW/outH | 720/480 | 输出帧尺寸 |
| preserveAspect | true | true=等比例 letterbox；false=按轴拉伸 |

两个常用预设：

1. **全屏源**（默认）：`srcW=320, srcH=240` -> 上面公式，scale=2.0；
2. **排除按钮条**（推荐给行人 ROI 覆盖全高）：`srcH=200`（绘图区）
   -> `scale = min(720/320, 480/200) = 2.25`，`offsetX=0, offsetY=15`，
   绘图区 y=0..199 映射到输出 y=15..462。

`zoneJson()` 在 `✓` 完成（顶点>=3）后生成与
`config/default_config.json` 的 `roi_zone` 同构的片段：

```json
"roi_zone": {
  "enabled": true,
  "name": "manual_roi",
  "polygon": [
    [360, 240],
    [678, 398]
  ],
  "color": [0, 0, 255],
  "thickness": 2
}
```

---

## 6. 接入 main.cc 的最小改动清单（不碰 main.cc，交给 lead/board-perf）

板端 `src/luckfox_rtsp_yolov5/src/main.cc` 现有 LVGL/ILI9341 初始化与主循环，
需要在其上做以下**增量**（本任务不修改 main.cc）：

- [ ] 引入头文件：`#include "roi_draw.h"`
- [ ] 创建适配器：`hw::Ili9341RoiDisplay roiDisplay(&lcd); hw::RoiDraw roi(&roiDisplay);`
- [ ] `roi.setCalibration(...)`：按第 4 节标定结果填入
- [ ] MAIN 页加一个触摸热区（或复用某物理按键）调用 `menu.enterRoiDraw(); roi.begin();`
- [ ] 主循环：`roi.feedTouch(x, y, pressure, nowMs)`（按第 3 节按下沿采样）
- [ ] 检测 `roi.isDone()`：
      取 `roi.zoneJson()` -> 写入/合并进 roi_zone 配置 -> `menu.leaveRoiDraw()`
- [ ] `menu.onKey(BACK)` 从 ROI_DRAW 返回时：`roi.cancel();`
- [ ] 需要时 `roi.setOutputMapping(...)` 选择第 5 节预设 2（排除按钮条）

状态迁移汇总：

```text
MAIN --(enterRoiDraw + roi.begin)--> ROI_DRAW
ROI_DRAW --(✓ DONE: roi.isDone -> menu.leaveRoiDraw)--> MAIN
ROI_DRAW --(BACK: menu.onKey(BACK) -> roi.cancel)--> MAIN
```

---

## 7. 验证状态

- `mingw32-make test`（Windows 主机，w64devkit）：全绿，含
  `test_lcd_menu 99 passed`、`test_roi_draw 131 passed`、
  `test_ili9341 123 passed`、`test_xpt2046 98 passed`。
- 真机（接屏）尚未验证：触摸手感、XPT2046 标定数值、SPI 窗口化填充的实际
  刷新率需上板后按第 4/6 节标定与联调。
