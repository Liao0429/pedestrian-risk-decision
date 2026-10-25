/**
 * @file kalman_filter.h
 * @brief 单目标离散 Kalman 滤波（常速度 CV 模型），接口与 Python 版 kalman_tracker.py 一致。
 *
 * 用于开发板（Luckfox）集成。头文件即实现（header-only），无第三方依赖，
 * 仅使用 C++ 标准库 <array>，可直接复制进嵌入式工程。
 *
 * 模型：
 *   状态       x = [px, py, vx, vy]          （地面坐标：米；速度：m/s）
 *   观测       z = [px, py]                   （IPM 逆透视映射得到的地面米坐标）
 *   运动模型    常速度（CV），固定帧间隔 dt
 *
 * 用法示例:
 *   @code
 *   KalmanFilter kf(1.0/30.0, 1.5, 0.5);   // dt=1/30s, 过程噪声 1.5, 观测噪声 0.5
 *   kf.init(1.2, 3.4);
 *   while (true) {
 *       kf.predict();
 *       kf.correct(zx, zy);               // z 为当前帧地面坐标
 *       double px = kf.x(), py = kf.y();
 *       double vx = kf.vx(), vy = kf.vy();
 *   }
 *   @endcode
 */

#ifndef PEDESTRIAN_KALMAN_FILTER_H
#define PEDESTRIAN_KALMAN_FILTER_H

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

// 内部矩阵工具：4x4 / 2x4 / 4x2 / 2x2 行主序运算，全部内联，无动态内存。
namespace kf_detail {

using Vec4 = std::array<double, 4>;
using Mat4 = std::array<double, 16>;   // 4x4 行主序
using Mat24 = std::array<double, 8>;  // 2x4 行主序
using Mat42 = std::array<double, 8>;   // 4x2 行主序
using Mat22 = std::array<double, 4>;   // 2x2 行主序

inline void mat4MulVec4(const Mat4& a, const Vec4& x, Vec4& y) {
    for (int i = 0; i < 4; ++i) {
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * x[k];
        y[i] = s;
    }
}

inline void mat4MulMat4(const Mat4& a, const Mat4& b, Mat4& c) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            c[i * 4 + j] = s;
        }
    }
}

inline void mat4Transpose(const Mat4& a, Mat4& at) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) at[j * 4 + i] = a[i * 4 + j];
}

// c[2x4] = a[2x4] * b[4x4]
inline void mat24MulMat4(const Mat24& a, const Mat4& b, Mat24& c) {
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            c[i * 4 + j] = s;
        }
    }
}

// c[2x2] = a[2x4] * b[4x2]
inline void mat24MulMat42(const Mat24& a, const Mat42& b, Mat22& c) {
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 2 + j];
            c[i * 2 + j] = s;
        }
    }
}

// c[4x2] = a[4x4] * b[4x2]
inline void mat4MulMat42(const Mat4& a, const Mat42& b, Mat42& c) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 2; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 2 + j];
            c[i * 2 + j] = s;
        }
    }
}

// c[4x4] = a[4x2] * b[2x4]
inline void mat42MulMat24(const Mat42& a, const Mat24& b, Mat4& c) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 2; ++k) s += a[i * 2 + k] * b[k * 4 + j];
            c[i * 4 + j] = s;
        }
    }
}

// 2x2 求逆（S 为正定对称，必然可逆；若行列式过小退化为零矩阵，保证数值稳定）
inline bool mat22Inverse(const Mat22& a, Mat22& inv) {
    const double det = a[0] * a[3] - a[1] * a[2];
    if (std::fabs(det) < 1e-12) {
        inv = {0.0, 0.0, 0.0, 0.0};
        return false;
    }
    const double d = 1.0 / det;
    inv[0] =  a[3] * d;
    inv[1] = -a[1] * d;
    inv[2] = -a[2] * d;
    inv[3] =  a[0] * d;
    return true;
}

// y[4x1] += a[4x2] * x[2x1]
inline void mat42MulVec2Add(const Mat42& a, double x0, double x1, Vec4& y) {
    for (int i = 0; i < 4; ++i) y[i] += a[i * 2] * x0 + a[i * 2 + 1] * x1;
}

}  // namespace kf_detail

/**
 * @brief 单目标常速度(CV)离散 Kalman 滤波器。
 *
 * 状态 [px, py, vx, vy]，观测 [px, py]，固定采样间隔 dt。
 */
class KalmanFilter {
public:
    /**
     * @param dt 两帧固定时间间隔（秒），如 1/30。
     * @param process_noise 过程噪声标准差 sigma_a（m/s^2，随机加速度）。
     *                      越大越信任观测、越易跟随机动；越小轨迹越平滑。
     * @param measurement_noise 观测噪声标准差 sigma_z（米，IPM 地面坐标噪声量级）。
     */
    explicit KalmanFilter(double dt = 1.0 / 30.0,
                       double process_noise = 1.5,
                       double measurement_noise = 0.5)
        : m_dt(dt), m_sigA(process_noise), m_sigZ(measurement_noise), m_init(false) {
        buildMatrices();
    }

    /** 用首个观测初始化，速度缺省为 0。 */
    void init(double x, double y, double vx = 0.0, double vy = 0.0) {
        m_x = {x, y, vx, vy};
        const double p_pos = std::pow(10.0 * m_sigZ, 2);
        const double p_vel = std::pow(10.0 * m_sigA * m_dt, 2);
        m_P = {
            p_pos, 0.0,   0.0,   0.0,
            0.0,   p_pos, 0.0,   0.0,
            0.0,   0.0,   p_vel, 0.0,
            0.0,   0.0,   0.0,   p_vel
        };
        m_init = true;
    }

    /** 一步状态预测（无观测时外推）。 */
    void predict() {
        if (!m_init) return;
        kf_detail::mat4MulVec4(m_F, m_x, m_x);
        kf_detail::Mat4 FP;
        kf_detail::mat4MulMat4(m_F, m_P, FP);
        kf_detail::Mat4 FPFT;
        kf_detail::mat4Transpose(m_F, m_FT);   // 每次更新转置，保持状态自洽
        kf_detail::mat4MulMat4(FP, m_FT, FPFT);
        for (std::size_t i = 0; i < m_P.size(); ++i) m_P[i] = FPFT[i] + m_Q[i];
    }

    /** 用观测 [zx, zy] 修正状态（标准 Kalman 更新）。 */
    void correct(double zx, double zy) {
        if (!m_init) return;
        // S = H P H^T + R   (2x2)
        kf_detail::Mat24 HP;
        kf_detail::mat24MulMat4(m_H, m_P, HP);
        kf_detail::Mat22 S;
        kf_detail::mat24MulMat42(HP, m_HT, S);
        S[0] += m_R[0];
        S[1] += m_R[1];
        S[2] += m_R[2];
        S[3] += m_R[3];

        kf_detail::Mat22 Sinv;
        kf_detail::mat22Inverse(S, Sinv);

        // K = P H^T S^-1  (4x2)
        kf_detail::Mat42 PHt;
        kf_detail::mat4MulMat42(m_P, m_HT, PHt);
        kf_detail::Mat42 K;
        for (int i = 0; i < 4; ++i) {
            K[i * 2]     = PHt[i * 2] * Sinv[0] + PHt[i * 2 + 1] * Sinv[2];
            K[i * 2 + 1] = PHt[i * 2] * Sinv[1] + PHt[i * 2 + 1] * Sinv[3];
        }

        // 新息 y = z - H x
        const double y0 = zx - m_x[0];
        const double y1 = zy - m_x[1];

        // x = x + K y
        kf_detail::mat42MulVec2Add(K, y0, y1, m_x);

        // P = (I - K H) P
        kf_detail::Mat4 KH;
        kf_detail::mat42MulMat24(K, m_H, KH);
        kf_detail::Mat4 IKH;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                IKH[i * 4 + j] = (i == j ? 1.0 : 0.0) - KH[i * 4 + j];
        kf_detail::Mat4 IKH_P;
        kf_detail::mat4MulMat4(IKH, m_P, IKH_P);
        m_P = IKH_P;
    }

    /** 便捷接口：一步预测 + 一步更新（带观测时使用）。 */
    void update(double zx, double zy) {
        predict();
        correct(zx, zy);
    }

    // ---------------- 只读访问 ----------------
    bool isInitialized() const { return m_init; }
    double dt() const { return m_dt; }
    double processNoise() const { return m_sigA; }
    double measurementNoise() const { return m_sigZ; }

    double x()  const { return m_x[0]; }
    double y()  const { return m_x[1]; }
    double vx() const { return m_x[2]; }
    double vy() const { return m_x[3]; }

    /** 位置 (px, py)。 */
    std::pair<double, double> position() const { return {m_x[0], m_x[1]}; }
    /** 速度 (vx, vy)。 */
    std::pair<double, double> velocity() const { return {m_x[2], m_x[3]}; }

    /** 状态向量 [px, py, vx, vy]。 */
    const kf_detail::Vec4& state() const { return m_x; }
    /** 协方差矩阵 P 的第 (i, j) 元素。 */
    double covariance(int i, int j) const { return m_P[i * 4 + j]; }

private:
    void buildMatrices() {
        const double dt = m_dt;
        // F = [[1,0,dt,0],[0,1,0,dt],[0,0,1,0],[0,0,0,1]]
        m_F = {
            1.0, 0.0, dt,  0.0,
            0.0, 1.0, 0.0, dt,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        };
        // H = [[1,0,0,0],[0,1,0,0]]
        m_H = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0};
        m_HT = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0};  // 4x2
        // Q = sigma_a^2 * G G^T, G = [[.5dt^2,0],[0,.5dt^2],[dt,0],[0,dt]]
        const double q = m_sigA * m_sigA;
        m_Q = {
            q * 0.25 * dt * dt * dt * dt, 0.0,                    q * 0.5 * dt * dt * dt, 0.0,
            0.0, q * 0.25 * dt * dt * dt * dt, 0.0,                    q * 0.5 * dt * dt * dt,
            q * 0.5 * dt * dt * dt,       0.0,                    q * dt * dt,           0.0,
            0.0, q * 0.5 * dt * dt * dt,  0.0,                    q * dt * dt
        };
        // R = diag(sigma_z^2, sigma_z^2)
        const double r = m_sigZ * m_sigZ;
        m_R = {r, 0.0, 0.0, r};
    }

    double m_dt;
    double m_sigA;
    double m_sigZ;
    bool m_init;

    kf_detail::Vec4  m_x;   // 状态 [px, py, vx, vy]
    kf_detail::Mat4  m_P;   // 协方差
    kf_detail::Mat4  m_F;   // 状态转移
    kf_detail::Mat4  m_FT;  // F 转置（缓存）
    kf_detail::Mat24 m_H;   // 观测矩阵 (2x4)
    kf_detail::Mat42 m_HT;  // H 转置 (4x2)
    kf_detail::Mat4  m_Q;   // 过程噪声
    kf_detail::Mat22 m_R;   // 观测噪声 (2x2)
};

#endif  // PEDESTRIAN_KALMAN_FILTER_H