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
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "yolov5.h"
#include "risk_engine.h"
#include "risk_config.h"
#include "enhanced_decision.h"
#include "ipm_transform.h"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#define DISP_WIDTH  720
#define DISP_HEIGHT 480

// disp size
int width    = DISP_WIDTH;
int height   = DISP_HEIGHT;

// model size
int model_width = 640;
int model_height = 640;	
float scale ;
int leftPadding ;
int topPadding  ;

cv::Mat letterbox(cv::Mat input)
{
	float scaleX = (float)model_width  / (float)width; 
	float scaleY = (float)model_height / (float)height; 
	scale = scaleX < scaleY ? scaleX : scaleY;
	
	int inputWidth   = (int)((float)width * scale);
	int inputHeight  = (int)((float)height * scale);

	leftPadding = (model_width  - inputWidth) / 2;
	topPadding  = (model_height - inputHeight) / 2;	
	

	cv::Mat inputScale;
    cv::resize(input, inputScale, cv::Size(inputWidth,inputHeight), 0, 0, cv::INTER_LINEAR);	
	cv::Mat letterboxImage(640, 640, CV_8UC3,cv::Scalar(0, 0, 0));
    cv::Rect roi(leftPadding, topPadding, inputWidth, inputHeight);
    inputScale.copyTo(letterboxImage(roi));

	return letterboxImage; 	
}

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
  	while (!g_stop)
	{	
		// get vi frame
		h264_frame.stVFrame.u32TimeRef = H264_TimeRef++;
		h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs(); 
		s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, -1);
		if(s32Ret == RK_SUCCESS)
		{
			void *vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);	

			cv::Mat yuv420sp(height + height / 2, width, CV_8UC1, vi_data);
			cv::Mat bgr(height, width, CV_8UC3, data);			
			
			cv::cvtColor(yuv420sp, bgr, cv::COLOR_YUV420sp2BGR);
			cv::resize(bgr, frame, cv::Size(width ,height), 0, 0, cv::INTER_LINEAR);
			
			//letterbox
			cv::Mat letterboxImage = letterbox(frame);	
			memcpy(rknn_app_ctx.input_mems[0]->virt_addr, letterboxImage.data, model_width*model_height*3);		
			inference_yolov5_model(&rknn_app_ctx, &od_results);

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
				// 仅绘制 target_classes 中的类别（默认只画 person），贴合毕设人群分析主题
				if (std::find(riskCfg.target_classes.begin(), riskCfg.target_classes.end(),
				              det_result->cls_id) == riskCfg.target_classes.end())
					continue;
				{
					sX = (int)(det_result->box.left   );	
					sY = (int)(det_result->box.top 	  );	
					eX = (int)(det_result->box.right  );	
					eY = (int)(det_result->box.bottom );
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
				cv::rectangle(frame, cv::Point(0,0), cv::Point(width, 30), color, -1);
				cv::putText(frame, info, cv::Point(6, 22),
							cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,0,0), 2);
			}

		}
		// ---- 取帧成功后才编码/推流/释放（失败时跳过本帧，避免陈旧帧编码与错误释放）----
		if (s32Ret == RK_SUCCESS)
		{
			memcpy(data, frame.data, width * height * 3);

			// encode H264
			RK_MPI_VENC_SendFrame(0, &h264_frame,-1);

			// rtsp
			s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);
			if(s32Ret == RK_SUCCESS)
			{
				if(g_rtsplive && g_rtsp_session)
				{
					//printf("len = %d PTS = %d \n",stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
					void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
					rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, stFrame.pstPack->u32Len,
								  stFrame.pstPack->u64PTS);
					rtsp_do_event(g_rtsplive);
				}
				s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
				if (s32Ret != RK_SUCCESS) {
					RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
				}
			}

			// release VI frame（成功取帧后必须释放）
			s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
			}
		}
		memset(text, 0, sizeof text);
	}
	// ---- 正常退出：安全清理所有已初始化资源 ----
	cleanup();
	return 0;
}
