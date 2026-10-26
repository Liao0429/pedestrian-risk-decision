#ifndef ENHANCED_DECISION_H
#define ENHANCED_DECISION_H

/**
 * @file enhanced_decision.h
 * @brief 增强决策模块（EnhancedDecision）
 *
 * 背景：
 *   luckfox_rtsp_yolov5 原本在 main.cc 中调用 RiskEngine（固定阈值规则）得到
 *   CROWD_SAFE / CROWD_WARNING / CROWD_DANGER。本模块提供"增强决策"：
 *   在 RKNN 行人检测 + IPM 得到地面米制脚点后，组合三个算法头文件：
 *     - kalman_filter.h    多目标 Kalman 平滑（最近邻关联），输出 smoothPts
 *     - graph_laplacian.h  网络特征（density / r_gcc / fiedler / clustering / ...）
 *     - risk_fsm.h        带滞回/去抖/保持的有限状态机，输出稳定风险等级
 *   最终返回 EnhancedResult{ level, n, density, r_gcc, fiedler, smoothPts, ... }。
 *
 * 接入 main.cc（示例，不改动现有 risk_engine 逻辑）：
 *   #include "enhanced_decision.h"
 *
 *   pedestrian::EnhancedDecision enhancedDecision;  // 或自定义 EnhancedConfig
 *   ...
 *   while (1) {
 *       // ... 取帧、RKNN 推理、遍历 person 检测框 ...
 *       std::vector<std::pair<float,float>> groundPts;   // 地面米制脚点 (x,y)
 *       for (auto& box : boxes) {   // 遍历每个 person 检测框
 *           float gx = 0.f, gy = 0.f;   // 脚点像素坐标经 IPM 映射到地面米制坐标
 *           mapCoordinates(&gx, &gy);
 *           groundPts.push_back(std::make_pair(gx, gy));
 *       }
 *       pedestrian::EnhancedResult er = enhancedDecision.processFrame(groundPts);
 *       int lvl = er.level;   // 0=SAFE 1=WARNING 2=DANGER，与 RiskLevel 对齐
 *       // 可用 er.n / er.density / er.r_gcc / er.fiedler 覆盖原 RiskEngine 输出，
 *       // 或用 er.level 作为稳定风险等级驱动画框颜色。
 *   }
 *
 * 编译（交叉编译，Luckfox SDK / 老电脑 Ubuntu）：
 *   arm-none-linux-gnueabihf-g++ -std=c++11 -O2 \
 *       -I src/luckfox_rtsp_yolov5/include \
 *       -I src/algorithms \
 *       src/luckfox_rtsp_yolov5/src/enhanced_decision.cc \
 *       src/luckfox_rtsp_yolov5/src/main.cc \
 *       src/luckfox_rtsp_yolov5/src/yolov5.cc \
 *       src/luckfox_rtsp_yolov5/src/postprocess.cc \
 *       src/luckfox_rtsp_yolov5/src/luckfox_mpi.cc \
 *       src/luckfox_rtsp_yolov5/src/risk_engine.cc \
 *       ...(RKNN/RTSP/OpenCV 链接参数按原 Makefile 补齐)... -o luckfox_demo
 *
 *   说明：
 *   - 本头文件通过相对路径包含算法头文件：
 *       #include "../../algorithms/graph_laplacian.h"   // 仅标准库
 *       #include "../../algorithms/risk_fsm.h"
 *       #include "../../algorithms/kalman_filter.h"
 *     因此即使不加 -I src/algorithms 也能编译；若改用
 *       #include "graph_laplacian.h" 这种风格，则必须加 -I src/algorithms。
 *   - enhanced_decision.h/.cc 不依赖 OpenCV / RKNN / RTSP，仅标准库 + 算法头文件。
 *   - 若算法头文件接口有缺失，已在 .cc 中用合理默认实现补全（见源码注释）。
 *
 * 依赖：C++11；仅标准库 <vector>/<utility>/<cstdint> 及上述算法头文件。
 */

#include <cstdint>
#include <utility>
#include <vector>

#include "../../algorithms/graph_laplacian.h"
#include "../../algorithms/kalman_filter.h"
#include "../../algorithms/risk_fsm.h"

namespace pedestrian {

// 增强决策配置
struct EnhancedConfig {
    double edge_dist_m;               // 图建边距离阈值（米），默认 2.0
    RiskFSMConfig fsm;                 // FSM 参数，默认与 Python 端 RiskFSMConfig 一致
    double frame_dt;                   // 帧间隔（秒），Kalman 平滑使用
    double kalman_process_noise;       // Kalman 过程噪声 sigma_a (m/s^2)
    double kalman_measurement_noise;   // Kalman 测量噪声 sigma_z（米）
    double kalman_gate_m;              // 多目标最近邻关联门限（米）
    int    kalman_max_miss;            // 轨迹连续未匹配帧数上限，超过则删除
    bool   enable_smoothing;           // false 时直接透传脚点，不做 Kalman 平滑

    EnhancedConfig()
        : edge_dist_m(2.0),
          fsm(),
          frame_dt(1.0 / 30.0),
          kalman_process_noise(1.5),
          kalman_measurement_noise(0.5),
          kalman_gate_m(2.0),
          kalman_max_miss(5),
          enable_smoothing(true) {}
};

// 增强决策结果
struct EnhancedResult {
    int    level;      // 0=SAFE 1=WARNING 2=DANGER（与 RiskLevel 对齐）
    int    n;          // 行人节点数（平滑后）
    double density;    // 网络密度
    double r_gcc;      // 最大连通分量占比
    double fiedler;    // Fiedler 值（第二小特征值 lambda_2）
    double clustering; // 平均局部聚类系数
    double spectral_radius;          // 谱半径
    double algebraic_connectivity;   // 代数连通度（与 fiedler 等价）
    double eig_entropy;              // 特征熵
    double score;                    // FSM 综合拥挤度评分 [0,1]
    bool   spectral_converged;      // 图拉普拉斯特征分解是否收敛（false 表示谱特征已安全回退 0）
    std::vector<std::pair<float,float>> smoothPts;   // 平滑后的地面脚点（米）

    EnhancedResult()
        : level(0), n(0), density(0.0), r_gcc(0.0), fiedler(0.0),
          clustering(0.0), spectral_radius(0.0),
          algebraic_connectivity(0.0), eig_entropy(0.0), score(0.0),
          spectral_converged(true) {}
};

// 增强决策模块
// 线程模型：与 main.cc 单线程 while 循环配合，同一实例只在一个线程内调用。
class EnhancedDecision {
public:
    explicit EnhancedDecision(const EnhancedConfig& cfg = EnhancedConfig());

    // 输入：本帧所有 person 的地面米制脚点 (x,y)（建议先经 IPM 映射）。
    // 内部：Kalman 平滑 -> GraphLaplacian 特征 -> RiskFSM 稳定等级。
    EnhancedResult processFrame(const std::vector<std::pair<float,float>>& groundPts);

    // 重置内部状态（FSM 回到 SAFE、清空轨迹、清空历史）
    void reset();

    const EnhancedConfig& config() const { return cfg_; }
    const RiskFSM& fsm() const { return fsm_; }
    double lastScore() const { return lastScore_; }

    static const char* levelName(int lvl) {
        return RiskFSM::levelName(static_cast<RiskLevel>(lvl));
    }

private:
    // 单条多目标 Kalman 轨迹：id 用于调试，miss 用于剔除长期丢失的目标
    struct KalmanTrack {
        uint32_t    id;
        KalmanFilter kf;
        int         miss;

        KalmanTrack(double dt, double qn, double rn, float x, float y)
            : id(0), kf(dt, qn, rn), miss(0) {
            kf.init(x, y);   // 首帧观测初始化，速度默认 0
        }
    };

    // 多目标平滑：每帧先对所有轨迹做一步 predict（预测位置作为门限中心），
    // 再最近邻贪心关联；匹配到的轨迹 correct，未匹配的点新建轨迹，
    // 长期未匹配的轨迹被删除。
    std::vector<std::pair<float,float>> smoothPoints(
        const std::vector<std::pair<float,float>>& pts);

    EnhancedConfig cfg_;
    GraphLaplacian graph_;
    RiskFSM fsm_;
    std::vector<KalmanTrack> tracks_;
    uint32_t nextTrackId_;
    int      prevN_;       // 上一帧节点数，用于计算趋势
    double   lastScore_;
};

} // namespace pedestrian

#endif // ENHANCED_DECISION_H