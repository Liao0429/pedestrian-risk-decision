#ifndef PEDESTRIAN_DETECT_THRESHOLDS_H
#define PEDESTRIAN_DETECT_THRESHOLDS_H

// =============================================================================
// detect_thresholds.h — 仓库内显式的检测阈值默认值
// -----------------------------------------------------------------------------
// 背景（2026-10-30 实测）：Luckfox 两套 SDK 的 include/rknn/postprocess.h 数值不一致
//   board_sdk  : NMS_THRESH 0.45 / BOX_THRESH 0.35
//   luckfox_sdk:                  BOX_THRESH 0.25
// 仓库代码若直接引用这两个宏，阈值就随"用哪套 SDK"漂移，干净仓库上无法复现阈值。
// 这里固定仓库口径，运行时由 RiskConfig::conf_threshold / nms_threshold
// （config/default_config.json 的 conf_threshold / nms_threshold）覆盖。
// =============================================================================

namespace pedestrian {

static const float kDefaultDetectConf = 0.25f;   // 与历史 BOX_THRESH 默认一致
static const float kDefaultDetectNms  = 0.45f;   // 与历史 NMS_THRESH 默认一致

}  // namespace pedestrian

#endif  // PEDESTRIAN_DETECT_THRESHOLDS_H
