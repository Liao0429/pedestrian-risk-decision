#ifndef PEDESTRIAN_IPM_TRANSFORM_H
#define PEDESTRIAN_IPM_TRANSFORM_H

// =============================================================================
// ipm_transform.h — 统一 IPM 单应变换（像素坐标 -> 地面米制坐标）
// -----------------------------------------------------------------------------
// 原先 main.cc 的 mapToGroundPixel() 与 RiskEngine::mapToGround() 各有一份重复
// 实现，现收敛到本公共 helper，消除第三份拷贝。两处调用共享同一份矩阵运算。
//
// 依赖：OpenCV core（cv::Mat），C++11。
// =============================================================================

#include <cmath>

#include <opencv2/core/core.hpp>

namespace pedestrian {

// 将图像像素坐标 (x,y) 经单应矩阵 H 映射到地面米制坐标。
// 与既有实现行为一致：w 接近 0 时保持原坐标不变。
inline void mapToGround(const cv::Mat& H, float& x, float& y) {
    if (H.empty()) return;
    const double wx = H.at<double>(0,0) * x + H.at<double>(0,1) * y + H.at<double>(0,2);
    const double wy = H.at<double>(1,0) * x + H.at<double>(1,1) * y + H.at<double>(1,2);
    const double w  = H.at<double>(2,0) * x + H.at<double>(2,1) * y + H.at<double>(2,2);
    if (std::fabs(w) > 1e-6) {
        x = static_cast<float>(wx / w);
        y = static_cast<float>(wy / w);
    }
}

} // namespace pedestrian

#endif // PEDESTRIAN_IPM_TRANSFORM_H