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

// 单调时钟微秒（板端 main.cc / yolov5.cc 共用）
static inline int64_t board_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

#endif  // BOARD_INFER_H
