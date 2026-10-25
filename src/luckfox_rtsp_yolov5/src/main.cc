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
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "yolov5.h"
#include "risk_engine.h"
#include "risk_config.h"
#include "enhanced_decision.h"

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

// 将原图像素坐标经 IPM 单应矩阵 H 映射到地面米制坐标（与 RiskEngine::mapToGround 一致）
void mapToGroundPixel(const cv::Mat& H, float& x, float& y) {
    double wx = H.at<double>(0,0) * x + H.at<double>(0,1) * y + H.at<double>(0,2);
    double wy = H.at<double>(1,0) * x + H.at<double>(1,1) * y + H.at<double>(1,2);
    double w  = H.at<double>(2,0) * x + H.at<double>(2,1) * y + H.at<double>(2,2);
    if (std::fabs(w) > 1e-6) {
        x = (float)(wx / w);
        y = (float)(wy / w);
    }
}


int main(int argc, char *argv[]) {
  system("RkLunch-stop.sh");

  // ==== 风险决策引擎 + 逆透视变换(IPM)标定 ====
  // 画面 720x480，源四边形取画面底部地面梯形(近大远小)，映射到地面 6m x 4m
  cv::Point2f srcPts[4] = { cv::Point2f(0,480), cv::Point2f(720,480),
                            cv::Point2f(560,300), cv::Point2f(160,300) };
  cv::Point2f dstPts[4] = { cv::Point2f(-3,3), cv::Point2f(3,3),
                            cv::Point2f(3,-1), cv::Point2f(-3,-1) };
  cv::Mat H = cv::getPerspectiveTransform(srcPts, dstPts);
  RiskEngine riskEngine;
  riskEngine.setHomography(H);
  // 运行时加载阈值配置（默认 ./config/default_config.json，缺失时回退到默认值）
  pedestrian::RiskConfig riskCfg =
      pedestrian::loadRiskConfigOr("./config/default_config.json", pedestrian::RiskConfig());
  riskEngine.setConfig(riskCfg);
#ifdef USE_ENHANCED_DECISION
  pedestrian::EnhancedConfig enhancedCfg;
  enhancedCfg.edge_dist_m = riskCfg.edge_dist_m;
  pedestrian::EnhancedDecision enhancedDecision(enhancedCfg);  // 增强决策模块
#endif
	RK_S32 s32Ret = 0; 
	int sX,sY,eX,eY; 
		
	// Rknn model
	char text[16];
	rknn_app_context_t rknn_app_ctx;	
	object_detect_result_list od_results;
    int ret;
	const char *model_path = "./model/yolov5.rknn";
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));	
	init_yolov5_model(model_path, &rknn_app_ctx);
	printf("init rknn model success!\n");
	init_post_process();

	//h264_frame	
	VENC_STREAM_S stFrame;	
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	RK_U64 H264_PTS = 0;
	RK_U32 H264_TimeRef = 0; 
	VIDEO_FRAME_INFO_S stViFrame;
	
	// Create Pool
	MB_POOL_CONFIG_S PoolCfg;
	memset(&PoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
	PoolCfg.u64MBSize = width * height * 3 ;
	PoolCfg.u32MBCnt = 1;
	PoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
	//PoolCfg.bPreAlloc = RK_FALSE;
	MB_POOL src_Pool = RK_MPI_MB_CreatePool(&PoolCfg);
	printf("Create Pool success !\n");	

	// Get MB from Pool 
	MB_BLK src_Blk = RK_MPI_MB_GetMB(src_Pool, width * height * 3, RK_TRUE);
	
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
	SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
	SAMPLE_COMM_ISP_Run(0);

	// rkmpi init
	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		return -1;
	}

	// rtsp init	
	rtsp_demo_handle g_rtsplive = NULL;
	rtsp_session_handle g_rtsp_session;
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
	
	// vi init
	vi_dev_init();
	vi_chn_init(0, width, height);

	// venc init
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	venc_init(0, width, height, enCodecType);

	printf("venc init success\n");	
	
  	while(1)
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
			CrowdLevel crowd = CROWD_SAFE;
			int featN = 0;
			double featRho = 0.0, featGcc = 0.0, featFiedler = 0.0;
#ifdef USE_ENHANCED_DECISION
			// 增强决策：IPM 地面米制脚点 -> Kalman 平滑 -> 谱特征 -> FSM 稳定等级
			{
				std::vector<std::pair<float,float>> groundPts;
				groundPts.reserve((size_t)od_results.count);
				for(int i = 0; i < od_results.count; i++) {
					object_detect_result *dres = &(od_results.results[i]);
					if (dres->cls_id != 0) continue; // COCO person = 0
					int px = (int)((dres->box.left + dres->box.right) / 2);
					int py = (int)dres->box.bottom;
					mapCoordinates(&px, &py);
					float gx = (float)px, gy = (float)py;
					mapToGroundPixel(H, gx, gy);   // 像素 -> 地面米
					groundPts.push_back(std::make_pair(gx, gy));
				}
				const pedestrian::EnhancedResult er = enhancedDecision.processFrame(groundPts);
				crowd     = (CrowdLevel)er.level;
				featN     = er.n;
				featRho   = er.density;
				featGcc   = er.r_gcc;
				featFiedler = er.fiedler;
				printf("[EnhancedDecision] level=%s n=%d rho=%.2f r_gcc=%.2f fiedler=%.3f\n",
				       RiskEngine::levelName(crowd), featN, featRho, featGcc, featFiedler);
			}
#else
			// 经典决策：RiskEngine 固定阈值规则（保留原有逻辑作为默认路径）
			riskEngine.clear();
			for(int i = 0; i < od_results.count; i++) {
				object_detect_result *dres = &(od_results.results[i]);
				if (dres->cls_id == 0) { // COCO person = 0
					int px = (int)((dres->box.left + dres->box.right) / 2);
					int py = (int)dres->box.bottom;
					mapCoordinates(&px, &py);
					riskEngine.addPedestrian(px, py);
				}
			}
			riskEngine.computeFeatures();
			crowd   = riskEngine.evaluate();
			featN   = riskEngine.features().n;
			featRho = riskEngine.features().density;
			featGcc = riskEngine.features().r_gcc;
#endif

			for(int i = 0; i < od_results.count; i++)
			{					
				if(od_results.count >= 1)
				{
					object_detect_result *det_result = &(od_results.results[i]);
	
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
					sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
					cv::putText(frame,text,cv::Point(sX, sY - 8),
										   cv::FONT_HERSHEY_SIMPLEX,1,
										   cv::Scalar(0,255,0),2);
				}
			}

			// ---- 叠加风险等级到画面 ----
			{
				cv::Scalar color(0,255,0);
				const char* lvl = RiskEngine::levelName(crowd);
				if (crowd == CROWD_WARNING)      color = cv::Scalar(0,255,255);
				else if (crowd == CROWD_DANGER)  color = cv::Scalar(0,0,255);
				char info[128];
#ifdef USE_ENHANCED_DECISION
				sprintf(info, "CROWD:%s n=%d rho=%.2f r_gcc=%.2f lambda2=%.3f",
				        lvl, featN, featRho, featGcc, featFiedler);
#else
				sprintf(info, "CROWD:%s n=%d rho=%.2f r_gcc=%.2f",
				        lvl, featN, featRho, featGcc);
#endif
				cv::rectangle(frame, cv::Point(0,0), cv::Point(width, 60), color, -1);
				cv::putText(frame, info, cv::Point(10, 42),
							cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0,0,0), 2);
			}

		}
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
		}

		// release frame 
		s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail %x", s32Ret);
		}
		s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
		if (s32Ret != RK_SUCCESS) {
			RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
		}
		memset(text,0,8);
	}


	// Destory MB
	RK_MPI_MB_ReleaseMB(src_Blk);
	// Destory Pool
	RK_MPI_MB_DestroyPool(src_Pool);
	
	RK_MPI_VI_DisableChn(0, 0);
	RK_MPI_VI_DisableDev(0);

	SAMPLE_COMM_ISP_Stop(0);
	
	RK_MPI_VENC_StopRecvFrame(0);
	RK_MPI_VENC_DestroyChn(0);

	free(stFrame.pstPack);

	if (g_rtsplive)
		rtsp_del_demo(g_rtsplive);
	
	RK_MPI_SYS_Exit();

	// Release rknn model
    release_yolov5_model(&rknn_app_ctx);		
	deinit_post_process();
	
	return 0;
}
