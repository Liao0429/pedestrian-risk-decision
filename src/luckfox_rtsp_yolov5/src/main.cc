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
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "yolov5.h"
#include "risk_engine.h"
#include "risk_config.h"
#include "enhanced_decision.h"
#include "preprocess.h"
#include "ipm_transform.h"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#define DISP_WIDTH  720
#define DISP_HEIGHT 480

// ---- 主循环看门狗与有限超时配置 ----
// 所有驱动调用使用有限超时（毫秒），避免 -1 无限阻塞导致进程假死（SIGTERM
// 无法及时退出、外部看门狗无从自愈）。正常板端帧率约 6.9FPS（约 145ms/帧），
// 1s 超时不会误伤正常取帧，仅在通道停滞时触发。
#define VI_GET_FRAME_TIMEOUT_MS    1000   // 单帧取帧超时
#define VENC_SEND_FRAME_TIMEOUT_MS 1000   // 编码输入超时
#define VENC_GET_STREAM_TIMEOUT_MS 1000   // 编码输出超时
#define WATCHDOG_MAX_CONSEC_VI_FAIL 30    // 连续取帧失败 N 次判定主链路挂死（约 30s），主动退出交由 systemd 重启
#define WATCHDOG_LOG_EVERY_N_FAIL   10    // 失败日志节流：每 N 次打印一次，避免刷屏
#define RTSP_CONSEC_TX_FAIL_WARN    60    // 连续推流失败 N 次打印汇总告警（非致命，等待新客户端连接）

// ---- 主循环耗时打点（P1-6）：定位 ~6.5FPS 瓶颈的耗时分布 ----
// 每 PROFILE_LOG_EVERY_N_FRAMES 帧打印一次各阶段平均耗时；不改变任何业务逻辑。
#define PROFILE_LOG_EVERY_N_FRAMES  90    // 每 90 帧打印一次（约 13s @6.9FPS）
struct ProfileTiming {
    unsigned long long frames   = 0;
    unsigned long long us_vi_get    = 0;  // RK_MPI_VI_GetChnFrame 取帧
    unsigned long long us_cvt_resize= 0;  // YUV420sp->BGR + resize
    unsigned long long us_letterbox = 0;  // letterbox + BGR2RGB + memcpy 送显存
    unsigned long long us_rknn      = 0;  // RKNN 推理
    unsigned long long us_decision  = 0;  // 脚点提取 + IPM + Kalman + 谱特征 + FSM
    unsigned long long us_draw      = 0;  // 画框 + 等级叠加
    unsigned long long us_venc_send = 0;  // 编码前 BGR2RGB + VENC_SendFrame
    unsigned long long us_venc_get  = 0;  // VENC_GetStream + RTSP 推流 + Release
    unsigned long long us_total     = 0;  // 整帧（含 VI 取帧）
};
static ProfileTiming g_prof;

static inline unsigned long long profileNowUs() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void profileReport() {
    if (g_prof.frames == 0) return;
    const double f = (double)g_prof.frames;
    printf("[PROFILE] frames=%llu avg_ms: vi_get=%.2f cvt_resize=%.2f letterbox=%.2f rknn=%.2f decision=%.2f draw=%.2f venc_send=%.2f venc_get_rtsp=%.2f total=%.2f fps=%.2f\n",
           g_prof.frames,
           g_prof.us_vi_get/1000.0/f, g_prof.us_cvt_resize/1000.0/f, g_prof.us_letterbox/1000.0/f,
           g_prof.us_rknn/1000.0/f, g_prof.us_decision/1000.0/f, g_prof.us_draw/1000.0/f,
           g_prof.us_venc_send/1000.0/f, g_prof.us_venc_get/1000.0/f, g_prof.us_total/1000.0/f,
           f / ((double)g_prof.us_total / 1000000.0));
    g_prof = ProfileTiming();
}

// disp size
int width    = DISP_WIDTH;
int height   = DISP_HEIGHT;

// model size
int model_width = 640;
int model_height = 640;	
float scale ;
int leftPadding ;
int topPadding  ;

// letterbox 预计算参数（阶段八：720x480 -> 640x640，几何与旧 letterbox() 一致）
pedestrian::LetterboxParams g_lb_params;

void mapCoordinates(int *x, int *y) {	
	int mx = *x - leftPadding;
	int my = *y - topPadding;

    *x = (int)((float)mx / scale);
    *y = (int)((float)my / scale);
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
  pedestrian::EnhancedDecision enhancedDecision(enhancedCfg);

  // ---- 资源句柄与初始化状态（任一关键初始化失败时安全清理） ----
    RK_S32 s32Ret = 0;
    int sX, sY, eX, eY;
    int ret = 0;
    char text[32];
    rknn_app_context_t rknn_app_ctx;
    object_detect_result_list od_results;
    // 图像预处理 letterbox 预计算参数（阶段八：NV12 直接采样生成 RGB letterbox）
    pedestrian::LetterboxParams g_lb_params;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

  VENC_STREAM_S stFrame;
  memset(&stFrame, 0, sizeof(stFrame));
  stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
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
  bool frame_pack_ready = (stFrame.pstPack != NULL);
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
    if (frame_pack_ready) free(stFrame.pstPack);
    if (rtsp_ready && g_rtsplive) rtsp_del_demo(g_rtsplive);
    if (sys_ready)       RK_MPI_SYS_Exit();
    if (rknn_ready)      release_yolov5_model(&rknn_app_ctx);
        if (postproc_ready)  deinit_post_process();
        pedestrian::letterbox_params_free(&g_lb_params);
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

    // ---- 图像预处理参数预计算（P1-6 阶段八）----
    // 模型输入尺寸以 RKNN 实际查询为准（不再是硬编码 640），letterbox 几何与
    // 坐标映射表只计算一次，每帧仅执行定点插值。旧实现每帧重复 resize+颜色转换
    // 共约 136ms（占帧间隔 85%），本模块合并为单次遍历并修复 NV12 颜色语义。
    model_width  = rknn_app_ctx.model_width;
    model_height = rknn_app_ctx.model_height;
    if (pedestrian::letterbox_prepare(width, height, model_width, model_height,
                                      &g_lb_params) != 0) {
      fprintf(stderr, "[FATAL] letterbox_prepare 失败\n");
      cleanup();
      return -1;
    }
    scale        = (float)g_lb_params.roi_w / (float)width;
    leftPadding  = g_lb_params.left_pad;
    topPadding   = g_lb_params.top_pad;
    printf("[PREPROCESS] model=%dx%d roi=%dx%d pads=(%d,%d) scale=%.6f\n",
           model_width, model_height, g_lb_params.roi_w, g_lb_params.roi_h,
           g_lb_params.left_pad, g_lb_params.top_pad, (double)scale);

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

    // 预计算 letterbox 参数（几何与旧 letterbox() 一致），并同步全局几何变量
    // （mapCoordinates / IPM 依赖 scale、leftPadding、topPadding）。
    if (pedestrian::letterbox_prepare(width, height, model_width, model_height,
                                      &g_lb_params) != 0) {
      fprintf(stderr, "[FATAL] letterbox_prepare 失败\n");
      cleanup();
      return -1;
    }
    scale = (float)g_lb_params.roi_w / (float)width;
    leftPadding = g_lb_params.left_pad;
    topPadding  = g_lb_params.top_pad;
    printf("[PREPROC] letterbox 几何: roi=%dx%d pad=%d,%d scale=%.4f\n",
           g_lb_params.roi_w, g_lb_params.roi_h,
           g_lb_params.left_pad, g_lb_params.top_pad, scale);
  	// ---- 主循环看门狗计数（取帧连续失败判定挂死；编码/RTSP 失败仅告警） ----
	int vi_consec_fail = 0;
	int venc_send_fail = 0;
	int venc_stream_fail = 0;
	int rtsp_consec_fail = 0;
	while (!g_stop)
	{
		const unsigned long long t_frame0 = profileNowUs();
		unsigned long long t_mark = t_frame0;
		// get vi frame
		h264_frame.stVFrame.u32TimeRef = H264_TimeRef++;
		h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs(); 
		s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, VI_GET_FRAME_TIMEOUT_MS);
		if(s32Ret == RK_SUCCESS)
		{
			vi_consec_fail = 0;
			void *vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);	

			// ---- 预处理（阶段八：NV12 单遍定点/NEON，替代 OpenCV 三遍流程）----
						// 旧流程：cvtColor(YUV420sp2BGR 实为 NV21 语义，红蓝互换) + 恒等 resize
						//        + letterbox(resize) + BGR2RGB + memcpy，实测 ~136ms/帧。
						// 新流程：
						//   1) yuv420sp_to_bgr：NV12 -> BGR 全帧（正确 NV12 语义，供画框/VENC）
						//   2) yuv420sp_to_rgb_letterbox：直接从 NV12 采样生成 RKNN RGB 输入
						// 均单遍处理，无 OpenCV 中间缓冲。
						pedestrian::yuv420sp_to_bgr(static_cast<const uint8_t *>(vi_data),
						                           width, height, data);
						{
							const unsigned long long t_now = profileNowUs();
							g_prof.us_cvt_resize += t_now - t_mark;
							t_mark = t_now;
						}
						pedestrian::yuv420sp_to_rgb_letterbox(
						    static_cast<const uint8_t *>(vi_data), g_lb_params,
						    static_cast<uint8_t *>(rknn_app_ctx.input_mems[0]->virt_addr));
						{
							const unsigned long long t_now = profileNowUs();
							g_prof.us_letterbox += t_now - t_mark;
							t_mark = t_now;
						}
			{
				const unsigned long long t_now = profileNowUs();
				g_prof.us_letterbox += t_now - t_mark;
				t_mark = t_now;
			}
			inference_yolov5_model(&rknn_app_ctx, &od_results);
			{
				const unsigned long long t_now = profileNowUs();
				g_prof.us_rknn += t_now - t_mark;
				t_mark = t_now;
			}

			// ---- 行人空间网络：只取 person 类，提取脚点 ----
			pedestrian::RiskLevel crowd = pedestrian::RISK_SAFE;
			int featN = 0;
			double featRho = 0.0, featGcc = 0.0, featFiedler = 0.0;
			// 增强决策（默认启用）：IPM 地面米制脚点 -> Kalman 平滑 -> 谱特征 -> FSM 稳定等级
			{
				std::vector<std::pair<float,float>> groundPts;
				groundPts.reserve((size_t)od_results.count);
				for(int i = 0; i < od_results.count; i++) {
					object_detect_result *dres = &(od_results.results[i]);
					if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(), dres->cls_id) == riskCfg.target_classes.end()) continue; // 非目标类别跳过
					// 按类别差异化置信度阈值（可选）：若该类配置了更高阈值且当前置信度不足，精筛跳过
					{
						const std::map<int,float>::const_iterator cit = riskCfg.class_conf_thresholds.find(dres->cls_id);
						if (cit != riskCfg.class_conf_thresholds.end() && dres->prop < cit->second) continue;
					}
					int px = (int)((dres->box.left + dres->box.right) / 2);
					int py = (int)dres->box.bottom;
					mapCoordinates(&px, &py);
					float gx = (float)px, gy = (float)py;
					pedestrian::mapToGround(H, gx, gy);   // 像素 -> 地面米（与 RiskEngine 同一 IPM 实现）
					groundPts.push_back(std::make_pair(gx, gy));
				}
				const pedestrian::EnhancedResult er = enhancedDecision.processFrame(groundPts);
				crowd     = static_cast<pedestrian::RiskLevel>(er.level);
				featN     = er.n;
				featRho   = er.density;
				featGcc   = er.r_gcc;
				featFiedler = er.fiedler;
				printf("[EnhancedDecision] level=%s n=%d rho=%.2f r_gcc=%.2f fiedler=%.3f\n",
				       RiskEngine::levelName(crowd), featN, featRho, featGcc, featFiedler);
			}
			{
				const unsigned long long t_now = profileNowUs();
				g_prof.us_decision += t_now - t_mark;
				t_mark = t_now;
			}

			// 经典决策旁路：默认关闭（enable_risk_engine_fallback=false）。
			// 增强决策已计算网络特征，RiskEngine 旁路会重复 computeFeatures，
			// 仅在显式开启回退时才运行，作为对比参考，不覆盖增强决策输出。
			if (riskCfg.enable_risk_engine_fallback) {
				riskEngine.clear();
				for(int i = 0; i < od_results.count; i++) {
					object_detect_result *dres = &(od_results.results[i]);
					if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(), dres->cls_id) != riskCfg.target_classes.end()) { // 目标类别
						int px = (int)((dres->box.left + dres->box.right) / 2);
						int py = (int)dres->box.bottom;
						mapCoordinates(&px, &py);
						riskEngine.addPedestrian(px, py);
					}
				}
				riskEngine.computeFeatures();
				// 旁路保留：经典 RiskEngine 仅作为对比/回退参考，不覆盖增强决策输出
				riskEngine.evaluate();
			}

			for(int i = 0; i < od_results.count; i++)
						{
							object_detect_result *det_result = &(od_results.results[i]);
							// 只画/打印目标类别（与风险引擎 target_classes 过滤一致，避免画面框数与决策人数不一致）
							if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(), det_result->cls_id) == riskCfg.target_classes.end()
								// 与决策路径一致：若该类配置了差异化置信度阈值且当前置信度不足，也不画框
								|| (riskCfg.class_conf_thresholds.count(det_result->cls_id) > 0
									&& det_result->prop < riskCfg.class_conf_thresholds.at(det_result->cls_id)))
								continue;
							sX = (int)(det_result->box.left);
							sY = (int)(det_result->box.top);
							eX = (int)(det_result->box.right);
							eY = (int)(det_result->box.bottom);
							mapCoordinates(&sX,&sY);
							mapCoordinates(&eX,&eY);

							printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
								   sX, sY, eX, eY, det_result->prop);

							cv::rectangle(frame,cv::Point(sX ,sY),
										  cv::Point(eX ,eY),
										  cv::Scalar(0,255,0),3);
							snprintf(text, sizeof text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
							cv::putText(frame,text,cv::Point(sX, sY - 8),
										cv::FONT_HERSHEY_SIMPLEX,1,
										cv::Scalar(0,255,0),2);
						}

			// ---- 叠加风险等级到画面 ----
			{
				cv::Scalar color(0,255,0);
				const char* lvl = RiskEngine::levelName(crowd);
				if (crowd == pedestrian::RISK_WARNING)      color = cv::Scalar(0,255,255);
				else if (crowd == pedestrian::RISK_DANGER)  color = cv::Scalar(0,0,255);
				char info[128];
				snprintf(info, sizeof info, "CROWD:%s n=%d rho=%.2f r_gcc=%.2f lambda2=%.3f",
				        lvl, featN, featRho, featGcc, featFiedler);
				cv::rectangle(frame, cv::Point(0,0), cv::Point(width, 60), color, -1);
				cv::putText(frame, info, cv::Point(10, 42),
							cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0,0,0), 2);
			}
			{
				const unsigned long long t_now = profileNowUs();
				g_prof.us_draw += t_now - t_mark;
				t_mark = t_now;
			}

		}
		else
		{
			++vi_consec_fail;
			if (vi_consec_fail == 1 || vi_consec_fail % WATCHDOG_LOG_EVERY_N_FAIL == 0) {
				fprintf(stderr, "[WATCHDOG] VI 取帧失败 (ret=0x%x)，连续 %d 次（超时 %dms）\n",
				        s32Ret, vi_consec_fail, VI_GET_FRAME_TIMEOUT_MS);
			}
			if (vi_consec_fail >= WATCHDOG_MAX_CONSEC_VI_FAIL) {
				fprintf(stderr,
				        "[WATCHDOG] VI 连续取帧失败 %d 次，主链路疑似挂死，主动退出交由 systemd 看门狗重启\n",
				        vi_consec_fail);
				cleanup();
				return 1;
			}
		}
		// ---- 取帧成功后才编码/推流/释放（失败时跳过本帧，避免陈旧帧编码与错误释放）----
				if (s32Ret == RK_SUCCESS)
				{
					// VENC 声明为 RK_FMT_RGB888（见 luckfox_mpi.cc / h264_frame），
					// 而 OpenCV 帧为 BGR，编码前必须原位转换，否则 RTSP 画面红蓝互换
					// （此前 memcpy(data, frame.data, ...) 为同地址自拷贝，无实际作用）。
					cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

		// encode H264（有限超时 + 返回值检查：编码通道拥塞/停止时不再无限阻塞）
		RK_S32 send_ret = RK_MPI_VENC_SendFrame(0, &h264_frame, VENC_SEND_FRAME_TIMEOUT_MS);
		if (send_ret != RK_SUCCESS) {
			++venc_send_fail;
			if (venc_send_fail == 1 || venc_send_fail % WATCHDOG_LOG_EVERY_N_FAIL == 0) {
				fprintf(stderr, "[WARN] RK_MPI_VENC_SendFrame 失败 (ret=0x%x)，累计 %d 次\n",
				        send_ret, venc_send_fail);
			}
		} else {
			venc_send_fail = 0;
		}
		{
			const unsigned long long t_now = profileNowUs();
			g_prof.us_venc_send += t_now - t_mark;
			t_mark = t_now;
		}

		// rtsp（有限超时；发送/事件返回值不再忽略）
		RK_S32 stream_ret = RK_MPI_VENC_GetStream(0, &stFrame, VENC_GET_STREAM_TIMEOUT_MS);
		if(stream_ret == RK_SUCCESS)
		{
			if(g_rtsplive && g_rtsp_session)
			{
				void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				// 注意：Rockchip rtsp_demo 中 rtsp_tx_video 成功时返回帧长度 len（正数），
				// 失败时返回负数（如 -1）。不能以 !=0 判断失败（无客户端时也返回 len）。
				int tx_ret = rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, stFrame.pstPack->u32Len,
										   stFrame.pstPack->u64PTS);
				if (tx_ret < 0) {
					++rtsp_consec_fail;
					if (rtsp_consec_fail == 1 || rtsp_consec_fail % WATCHDOG_LOG_EVERY_N_FAIL == 0) {
						fprintf(stderr, "[RTSP] rtsp_tx_video 失败 (ret=%d)，连续 %d 次\n",
						        tx_ret, rtsp_consec_fail);
					}
					if (rtsp_consec_fail >= RTSP_CONSEC_TX_FAIL_WARN) {
						fprintf(stderr,
						        "[RTSP] 连续推流失败 %d 次（可能无客户端或网络异常），继续运行等待新连接\n",
						        rtsp_consec_fail);
						rtsp_consec_fail = 0;   // 已告警，重置计数避免重复刷屏
					}
				} else {
					rtsp_consec_fail = 0;
				}
				int ev_ret = rtsp_do_event(g_rtsplive);
				if (ev_ret < 0) {
					fprintf(stderr, "[RTSP] rtsp_do_event 返回 %d\n", ev_ret);
				}
			}
			RK_S32 rel_ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
			if (rel_ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", rel_ret);
			}
		}
		else
		{
			++venc_stream_fail;
			if (venc_stream_fail == 1 || venc_stream_fail % WATCHDOG_LOG_EVERY_N_FAIL == 0) {
				fprintf(stderr, "[WARN] RK_MPI_VENC_GetStream 失败/超时 (ret=0x%x)，累计 %d 次\n",
				        stream_ret, venc_stream_fail);
			}
		}
			// release VI frame（成功取帧后必须释放）
			s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
			}
			{
				const unsigned long long t_now = profileNowUs();
				g_prof.us_venc_get += t_now - t_mark;
				g_prof.us_total  += t_now - t_frame0;
				++g_prof.frames;
				if (g_prof.frames % PROFILE_LOG_EVERY_N_FRAMES == 0) {
					profileReport();
				}
			}
		}
		memset(text, 0, sizeof text);
	}
	// ---- 正常退出：安全清理所有已初始化资源 ----
	cleanup();
	return 0;
}
