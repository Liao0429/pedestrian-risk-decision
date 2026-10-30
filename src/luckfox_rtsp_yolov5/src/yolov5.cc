// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#include "yolov5.h"
#include "detect_thresholds.h"
#include "board_infer.h"

// ---- NPU 归因/调优开关（默认值 = 板上已验证行为，全部可用 -D 覆盖做 A/B）----
#ifndef BOARD_RKNN_COLLECT_PERF
#define BOARD_RKNN_COLLECT_PERF 0   // 1: init 加 RKNN_FLAG_COLLECT_PERF_MASK，可查 PERF_RUN（略降帧率）
#endif
#ifndef BOARD_RKNN_SYNC_MODE
#define BOARD_RKNN_SYNC_MODE 2      // 0=不刷缓存 / 1=只 SYNC_TO_DEVICE / 2=双向（当前行为）
#endif
#ifndef BOARD_RKNN_NONBLOCK
#define BOARD_RKNN_NONBLOCK 0       // 1: non_block 提交 + 立即 rknn_wait（拆 submit/wait，语义等价）
#endif
#ifndef BOARD_RKNN_SET_CORE0
#define BOARD_RKNN_SET_CORE0 0      // 1: 显式 rknn_set_core_mask(CORE_0)（RV1106 单核应等价 AUTO）
#endif

static int64_t s_npu_run_us = -1;
static int64_t s_submit_us = -1;
static int64_t s_wait_us = -1;

extern "C" int64_t board_infer_last_npu_us(void) { return s_npu_run_us; }
extern "C" int64_t board_infer_last_submit_us(void) { return s_submit_us; }
extern "C" int64_t board_infer_last_wait_us(void) { return s_wait_us; }
extern "C" int board_infer_sync_mode(void) { return BOARD_RKNN_SYNC_MODE; }

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

int init_yolov5_model(const char *model_path, rknn_app_context_t *app_ctx)
{
    int ret;
    int model_len = 0;
    char *model;
    rknn_context ctx = 0;
    // 两个 vector 提前声明（而不是在 goto 目标之前就地构造），
    // 否则 goto fail 会跨过它们的初始化：C++ 报
    // "jump to label 'fail' crosses initialization"。
    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;

    uint32_t init_flag = RKNN_FLAG_PRIOR_HIGH;
#if BOARD_RKNN_COLLECT_PERF
    init_flag |= RKNN_FLAG_COLLECT_PERF_MASK;   // 打开后才能 RKNN_QUERY_PERF_RUN / PERF_DETAIL
#endif
    ret = rknn_init(&ctx, (char *)model_path, 0, init_flag, NULL);
    if (ret < 0)
    {
        printf("rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    // SDK/驱动版本：归因报告需要固定口径（api/drv 不同版本 NPU 性能差异很大）
    {
        rknn_sdk_version sdkv;
        memset(&sdkv, 0, sizeof(sdkv));
        if (rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &sdkv, sizeof(sdkv)) == RKNN_SUCC)
            printf("[RKNN] api_version=%s drv_version=%s collect_perf=%d\n",
                   sdkv.api_version, sdkv.drv_version, (int)BOARD_RKNN_COLLECT_PERF);
    }

    // Get Model Input Output Number
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC)
    {
        printf("rknn_query fail! ret=%d\n", ret);
        goto fail;
    }
    //printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    // VLA 改 std::vector：io_num 可能为 0，先校验再分配，避免 C99 VLA 越界。
    // 同时校验上限：rknn_app_context_t 里 input_mems[1] / output_mems[3] 是定长数组
    // （SDK include/rknn/yolov5.h），旧实现无上限检查，换多输出模型会写越界。
    if (io_num.n_input == 0 || io_num.n_output == 0 ||
        io_num.n_input > 1 || io_num.n_output > 3) {
        printf("rknn_query invalid io num! n_input=%u (<=1), n_output=%u (<=3)\n",
               (unsigned)io_num.n_input, (unsigned)io_num.n_output);
        goto fail;
    }

    // Get Model Input Info
    //printf("input tensors:\n");
    input_attrs.resize(io_num.n_input);
    memset(input_attrs.data(), 0, sizeof(rknn_tensor_attr) * input_attrs.size());
    for (uint32_t i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("rknn_query fail! ret=%d\n", ret);
            goto fail;
        }
        dump_tensor_attr(&(input_attrs[i]));
    }

    // Get Model Output Info
    //printf("output tensors:\n");
    output_attrs.resize(io_num.n_output);
    memset(output_attrs.data(), 0, sizeof(rknn_tensor_attr) * output_attrs.size());
    for (uint32_t i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        //When using the zero-copy API interface, query the native output tensor attribute
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("rknn_query fail! ret=%d\n", ret);
            goto fail;
        }
        dump_tensor_attr(&(output_attrs[i]));
    }

    // default input type is int8 (normalize and quantize need compute in outside)
    // if set uint8, will fuse normalize and quantize to npu
    input_attrs[0].type = RKNN_TENSOR_UINT8;
    // default fmt is NHWC,1106 npu only support NHWC in zero copy mode
    input_attrs[0].fmt = RKNN_TENSOR_NHWC;
    //printf("input_attrs[0].size_with_stride=%d\n", input_attrs[0].size_with_stride);
    app_ctx->input_mems[0] = rknn_create_mem(ctx, input_attrs[0].size_with_stride);
    if (app_ctx->input_mems[0] == NULL) {
        printf("input rknn_create_mem fail!\n");
        goto fail;
    }

    // Set input tensor memory
    ret = rknn_set_io_mem(ctx, app_ctx->input_mems[0], &input_attrs[0]);
    if (ret < 0) {
        printf("input_mems rknn_set_io_mem fail! ret=%d\n", ret);
        goto fail;
    }

    // Set output tensor memory
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        app_ctx->output_mems[i] = rknn_create_mem(ctx, output_attrs[i].size_with_stride);
        if (app_ctx->output_mems[i] == NULL) {
            printf("output[%u] rknn_create_mem fail!\n", (unsigned)i);
            goto fail;
        }
        ret = rknn_set_io_mem(ctx, app_ctx->output_mems[i], &output_attrs[i]);
        if (ret < 0) {
            printf("output_mems rknn_set_io_mem fail! ret=%d\n", ret);
            goto fail;
        }
    }

    // Set to context
    app_ctx->rknn_ctx = ctx;
#if BOARD_RKNN_SET_CORE0
    {
        // RV1106 只有一个 NPU core；显式设 CORE_0 用于 A/B 验证 AUTO 是否等价
        int cret = rknn_set_core_mask(ctx, RKNN_NPU_CORE_0);
        printf("[RKNN] rknn_set_core_mask(CORE_0) ret=%d\n", cret);
    }
#endif

    // TODO
    if (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC)
    {
        app_ctx->is_quant = true;
    }
    else
    {
        app_ctx->is_quant = false;
    }

    app_ctx->io_num = io_num;
    app_ctx->input_attrs = (rknn_tensor_attr *)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    app_ctx->output_attrs = (rknn_tensor_attr *)malloc(io_num.n_output * sizeof(rknn_tensor_attr));
    if (app_ctx->input_attrs == NULL || app_ctx->output_attrs == NULL) {
        printf("malloc tensor attrs fail!\n");
        goto fail;
    }
    memcpy(app_ctx->input_attrs, input_attrs.data(), io_num.n_input * sizeof(rknn_tensor_attr));
    memcpy(app_ctx->output_attrs, output_attrs.data(), io_num.n_output * sizeof(rknn_tensor_attr));

    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) 
    {
        printf("model is NCHW input fmt\n");
        app_ctx->model_channel = input_attrs[0].dims[1];
        app_ctx->model_height  = input_attrs[0].dims[2];
        app_ctx->model_width   = input_attrs[0].dims[3];
    } else 
    {
        printf("model is NHWC input fmt\n");
        app_ctx->model_height  = input_attrs[0].dims[1];
        app_ctx->model_width   = input_attrs[0].dims[2];
        app_ctx->model_channel = input_attrs[0].dims[3];
    } 

    printf("model input height=%d, width=%d, channel=%d\n",
           app_ctx->model_height, app_ctx->model_width, app_ctx->model_channel);

    return 0;

fail:
    // 错误路径统一释放：旧实现在 query/set_io_mem/malloc 失败时直接 return -1，
    // rknn_context 与 DMA 缓冲全部泄漏（反复 restart 会累积）。
    if (ctx != 0) {
        if (app_ctx->input_mems[0] != NULL) {
            rknn_destroy_mem(ctx, app_ctx->input_mems[0]);
            app_ctx->input_mems[0] = NULL;
        }
        for (int i = 0; i < 3; ++i) {
            if (app_ctx->output_mems[i] != NULL) {
                rknn_destroy_mem(ctx, app_ctx->output_mems[i]);
                app_ctx->output_mems[i] = NULL;
            }
        }
        rknn_destroy(ctx);
        app_ctx->rknn_ctx = 0;
    }
    if (app_ctx->input_attrs != NULL) { free(app_ctx->input_attrs); app_ctx->input_attrs = NULL; }
    if (app_ctx->output_attrs != NULL) { free(app_ctx->output_attrs); app_ctx->output_attrs = NULL; }
    return -1;
}

int release_yolov5_model(rknn_app_context_t *app_ctx)
{
    if (app_ctx->input_attrs != NULL)
    {
        free(app_ctx->input_attrs);
        app_ctx->input_attrs = NULL;
    }
    if (app_ctx->output_attrs != NULL)
    {
        free(app_ctx->output_attrs);
        app_ctx->output_attrs = NULL;
    }
    for (int i = 0; i < app_ctx->io_num.n_input; i++) {
        if (app_ctx->input_mems[i] != NULL) {
            rknn_destroy_mem(app_ctx->rknn_ctx, app_ctx->input_mems[i]);
        }
    }
    for (int i = 0; i < app_ctx->io_num.n_output; i++) {
        if (app_ctx->output_mems[i] != NULL) {
            rknn_destroy_mem(app_ctx->rknn_ctx, app_ctx->output_mems[i]);
        }
    }
    if (app_ctx->net_mem != NULL && app_ctx->rknn_ctx != 0) {
        rknn_destroy_mem(app_ctx->rknn_ctx, app_ctx->net_mem);
        app_ctx->net_mem = NULL;
    }
    if (app_ctx->rknn_ctx != 0)
    {
        rknn_destroy(app_ctx->rknn_ctx);

        app_ctx->rknn_ctx = 0;
    }

    printf("Release success\n");
    return 0;
}

// 置信度阈值由调用方传入（main.cc 用 riskCfg.conf_threshold，来自
// config/default_config.json 的 conf_threshold；旧实现是编译期 pedestrian::kDefaultDetectConf，
// 配置里的 conf_threshold 整条链路无人读取 —— 现场调参实际无效）。
int inference_yolov5_model(rknn_app_context_t *app_ctx,
                           object_detect_result_list *od_results,
                           float conf_threshold)
{
    if (app_ctx == NULL || od_results == NULL) return -1;
    const float nms_threshold = pedestrian::kDefaultDetectNms;   // 默认 NMS 阈值
    float box_conf_threshold = conf_threshold;
    if (!(box_conf_threshold > 0.0f && box_conf_threshold < 1.0f))
        box_conf_threshold = pedestrian::kDefaultDetectConf;      // 非法值回退编译期默认

    // ---- 零拷贝 DMA 缓存一致性 ----
    // CPU 刚写完 letterbox 数据；NPU 通过 DMA 读同一块物理内存，提交前必须
    // SYNC_TO_DEVICE 回写缓存（rknn 映射可能是 cacheable）。旧代码假设不可缓存、
    // 未做任何 sync，属于潜在脏读/撕裂根因。
#if BOARD_RKNN_SYNC_MODE >= 1
    {
        static bool in_sync_warned = false;
        int sret = rknn_mem_sync(app_ctx->rknn_ctx, app_ctx->input_mems[0],
                                 RKNN_MEMORY_SYNC_TO_DEVICE);
        if (sret != 0 && !in_sync_warned) {
            printf("rknn_mem_sync(input,TO_DEVICE) ret=%d (内存不可缓存时可忽略)\n", sret);
            in_sync_warned = true;
        }
    }
#endif

    rknn_run_extend run_ext;
    memset(&run_ext, 0, sizeof(run_ext));
    run_ext.timeout_ms = 2000;   // 驱动异常时不至于像 timeout=-1 那样永久阻塞
#if BOARD_RKNN_NONBLOCK
    run_ext.non_block = 1;       // 提交后立即 wait：等价同步，但可把 submit/wait 拆开计时
#endif
    const int64_t t_submit = board_now_us();
    int ret = rknn_run(app_ctx->rknn_ctx, &run_ext);
    if (ret < 0) {
        printf("rknn_run fail! ret=%d\n", ret);
        return -1;
    }
#if BOARD_RKNN_NONBLOCK
    {
        const int64_t t_submitted = board_now_us();
        const int wret = rknn_wait(app_ctx->rknn_ctx, &run_ext);
        const int64_t t_waited = board_now_us();
        s_submit_us = t_submitted - t_submit;
        s_wait_us   = t_waited - t_submitted;
        if (wret != 0) printf("rknn_wait ret=%d\n", wret);
    }
#endif

#if BOARD_RKNN_COLLECT_PERF
    {
        // NPU 侧真实推理时间（us），与 CPU 观测到的 infer 墙钟时间对比即可定位开销
        rknn_perf_run pr;
        memset(&pr, 0, sizeof(pr));
        if (rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_PERF_RUN, &pr, sizeof(pr)) == RKNN_SUCC)
            s_npu_run_us = pr.run_duration;
    }
#endif

#if BOARD_RKNN_SYNC_MODE >= 2
    // NPU 写完输出后，CPU 读之前需要 SYNC_FROM_DEVICE（零拷贝输出）
    for (uint32_t mi = 0; mi < app_ctx->io_num.n_output; ++mi) {
        if (app_ctx->output_mems[mi] != NULL)
            rknn_mem_sync(app_ctx->rknn_ctx, app_ctx->output_mems[mi],
                          RKNN_MEMORY_SYNC_FROM_DEVICE);
    }
#endif

    // Post Process：旧实现忽略返回值，解码失败时下游会拿到上一帧的陈旧框
    ret = post_process(app_ctx, app_ctx->output_mems, box_conf_threshold,
                       nms_threshold, od_results);
    if (ret != 0) {
        printf("post_process fail! ret=%d\n", ret);
        od_results->count = 0;
        return -1;
    }
    return 0;
}