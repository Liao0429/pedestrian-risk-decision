/*****************************************************************************
* | Author      :   Luckfox team
* | Function    :   
* | Info        :
*
*----------------
* | This version:   V1.1
* | Date        :   2024-08-26
* | Info        :   Basic version
*
******************************************************************************/

#include "luckfox_mpi.h"

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

int vi_dev_init() {
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			// 错误路径回滚：Dev 已 Enable，不 Disable 会残留设备句柄
			RK_MPI_VI_DisableDev(devId);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int vi_chn_init(int channelId, int width, int height) {
	int ret;
	int buf_cnt = 2;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	// 旧实现用 ret |= ... 合并两步返回值：SetChnAttr 失败但 EnableChn 成功时错误码
	// 被掩盖、且已使能的通道无人 Disable（资源泄漏）；两个错误码位或还可能互相抵消。
	// 改为逐步检查 + 失败回滚。
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	if (ret != RK_SUCCESS) {
		printf("ERROR: RK_MPI_VI_SetChnAttr ret=%x\n", ret);
		return -1;
	}
	ret = RK_MPI_VI_EnableChn(0, channelId);
	if (ret != RK_SUCCESS) {
		printf("ERROR: RK_MPI_VI_EnableChn ret=%x\n", ret);
		RK_MPI_VI_DisableChn(0, channelId);
		return -1;
	}

	return 0;
}

int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
	printf("%s\n",__func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	if (enType == RK_VIDEO_ID_AVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH264Cbr.u32Gop = 1;
	} else if (enType == RK_VIDEO_ID_HEVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		stAttr.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_MJPEG) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		stAttr.stRcAttr.stMjpegCbr.u32BitRate = 10 * 1024;
	}

	stAttr.stVencAttr.enType = enType;
	// 帧缓冲是 OpenCV BGR 字节序（B,G,R）。旧值 RK_FMT_RGB888 按内存字节序 R,G,B
	// 解释，导致整条 RTSP 流 R/B 互换（2026-10-30 抓帧实测：画黄显青、画红显蓝）。
	// 历史上未暴露是因为旧链路的 cvtColor NV21 别名错误恰好与之抵消；该错误修复后
	// 必须用 BGR888 才能让流内场景与叠加层颜色所见即所得。
	stAttr.stVencAttr.enPixelFormat = RK_FMT_BGR888;
	if (enType == RK_VIDEO_ID_AVC)
		stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	stAttr.stVencAttr.enMirror = MIRROR_NONE;

	RK_S32 venc_ret = RK_MPI_VENC_CreateChn(chnId, &stAttr);
	if (venc_ret != RK_SUCCESS) {
		printf("RK_MPI_VENC_CreateChn %x\n", venc_ret);
		return -1;
	}

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	venc_ret = RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);
	if (venc_ret != RK_SUCCESS) {
		printf("RK_MPI_VENC_StartRecvFrame %x\n", venc_ret);
		// 错误路径回滚：通道已 Create，不 Destroy 会残留（restart 后 CreateChn 失败）
		RK_MPI_VENC_DestroyChn(chnId);
		return -1;
	}

	return 0;
}