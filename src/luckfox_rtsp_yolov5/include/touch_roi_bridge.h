// =============================================================================
// touch_roi_bridge.h — BOARD_TOUCH_ROI 触摸画框 UI 与视频主循环的桥接层
// -----------------------------------------------------------------------------
// 设计目标（task-14 后续，默认不编译）：
//   1) 编译开关 BOARD_TOUCH_ROI 默认 0：关闭时本文件与 touch_roi_bridge.cc 都不
//      参与构建，main.cc 里的调用点也被 #if 掉 → 零开销、零风险；
//   2) 运行时开关 config 的 touch_roi_enabled 默认 false：即使编译进来了，也要
//      显式打开才初始化屏幕/触摸（防止接屏前误开导致启动变慢或 SPI 抢总线）；
//   3) 所有硬件操作都在主循环里**协作式**执行，节流 <=50Hz，不建线程、不忙等。
//
// 与主循环的契约：
//   touch_roi::init()      —— 打开 SPI/LCD/触摸并显示 ROI_DRAW 入口；失败返回 false
//                             （调用方必须继续正常检测，绝不能因为 UI 失败而退出）；
//   touch_roi::tick(now)   —— 每帧调用一次（内部按 20ms 节流）；负责：
//                             入口热区按下 -> begin()；绘制中采样 -> feedTouch + 增量
//                             render；完成 -> 把 zoneJson() 合并写回 config（主程序
//                             10 帧 mtime 热加载随即生效）；取消 -> 退出；
//   touch_roi::active()    —— 是否处于画框模式（供 OSD/日志使用）；
//   touch_roi::shutdown()  —— 退出时释放 GPIO/SPI（幂等）。
// =============================================================================
#ifndef TOUCH_ROI_BRIDGE_H
#define TOUCH_ROI_BRIDGE_H

#include <stdint.h>

namespace touch_roi {

// 打开屏幕与触摸并进入待命（未开始时显示"点右下角进入画框"）。
// 返回 false 表示硬件不可用（未接屏/SPI 打不开），调用方应静默降级。
bool init();

// 主循环每帧调用；内部节流到 <=50Hz。now_ms 为单调时钟毫秒。
void tick(uint32_t now_ms);

// 是否正在画框（ROI_DRAW 状态）。
bool active();

// 释放资源（幂等）。
void shutdown();

}  // namespace touch_roi

#endif  // TOUCH_ROI_BRIDGE_H
