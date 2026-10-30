/**
 * @file bytetrack_tracker.h
 * @brief ByteTrack 多目标跟踪器（低分框二次关联），接口与 Python 版
 *        bytetrack_tracker.py 一致，供开发板（Luckfox）集成。
 *
 * 原理（ByteTrack, ECCV 2022）：
 *   1. 每个目标一个线性匀速 Kalman 滤波器（8 维状态 [cx, cy, a, h, vx, vy, va, vh]），
 *      对检测框做一步预测，得到预测框；
 *   2. 检测框与预测框按 IoU 代价（1 - IoU）做线性分配（匈牙利算法）关联；
 *   3. 低分框二次关联：第一轮用高分检测（score >= track_thresh）匹配全部轨迹，
 *      剩余未匹配轨迹再与低分检测（det_thresh <= score < track_thresh）匹配，
 *      从而在遮挡 / 目标部分可见时维持轨迹，避免 ID 漂移；
 *   4. 未匹配的高分检测新建轨迹；超过 max_lost 帧未关联的轨迹被删除。
 *
 * 纯头文件实现（header-only），C++11，无第三方依赖（不依赖 Eigen/OpenCV），
 * 仅使用标准库 <array>/<vector>/<cmath>/<limits>/<utility>/<cstddef>，
 * 可直接复制进边缘端工程交叉编译。
 *
 * 用法示例:
 *   @code
 *   #include "bytetrack_tracker.h"
 *
 *   ByteTrack tracker(0.5, 0.1, 0.8, 30, 3, 1.0, false);
 *   while (true) {
 *       std::vector<std::vector<double>> dets;   // [x1,y1,x2,y2,score]
 *       // ... 从 YOLO 取检测框填充 dets ...
 *       std::vector<TrackResult> tracks = tracker.update(dets, frame_id);
 *       for (const TrackResult& t : tracks) {
 *           printf("id=%d box=(%.1f,%.1f,%.1f,%.1f) conf=%.2f\n",
 *                  t.id, t.x1, t.y1, t.x2, t.y2, t.score);
 *       }
 *   }
 *   @endcode
 *
 * 说明：本头文件的算法与 tests/test_bytetrack_tracker.py 验证过的 Python 版
 * 一一对应；因板端不便链接第三方线性代数库，匈牙利算法与 Kalman 滤波均为
 * 纯 C++ 手写实现。数值一致性由 tests/test_bytetrack_sequences.py 用真实编译产物
 * 在多人交叉 / 遮挡重现 / ID 切换 / 重复框 等真实序列上逐值比对（%.17g）。
 *
 * ---------------------------------------------------------------------------
 * 生命周期与 ID 分配契约（接线前必读，踩过坑的地方都在这里）
 * ---------------------------------------------------------------------------
 * 1. **frame_id 必须递增且从 > 1 开始，或直接传 -1 让内部自增**：
 *    createTrack 用 "frame_id <= 1" 判定"首帧直接确认"（与 Python 版一致）。
 *    若调用方每帧都传同一个 frame_id（比如恒传 0），每条新建轨迹都会立刻
 *    confirmed=true，min_hits 形同虚设、未确认轨迹防抖失效。
 *    推荐：update(dets) 不传 frame_id（内部自增 1,2,3...），或传递增的真帧号。
 *
 * 2. **ID 单调递增，但 reset() 会从 0 重新分配**：m_nextId 只在 reset() 时归零。
 *    任何按 track_id 建索引的下游模块（如 track_counter.h 的身份映射）必须与
 *    ByteTrack 同生命周期 —— reset() 时同步 reset()，否则旧 ID 会映射到新目标。
 *
 * 3. **轨迹删除只有一条路径**：连续 misses > max_lost 帧后 prune 掉（即容忍
 *    max_lost+1 帧漏检）。未确认的"幽灵轨迹"走同一条路径，不会永远驻留。
 *
 * 4. **states() 返回全部活跃轨迹，含未确认（confirmed=false）的**：
 *    output_confirmed_only=true 才会过滤。计数场景应当用 trackInfo() 的
 *    confirmed 自行过滤（tentative 不计人数），而不是把 states() 直接当人数。
 *
 * 5. **trackInfo() 与 states() 同序**：m_tracks 始终按创建顺序（= ID 升序）排列，
 *    prune() 保序压缩，因此二者可以用同一个下标对齐。
 *
 * 6. **参数校验不抛异常**：板端常以 -fno-exceptions 编译，构造器对非法参数
 *    （如 match_thresh>1 会接受任意零 IoU 配对）保持宽容；请用
 *    ByteTrack::paramsValid(...) 显式校验（条件与 Python 版 ValueError 完全一致）。
 */

#ifndef PEDESTRIAN_BYTETRACK_TRACKER_H
#define PEDESTRIAN_BYTETRACK_TRACKER_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace bt_detail {

// ----------------------------------------------------------------------
// 基本类型与 IoU
// ----------------------------------------------------------------------
struct Box {
    double x1, y1, x2, y2;
};

inline double iou(const Box& a, const Box& b) {
    const double ix1 = std::max(a.x1, b.x1);
    const double iy1 = std::max(a.y1, b.y1);
    const double ix2 = std::min(a.x2, b.x2);
    const double iy2 = std::min(a.y2, b.y2);
    const double iw = std::max(0.0, ix2 - ix1);
    const double ih = std::max(0.0, iy2 - iy1);
    const double inter = iw * ih;
    const double area_a = std::max(0.0, a.x2 - a.x1) * std::max(0.0, a.y2 - a.y1);
    const double area_b = std::max(0.0, b.x2 - b.x1) * std::max(0.0, b.y2 - b.y1);
    const double union_area = area_a + area_b - inter;
    if (union_area <= 1e-12) return 0.0;
    return inter / union_area;
}

inline std::vector<std::vector<double>> iouMatrix(
    const std::vector<Box>& a, const std::vector<Box>& b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<double>> out(n, std::vector<double>(m, 0.0));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < m; ++j)
            out[i][j] = iou(a[i], b[j]);
    return out;
}

// ----------------------------------------------------------------------
// 匈牙利算法（方阵最小代价完全匹配，e-maxx 实现，O(n^3)）
// ----------------------------------------------------------------------
inline std::vector<std::pair<int, int>> hungarian(
    const std::vector<std::vector<double>>& cost) {
    const int n = static_cast<int>(cost.size());
    std::vector<std::pair<int, int>> result;
    if (n == 0) return result;

    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0), minv(n + 1);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    std::vector<bool> used(n + 1, false);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        for (int j = 0; j <= n; ++j) {
            minv[j] = std::numeric_limits<double>::infinity();
            used[j] = false;
        }
        do {
            used[j0] = true;
            const int i0 = p[j0];
            double delta = std::numeric_limits<double>::infinity();
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (!used[j]) {
                    const double cur =
                        cost[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    for (int j = 1; j <= n; ++j)
        if (p[j] != 0)
            result.push_back(std::make_pair(p[j] - 1, j - 1));
    return result;
}

// ----------------------------------------------------------------------
// 关联：检测框 vs 预测框，按 IoU 代价 + 阈值门限
// thresh 为代价阈值：cost = 1 - IoU <= thresh 的配对才被接受（即 IoU >= 1 - thresh）。
// ----------------------------------------------------------------------
inline void associate(
    const std::vector<Box>& dets, const std::vector<Box>& trks,
    double thresh,
    std::vector<std::pair<int, int>>& matches,
    std::vector<int>& unmatched_dets,
    std::vector<int>& unmatched_trks) {
    matches.clear();
    unmatched_dets.clear();
    unmatched_trks.clear();

    const int n = static_cast<int>(dets.size());
    const int m = static_cast<int>(trks.size());
    if (n == 0) {
        for (int j = 0; j < m; ++j) unmatched_trks.push_back(j);
        return;
    }
    if (m == 0) {
        for (int i = 0; i < n; ++i) unmatched_dets.push_back(i);
        return;
    }

    // 代价矩阵 = 1 - IoU，补成方阵后做匈牙利匹配
    const std::vector<std::vector<double>> ious = iouMatrix(dets, trks);
    const int size = std::max(n, m);
    std::vector<std::vector<double>> cost_pad(
        size, std::vector<double>(size, 1e5));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j)
            cost_pad[i][j] = 1.0 - ious[i][j];

    const std::vector<std::pair<int, int>> assign = hungarian(cost_pad);

    std::vector<bool> det_matched(n, false);
    std::vector<bool> trk_matched(m, false);
    for (std::size_t k = 0; k < assign.size(); ++k) {
        const int i = assign[k].first;
        const int j = assign[k].second;
        if (i < n && j < m && cost_pad[i][j] <= thresh) {
            matches.push_back(std::make_pair(i, j));
            det_matched[i] = true;
            trk_matched[j] = true;
        }
    }
    for (int i = 0; i < n; ++i)
        if (!det_matched[i]) unmatched_dets.push_back(i);
    for (int j = 0; j < m; ++j)
        if (!trk_matched[j]) unmatched_trks.push_back(j);
}

// ----------------------------------------------------------------------
// 单目标框 Kalman 滤波器（8 维线性匀速模型）
// 状态：x = [cx, cy, a, h, vcx, vcy, va, vh]
// 运动模型：F = [[I4, dt*I4], [0, I4]]，观测 H = [I4 | 0]
// ----------------------------------------------------------------------
// D4：形参必须是 8 维（kf.mean() 是 [cx,cy,a,h,vcx,vcy,va,vh]）；
// 旧签名写 4 维导致 Track::box() 调用点编译不过（该头文件从未被任何 .cc include，
// 所以 make test 一直发现不了）。函数体只使用 x[0..3]。
inline Box xyahToBox(const std::array<double, 8>& x) {
    const double cx = x[0], cy = x[1], a = x[2], h = x[3];
    Box b;
    if (a <= 0.0 || h <= 0.0) {
        b.x1 = b.x2 = cx;
        b.y1 = b.y2 = cy;
        return b;
    }
    const double w = a * h;
    b.x1 = cx - 0.5 * w;
    b.y1 = cy - 0.5 * h;
    b.x2 = cx + 0.5 * w;
    b.y2 = cy + 0.5 * h;
    return b;
}

inline std::array<double, 4> boxToXyah(const Box& b) {
    const double w = std::max(b.x2 - b.x1, 1e-6);
    const double h = std::max(b.y2 - b.y1, 1e-6);
    std::array<double, 4> z;
    z[0] = b.x1 + 0.5 * w;   // cx
    z[1] = b.y1 + 0.5 * h;   // cy
    z[2] = w / h;            // a = w/h
    z[3] = h;              // h
    return z;
}

class KalmanBoxFilter {
public:
    explicit KalmanBoxFilter(double dt = 1.0,
                             double std_pos = 1.0 / 20.0,
                             double std_vel = 1.0 / 160.0)
        : m_dt(dt), m_stdPos(std_pos), m_stdVel(std_vel), m_init(false) {
        m_mean.fill(0.0);
        m_cov.fill(0.0);
    }

    /** 用观测 [cx, cy, a, h] 初始化。 */
    void initiate(const std::array<double, 4>& z) {
        for (int i = 0; i < 4; ++i) m_mean[i] = z[i];
        for (int i = 4; i < 8; ++i) m_mean[i] = 0.0;

        const double h = std::max(z[3], 1e-3);
        // 初始速度不确定度取 10 倍，位置取 2 倍（与原始 ByteTrack 一致）
        const double std_pos_arr[4] = {
            2 * m_stdPos * h, 2 * m_stdPos * h, 1e-2, 2 * m_stdPos * h};
        const double std_vel_arr[4] = {
            10 * m_stdVel * h, 10 * m_stdVel * h, 1e-5, 10 * m_stdVel * h};

        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 8; ++j) m_cov[i * 8 + j] = 0.0;
            const double s = (i < 4) ? std_pos_arr[i] : std_vel_arr[i - 4];
            m_cov[i * 8 + i] = s * s;
        }
        m_init = true;
    }

    /** 一步状态预测（漏检时自然外推）。 */
    void predict() {
        if (!m_init) return;
        // x = F x
        std::array<double, 8> x_new;
        for (int i = 0; i < 8; ++i) {
            x_new[i] = m_mean[i];
            if (i < 4) x_new[i] += m_dt * m_mean[i + 4];
        }
        m_mean = x_new;

        // P = F P F^T + Q
        std::array<double, 64> FP;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                double s = m_cov[i * 8 + j];
                if (i < 4) s += m_dt * m_cov[(i + 4) * 8 + j];
                FP[i * 8 + j] = s;
            }
        const double h = std::max(m_mean[3], 1e-3);
        const double q_pos[4] = {
            m_stdPos * h, m_stdPos * h, 1e-2, m_stdPos * h};
        const double q_vel[4] = {
            m_stdVel * h, m_stdVel * h, 1e-5, m_stdVel * h};
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                // FP * F^T: 只考虑列耦合（F^T 在列 4..7 有非零 dt）
                double s = FP[i * 8 + j];
                if (j < 4) s += m_dt * FP[i * 8 + (j + 4)];
                // 加过程噪声 Q（对角）
                double q = 0.0;
                if (i == j) {
                    const double sq =
                        (i < 4) ? q_pos[i] : q_vel[i - 4];
                    q = sq * sq;
                }
                m_cov[i * 8 + j] = s + q;
            }
    }

    /** 用观测 [cx, cy, a, h] 做标准 Kalman 更新。 */
    void update(const std::array<double, 4>& z) {
        if (!m_init) return;

        // S = H P H^T + R（4x4，H P H^T = P 的左上 4x4 块）
        const double h = std::max(m_mean[3], 1e-3);
        const double r_std[4] = {
            m_stdPos * h, m_stdPos * h, 1e-1, m_stdPos * h};
        std::array<double, 16> S;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                S[i * 4 + j] = m_cov[i * 8 + j];
                if (i == j) S[i * 4 + i] += r_std[i] * r_std[i];
            }

        // S 求逆（4x4，Gauss-Jordan）。S 为对称正定（P+R），主元必非零；
        // 若数值退化则返回单位阵兜底。
        std::array<double, 16> Sinv;
        Sinv.fill(0.0);
        for (int i = 0; i < 4; ++i) Sinv[i * 4 + i] = 1.0;
        {
            std::array<double, 32> aug;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 8; ++j)
                    aug[i * 8 + j] =
                        (j < 4) ? S[i * 4 + j] : ((j - 4 == i) ? 1.0 : 0.0);
            bool ok = true;
            for (int col = 0; col < 4 && ok; ++col) {
                int pivot = col;
                for (int r = col + 1; r < 4; ++r)
                    if (std::fabs(aug[r * 8 + col]) >
                        std::fabs(aug[pivot * 8 + col]))
                        pivot = r;
                if (std::fabs(aug[pivot * 8 + col]) < 1e-12) {
                    ok = false;
                    break;
                }
                if (pivot != col)
                    for (int j = 0; j < 8; ++j)
                        std::swap(aug[col * 8 + j], aug[pivot * 8 + j]);
                const double d = aug[col * 8 + col];
                for (int j = 0; j < 8; ++j) aug[col * 8 + j] /= d;
                for (int r = 0; r < 4; ++r) {
                    if (r == col) continue;
                    const double factor = aug[r * 8 + col];
                    if (factor == 0.0) continue;
                    for (int j = 0; j < 8; ++j)
                        aug[r * 8 + j] -= factor * aug[col * 8 + j];
                }
            }
            if (ok)
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        Sinv[i * 4 + j] = aug[i * 8 + (4 + j)];
        }

        // 新息 y = z - H x
        std::array<double, 4> y;
        for (int i = 0; i < 4; ++i) y[i] = z[i] - m_mean[i];

        // K = P H^T S^-1（8x4）：K[i][j] = sum_k P[i][k] * Sinv[k][j]
        std::array<double, 32> K;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) {
                double s = 0.0;
                for (int k = 0; k < 4; ++k)
                    s += m_cov[i * 8 + k] * Sinv[k * 4 + j];
                K[i * 4 + j] = s;
            }

        // x = x + K y
        std::array<double, 8> x_new;
        for (int i = 0; i < 8; ++i) {
            double s = m_mean[i];
            for (int j = 0; j < 4; ++j) s += K[i * 4 + j] * y[j];
            x_new[i] = s;
        }
        m_mean = x_new;

        // P = (I - K H) P
        std::array<double, 64> KH;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                double s = 0.0;
                for (int k = 0; k < 4; ++k)
                    s += K[i * 4 + k] * (k == j ? 1.0 : 0.0);
                KH[i * 8 + j] = s;
            }
        std::array<double, 64> IKH_P;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                double s = 0.0;
                for (int k = 0; k < 8; ++k) {
                    const double ikh = ((i == k) ? 1.0 : 0.0) - KH[i * 8 + k];
                    s += ikh * m_cov[k * 8 + j];
                }
                IKH_P[i * 8 + j] = s;
            }
        m_cov = IKH_P;
    }

    std::array<double, 8> mean() const { return m_mean; }
    std::array<double, 64> covariance() const { return m_cov; }
    bool initialized() const { return m_init; }

private:
    double m_dt;
    double m_stdPos;
    double m_stdVel;
    bool m_init;
    std::array<double, 8> m_mean;
    std::array<double, 64> m_cov;
};

}  // namespace bt_detail

// ----------------------------------------------------------------------
// 对外接口：轨迹输出 / 轨迹信息
// ----------------------------------------------------------------------
/** 一帧输出的一条轨迹。 */
struct TrackResult {
    int id;
    double x1, y1, x2, y2, score;
};

/** 一条轨迹的生命周期信息。 */
struct TrackInfo {
    bt_detail::Box bbox;
    double score;
    int hits;
    int misses;
    int age;
    int last_seen;
    int start_frame;
    bool stable;     // 等价于 confirmed
    bool confirmed;
};

/**
 * @brief ByteTrack 多目标跟踪器（接口与 Python 版一致）。
 *
 * 参数含义与 Python 版 ByteTrack 完全相同：
 * - track_thresh：高分检测阈值（默认 0.5）；
 * - det_thresh：检测置信度下界（默认 0.1），低于该值丢弃；
 * - match_thresh：IoU 代价阈值（即 1 - IoU <= match_thresh 才接受，默认 0.8，
 *   等价于要求 IoU >= 0.2；数值越大越宽松）；
 * - max_lost：轨迹连续未关联多少帧即删除（默认 30）；
 * - min_hits：新轨迹连续关联多少帧后被标记为已确认（默认 3）；
 * - dt：固定帧间隔（默认 1.0，即逐帧演化）；
 * - output_confirmed_only：为 true 时只输出已确认轨迹。
 */
class ByteTrack {
public:
    ByteTrack(double track_thresh = 0.5,
               double det_thresh = 0.1,
               double match_thresh = 0.8,
               int max_lost = 30,
               int min_hits = 3,
               double dt = 1.0,
               bool output_confirmed_only = false)
        : m_trackThresh(track_thresh),
          m_detThresh(det_thresh),
          m_matchThresh(match_thresh),
          m_maxLost(max_lost),
          m_minHits(min_hits),
          m_dt(dt),
          m_outputConfirmedOnly(output_confirmed_only),
          m_nextId(0),
          m_frameId(0) {}

    /**
     * @brief 参数合法性校验（条件与 Python 版 ByteTrack.__init__ 的 ValueError 一致）。
     *
     * 板端常以 -fno-exceptions 编译，故不抛异常：构造器保持宽容，由调用方显式
     * 校验，避免"阈值写反了但静默跑偏"（例如 match_thresh > 1 会让任意零 IoU
     * 配对都通过代价门限，直接造成 ID 串号）。
     *
     * @param err 可选，失败时写入原因。
     * @return 全部参数合法返回 true。
     */
    static bool paramsValid(double track_thresh, double det_thresh,
                            double match_thresh, int max_lost, int min_hits,
                            std::string* err = 0) {
        if (!(track_thresh > 0.0 && track_thresh <= 1.0)) {
            if (err) *err = "track_thresh 必须在 (0, 1]";
            return false;
        }
        if (!(det_thresh >= 0.0 && det_thresh < track_thresh)) {
            if (err) *err = "det_thresh 必须满足 0 <= det_thresh < track_thresh";
            return false;
        }
        if (!(match_thresh > 0.0 && match_thresh <= 1.0)) {
            if (err) *err = "match_thresh 必须在 (0, 1]";
            return false;
        }
        if (max_lost < 1) {
            if (err) *err = "max_lost 必须为正整数";
            return false;
        }
        if (min_hits < 1) {
            if (err) *err = "min_hits 必须为正整数";
            return false;
        }
        return true;
    }

    /** 当前实例的构参是否合法（等价于 paramsValid(构造器入参)）。 */
    bool paramsValid() const {
        return paramsValid(m_trackThresh, m_detThresh, m_matchThresh,
                           m_maxLost, m_minHits, 0);
    }

    /**
     * 输入一帧检测结果，返回所有活跃轨迹。
     *
     * @param detections 检测框列表，每元素为 (x1, y1, x2, y2, score)；
     *                   长度 < 5 或含 NaN/Inf 的行被**丢弃**（防御上游异常，
     *                   Python 版对应位置会抛 IndexError；板端宁可丢一帧检测，
     *                   也不能越界读 UB）。
     * @param frame_id   显式帧号；<0 时使用内部自增计数。注意：<=1 的帧号会让
     *                   本帧新建轨迹直接 confirmed（见文件头契约第 1 条）。
     * @return 按 track_id 升序的 TrackResult 列表（含未确认轨迹）。
     */
    std::vector<TrackResult> update(
        const std::vector<std::vector<double>>& detections,
        int frame_id = -1) {
        if (frame_id < 0) {
            ++m_frameId;
            frame_id = m_frameId;
        } else {
            m_frameId = frame_id;
        }

        // 1. 预测所有轨迹
        for (Track& trk : m_tracks) trk.predict();

        // 2. 划分高低分检测
        std::vector<bt_detail::Box> high_boxes, low_boxes;
        std::vector<double> high_scores, low_scores;
        for (const auto& d : detections) {
            if (d.size() < 5) continue;                     // 防御：行长度不足
            if (!std::isfinite(d[0]) || !std::isfinite(d[1]) ||
                !std::isfinite(d[2]) || !std::isfinite(d[3]) ||
                !std::isfinite(d[4]))
                continue;                                   // 防御：NaN / Inf
            bt_detail::Box b{d[0], d[1], d[2], d[3]};
            const double score = d[4];
            if (score >= m_trackThresh) {
                high_boxes.push_back(b);
                high_scores.push_back(score);
            } else if (score >= m_detThresh) {
                low_boxes.push_back(b);
                low_scores.push_back(score);
            }
        }

        const std::size_t n_trk = m_tracks.size();

        // 3. 第一轮关联：高分检测 vs 全部轨迹
        std::vector<int> unmatched_high;
        std::vector<int> unmatched_trk;
        std::vector<bt_detail::Box> trk_boxes;
        for (const Track& trk : m_tracks) trk_boxes.push_back(trk.box());

        if (!high_boxes.empty() && n_trk > 0) {
            std::vector<std::pair<int, int>> matches1;
            // 代价阈值 = m_matchThresh（1-IoU <= matchThresh 即 IoU >= 1-matchThresh）
            associate(high_boxes, trk_boxes, m_matchThresh,
                      matches1, unmatched_high, unmatched_trk);
            for (std::size_t k = 0; k < matches1.size(); ++k) {
                Track& trk = m_tracks[matches1[k].second];
                trk.update(high_boxes[matches1[k].first], high_scores[matches1[k].first],
                           frame_id);
                if (trk.hits >= m_minHits) trk.confirmed = true;
            }
        } else {
            for (int i = 0; i < static_cast<int>(high_boxes.size()); ++i)
                unmatched_high.push_back(i);
            for (int j = 0; j < static_cast<int>(n_trk); ++j)
                unmatched_trk.push_back(j);
        }

        // 4. 第二轮关联（低分二次关联）：低分检测 vs 未匹配的已确认轨迹
        std::vector<bt_detail::Box> conf_boxes;
        std::vector<int> conf_trk_pos;
        for (std::size_t k = 0; k < unmatched_trk.size(); ++k) {
            const int pos = unmatched_trk[k];
            if (pos < static_cast<int>(n_trk) && m_tracks[pos].confirmed) {
                conf_boxes.push_back(m_tracks[pos].box());
                conf_trk_pos.push_back(pos);
            }
        }
        if (!low_boxes.empty() && !conf_boxes.empty()) {
            std::vector<std::pair<int, int>> matches2;
            std::vector<int> unused_det, unused_trk;
            // 低分二次关联同样使用 m_matchThresh 作为代价阈值
            associate(low_boxes, conf_boxes, m_matchThresh,
                      matches2, unused_det, unused_trk);
            for (std::size_t k = 0; k < matches2.size(); ++k) {
                Track& trk = m_tracks[conf_trk_pos[matches2[k].second]];
                trk.update(low_boxes[matches2[k].first],
                           low_scores[matches2[k].first], frame_id);
                if (trk.hits >= m_minHits) trk.confirmed = true;
            }
        }

        // 5. 未匹配的高分检测 -> 新建轨迹
        for (std::size_t k = 0; k < unmatched_high.size(); ++k) {
            const bt_detail::Box& b = high_boxes[unmatched_high[k]];
            createTrack(b, high_scores[unmatched_high[k]], frame_id,
                        frame_id <= 1);
        }

        // 6. 删除过期轨迹
        prune();

        return states();
    }

    /** 返回所有活跃轨迹（与 Python 版 states() 一致）。 */
    std::vector<TrackResult> states() const {
        std::vector<TrackResult> out;
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            const Track& trk = m_tracks[i];
            if (m_outputConfirmedOnly && !trk.confirmed) continue;
            const bt_detail::Box b = trk.box();
            TrackResult r;
            r.id = trk.id;
            r.x1 = b.x1;
            r.y1 = b.y1;
            r.x2 = b.x2;
            r.y2 = b.y2;
            r.score = trk.score;
            out.push_back(r);
        }
        std::sort(out.begin(), out.end(),
                  [](const TrackResult& a, const TrackResult& b) {
                      return a.id < b.id;
                  });
        return out;
    }

    /** 返回每条轨迹的生命周期信息（按 track_id 索引）。 */
    std::vector<TrackInfo> trackInfo() const {
        std::vector<TrackInfo> out;
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            const Track& trk = m_tracks[i];
            TrackInfo info;
            info.bbox = trk.box();
            info.score = trk.score;
            info.hits = trk.hits;
            info.misses = trk.misses;
            info.age = trk.age;
            info.last_seen = trk.last_seen;
            info.start_frame = trk.start_frame;
            info.stable = trk.confirmed;
            info.confirmed = trk.confirmed;
            out.push_back(info);
        }
        return out;
    }

    int trackCount() const { return static_cast<int>(m_tracks.size()); }

    void reset() {
        m_tracks.clear();
        m_nextId = 0;
        m_frameId = 0;
    }

private:
    /** 内部轨迹对象。 */
    struct Track {
        int id;
        bt_detail::KalmanBoxFilter kf;
        double score;
        int hits;
        int misses;
        int age;
        int last_seen;
        int start_frame;
        bool confirmed;

        Track() : id(0), score(0.0), hits(0), misses(0), age(0),
                  last_seen(0), start_frame(0), confirmed(false) {}

        void predict() {
            kf.predict();
            ++age;
            ++misses;
        }

        void update(const bt_detail::Box& b, double score_in, int frame_id) {
            kf.update(bt_detail::boxToXyah(b));
            score = score_in;
            ++hits;
            misses = 0;
            last_seen = frame_id;
        }

        bt_detail::Box box() const { return bt_detail::xyahToBox(kf.mean()); }
    };

    void createTrack(const bt_detail::Box& b, double score, int frame_id,
                     bool first_frame) {
        Track trk;
        trk.id = m_nextId++;
        trk.kf = bt_detail::KalmanBoxFilter(m_dt);
        trk.kf.initiate(bt_detail::boxToXyah(b));
        trk.score = score;
        trk.hits = 1;
        trk.misses = 0;
        trk.age = 1;
        trk.last_seen = frame_id;
        trk.start_frame = frame_id;
        trk.confirmed = first_frame || (m_minHits <= 1);
        m_tracks.push_back(trk);
    }

    void prune() {
        std::vector<Track> alive;
        alive.reserve(m_tracks.size());
        for (std::size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].misses <= m_maxLost)
                alive.push_back(m_tracks[i]);
        m_tracks.swap(alive);
    }

    double m_trackThresh;
    double m_detThresh;
    double m_matchThresh;
    int m_maxLost;
    int m_minHits;
    double m_dt;
    bool m_outputConfirmedOnly;
    int m_nextId;
    int m_frameId;
    std::vector<Track> m_tracks;
};

#endif  // PEDESTRIAN_BYTETRACK_TRACKER_H