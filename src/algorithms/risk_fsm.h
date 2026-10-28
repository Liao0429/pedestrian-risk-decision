#ifndef PEDESTRIAN_RISK_FSM_H
#define PEDESTRIAN_RISK_FSM_H

// =============================================================================
// 风险决策有限状态机（RiskFSM）
// -----------------------------------------------------------------------------
// 与 Python 端 src/algorithms/risk_fsm.py 的 RiskFSM / RiskFSMConfig 接口一致。
//
// 解决原 RiskEngine::evaluate() 固定阈值硬切导致的逐帧抖动问题：
//   1. 滞回（hysteresis）：升级/降级使用不同阈值带；
//   2. 确认帧（debounce）：升级需连续 confirm_up 帧，降级需连续 confirm_down 帧；
//   3. 状态保持（hold）：进入 DANGER 后至少保持 danger_hold_frames 帧 /
//      danger_hold_ms 毫秒。
//
// 综合拥挤度评分（归一化到 [0,1]）：
//   score = w1 * norm(n) + w2 * density + w3 * r_gcc
//           + w4 * fiedler_norm * r_gcc        // 谱特征修正项
//   norm(n) = min(n / n_ref, 1.0)
//   fiedler_norm = fiedler / (fiedler + 1)     // 单调映射到 [0,1)
//   可选运动趋势 trend（[-1,1]，>0 表示聚集/人数上升）：
//   score += trend_bias * clamp(trend, -1, 1)
//
// 状态相关动态阈值（滞回带）：
//   SAFE    --(score >= up_warning)-->     WARNING
//   WARNING --(score >= up_danger)-->      DANGER
//   WARNING --(score <= down_safe)-->       SAFE
//   DANGER  --(score <= down_warning)-->    WARNING
//
// 纯头文件、无第三方依赖、C++11 兼容，可直接 include 到 Luckfox 边缘端工程。
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <string>

namespace pedestrian {

// 风险等级（与 Python 端 LEVELS 顺序一致）
enum RiskLevel : int {
    RISK_SAFE = 0,
    RISK_WARNING = 1,
    RISK_DANGER = 2
};

// 每帧输入特征（与 risk_engine.h 的 RiskFeatures 兼容的轻量子集）
struct FsmFeatures {
    int    n = 0;          // 行人数
    double density = 0.0; // 网络密度 [0,1]
    double r_gcc = 0.0;    // 最大连通分量占比 [0,1]
    double clustering = 0.0; // 平均聚类系数（预留，当前不参与评分）
    double fiedler = 0.0; // Fiedler 特征值（谱连通度），参与 w4 谱项
    double trend = 0.0;    // 运动趋势 [-1,1]，>0 聚集/人数上升
};

// 配置（默认值与 Python 端 RiskFSMConfig 一致）
struct RiskFSMConfig {
    // 综合拥挤度评分权重
    double w1 = 0.4;
    double w2 = 0.3;
    double w3 = 0.3;
    double w4 = 0.1;          // 谱特征权重（score += w4 * fiedler/(fiedler+1) * r_gcc，0=禁用）
    int    n_ref = 20;         // 人数归一化参考值
    // 动态（状态相关）滞回阈值
    double up_warning = 0.40;  // SAFE -> WARNING
    double up_danger = 0.65;  // WARNING -> DANGER
    double down_safe = 0.28;   // WARNING -> SAFE
    double down_warning = 0.55; // DANGER -> WARNING
    // 确认帧数
    int confirm_up = 3;        // 升级需连续帧数
    int confirm_down = 5;      // 降级需连续帧数
    // 状态保持
    int    danger_hold_frames = 10; // DANGER 至少保持帧数（0=禁用）
    int    danger_hold_ms = 1000;    // DANGER 至少保持毫秒（0 或 now_ms<0 时禁用）
    // 运动趋势偏置
    double trend_bias = 0.05;

    // 校验配置合法性（与 Python 端 RiskFSMConfig.__post_init__ 一致）。
    // 返回 true 表示合法；false 时 err 填充具体原因，便于 JSON 加载时报错。
    bool validate(std::string& err) const {
        if (w1 < 0.0 || w2 < 0.0 || w3 < 0.0 || w4 < 0.0) { err = "权重 w1/w2/w3/w4 必须为非负值"; return false; }
        if (w1 + w2 + w3 <= 0.0)              { err = "权重之和必须大于 0"; return false; }
        if (w4 > 1.0)                          { err = "w4 权重不应超过 1.0（谱项为辅助修正）"; return false; }
        if (n_ref <= 0)                       { err = "n_ref 必须为正整数"; return false; }
        if (confirm_up < 1 || confirm_down < 1) { err = "confirm_up / confirm_down 必须 >= 1"; return false; }
        if (danger_hold_frames < 0 || danger_hold_ms < 0) { err = "danger_hold_frames / danger_hold_ms 必须 >= 0"; return false; }
        if (up_warning < 0.0 || up_warning > 1.0 ||
            up_danger  < 0.0 || up_danger  > 1.0 ||
            down_safe  < 0.0 || down_safe  > 1.0 ||
            down_warning < 0.0 || down_warning > 1.0) {
            err = "阈值必须在 [0,1] 范围内"; return false;
        }
        // 滞回一致性：升级阈值必须严格高于对应降级阈值，否则无滞回带
        if (up_warning <= down_safe) { err = "up_warning 必须大于 down_safe（否则无滞回带）"; return false; }
        if (up_danger <= down_warning) { err = "up_danger 必须大于 down_warning（否则无滞回带）"; return false; }
        return true;
    }
};

class RiskFSM {
public:
    RiskFSM() = default;
    explicit RiskFSM(const RiskFSMConfig& cfg) : cfg_(cfg) {}

    // 重置到 SAFE，清空所有内部状态
    void reset() {
        level_ = RISK_SAFE;
        score_ = 0.0;
        upCount_ = 0;
        downCount_ = 0;
        frame_ = 0;
        entryFrame_ = 0;
        entryMs_ = -1;
    }

    // 核心接口：输入一帧特征 + 当前时间戳(ms)，返回更新后的风险等级。
    // now_ms < 0 表示无时间信息，此时禁用基于毫秒的 DANGER 保持。
    RiskLevel update(const FsmFeatures& f, int64_t now_ms) {
        ++frame_;
        score_ = computeScore(f);
        RiskLevel target = rawTarget(score_, level_);
        return transition(target, now_ms);
    }

    // 访问器
    RiskLevel level() const { return level_; }
    double score() const { return score_; }
    const RiskFSMConfig& config() const { return cfg_; }

    // 等级名（与 Python 端一致）
    static const char* levelName(RiskLevel lvl) {
        switch (lvl) {
            case RISK_SAFE:    return "SAFE";
            case RISK_WARNING: return "WARNING";
            case RISK_DANGER:  return "DANGER";
            default:           return "SAFE";
        }
    }

private:
    double computeScore(const FsmFeatures& f) const {
        double normN = (cfg_.n_ref > 0)
            ? std::min(static_cast<double>(std::max(f.n, 0)) / cfg_.n_ref, 1.0)
            : 0.0;
        double density = clamp01(f.density);
        double r_gcc = clamp01(f.r_gcc);
        double s = cfg_.w1 * normN + cfg_.w2 * density + cfg_.w3 * r_gcc;
        // 谱特征修正项：仅对连通结构生效，紧密聚集（fiedler 大）加分明显、
        // 链状伪聚集（r_gcc=1 但 fiedler 小）加分极小、分散场景（r_gcc≈0）不加分
        if (cfg_.w4 != 0.0 && r_gcc > 0.0 && f.fiedler > 0.0) {
            double fiedlerNorm = f.fiedler / (f.fiedler + 1.0);
            s += cfg_.w4 * fiedlerNorm * r_gcc;
        }
        if (cfg_.trend_bias != 0.0 && f.trend != 0.0) {
            s += cfg_.trend_bias * clamp(f.trend, -1.0, 1.0);
        }
        return clamp01(s);
    }

    // 根据当前状态 + score 应用滞回阈值，得到"理想目标等级"
    RiskLevel rawTarget(double score, RiskLevel state) const {
        if (state == RISK_SAFE) {
            return (score >= cfg_.up_warning) ? RISK_WARNING : RISK_SAFE;
        }
        if (state == RISK_WARNING) {
            if (score >= cfg_.up_danger) return RISK_DANGER;
            if (score <= cfg_.down_safe) return RISK_SAFE;
            return RISK_WARNING;
        }
        // RISK_DANGER
        return (score <= cfg_.down_warning) ? RISK_WARNING : RISK_DANGER;
    }

    RiskLevel transition(RiskLevel target, int64_t now_ms) {
        RiskLevel current = level_;
        if (target == current) {
            upCount_ = 0;
            downCount_ = 0;
            return current;
        }
        if (target > current) {
            // 升级
            downCount_ = 0;
            ++upCount_;
            if (upCount_ >= cfg_.confirm_up) {
                level_ = target;
                entryMs_ = now_ms;
                entryFrame_ = frame_;
                upCount_ = 0;
            }
            return level_;
        }
        // 降级
        upCount_ = 0;
        if (current == RISK_DANGER && !holdElapsed(now_ms)) {
            downCount_ = 0;
            return current;
        }
        ++downCount_;
        if (downCount_ >= cfg_.confirm_down) {
            level_ = target;
            downCount_ = 0;
        }
        return level_;
    }

    // DANGER 强制保持期是否已结束（帧数与毫秒数同时满足才结束）
    bool holdElapsed(int64_t now_ms) const {
        if (level_ != RISK_DANGER) return true;  // 仅 DANGER 状态需要保持判断
        bool frameOk = true;
        if (cfg_.danger_hold_frames > 0) {
            frameOk = (frame_ - entryFrame_) >= cfg_.danger_hold_frames;
        }
        bool msOk = true;
        if (cfg_.danger_hold_ms > 0 && now_ms >= 0 && entryMs_ >= 0) {
            msOk = (now_ms - entryMs_) >= cfg_.danger_hold_ms;
        }
        return frameOk && msOk;
    }

    static double clamp01(double v) { return clamp(v, 0.0, 1.0); }
    static double clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    RiskFSMConfig cfg_;
    RiskLevel level_ = RISK_SAFE;
    double score_ = 0.0;
    int upCount_ = 0;
    int downCount_ = 0;
    int frame_ = 0;
    int entryFrame_ = 0;
    int64_t entryMs_ = -1;
};

} // namespace pedestrian

#endif // PEDESTRIAN_RISK_FSM_H