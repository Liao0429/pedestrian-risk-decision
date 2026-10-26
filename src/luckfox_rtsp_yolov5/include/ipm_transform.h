#ifndef PEDESTRIAN_IPM_TRANSFORM_H
#define PEDESTRIAN_IPM_TRANSFORM_H

// =============================================================================
// ipm_transform.h — 统一 IPM 单应变换（像素坐标 -> 地面米制坐标）
// -----------------------------------------------------------------------------
// 原先 main.cc 的 mapToGroundPixel() 与 RiskEngine::mapToGround() 各有一份重复
// 实现，现收敛到本公共 helper，消除第三份拷贝。两处调用共享同一份矩阵运算。
//
// 除 mapToGround 外，还提供外部标定文件加载：
//   loadHomographyFromFile(path, H) — 读取文本 3x3 单应（与 Python 端
//     src/algorithms/ipm_calibrate.py 的 save_homography(..., .txt/.csv) 输出兼容），
//     成功返回 true，失败返回 false（不抛异常，便于调用方回退默认）。
//   loadHomographyOr(path, fallback) — 便捷回退：文件可加载则用文件，否则用 fallback。
// 这样相机位姿变化后只需重新标定保存文件，无需重编译。
//
// 依赖：OpenCV core（cv::Mat），C++11。
// =============================================================================

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

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

// 从文本文件加载 3x3 单应矩阵 H（CV_64F）。
// 兼容 Python np.savetxt 输出（3 行 x 3 列，空白或逗号分隔）。
// 成功返回 true 并填充 H；文件不存在 / 数值不足 9 个 / 分母行全 0（退化）返回 false。
inline bool loadHomographyFromFile(const std::string& path, cv::Mat& H) {
    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();
    // 把逗号/分号/括号统一替换为空格，便于按空白解析（np.savetxt 用空格，csv 用逗号）
    for (size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (c == ',' || c == ';' || c == '[' || c == ']' || c == '(' || c == ')') {
            content[i] = ' ';
        }
    }
    std::istringstream iss(content);
    double vals[9] = {0.0};
    int n = 0;
    while (n < 9 && (iss >> vals[n])) ++n;
    // 必须恰好解析出 9 个数值（允许行尾换行/额外空白）
    if (n != 9) return false;
    // 退化透视：分母行（第三行）全 0 无法归一化
    if (std::fabs(vals[6]) < 1e-12 && std::fabs(vals[7]) < 1e-12 &&
        std::fabs(vals[8]) < 1e-12) {
        return false;
    }
    H = cv::Mat(3, 3, CV_64F);
    double* p = reinterpret_cast<double*>(H.data);
    for (int i = 0; i < 9; ++i) p[i] = vals[i];
    return true;
}

// 便捷回退：path 非空且可加载则返回加载的 H，否则返回 fallback（不打印，由调用方决定是否告警）。
inline cv::Mat loadHomographyOr(const std::string& path, const cv::Mat& fallback) {
    cv::Mat H;
    if (!path.empty() && loadHomographyFromFile(path, H)) return H;
    return fallback;
}

} // namespace pedestrian

#endif // PEDESTRIAN_IPM_TRANSFORM_H