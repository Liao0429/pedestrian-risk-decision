#ifndef PEDESTRIAN_EMA_SMOOTHER_H
#define PEDESTRIAN_EMA_SMOOTHER_H

// =============================================================================
// ema_smoother.h - 指数滑动平均（EMA）时序平滑器（C++ header-only）
// -----------------------------------------------------------------------------
// 与 Python 端 src/algorithms/ema_smoother.py 的 EMASmoother 接口一致。
//
// 对风险特征序列（如 n/density/r_gcc、行人/车速、TTC 等）做指数滑动平均，
// 输出平滑后的特征值，用于 FSM 输入降噪，抑制检测抖动与 IPM 误差带来的
// 单帧跳变。纯头文件、无第三方依赖、C++11 兼容，可直接 include 到 Luckfox
// 边缘端工程。
//
// 递推（帧级固定 alpha）：
//     s_t = alpha * x_t + (1 - alpha) * s_{t-1}      (alpha ∈ (0,1])
// 首帧初始化：s_0 = x_0。
//
// 支持：
//   - 标量 / 多变量（向量）特征，各分量独立平滑；
//   - 每维独立 alpha（传入 std::vector<double>）；
//   - 时间感知模式（use_timestamp=true）：
//         alpha_t = 1 - exp(-dt / tau)，dt = t - t_prev；
//   - reset() 重置，便于场景 / 轨迹切换。
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pedestrian {

class EmaSmoother {
public:
    // alpha ∈ (0,1]；dim 为特征维度（标量视为 1）。
    explicit EmaSmoother(double alpha = 0.3,
                         std::size_t dim = 1,
                         bool use_timestamp = false,
                         double tau = 1.0)
        : m_useTimestamp(use_timestamp), m_tau(tau) {
        setAlpha(alpha, dim);
    }

    // 每维独立 alpha（长度须为 dim）。
    EmaSmoother(const std::vector<double>& alpha,
                bool use_timestamp = false,
                double tau = 1.0)
        : m_useTimestamp(use_timestamp), m_tau(tau) {
        setAlpha(alpha);
    }

    // ---- 配置 ----

    // 设置标量 alpha（广播到所有维度）。
    void setAlpha(double alpha, std::size_t dim = 1) {
        if (!(alpha > 0.0 && alpha <= 1.0)) {
            alpha = 0.3;  // 非法输入回退到默认（工程健壮性）
        }
        if (dim == 0) dim = 1;
        m_alpha.assign(dim, alpha);
        if (dim != m_dim) {
            // 维度变化：旧状态失效，重置，避免 m_s 越界
            m_s.clear();
            m_lastT = -1;
            m_initialized = false;
        }
        m_dim = dim;
    }

    // 设置每维独立 alpha。
    void setAlpha(const std::vector<double>& alpha) {
        if (alpha.empty()) {
            setAlpha(0.3, 1);
            return;
        }
        std::vector<double> a = alpha;
        for (double& x : a) {
            if (!(x > 0.0 && x <= 1.0)) x = 0.3;
        }
        if (a.size() != m_dim) {
            // 维度变化：旧状态失效，重置，避免 m_s 越界
            m_s.clear();
            m_lastT = -1;
            m_initialized = false;
        }
        m_alpha = a;
        m_dim = a.size();
    }

    void setUseTimestamp(bool use) { m_useTimestamp = use; }
    bool useTimestamp() const { return m_useTimestamp; }
    void setTau(double tau) { m_tau = (tau > 0.0) ? tau : 1.0; }
    double tau() const { return m_tau; }

    std::size_t dim() const { return m_dim; }
    const std::vector<double>& alpha() const { return m_alpha; }

    // ---- 查询 ----

    bool initialized() const { return m_initialized; }

    // 当前平滑值（未初始化返回空 vector）。
    const std::vector<double>& smoothed() const { return m_s; }

    // 第 i 维的当前平滑值（未初始化或越界返回 0）。
    double smoothedAt(std::size_t i) const {
        if (!m_initialized || i >= m_s.size()) return 0.0;
        return m_s[i];
    }

    // ---- 主接口 ----

    // 标量版本：dim==1 时平滑单一维度；dim>1 时仅更新第 0 维，
    // 其余维度保持原值，避免一维输入与全维状态错配导致越界。
    double update(double value, int64_t t = -1) {
        if (m_dim <= 1) {
            std::vector<double> v(1, value);
            return update(v, t)[0];
        }
        // dim > 1：只对第 0 维做标量平滑（其余维度以 0 起算，等待向量输入）
        if (!m_initialized) {
            m_s.assign(m_dim, 0.0);
            m_lastT = t;
            m_initialized = true;
        }
        if (m_alpha.size() != m_dim) m_alpha.assign(m_dim, 0.3);
        const std::vector<double> a = effectiveAlpha(t);
        m_s[0] = a[0] * value + (1.0 - a[0]) * m_s[0];
        m_lastT = t;
        return m_s[0];
    }

    // 多变量版本：输入长度须等于 dim。
    // t 为时间戳（秒或帧号），仅在 use_timestamp=true 时用于计算 dt。
    std::vector<double> update(const std::vector<double>& value,
                               int64_t t = -1) {
        if (value.empty()) return m_s;

        if (!m_initialized) {
            // 首次调用：确定维度并初始化状态
            if (m_dim == 0) {
                m_dim = value.size();
                m_alpha.assign(m_dim, 0.3);
            }
            if (value.size() != m_dim) {
                // 维度不匹配：保持未初始化，等待与 dim 匹配的输入，
                // 避免 m_s 长度与 m_dim 不一致导致后续越界
                return m_s;
            }
            m_s = value;
            m_lastT = t;
            m_initialized = true;
            return m_s;
        }

        if (value.size() != m_dim) {
            return m_s;  // 维度不匹配：忽略本帧
        }

        const std::vector<double> a = effectiveAlpha(t);
        for (std::size_t i = 0; i < m_dim; ++i) {
            m_s[i] = a[i] * value[i] + (1.0 - a[i]) * m_s[i];
        }
        m_lastT = t;
        return m_s;
    }

    // ---- 维护 ----

    void reset() {
        m_s.clear();
        m_lastT = -1;
        m_initialized = false;
        // m_dim / m_alpha 保留：同一平滑器一般复用同一特征维度
    }

private:
    std::vector<double> m_alpha;   // 每维 alpha
    std::size_t        m_dim = 0;  // 特征维度
    bool               m_useTimestamp = false;
    double             m_tau = 1.0;

    std::vector<double> m_s;       // 平滑状态
    int64_t            m_lastT = -1;
    bool               m_initialized = false;

    std::vector<double> effectiveAlpha(int64_t t) const {
        if (!m_useTimestamp || t < 0 || m_lastT < 0 || t <= m_lastT) {
            return m_alpha;
        }
        const double dt = static_cast<double>(t - m_lastT);
        double a_t = 1.0 - std::exp(-dt / m_tau);
        a_t = std::max(1e-9, std::min(1.0, a_t));
        // 标量广播到所有维度
        std::vector<double> a(m_dim, a_t);
        return a;
    }
};

}  // namespace pedestrian

#endif // PEDESTRIAN_EMA_SMOOTHER_H