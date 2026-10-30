#ifndef BOARD_INFER_H
#define BOARD_INFER_H

// =============================================================================
// board_infer.h — RKNN 推理归因/调优的可观测接口（board-perf / task-8）
// -----------------------------------------------------------------------------
// 目的：把板端 [PERF] 里的 infer=91.5ms 拆开，判定"NPU 真算 vs 提交/等待/同步开销"。
//   * board_infer_last_npu_us()    : RKNN_QUERY_PERF_RUN 的 run_duration（NPU 侧真实
//     推理时间，us）；需要编译期 BOARD_RKNN_COLLECT_PERF=1（init 时加
//     RKNN_FLAG_COLLECT_PERF_MASK，会略微降低帧率）。未开启时返回 -1。
//   * board_infer_last_submit_us() : 仅 BOARD_RKNN_NONBLOCK=1 时有效：rknn_run 提交
//     返回耗时。
//   * board_infer_last_wait_us()   : 同上：rknn_wait 等待耗时（= NPU 实际执行时间）。
//   * board_infer_sync_mode()      : 缓存同步档位 0/1/2（见 yolov5.cc 的
//     BOARD_RKNN_SYNC_MODE），用于 A/B 判定 mem_sync 是否有实耗。
// =============================================================================

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t board_infer_last_npu_us(void);
int64_t board_infer_last_submit_us(void);
int64_t board_infer_last_wait_us(void);
int board_infer_sync_mode(void);

#ifdef __cplusplus
}
#endif

// ---- 检测结果计数钳制（task-8 崩溃修复的泛化版本，永远生效）----
// 背景：object_detect_result_list od_results 是栈上结构体，main.cc 早期只 memset 了
// rknn_app_ctx，od_results 依赖 post_process 每帧清零。任何"跳过推理"的路径
// （推理降频、推理失败、模型初始化失败、未来新增开关）都会让 count 保持未初始化
// 栈垃圾；后续 reserve(count) 会抛 std::length_error -> terminate -> SIGABRT
// （2026-10-31 板端 dec2 变体 100% 复现，systemd 进入 Restart=on-failure 循环）。
// 修复原则：清零放在声明处，且每个使用点都钳制一次（不依赖上游）。
static inline int board_clamp_detection_count(int count, int max_size) {
    if (count < 0 || count > max_size) return 0;
    return count;
}

// 推理降频调度：decimate<=1 或首帧必须推理；否则每 decimate 帧推理一次。
static inline bool board_should_run_inference(unsigned long long frame_seq, int decimate) {
    if (decimate <= 1) return true;
    if (frame_seq == 1) return true;                    // 首帧必须有可用检测结果
    return (frame_seq % (unsigned long long)decimate) == 0;
}

// 单调时钟微秒（板端 main.cc / yolov5.cc 共用）
static inline int64_t board_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

#endif  // BOARD_INFER_H
