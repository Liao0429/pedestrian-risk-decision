// @file enhanced_decision.cc
// @brief 增强决策模块实现（EnhancedDecision）
//
// 接入方式与编译命令详见 ../include/enhanced_decision.h 顶部注释。
// 本文件仅使用 C++ 标准库 + src/algorithms 下的 header-only 算法头文件，
// 不依赖 OpenCV / RKNN / RTSP，可被 arm-none-linux-gnueabihf-g++ 交叉编译。
//
// 关于算法头文件：
//   - graph_laplacian.h / risk_fsm.h / kalman_filter.h 均为 header-only 且接口完整，
//     本文件直接使用，无需补全实现。
//   - KalmanFilter 为单目标 4 态（px,py,vx,vy）线性匀速模型；多目标平滑在本文件
//     通过"每目标一个 KalmanFilter + 最近邻贪心关联"实现（见 smoothPoints）。

#include "../include/enhanced_decision.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace pedestrian {

namespace {

// 单调时钟毫秒时间戳（自某个固定起点，仅用于 FSM 的毫秒级 DANGER 保持判断）
inline int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // anonymous namespace

EnhancedDecision::EnhancedDecision(const EnhancedConfig& cfg)
    : cfg_(cfg),
      graph_(cfg.edge_dist_m),
      fsm_(cfg.fsm),
      nextTrackId_(0),
      prevN_(0),
      lastScore_(0.0) {}

void EnhancedDecision::reset() {
    fsm_.reset();
    tracks_.clear();
    nextTrackId_ = 0;
    prevN_ = 0;
    lastScore_ = 0.0;
}

std::vector<std::pair<float,float>> EnhancedDecision::smoothPoints(
        const std::vector<std::pair<float,float>>& pts) {
    // 关闭平滑：直接透传原始脚点
    if (!cfg_.enable_smoothing) {
        return pts;
    }
    // 空输入：清空轨迹，避免旧轨迹在下一帧人少时产生"幽灵点"
    if (pts.empty()) {
        tracks_.clear();
        return std::vector<std::pair<float,float>>();
    }

    const size_t nt = tracks_.size();
    const size_t np = pts.size();

    // 先对所有轨迹做一步 Kalman 预测，使关联门限中心使用预测位置
    // （而非上一帧修正后的陈旧位置），降低交叉/遮挡场景的误关联。
    for (size_t i = 0; i < nt; ++i) {
        tracks_[i].kf.predict();
    }

    std::vector<bool> trackUsed(nt, false);
    std::vector<bool> ptUsed(np, false);

    // 生成候选匹配对 (距离, (轨迹下标, 点下标)) 并按距离升序排序，
    // 实现确定性的最近邻贪心关联（全局最优近似，行人规模小完全够用）。
    std::vector<std::pair<double, std::pair<size_t,size_t>>> cands;
    cands.reserve(nt * np);
    for (size_t i = 0; i < nt; ++i) {
        const double tx = tracks_[i].kf.x();
        const double ty = tracks_[i].kf.y();
        for (size_t j = 0; j < np; ++j) {
            const double dx = tx - static_cast<double>(pts[j].first);
            const double dy = ty - static_cast<double>(pts[j].second);
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d <= cfg_.kalman_gate_m) {
                cands.push_back(std::make_pair(d, std::make_pair(i, j)));
            }
        }
    }
    std::sort(cands.begin(), cands.end());

    std::vector<std::pair<float,float>> out;
    out.reserve(np);

    // 匹配：同一轨迹/同一观测点每帧只使用一次
    for (size_t k = 0; k < cands.size(); ++k) {
        const size_t i = cands[k].second.first;
        const size_t j = cands[k].second.second;
        if (trackUsed[i] || ptUsed[j]) continue;
        trackUsed[i] = true;
        ptUsed[j]   = true;

        KalmanTrack& t = tracks_[i];
        // 轨迹已在关联前 predict 过，这里只做修正，避免二次预测
        t.kf.correct(static_cast<double>(pts[j].first),
                     static_cast<double>(pts[j].second));
        t.miss = 0;
        out.push_back(std::make_pair(static_cast<float>(t.kf.x()),
                                     static_cast<float>(t.kf.y())));
    }

    // 未匹配的观测点 -> 新建轨迹（首帧直接使用观测值）
    for (size_t j = 0; j < np; ++j) {
        if (ptUsed[j]) continue;
        KalmanTrack t(cfg_.frame_dt,
                      cfg_.kalman_process_noise,
                      cfg_.kalman_measurement_noise,
                      pts[j].first, pts[j].second);
        t.id = nextTrackId_++;
        tracks_.push_back(t);
        out.push_back(pts[j]);
    }

    // 未匹配到的轨迹 miss 计数 +1（只统计本帧之前的轨迹；新建轨迹 miss=0）
    for (size_t i = 0; i < nt; ++i) {
        if (!trackUsed[i]) ++tracks_[i].miss;
    }
    // 超限删除（倒序遍历避免下标失效）
    for (int i = static_cast<int>(tracks_.size()) - 1; i >= 0; --i) {
        if (tracks_[static_cast<size_t>(i)].miss > cfg_.kalman_max_miss) {
            tracks_.erase(tracks_.begin() + i);
        }
    }

    return out;
}

EnhancedResult EnhancedDecision::processFrame(
        const std::vector<std::pair<float,float>>& groundPts) {
    EnhancedResult res;

    // 1. 多目标 Kalman 平滑脚点
    res.smoothPts = smoothPoints(groundPts);

    // 2. 图拉普拉斯网络特征（含 Fiedler 值）
    // setEdgeDist 已在构造函数中设置，每帧重设无实际作用
    graph_.setPoints(res.smoothPts);
    const SpectralFeatures feat = graph_.computeFeatures();
    res.n                      = feat.n;
    res.density                = feat.density;
    res.r_gcc                  = feat.r_gcc;
    res.clustering             = feat.clustering;
    res.fiedler                = feat.fiedler;
    res.spectral_radius        = feat.spectral_radius;
    res.algebraic_connectivity = feat.algebraic_connectivity;
    res.eig_entropy            = feat.eig_entropy;
    res.spectral_converged     = feat.converged;

    // 3. FSM 稳定等级
    FsmFeatures f;
    f.n         = feat.n;
    f.density   = feat.density;
    f.r_gcc     = feat.r_gcc;
    f.clustering = feat.clustering;
    f.fiedler   = feat.fiedler;   // 谱特征参与 FSM 评分（w4 项）
    // 运动趋势：人数上升为正（增大风险），归一化到 [-1,1]
    const int delta = feat.n - prevN_;
    f.trend = clampd(static_cast<double>(delta) / 3.0, -1.0, 1.0);
    prevN_ = feat.n;

    const int64_t nowMs = nowMillis();
    const RiskLevel lvl = fsm_.update(f, nowMs);
    res.level = static_cast<int>(lvl);   // 0=SAFE 1=WARNING 2=DANGER
    res.score = fsm_.score();
    lastScore_ = fsm_.score();
    return res;
}

} // namespace pedestrian