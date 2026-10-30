#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "yolov5.h"
#include "risk_engine.h"
#include "risk_config.h"
#include "enhanced_decision.h"
#include "ipm_transform.h"
#include "chinese_font.h"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "preprocess.h"
#include "../../algorithms/crowd_dbscan.h"   // DBSCAN 簇计数（OSD Clusters 行）

#define DISP_WIDTH  720
#define DISP_HEIGHT 480

// =============================================================================
// 板端热路径调试/统计开关
// -----------------------------------------------------------------------------
// BOARD_VERBOSE_DETECT=1：恢复"每帧每个检测框"的 printf / fprintf(stderr)。
//   旧实现每帧都写 stdout+stderr：stderr 无缓冲，stdout 重定向到管道时是块缓冲；
//   一旦消费方（systemd journal / ssh / 串口）跟不上，管道缓冲写满后 write()
//   会阻塞主循环 —— 这是"板子卡死"的可疑诱因之一（看门狗 pedestrian-watchdog.sh
//   正是靠 /tmp/enhanced_latest.log 的 mtime 判活）。默认关闭。
#ifndef BOARD_VERBOSE_DETECT
#define BOARD_VERBOSE_DETECT 0
#endif
// BOARD_PERF_HEARTBEAT=1：每秒一行 [PERF]（FPS + 分阶段耗时）。
//   必须保留 ≥1 行/秒：看门狗 STALE_SEC=30，心跳停 >30s 会被重启。
#ifndef BOARD_PERF_HEARTBEAT
#define BOARD_PERF_HEARTBEAT 1
#endif
// VENC 单帧最大 pack 数（SPS/PPS 与 slice 可能分成多个 pack）
#define VENC_MAX_PACKS 8

// BOARD_OLD_PREPROCESS=1：A/B 对照用，退回旧预处理链路
//   （cv::cvtColor(NV21) + cv::resize + 整幅黑边 + copyTo + 1.2MB memcpy + 1MB 自拷贝）
// 同一块板、同一场景下量化"旧 vs 新"的 prep 耗时；生产默认 0（新链路）。
#ifndef BOARD_OLD_PREPROCESS
#define BOARD_OLD_PREPROCESS 0
#endif

static inline uint64_t nowUs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

// 分阶段耗时累加器（每秒清零并打印）
struct StageAccum {
  uint64_t vi_us;
  uint64_t prep_us;
  uint64_t infer_us;
  uint64_t decide_us;
  uint64_t draw_us;
  uint64_t enc_us;
  int frames;
  uint64_t window_start_us;
  StageAccum()
      : vi_us(0), prep_us(0), infer_us(0), decide_us(0), draw_us(0), enc_us(0),
        frames(0), window_start_us(0) {}
};

// disp size
int width    = DISP_WIDTH;
int height   = DISP_HEIGHT;

// model size
int model_width = 640;
int model_height = 640;	
float scale ;
int leftPadding ;
int topPadding  ;

// letterbox 已改由 pedestrian::yuv420sp_to_rgb_letterbox()（src/preprocess.cc）
// 直接完成：NV12 -> RGB letterbox 单遍定点插值，且结果直接写进 RKNN 输入 DMA
// 缓冲（零拷贝）。旧实现每次 resize 到 640x426 + 整幅黑边清零 + copyTo +
// memcpy 1.2MB 进 NPU 缓冲，且喂给模型的是 BGR（模型要 RGB，红蓝颠倒）。
// 几何参数（scale/leftPadding/topPadding）在 main() 里由 letterbox_prepare()
// 写回，mapCoordinates() 行为不变。
void mapCoordinates(int *x, int *y) {	
	int mx = *x - leftPadding;
	int my = *y - topPadding;

    *x = (int)((float)mx / scale);
    *y = (int)((float)my / scale);
}

// ---- ROI 射线法：点 (px,py) 是否在多边形 poly 内（double 全程 PNPOLY）----
// 与旧内联实现的差异：
//   1. 交点 x 全程 double 运算，去掉 `(yj-yi+0.0001)` 的 epsilon —— 该 epsilon
//      会给每条边的交点都引入偏置（不只是防除零），属于隐患写法；
//   2. (yi>py)!=(yj>py) 半开边规则：水平边（yi==yj）恒不进入分支，天然无除零；
//      py 恰好落在顶点 y 上时该顶点只归一侧计数，不会重复翻转导致奇偶错乱；
//   3. 保护条件：顶点数 <3 直接判 outside；包围盒快筛先剔除盒外点，同时挡住
//      顶点越界/退化配置造成的"全员判内"式误判。
static bool pointInPolygonRoi(int px, int py,
                              const std::vector<std::pair<int,int>> &poly) {
    const int nv = (int)poly.size();
    if (nv < 3) return false;
    int minX = poly[0].first, maxX = minX, minY = poly[0].second, maxY = minY;
    for (int k = 1; k < nv; ++k) {
        minX = std::min(minX, poly[k].first);  maxX = std::max(maxX, poly[k].first);
        minY = std::min(minY, poly[k].second); maxY = std::max(maxY, poly[k].second);
    }
    if (px < minX || px > maxX || py < minY || py > maxY) return false;
    bool inside = false;
    const double dx = (double)px, dy = (double)py;
    for (int k = 0, j = nv - 1; k < nv; j = k++) {
        const double xi = (double)poly[k].first,  yi = (double)poly[k].second;
        const double xj = (double)poly[j].first,  yj = (double)poly[j].second;
        if ((yi > dy) != (yj > dy)) {          // 进入分支即保证 yi != yj，除法安全
            const double xCross = xi + (dy - yi) * (xj - xi) / (yj - yi);
            if (dx < xCross) inside = !inside;
        }
    }
    return inside;
}

// ---- 24x24 点阵中文绘制（从 chinese_font.h 读取字模，逐像素画到 frame）----
// pixel_scale=1 → 字高 24px；不带描边（横幅背景已有填充色，描边反而不清）
static void draw_cn(cv::Mat &img, const char *chars, int x, int y,
                    const cv::Scalar &color) {
    int pen_x = x;
    for (const char *p = chars; *p;) {
        if ((static_cast<unsigned char>(*p) & 0xE0) == 0xE0 && p[1] && p[2]) {
            char ch[4] = {p[0], p[1], p[2], 0};
            const uint8_t *g = cn_find_glyph(ch);
            if (g) {
                for (int row = 0; row < 24; ++row) {
                    const uint32_t bits = ((uint32_t)g[row * 3] << 16) |
                                          ((uint32_t)g[row * 3 + 1] << 8) |
                                          (uint32_t)g[row * 3 + 2];
                    for (int col = 0; col < 24; ++col) {
                        if (bits & (0x800000u >> col)) {
                            int px = pen_x + col, py = y + row;
                            if (px >= 0 && py >= 0 && px < img.cols && py < img.rows)
                                img.at<cv::Vec3b>(py, px) = cv::Vec3b(
                                    (uchar)color[0], (uchar)color[1], (uchar)color[2]);
                        }
                    }
                }
                pen_x += 24 + 2;
                p += 3;
                continue;
            }
            p += 3; pen_x += 24 + 2;
            continue;
        }
        // ASCII 直接 putText
        char ascii[2] = {*p, 0};
        cv::putText(img, ascii, cv::Point(pen_x, y + 18),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
        pen_x += 10; ++p;
    }
}


// ---- 全局停止标志与信号处理（优雅退出，配合 systemd 看门狗自愈） ----
static volatile sig_atomic_t g_stop = 0;
static void signal_handler(int sig) { (void)sig; g_stop = 1; }

int main(int argc, char *argv[]) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  system("RkLunch-stop.sh");

  // ==== 风险决策引擎 + 逆透视变换(IPM)标定 ====
  // 画面 720x480，源四边形取画面底部地面梯形(近大远小)，映射到地面 6m x 4m
  cv::Point2f srcPts[4] = { cv::Point2f(0,480), cv::Point2f(720,480),
                            cv::Point2f(560,300), cv::Point2f(160,300) };
  cv::Point2f dstPts[4] = { cv::Point2f(-3,3), cv::Point2f(3,3),
                            cv::Point2f(3,-1), cv::Point2f(-3,-1) };
  cv::Mat H = cv::getPerspectiveTransform(srcPts, dstPts);
  // 运行时加载阈值配置（默认 ./config/default_config.json，缺失时回退到默认值）
  pedestrian::RiskConfig riskCfg =
      pedestrian::loadRiskConfigOr("./config/default_config.json", pedestrian::RiskConfig());
  // 若配置了外部 IPM 标定文件（homography_file），则用其替换默认硬编码单应，
  // 相机位姿变化后无需重编译（单应矩阵外部标定）。
  if (!riskCfg.homography_file.empty()) {
      cv::Mat H_ext;
      if (pedestrian::loadHomographyFromFile(riskCfg.homography_file, H_ext)) {
          H = H_ext;
          fprintf(stderr, "[IPM] 已从外部标定文件加载单应矩阵: %s\n",
                  riskCfg.homography_file.c_str());
      } else {
          fprintf(stderr,
                  "[IPM] 警告: 无法加载外部标定文件 %s（不存在或格式非法），"
                  "使用默认硬编码单应\n",
                  riskCfg.homography_file.c_str());
      }
  }
  RiskEngine riskEngine;
  riskEngine.setHomography(H);
  riskEngine.setConfig(riskCfg);
  // 增强决策模块默认启用（不再依赖编译宏开关）
  pedestrian::EnhancedConfig enhancedCfg;
  enhancedCfg.edge_dist_m = riskCfg.edge_dist_m;
  enhancedCfg.fsm = riskCfg.fsm;      // FSM 参数走 JSON 配置（与 Python 端一致）
  enhancedCfg.frame_dt = riskCfg.frame_dt;  // Kalman 时间基准走配置（按实际帧率调整）
  // 谱特征降频间隔走配置（config/default_config.json 可覆盖，缺省 3）
  enhancedCfg.spectral_interval = riskCfg.spectral_interval;
  pedestrian::EnhancedDecision enhancedDecision(enhancedCfg);

  // ROI 配置自检：启动即打印生效的多边形顶点；顶点越界说明配置坐标系不是
  // 720x480 显示坐标（如误用了模型输入/抓图分辨率坐标），闯入判定必然失真。
  if (riskCfg.roi.enabled) {
    fprintf(stderr, "[ROI] enabled zone='%s' polygon(%d):",
            riskCfg.roi.name.c_str(), (int)riskCfg.roi.polygon.size());
    for (size_t v = 0; v < riskCfg.roi.polygon.size(); ++v)
      fprintf(stderr, " (%d,%d)", riskCfg.roi.polygon[v].first, riskCfg.roi.polygon[v].second);
    fprintf(stderr, "\n");
    for (size_t v = 0; v < riskCfg.roi.polygon.size(); ++v) {
      if (riskCfg.roi.polygon[v].first < 0 || riskCfg.roi.polygon[v].first >= width ||
          riskCfg.roi.polygon[v].second < 0 || riskCfg.roi.polygon[v].second >= height) {
        fprintf(stderr,
                "[ROI] 警告: 顶点 (%d,%d) 超出画面 %dx%d —— 配置坐标系与显示坐标系不一致，"
                "请用 tools/roi_live.html 重新框选并发送\n",
                riskCfg.roi.polygon[v].first, riskCfg.roi.polygon[v].second, width, height);
      }
    }
    if (riskCfg.roi.polygon.size() < 3)
      fprintf(stderr, "[ROI] 警告: 多边形顶点数 %d < 3，闯入判定将恒为 outside\n",
              (int)riskCfg.roi.polygon.size());
  }

  // ---- 资源句柄与初始化状态（任一关键初始化失败时安全清理） ----
  RK_S32 s32Ret = 0;
  int sX, sY, eX, eY;
  int ret = 0;
  rknn_app_context_t rknn_app_ctx;
  object_detect_result_list od_results;
  memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

  VENC_STREAM_S stFrame;
  memset(&stFrame, 0, sizeof(stFrame));
  // 驱动可能回多个 pack（SPS/PPS 独立包、多 slice）；旧实现只分配 1 个 pack 且
  // 只发送 pstPack[0] -> 丢包导致花屏/解码器报错，多 pack 时还会写越界。
  VENC_PACK_S stPacks[VENC_MAX_PACKS];
  memset(stPacks, 0, sizeof(stPacks));
  stFrame.pstPack = stPacks;
  RK_U64 H264_PTS = 0;
  RK_U32 H264_TimeRef = 0;
  VIDEO_FRAME_INFO_S stViFrame;

  MB_POOL_CONFIG_S PoolCfg;
  memset(&PoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
  MB_POOL src_Pool = 0;
  MB_BLK src_Blk = 0;

  const char *model_path = "./model/yolov5.rknn";

  bool rknn_ready = false;
  bool postproc_ready = false;
  bool pool_ready = false;
  bool blk_ready = false;
  bool isp_ready = false;
  bool sys_ready = false;
  bool rtsp_ready = false;
  bool vi_ready = false;
  bool venc_ready = false;
  rtsp_demo_handle g_rtsplive = NULL;
  rtsp_session_handle g_rtsp_session = NULL;

  // ---- 安全清理（正常退出 / 初始化失败共用） ----
  auto cleanup = [&]() {
    if (blk_ready)       RK_MPI_MB_ReleaseMB(src_Blk);
    if (pool_ready)      RK_MPI_MB_DestroyPool(src_Pool);
    if (vi_ready) {      RK_MPI_VI_DisableChn(0, 0); RK_MPI_VI_DisableDev(0); }
    if (isp_ready)       SAMPLE_COMM_ISP_Stop(0);
    if (venc_ready) {    RK_MPI_VENC_StopRecvFrame(0); RK_MPI_VENC_DestroyChn(0); }
    if (rtsp_ready && g_rtsplive) rtsp_del_demo(g_rtsplive);
    if (sys_ready)       RK_MPI_SYS_Exit();
    if (rknn_ready)      release_yolov5_model(&rknn_app_ctx);
    if (postproc_ready)  deinit_post_process();
  };

  // ==== 初始化序列：任一关键初始化失败即打印明确错误并安全退出 ====
  ret = init_yolov5_model(model_path, &rknn_app_ctx);
  if (ret != 0) {
    fprintf(stderr, "[FATAL] init_yolov5_model 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  rknn_ready = true;
  printf("init rknn model success!\n");

  ret = init_post_process();
  if (ret != 0) {
    fprintf(stderr, "[FATAL] init_post_process 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  postproc_ready = true;

  // Create Pool
  PoolCfg.u64MBSize = width * height * 3;
  PoolCfg.u32MBCnt = 1;
  PoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
  //PoolCfg.bPreAlloc = RK_FALSE;
  src_Pool = RK_MPI_MB_CreatePool(&PoolCfg);
  for (int attempt = 0; src_Pool == NULL && attempt < 3; ++attempt) {
    fprintf(stderr, "[WARN] RK_MPI_MB_CreatePool 第 %d 次失败，重试...\n", attempt + 1);
    usleep(200 * 1000);
    src_Pool = RK_MPI_MB_CreatePool(&PoolCfg);
  }
  if (src_Pool == NULL) {
    fprintf(stderr, "[FATAL] RK_MPI_MB_CreatePool 重试后仍失败\n");
    cleanup();
    return -1;
  }
  pool_ready = true;
  printf("Create Pool success !\n");

  // Get MB from Pool
  src_Blk = RK_MPI_MB_GetMB(src_Pool, width * height * 3, RK_TRUE);
  if (src_Blk == NULL) {
    fprintf(stderr, "[FATAL] RK_MPI_MB_GetMB 失败\n");
    cleanup();
    return -1;
  }
  blk_ready = true;

  // Build h264_frame
  VIDEO_FRAME_INFO_S h264_frame;
  h264_frame.stVFrame.u32Width = width;
  h264_frame.stVFrame.u32Height = height;
  h264_frame.stVFrame.u32VirWidth = width;
  h264_frame.stVFrame.u32VirHeight = height;
  h264_frame.stVFrame.enPixelFormat =  RK_FMT_RGB888;
  h264_frame.stVFrame.u32FrameFlag = 160;
  h264_frame.stVFrame.pMbBlk = src_Blk;
  unsigned char *data = (unsigned char *)RK_MPI_MB_Handle2VirAddr(src_Blk);
  cv::Mat frame(cv::Size(width,height),CV_8UC3,data);

  // rkaiq init
  RK_BOOL multi_sensor = RK_FALSE;
  const char *iq_dir = "/etc/iqfiles";
  rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
  //hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;
  ret = SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
  if (ret != RK_SUCCESS) {
    fprintf(stderr, "[FATAL] SAMPLE_COMM_ISP_Init 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  ret = SAMPLE_COMM_ISP_Run(0);
  if (ret != RK_SUCCESS) {
    fprintf(stderr, "[FATAL] SAMPLE_COMM_ISP_Run 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  isp_ready = true;

  // rkmpi init
  if (RK_MPI_SYS_Init() != RK_SUCCESS) {
    RK_LOGE("rk mpi sys init fail!");
    cleanup();
    return -1;
  }
  sys_ready = true;

  // rtsp init
  g_rtsplive = create_rtsp_demo(554);
  if (g_rtsplive == NULL) {
    fprintf(stderr, "[FATAL] create_rtsp_demo 失败\n");
    cleanup();
    return -1;
  }
  g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
  if (g_rtsp_session == NULL) {
    fprintf(stderr, "[FATAL] rtsp_new_session 失败\n");
    cleanup();
    return -1;
  }
  rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
  rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
  rtsp_ready = true;

  // vi init
  ret = vi_dev_init();
  if (ret != 0) {
    fprintf(stderr, "[FATAL] vi_dev_init 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  ret = vi_chn_init(0, width, height);
  if (ret != 0) {
    fprintf(stderr, "[FATAL] vi_chn_init 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  vi_ready = true;

  // venc init
  RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
  ret = venc_init(0, width, height, enCodecType);
  if (ret != 0) {
    fprintf(stderr, "[FATAL] venc_init 失败 (ret=%d)\n", ret);
    cleanup();
    return -1;
  }
  venc_ready = true;
  printf("venc init success\n");

  // ---- 预处理常驻参数：NV12 采样表只算一次，每帧零 malloc ----
  pedestrian::LetterboxParams lbp;
  if (pedestrian::letterbox_prepare(width, height, model_width, model_height, &lbp) != 0) {
    fprintf(stderr, "[FATAL] letterbox_prepare(%dx%d -> %dx%d) 失败\n",
            width, height, model_width, model_height);
    cleanup();
    return -1;
  }
  // mapCoordinates() 仍用同一组全局几何参数（与旧 letterbox() 完全一致的取值）
  scale       = (float)lbp.roi_w / (float)width;
  leftPadding = lbp.left_pad;
  topPadding  = lbp.top_pad;
  printf("preprocess ready: roi=%dx%d pad=(%d,%d) scale=%.4f\n",
         lbp.roi_w, lbp.roi_h, lbp.left_pad, lbp.top_pad, (double)scale);
#if BOARD_OLD_PREPROCESS
  // A/B 基线：旧链路需要的中间缓冲（仅对照构建使用）
  cv::Mat oldScaled;
  cv::Mat oldLetterbox;
  oldScaled.create(lbp.roi_h, lbp.roi_w, CV_8UC3);
  oldLetterbox.create(model_height, model_width, CV_8UC3);
  oldLetterbox.setTo(cv::Scalar(0, 0, 0));
  printf("[PREPROCESS] A/B 基线：旧链路 (BOARD_OLD_PREPROCESS=1)\n");
#else
  printf("[PREPROCESS] 新链路：preprocess.cc 单遍零拷贝\n");
#endif

  // ==== 主循环 ====
  // 与旧实现的差异（详见 docs/review/20261030_round3/board_perf.md）：
  //  1) 所有 RK_MPI 阻塞调用带有限超时 + 连续失败计数：VENC 背压/ISP stall 时
  //     不会永久挂死（旧实现 timeout=-1 且 SendFrame 返回值未检查），超限即
  //     break -> cleanup -> systemd 重启自愈。
  //  2) 预处理合并为零拷贝单遍：NV12->BGR 直写 VENC 的 DMA 缓冲，
  //     NV12->RGB letterbox 直写 RKNN 输入缓冲；去掉 cvtColor / 恒等 resize /
  //     整幅 letterbox / 1.2MB memcpy / 1MB 同地址自拷贝，并修正 BGR/RGB 通道。
  //  3) VENC 多 pack 全发（旧实现只发 pstPack[0]）。
  //  4) 逐帧/逐框调试打印改编译开关，运行期只保留 1Hz 心跳。
  StageAccum acc;
  acc.window_start_us = nowUs();
  int vi_fail = 0;
  int venc_fail = 0;
  const int kViTimeoutMs   = 200;  // VI 无帧：200ms 回循环检查 g_stop
  const int kVencTimeoutMs = 100;  // VENC 收发：100ms 超时视为背压
  const int kMaxConsecFail = 50;   // 连续失败上限（约 5~10s）后退出重启

  while (!g_stop) {
    const uint64_t t_frame = nowUs();

    // ---- 配置热重载：每 300 帧查一次 mtime（旧实现每帧 stat 一次）----
    {
      static int cfgCheckFrame = 0;
      static time_t cfgMtime = 0;
      if (++cfgCheckFrame % 300 == 0) {
        struct stat st;
        if (stat("./config/default_config.json", &st) == 0 && st.st_mtime != cfgMtime) {
          cfgMtime = st.st_mtime;
          pedestrian::RiskConfig newCfg =
              pedestrian::loadRiskConfigOr("./config/default_config.json", riskCfg);
          if (newCfg.roi.enabled != riskCfg.roi.enabled ||
              newCfg.roi.polygon != riskCfg.roi.polygon ||
              newCfg.conf_threshold != riskCfg.conf_threshold) {
            riskCfg = newCfg;
            fprintf(stderr, "[ROI] Config reloaded: roi enabled=%d vertices=%zu conf=%.2f\n",
                    (int)riskCfg.roi.enabled, riskCfg.roi.polygon.size(),
                    (double)riskCfg.conf_threshold);
          }
        }
      }
    }

    // ---- 取 VI 帧（有限超时）----
    h264_frame.stVFrame.u32TimeRef = H264_TimeRef++;
    h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs();
    s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, kViTimeoutMs);
    if (s32Ret != RK_SUCCESS) {
      if (++vi_fail >= kMaxConsecFail) {
        RK_LOGE("[FATAL] VI GetChnFrame 连续失败 %d 次 (ret=%x)，退出交由 systemd 重启",
                vi_fail, s32Ret);
        break;
      }
      continue;
    }
    vi_fail = 0;
    const uint64_t t_vi = nowUs();

    // ---- 预处理（单遍、零拷贝）----
    const uint8_t *vi_yuv =
        (const uint8_t *)RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
#if BOARD_OLD_PREPROCESS
    {
      // ---- 旧链路（A/B 基线，逐行等价于历史 main.cc）----
      cv::Mat yuv420sp(height + height / 2, width, CV_8UC1, (void *)vi_yuv);
      cv::cvtColor(yuv420sp, frame, cv::COLOR_YUV420sp2BGR);   // 注意：NV21 语义
      cv::resize(frame, oldScaled, oldScaled.size(), 0, 0, cv::INTER_LINEAR);
      const cv::Rect oldRoi(lbp.left_pad, lbp.top_pad, lbp.roi_w, lbp.roi_h);
      const cv::Rect oldPads[4] = {
          cv::Rect(0, 0, model_width, oldRoi.y),
          cv::Rect(0, oldRoi.y + oldRoi.height, model_width,
                   model_height - (oldRoi.y + oldRoi.height)),
          cv::Rect(0, oldRoi.y, oldRoi.x, oldRoi.height),
          cv::Rect(oldRoi.x + oldRoi.width, oldRoi.y,
                   model_width - (oldRoi.x + oldRoi.width), oldRoi.height)};
      for (int pi = 0; pi < 4; ++pi)
        if (oldPads[pi].width > 0 && oldPads[pi].height > 0)
          oldLetterbox(oldPads[pi]).setTo(cv::Scalar(0, 0, 0));
      oldScaled.copyTo(oldLetterbox(oldRoi));
      memcpy(rknn_app_ctx.input_mems[0]->virt_addr, oldLetterbox.data,
             model_width * model_height * 3);
      memcpy(data, frame.data, width * height * 3);   // 历史同地址自拷贝，保留以对齐口径
    }
#else
    // 1) NV12 -> BGR 全帧，直接写 VENC 用的 DMA 缓冲（画框也在这块上）
    pedestrian::yuv420sp_to_bgr(vi_yuv, width, height, data);
    // 2) NV12 -> RGB letterbox，直接写 RKNN 输入缓冲（省 1.2MB/帧 memcpy）
    pedestrian::yuv420sp_to_rgb_letterbox(
        vi_yuv, lbp, (uint8_t *)rknn_app_ctx.input_mems[0]->virt_addr);
#endif
    // VI 缓冲尽早归还（u32Depth=2，晚释放会挤掉下一帧）
    s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
    if (s32Ret != RK_SUCCESS) RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
    const uint64_t t_prep = nowUs();

    // ---- NPU 推理（置信度阈值来自运行时配置 conf_threshold）----
    const int infer_ret =
        inference_yolov5_model(&rknn_app_ctx, &od_results, riskCfg.conf_threshold);
    if (infer_ret != 0) {
      RK_LOGE("inference_yolov5_model fail ret=%d", infer_ret);
      od_results.count = 0;
    }
    const uint64_t t_infer = nowUs();

    // ---- 行人空间网络：只取 target_classes，提取 IPM 地面米制脚点 ----
    pedestrian::RiskLevel crowd = pedestrian::RISK_SAFE;
    int featN = 0;
    int featClusters = 0;   // DBSCAN 簇数（OSD 用；不影响决策等级）
    double featRho = 0.0, featGcc = 0.0, featFiedler = 0.0;
    {
      std::vector<std::pair<float,float> > groundPts;
      groundPts.reserve((size_t)od_results.count);
      for (int i = 0; i < od_results.count; i++) {
        object_detect_result *dres = &(od_results.results[i]);
        if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(),
                      dres->cls_id) == riskCfg.target_classes.end()) continue;
        {
          const std::map<int,float>::const_iterator cit =
              riskCfg.class_conf_thresholds.find(dres->cls_id);
          if (cit != riskCfg.class_conf_thresholds.end() && dres->prop < cit->second) continue;
        }
        int px = (int)((dres->box.left + dres->box.right) / 2);
        int py = (int)dres->box.bottom;
        mapCoordinates(&px, &py);
        float gx = (float)px, gy = (float)py;
        pedestrian::mapToGround(H, gx, gy);   // 像素 -> 地面米（与 RiskEngine 同一 IPM）
        groundPts.push_back(std::make_pair(gx, gy));
      }
      const pedestrian::EnhancedResult er = enhancedDecision.processFrame(groundPts);
      crowd       = static_cast<pedestrian::RiskLevel>(er.level);
      featN       = er.n;
      featRho     = er.density;
      featGcc     = er.r_gcc;
      featFiedler = er.fiedler;
      // DBSCAN 簇计数（eps=配置 edge_dist_m，min_samples=2，与 Python 端
      // src/algorithms/crowd_dbscan.py 默认一致）：仅用于 OSD 显示。
      {
        CrowdDBSCAN dbscan(riskCfg.edge_dist_m, 2);   // crowd_dbscan.h 在全局命名空间
        dbscan.setPoints(groundPts);
        const CrowdClusterFeatures cfeat = dbscan.compute();
        featClusters = cfeat.n_clusters;
      }
#if BOARD_VERBOSE_DETECT
      printf("[EnhancedDecision] level=%s n=%d rho=%.2f r_gcc=%.2f fiedler=%.3f\n",
             RiskEngine::levelName(crowd), featN, featRho, featGcc, featFiedler);
#endif
    }

    // 经典决策旁路：默认关闭（enable_risk_engine_fallback=false）。
    if (riskCfg.enable_risk_engine_fallback) {
      riskEngine.clear();
      for (int i = 0; i < od_results.count; i++) {
        object_detect_result *dres = &(od_results.results[i]);
        if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(),
                      dres->cls_id) != riskCfg.target_classes.end()) {
          int px = (int)((dres->box.left + dres->box.right) / 2);
          int py = (int)dres->box.bottom;
          mapCoordinates(&px, &py);
          riskEngine.addPedestrian(px, py);
        }
      }
      riskEngine.computeFeatures();
      riskEngine.evaluate();
    }
    const uint64_t t_decide = nowUs();

    // ---- 绘制：目标框 / ROI / OSD ----
    bool roiPolyLogged = false;
    int dangerPersons = 0;   // 当前帧检测框中心落入 ROI 的目标数（OSD Dangerous Person）
    for (int i = 0; i < od_results.count; i++) {
      object_detect_result *det_result = &(od_results.results[i]);
      if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(),
                    det_result->cls_id) == riskCfg.target_classes.end())
        continue;
      {
        sX = (int)(det_result->box.left);
        sY = (int)(det_result->box.top);
        eX = (int)(det_result->box.right);
        eY = (int)(det_result->box.bottom);
        mapCoordinates(&sX, &sY);
        mapCoordinates(&eX, &eY);

#if BOARD_VERBOSE_DETECT
        printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
               sX, sY, eX, eY, det_result->prop);
#endif

        // blue box + label
        cv::Scalar box_color(255, 0, 0);
        cv::rectangle(frame, cv::Point(sX, sY), cv::Point(eX, eY), box_color, 2);
        char label[32];
        snprintf(label, sizeof label, "person %.2f", det_result->prop);
        int baseline = 0;
        cv::Size lbsz = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.45, 1, &baseline);
        cv::rectangle(frame, cv::Point(sX, sY - lbsz.height - 6),
                      cv::Point(sX + lbsz.width + 6, sY), box_color, -1);
        cv::putText(frame, label, cv::Point(sX + 3, sY - 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        int fx = (sX + eX) / 2, fy = eY;
        cv::circle(frame, cv::Point(fx, fy), 3, cv::Scalar(0, 255, 0), -1);

        // 闯入检测：检测框中心点是否落在 ROI 多边形内
        if (riskCfg.roi.enabled && !riskCfg.roi.polygon.empty()) {
          int cx = (sX + eX) / 2, cy = (sY + eY) / 2;
#if BOARD_VERBOSE_DETECT
          if (!roiPolyLogged) {
            fprintf(stderr, "[ROI] polygon(%d):", (int)riskCfg.roi.polygon.size());
            for (size_t v = 0; v < riskCfg.roi.polygon.size(); ++v)
              fprintf(stderr, " (%d,%d)", riskCfg.roi.polygon[v].first,
                      riskCfg.roi.polygon[v].second);
            fprintf(stderr, "\n");
            roiPolyLogged = true;
          }
          fprintf(stderr, "[ROI] box#%d center=(%d,%d) inside=%d\n",
                  i, cx, cy, (int)pointInPolygonRoi(cx, cy, riskCfg.roi.polygon));
#endif
          const bool inside = pointInPolygonRoi(cx, cy, riskCfg.roi.polygon);
          if (inside) {
            ++dangerPersons;
            cv::rectangle(frame, cv::Point(sX - 3, sY - 3), cv::Point(eX + 3, eY + 3),
                          cv::Scalar(0, 0, 255), 3);
            cv::putText(frame, "INTRUSION", cv::Point(sX, sY - lbsz.height - 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
          }
        }
      }
    }
    (void)roiPolyLogged;

    // ---- ROI 危险区域绘制 ----
    if (riskCfg.roi.enabled && !riskCfg.roi.polygon.empty()) {
      int nv = (int)riskCfg.roi.polygon.size();
      std::vector<cv::Point> pts;
      pts.reserve((size_t)nv);
      for (int k = 0; k < nv; ++k)
        pts.push_back(cv::Point(riskCfg.roi.polygon[k].first, riskCfg.roi.polygon[k].second));
      // 颜色/线宽走配置（risk_config.h 已解析 roi_zone.color，旧实现硬编码亮青）。
      // 默认 config：color [0,0,255] -> B=0,G=0,R=255 = 红色，与参考视频一致。
      cv::polylines(frame, pts, true,
                    cv::Scalar(riskCfg.roi.color_b, riskCfg.roi.color_g, riskCfg.roi.color_r),
                    riskCfg.roi.thickness, cv::LINE_AA);
    }

    // ---- OSD: Num of Person / Danger Level / Dangerous Person / Clusters ----
    {
      cv::Scalar osd_green(0, 255, 0);
      cv::Scalar osd_yellow(0, 255, 255);
      cv::Scalar osd_red(0, 0, 255);
      cv::Scalar lvl_color = osd_green;
      const char* lvl = RiskEngine::levelName(crowd);
      if (crowd == pedestrian::RISK_WARNING)      lvl_color = osd_yellow;
      else if (crowd == pedestrian::RISK_DANGER)  lvl_color = osd_red;

      char line1[64];
      snprintf(line1, sizeof line1, "Num of Person: %d", featN);
      cv::putText(frame, line1, cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                  osd_green, 1, cv::LINE_AA);

      char line2[64];
      snprintf(line2, sizeof line2, "Danger Level: %s", lvl);
      cv::putText(frame, line2, cv::Point(8, 42), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                  lvl_color, 1, cv::LINE_AA);

      // 第三行 Dangerous Person：本帧检测框中心落在 ROI 内的目标数（对齐参考作品 OSD）
      char line3[64];
      snprintf(line3, sizeof line3, "Dangerous Person: %d", dangerPersons);
      cv::putText(frame, line3, cv::Point(8, 64), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                  osd_red, 1, cv::LINE_AA);

      // 第四行 Clusters（DBSCAN 簇数）与 Assembly 占比并列（对齐参考作品）
      char line4[96];
      snprintf(line4, sizeof line4, "Clusters: %d  Assembly: %.0f%%", featClusters,
               (featRho + featGcc) / 2.0 * 100.0);
      cv::putText(frame, line4, cv::Point(8, 86), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                  osd_yellow, 1, cv::LINE_AA);
    }
    // compact Chinese status (bottom-right, small dark bg)
    {
      const char *cn_lvl = "\xe5\xae\x89\xe5\x85\xa8";
      int cn_color_r = 0, cn_color_g = 255, cn_color_b = 0;
      if (crowd == pedestrian::RISK_WARNING) {
        cn_lvl = "\xe8\xad\xa6\xe5\x91\x8a"; cn_color_r = 0; cn_color_g = 255; cn_color_b = 255;
      } else if (crowd == pedestrian::RISK_DANGER) {
        cn_lvl = "\xe5\x8d\xb1\xe9\x99\xa9"; cn_color_r = 0; cn_color_g = 0; cn_color_b = 255;
      }
      char cn_line[64];
      snprintf(cn_line, sizeof cn_line, "%s %d\xe4\xba\xba", cn_lvl, featN);
      int cn_w = ((int)strlen(cn_line) / 3) * (24 + 2) + 8;
      int cn_x = width - cn_w - 6;
      static cv::Mat cn_bg_dark;   // 复用：避免每帧 malloc
      cv::Mat cn_bg = frame(cv::Rect(cn_x, height - 40, cn_w, 30));
      cn_bg.copyTo(cn_bg_dark);
      cn_bg_dark *= 0.35;
      cn_bg_dark.copyTo(cn_bg);
      draw_cn(frame, cn_line, cn_x + 2, height - 35,
              cv::Scalar(cn_color_b, cn_color_g, cn_color_r));
    }
    const uint64_t t_draw = nowUs();

    // ---- ARM/DMA 缓存一致性：回写 CPU 画好的帧，VENC 硬件 DMA 才能读到 ----
    // 旧实现的 memcpy(data, frame.data, ...) 是"data 就是 frame.data"的同地址自拷贝，
    // 对缓存一致性毫无作用；正确做法是 MMZ/SYS 缓存回写。缺失它会让编码器读到旧像素
    // （扫描线/撕裂/花屏）——与 VENC 多 pack 是撕裂的两个独立根因。
    s32Ret = RK_MPI_SYS_MmzFlushCache(src_Blk, RK_FALSE);   // RK_FALSE = 写回（非只读）
    if (s32Ret != RK_SUCCESS) RK_LOGE("RK_MPI_SYS_MmzFlushCache fail %x", s32Ret);

    // ---- 编码 + 推流（有限超时，多 pack 全发）----
    s32Ret = RK_MPI_VENC_SendFrame(0, &h264_frame, kVencTimeoutMs);
    if (s32Ret != RK_SUCCESS) {
      if (++venc_fail >= kMaxConsecFail) {
        RK_LOGE("[FATAL] VENC SendFrame 连续失败 %d 次 (ret=%x)，退出交由 systemd 重启",
                venc_fail, s32Ret);
        break;
      }
    } else {
      s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, kVencTimeoutMs);
      if (s32Ret == RK_SUCCESS) {
        venc_fail = 0;
        if (g_rtsplive && g_rtsp_session) {
          RK_U32 packCount = stFrame.u32PackCount;
          if (packCount > VENC_MAX_PACKS) packCount = VENC_MAX_PACKS;
          for (RK_U32 pk = 0; pk < packCount; ++pk) {
            if (stFrame.pstPack[pk].u32Len == 0) continue;
            void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack[pk].pMbBlk);
            rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, stFrame.pstPack[pk].u32Len,
                          stFrame.pstPack[pk].u64PTS);
          }
          rtsp_do_event(g_rtsplive);
        }
        s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
        if (s32Ret != RK_SUCCESS) RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
      } else if (++venc_fail >= kMaxConsecFail) {
        RK_LOGE("[FATAL] VENC GetStream 连续失败 %d 次 (ret=%x)，退出交由 systemd 重启",
                venc_fail, s32Ret);
        break;
      }
    }
    const uint64_t t_enc = nowUs();

    // ---- 分阶段耗时累加 + 1Hz 心跳（兼作看门狗存活心跳）----
    acc.vi_us     += t_vi - t_frame;
    acc.prep_us   += t_prep - t_vi;
    acc.infer_us  += t_infer - t_prep;
    acc.decide_us += t_decide - t_infer;
    acc.draw_us   += t_draw - t_decide;
    acc.enc_us    += t_enc - t_draw;
    ++acc.frames;
#if BOARD_PERF_HEARTBEAT
    if (t_enc - acc.window_start_us >= 1000000ull) {
      const double win_s = (double)(t_enc - acc.window_start_us) / 1000000.0;
      const int n = acc.frames > 0 ? acc.frames : 1;
      printf("[PERF] fps=%.2f frames=%d persons=%d level=%s vi=%.1f prep=%.1f "
             "infer=%.1f decide=%.1f draw=%.1f enc=%.1f | per-frame ms: "
             "vi=%.2f prep=%.2f infer=%.2f decide=%.2f draw=%.2f enc=%.2f\n",
             acc.frames / win_s, acc.frames, featN, RiskEngine::levelName(crowd),
             acc.vi_us / 1000.0, acc.prep_us / 1000.0, acc.infer_us / 1000.0,
             acc.decide_us / 1000.0, acc.draw_us / 1000.0, acc.enc_us / 1000.0,
             acc.vi_us / 1000.0 / n, acc.prep_us / 1000.0 / n, acc.infer_us / 1000.0 / n,
             acc.decide_us / 1000.0 / n, acc.draw_us / 1000.0 / n, acc.enc_us / 1000.0 / n);
      fflush(stdout);   // 心跳必须立即落盘：看门狗按文件 mtime 判活
      acc = StageAccum();
      acc.window_start_us = t_enc;
    }
#endif
  }

  // ---- 正常退出：安全清理所有已初始化资源 ----
  pedestrian::letterbox_params_free(&lbp);
  cleanup();
  return 0;
}
