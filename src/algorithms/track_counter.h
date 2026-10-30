/**
 * @file track_counter.h
 * @brief 轨迹级计数去重 + ROI 进出事件（ByteTrack 接线前置模块，C++11 header-only）。
 *
 * 本文件是 src/algorithms/track_counter.py 的逐值镜像：两套实现在
 * tests/test_cpp_python_parity.py 里用真实编译产物做逐值比较。纯标准库
 * （<algorithm>/<cmath>/<map>/<vector>/<utility>/<limits>），可直接交叉编译进
 * Luckfox 工程。
 *
 * 为什么需要本模块
 * ----------------
 * 板端旧口径把"当帧检测框数"直接当人数（src/luckfox_rtsp_yolov5/src/main.cc
 * 的 dangerPersons / featN，以及 enhanced_decision.cc 的贪心最近邻 + Kalman 平滑）。
 * YOLO 对同一行人吐出两个不同 anchor 尺度的框时，只要脚点距离超过 2cm，
 * GraphLaplacian 的 2cm 去重就兜不住，人数直接 +1 —— 即参考作品第 4 个坑
 * "跟踪器重复计数"在本项目里的同构形态。本模块把计数口径从"框"提升到"轨迹身份"：
 *
 *     同一条身份（identity）跨帧只计 1 人。
 *
 * 五个必须讲清的语义
 * ------------------
 * 1. 同一轨迹跨帧只计 1 人：人数 = 本帧出现且已确认的"身份"个数，而不是轨迹条数、
 *    也不是检测框数。一条轨迹持续 N 帧，任何一帧都只贡献 1。
 *
 * 2. ID 切换 / 短时丢失的合并策略（re-id merge）：ByteTrack 在遮挡后重现、或检测框
 *    IoU 掉到门限以下时会分配新 track_id。本模块维护 track_id -> identity_id 映射：
 *    新 track_id 若脚点落在某条"本帧未出现、且丢失不超过 reid_max_gap 帧"的身份的
 *    reid_radius 半径内，则并入该身份（取最近者，并列取 identity_id 小者），而不是
 *    新建身份。合并是粘性的，帧间不会来回翻转（避免计数抖动）。reid_radius<=0 关闭。
 *
 * 3. 未确认轨迹（tentative）不计入人数：ByteTrack 的 confirmed 由 min_hits 决定
 *    （默认连续命中 3 帧）。未确认轨迹只参与身份登记（防止它另起炉灶），不计人数、
 *    不产生 ROI 进出事件。count_tentative=true 可放开（调试用）。
 *
 * 4. 与 min_danger_n 门控的交互：risk_fsm 的 min_danger_n（默认 3）是 DANGER 的硬门槛。
 *    旧口径下"同一人两个框"就能把 n 从 1 顶到 2，两个人就能凑出 n>=3 而误升 DANGER。
 *    Result::gate_n 是应喂给 FSM 的 n（去重后人数），gate_ok / legacy_gate_ok 用来量化
 *    "旧口径是否会误开门"。
 *
 * 5. 与脚点 2cm 去重的先后顺序：(a) 跨帧身份归并（re-id merge，时间维） ->
 *    (b) 同帧空间去重（三条判据见 samePersonInFrame，空间维） ->
 *    (c) GraphLaplacian 内部的 2cm 去重（在平滑脚点上再做一次，几何兜底）。
 *    即"先身份、后空间、最后几何兜底"。(b) 是粘性合并：同帧两身份脚点距离/框包含
 *    关系命中时永久并为一个身份（置信度高者为规范身份，并列取 ID 小者），而不是
 *    每帧重新选一次 —— 每帧重选会在置信度抖动时造成计数抖动。
 *    (c) 保留在 GraphLaplacian 里不动，与本模块不冲突（只会更少不会更多）。
 *
 * 坐标系与单位（板端现实：ROI 是像素，脚点是 IPM 后的米制）
 * --------------------------------------------------------
 * - Obs::foot_x/foot_y：去重 / re-id 用的脚点，单位与 dedup_tol / dup_foot_max /
 *   reid_radius 一致（板端=IPM 后的米）；has_foot=false 时退回 box 底边中点。
 * - Obs::roi_x/roi_y：ROI 判定点，单位与 roi_polygon / roi_hysteresis 一致
 *   （板端=像素）；has_roi=false 时退回 box 中心。
 * 两套点各自成体系、互不换算。
 *
 * ROI 滞回 + 最短驻留
 * -------------------
 * 对多边形做"点到边界的有符号距离" sd（内部为正、外部为负）：
 * - 当前提交状态 OUT：sd >= +roi_hysteresis 才算"进入候选"（必须深入内部）；
 * - 当前提交状态 IN ：sd > -roi_hysteresis 才算"仍在内部"（允许贴边抖动）。
 * 滞回带内的点保持上一状态；状态翻转还须连续满足 min_dwell_frames 帧（最短驻留），
 * 缺席帧 / 未确认帧按 outside 计。事件只在翻转提交那一帧产生，并带 since_frame。
 * 身份被合并时沿用更接近 IN 的一侧状态，避免 ID 切换 / 重复框把一次进入拆成两次。
 *
 * 用法示例:
 *   @code
 *   #include "track_counter.h"
 *
 *   track_count::Config cfg;              // 默认：2cm 脚点去重 + 20cm 放宽 + 框包含
 *   cfg.roi_polygon = {{580,50},{700,50},{700,430},{580,430}};   // 像素
 *   cfg.min_danger_n = 3;                 // 与 config/default_config.json 的 fsm 一致
 *   track_count::TrackCounter counter(cfg);
 *
 *   // 每帧：ByteTrack::states() + trackInfo() 组装观测
 *   track_count::FrameObs fo;
 *   fo.frame_id = frame_no;
 *   fo.n_detections = det_count;          // 旧口径基线（<0 = 用 tracks.size()）
 *   for (const TrackResult& t : tracks) {
 *       track_count::Obs o;
 *       o.track_id = t.id; o.x1 = t.x1; o.y1 = t.y1; o.x2 = t.x2; o.y2 = t.y2;
 *       o.score = t.score; o.confirmed = true;
 *       o.has_foot = true; o.foot_x = ground_x; o.foot_y = ground_y;   // IPM 米制
 *       fo.tracks.push_back(o);
 *   }
 *   const track_count::Result r = counter.update(fo);
 *   int n_for_fsm = r.gate_n;             // 喂给 RiskFSM 的 n
 *   @endcode
 *
 * 依赖：C++11；仅标准库。不依赖 OpenCV / RKNN / RTSP。
 */

#ifndef PEDESTRIAN_TRACK_COUNTER_H
#define PEDESTRIAN_TRACK_COUNTER_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace track_count {

// ----------------------------------------------------------------------
// 配置
// ----------------------------------------------------------------------
/**
 * 计数去重配置（单位见各字段说明，默认值与 Python 版 TrackCounterConfig 完全一致）。
 */
struct Config {
    // 同帧脚点去重容差（米，与 Obs::foot 同单位）。默认 0.02 = 2cm，与
    // graph_laplacian 的 DEDUP_TOL_M 对齐。<=0 关闭该分支。
    double dedup_tol;
    // 同帧"脚点近到物理上不可能有两个人"的上界（米）。默认 0.20：两个成年人的
    // 地面脚点相距 20cm 以内等同同一人。是 dedup_tol 的放宽版，覆盖不同 anchor
    // 尺度导致框底不一致的重复框。<=0 关闭该分支。
    double dup_foot_max;
    // 同帧"框包含式"重复判据的 IoMin 门限（交集 / 较小框面积）。默认 0.75。
    // 覆盖"脚点差得远但身体区域高度重叠"的重复框；等大框并排错开 25% 宽度才会
    // 误并（40px 框 = 10px，正常两人不会站这么近），并额外要求两框中心互含。
    // <=0 关闭该分支。
    double dup_iou_min;
    // 框包含式重复判据的高度比上限（默认 1.5）。防止"远处小人整框落在近处大人
    // 框内"被误判为同一人。
    double dup_height_ratio;
    // ID 切换 / 短时丢失合并半径（米）。默认 0.6。<=0 关闭 re-id。
    double reid_radius;
    // 允许的丢失帧数（超过则不再合并）。默认 15。
    int reid_max_gap;
    // 身份记录回收：已 OUT 且连续 identity_ttl 帧未出现则删除记录。默认 300。
    int identity_ttl;
    // ROI 多边形（像素），点数 < 3 = 不启用 ROI 事件。
    std::vector<std::pair<double, double> > roi_polygon;
    // ROI 滞回带半宽（像素）。默认 8.0；<=0 时退化为普通点在多边形判定。
    double roi_hysteresis;
    // 最短驻留帧数（进入 / 离开都要连续满足）。默认 3，>=1。
    int min_dwell_frames;
    // 与 risk_fsm.min_danger_n 对齐的门控门槛。默认 3。
    int min_danger_n;
    // 是否把未确认轨迹也计入人数（默认 false）。
    bool count_tentative;
    // "滑行帧数"：轨迹本帧未被关联（ByteTrack 仍输出外推预测框，Obs::misses > 0）
    // 时最多再算它存在几帧。默认 1 —— 允许单帧漏检不抖人数，但连续 2 帧以上没被
    // 关联就不再计入人数（否则人已出画，ByteTrack 还会带预测框滑行 max_lost 帧）。
    // 0 = 只算本帧真正关联上的轨迹。滑行期间 ROI 状态机不翻转。
    int coast_frames;

    Config()
        : dedup_tol(0.02),
          dup_foot_max(0.20),
          dup_iou_min(0.75),
          dup_height_ratio(1.5),
          reid_radius(0.6),
          reid_max_gap(15),
          identity_ttl(300),
          roi_polygon(),
          roi_hysteresis(8.0),
          min_dwell_frames(3),
          min_danger_n(3),
          count_tentative(false),
          coast_frames(1) {}
};

// ----------------------------------------------------------------------
// 输入结构
// ----------------------------------------------------------------------
/** 一帧里的一条轨迹观测（由 ByteTrack::states() + trackInfo() 组装）。 */
struct Obs {
    int track_id;          // ByteTrack 分配的轨迹 ID（必须 >= 0）
    double x1, y1, x2, y2; // 轨迹框（像素）
    double score;          // 最近一次关联检测的置信度
    bool confirmed;        // ByteTrack 的 confirmed（未确认轨迹不计人数）
    int misses;            // 连续未关联帧数（仅透传）
    double foot_x, foot_y; // 去重 / re-id 脚点（板端=IPM 米制）
    double roi_x, roi_y;   // ROI 判定点（板端=像素，通常取框中心）
    bool has_foot;         // false -> 用 box 底边中点
    bool has_roi;          // false -> 用 box 中心

    Obs()
        : track_id(-1), x1(0.0), y1(0.0), x2(0.0), y2(0.0), score(0.0),
          confirmed(true), misses(0), foot_x(0.0), foot_y(0.0),
          roi_x(0.0), roi_y(0.0), has_foot(false), has_roi(false) {}
};

/** 一帧输入。 */
struct FrameObs {
    int frame_id;              // 帧号，必须严格递增（re-id 丢失时长判断）
    std::vector<Obs> tracks;   // 本帧输出轨迹
    int n_detections;          // 当帧检测框数（旧口径基线）；<0 = 用 tracks.size()

    FrameObs() : frame_id(0), tracks(), n_detections(-1) {}
};

// ----------------------------------------------------------------------
// 输出结构
// ----------------------------------------------------------------------
/** 一个被计入人数的身份（本帧状态）。 */
struct PersonState {
    int identity_id;
    std::vector<int> track_ids;   // 本帧归并到该身份的所有轨迹 ID（升序）
    double x1, y1, x2, y2;
    double score;
    double foot_x, foot_y;
    double roi_x, roi_y;
    bool in_roi;                  // 滞回状态机当前是否 IN
    int dwell;                    // 当前状态连续帧数
    bool confirmed;

    PersonState()
        : identity_id(-1), track_ids(), x1(0.0), y1(0.0), x2(0.0), y2(0.0),
          score(0.0), foot_x(0.0), foot_y(0.0), roi_x(0.0), roi_y(0.0),
          in_roi(false), dwell(0), confirmed(false) {}
};

/** 一次 ROI 状态翻转（已过滞回 + 最短驻留）。 */
struct RoiEvent {
    int frame_id;
    int identity_id;
    int track_id;
    bool entering;
    int since_frame;          // 穿越实际开始帧号（提交帧号 = frame_id）
    double signed_distance;   // 提交帧对 ROI 边界的有符号距离（内部为正）

    RoiEvent()
        : frame_id(0), identity_id(-1), track_id(-1), entering(false),
          since_frame(0), signed_distance(0.0) {}
};

/** 累计统计（便于量化"改进前 vs 改进后"）。 */
struct CountStats {
    int frames;
    int duplicates_suppressed;    // Σ 被抑制的重复轨迹数（同帧空间合并）
    int id_switch_merges;         // Σ 跨帧 re-id 归并次数
    int total_legacy_count;       // Σ 旧口径（当帧检测框数）
    int total_spatial_count;      // Σ 仅同帧 dedup_tol 空间去重后
    int total_dedup_count;        // Σ 去重后人数
    int max_legacy_count;
    int max_dedup_count;
    int frames_legacy_gate_open;  // 旧口径下 n >= min_danger_n 的帧数
    int frames_dedup_gate_open;   // 去重后 n >= min_danger_n 的帧数

    CountStats()
        : frames(0), duplicates_suppressed(0), id_switch_merges(0),
          total_legacy_count(0), total_spatial_count(0), total_dedup_count(0),
          max_legacy_count(0), max_dedup_count(0),
          frames_legacy_gate_open(0), frames_dedup_gate_open(0) {}
};

/** 一帧计数结果。 */
struct Result {
    int frame_id;
    int n;                 // 去重后人数（轨迹身份口径）
    int n_detections;      // 旧口径：当帧检测框数
    int n_spatial_dedup;   // 只做同帧 dedup_tol 空间去重（不跨帧）
    int n_tracks;          // 输入轨迹条数
    int n_suppressed;      // 本帧被抑制的重复轨迹条数
    std::vector<PersonState> persons;
    std::vector<RoiEvent> events;
    int dangerous_now;     // 当前滞回状态为 IN 的身份数（OSD Dangerous Person）
    int visitors_total;    // 累计到访身份数（ID 合并后不重复计）
    int gate_n;            // 建议喂给 FSM 的 n（== n）
    bool gate_ok;          // gate_n >= min_danger_n
    bool legacy_gate_ok;   // 旧口径 n_detections >= min_danger_n
    CountStats stats;

    Result()
        : frame_id(0), n(0), n_detections(0), n_spatial_dedup(0), n_tracks(0),
          n_suppressed(0), persons(), events(), dangerous_now(0),
          visitors_total(0), gate_n(0), gate_ok(false), legacy_gate_ok(false),
          stats() {}
};

// ----------------------------------------------------------------------
// 几何：多边形内部判定 + 有符号距离
// ----------------------------------------------------------------------
/** 射线法（PNPOLY），与 src/luckfox_rtsp_yolov5/src/main.cc 的 pointInPolygonRoi
 *  同语义：边界上的点由射线判据决定（未做特殊处理）。 */
inline bool pointInPolygon(double x, double y,
                           const std::vector<std::pair<double, double> >& poly) {
    const std::size_t n = poly.size();
    if (n < 3) return false;
    bool inside = false;
    std::size_t j = n - 1;
    for (std::size_t i = 0; i < n; ++i) {
        const double xi = poly[i].first, yi = poly[i].second;
        const double xj = poly[j].first, yj = poly[j].second;
        if ((yi > y) != (yj > y)) {
            const double xCross = (xj - xi) * (y - yi) / (yj - yi) + xi;
            if (x < xCross) inside = !inside;
        }
        j = i;
    }
    return inside;
}

/** 点到线段的最短距离（与 Python 版逐步同序，保证逐值一致）。 */
inline double pointSegmentDistance(double px, double py, double ax, double ay,
                                   double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double d2 = dx * dx + dy * dy;
    if (d2 <= 1e-18) return std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
    double t = ((px - ax) * dx + (py - ay) * dy) / d2;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    const double cx = ax + t * dx, cy = ay + t * dy;
    return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
}

/** 点到多边形边界的最短距离，内部为正、外部为负；点数 < 3 返回 -inf。 */
inline double polygonSignedDistance(
    double x, double y, const std::vector<std::pair<double, double> >& poly) {
    const std::size_t n = poly.size();
    if (n < 3) return -std::numeric_limits<double>::infinity();
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < n; ++i) {
        const double ax = poly[i].first, ay = poly[i].second;
        const double bx = poly[(i + 1) % n].first, by = poly[(i + 1) % n].second;
        const double d = pointSegmentDistance(x, y, ax, ay, bx, by);
        if (d < best) best = d;
    }
    if (best == std::numeric_limits<double>::infinity()) best = 0.0;
    return pointInPolygon(x, y, poly) ? best : -best;
}

// ----------------------------------------------------------------------
// 对外接口
// ----------------------------------------------------------------------
/**
 * @brief 轨迹级计数去重 + ROI 进出事件（与 Python 版 TrackCounter 逐值一致）。
 *
 * 线程模型：与 main.cc 单线程 while 循环配合，同一实例只在一个线程内调用。
 * 生命周期：必须与 ByteTrack 同生命周期 —— ByteTrack::reset() 时同步调用
 * reset()，否则 track_id 计数会复用而身份映射错位。
 */
class TrackCounter {
public:
    explicit TrackCounter(const Config& cfg = Config()) : cfg_(cfg) {
        if (cfg_.min_dwell_frames < 1) cfg_.min_dwell_frames = 1;
        if (cfg_.reid_max_gap < 0) cfg_.reid_max_gap = 0;
        if (cfg_.identity_ttl < 0) cfg_.identity_ttl = 0;
        if (cfg_.min_danger_n < 1) cfg_.min_danger_n = 1;
        if (cfg_.coast_frames < 0) cfg_.coast_frames = 0;
        poly_ = cfg_.roi_polygon;
        reset();
    }

    /** 清空全部状态（必须与 ByteTrack::reset() 同步调用）。 */
    void reset() {
        next_identity_ = 0;
        t2i_.clear();
        ids_.clear();
        merge_.clear();
        frame_id_ = 0;
        visitors_total_ = 0;
        stats_ = CountStats();
    }

    /** 轨迹 ID -> 规范身份 ID（未登记返回 -1）。 */
    int identityOf(int track_id) const {
        std::map<int, int>::const_iterator it = t2i_.find(track_id);
        if (it == t2i_.end()) return -1;
        return canonical(it->second);
    }

    int mergeCount() const { return static_cast<int>(merge_.size()); }

    int identityCount() const {
        int c = 0;
        for (std::map<int, Identity>::const_iterator it = ids_.begin();
             it != ids_.end(); ++it)
            if (canonical(it->first) == it->first) ++c;
        return c;
    }

    const Config& config() const { return cfg_; }

    /** 处理一帧，返回去重后人数、目标身份列表与 ROI 进出事件。 */
    Result update(const FrameObs& frame) {
        Result res;
        const int frame_id = frame.frame_id;
        frame_id_ = frame_id;

        // ---- 0. 归一化输入（按 track_id 升序，防御上游重复输出） ----
        std::map<int, NObs> by_id;
        for (std::size_t i = 0; i < frame.tracks.size(); ++i) {
            NObs o;
            if (!normalize(frame.tracks[i], o)) continue;
            std::map<int, NObs>::iterator prev = by_id.find(o.track_id);
            if (prev == by_id.end() || better(o, prev->second)) by_id[o.track_id] = o;
        }
        std::vector<NObs> obs;
        obs.reserve(by_id.size());
        for (std::map<int, NObs>::iterator it = by_id.begin(); it != by_id.end(); ++it)
            obs.push_back(it->second);

        const int n_tracks = static_cast<int>(obs.size());
        int n_dets = frame.n_detections < 0 ? n_tracks : frame.n_detections;
        if (n_dets < 0) n_dets = 0;
        const int n_spatial = spatialDedupCount(obs, cfg_.dedup_tol);

        // ---- 1. 身份归并（跨帧 re-id）+ 登记 ----
        std::map<int, NObs> claims;
        std::map<int, std::vector<int> > canon_tids;
        for (std::size_t k = 0; k < obs.size(); ++k) {
            const NObs& o = obs[k];
            int ident = -1;
            std::map<int, int>::const_iterator m = t2i_.find(o.track_id);
            if (m != t2i_.end()) ident = canonical(m->second);
            if (ident < 0 || ids_.find(ident) == ids_.end())
                ident = reidMatch(o, claims, frame_id);
            if (ident < 0) ident = newIdentity();
            t2i_[o.track_id] = ident;
            std::map<int, NObs>::iterator c = claims.find(ident);
            if (c == claims.end() || better(o, c->second)) claims[ident] = o;
            canon_tids[ident].push_back(o.track_id);
        }

        // ---- 2. 同帧空间去重（粘性身份合并） ----
        std::vector<int> order;
        order.reserve(claims.size());
        for (std::map<int, NObs>::const_iterator it = claims.begin();
             it != claims.end(); ++it)
            order.push_back(it->first);
        std::sort(order.begin(), order.end(), OrderByScore(claims));

        std::vector<int> kept;
        int n_suppressed = 0;
        for (std::size_t k = 0; k < order.size(); ++k) {
            const int ident = order[k];
            bool absorbed = false;
            for (std::size_t j = 0; j < kept.size(); ++j) {
                if (samePersonInFrame(claims[kept[j]], claims[ident], cfg_)) {
                    mergeIdentity(ident, kept[j]);
                    ++n_suppressed;
                    ++stats_.duplicates_suppressed;
                    absorbed = true;
                    break;
                }
            }
            if (!absorbed) kept.push_back(ident);
        }

        // ---- 3. 更新规范身份的本帧观测 ----
        std::vector<int> present_canon;
        for (std::map<int, NObs>::const_iterator it = claims.begin();
             it != claims.end(); ++it) {
            if (canonical(it->first) != it->first) continue;
            const int ident = it->first;
            Identity& rec = ids_[ident];
            const NObs& o = it->second;
            rec.last_frame = frame_id;
            rec.last_foot_x = o.foot_x;
            rec.last_foot_y = o.foot_y;
            rec.last_roi_x = o.roi_x;
            rec.last_roi_y = o.roi_y;
            rec.x1 = o.x1; rec.y1 = o.y1; rec.x2 = o.x2; rec.y2 = o.y2;
            rec.last_score = o.score;
            rec.last_track_id = o.track_id;
            if (o.observed) rec.last_observed = frame_id;
            present_canon.push_back(ident);
        }

        // 合并（吸收）后，把轨迹 ID 列表归到规范身份上
        std::map<int, std::vector<int> > merged_tids;
        for (std::map<int, std::vector<int> >::const_iterator it = canon_tids.begin();
             it != canon_tids.end(); ++it) {
            const int canon = canonical(it->first);
            std::vector<int>& dst = merged_tids[canon];
            dst.insert(dst.end(), it->second.begin(), it->second.end());
        }
        for (std::map<int, std::vector<int> >::iterator it = merged_tids.begin();
             it != merged_tids.end(); ++it)
            std::sort(it->second.begin(), it->second.end());

        // ---- 4. ROI 状态机（滞回 + 最短驻留） ----
        std::vector<RoiEvent> events;
        const bool roi_enabled = poly_.size() >= 3;
        std::vector<int> counted;
        for (std::size_t k = 0; k < present_canon.size(); ++k) {
            const int ident = present_canon[k];
            const NObs& o = claims[ident];
            if (!(o.confirmed || cfg_.count_tentative)) continue;
            // 本帧真正关联上（observed），或最后一次关联距今不超过 coast_frames
            const Identity& rec = ids_[ident];
            if (o.observed || frame_id - rec.last_observed <= cfg_.coast_frames)
                counted.push_back(ident);
        }
        for (std::map<int, Identity>::iterator it = ids_.begin(); it != ids_.end(); ++it) {
            if (canonical(it->first) != it->first) continue;
            Identity& rec = it->second;
            if (rec.last_frame < 0) continue;
            const bool is_present =
                std::find(present_canon.begin(), present_canon.end(), it->first)
                != present_canon.end();
            const bool is_counted =
                std::find(counted.begin(), counted.end(), it->first) != counted.end();
            double sd;
            if (roi_enabled && is_present && is_counted)
                sd = polygonSignedDistance(rec.last_roi_x, rec.last_roi_y, poly_);
            else
                sd = -std::numeric_limits<double>::infinity();
            stepRoi(rec, sd, frame_id, events);
        }

        // ---- 5. 回收长期不出现的身份记录 ----
        prune(frame_id);

        // ---- 6. 统计 + 组装 ----
        for (std::size_t k = 0; k < counted.size(); ++k) {
            const int canon = counted[k];
            const NObs& o = claims[canon];
            const Identity& rec = ids_[canon];
            PersonState p;
            p.identity_id = canon;
            p.track_ids = merged_tids[canon];
            p.x1 = o.x1; p.y1 = o.y1; p.x2 = o.x2; p.y2 = o.y2;
            p.score = o.score;
            p.foot_x = o.foot_x; p.foot_y = o.foot_y;
            p.roi_x = o.roi_x; p.roi_y = o.roi_y;
            p.in_roi = rec.state_in;
            p.dwell = rec.dwell;
            p.confirmed = o.confirmed;
            res.persons.push_back(p);
        }

        const int n = static_cast<int>(res.persons.size());
        int dangerous = 0;
        for (std::map<int, Identity>::const_iterator it = ids_.begin();
             it != ids_.end(); ++it)
            if (canonical(it->first) == it->first && it->second.last_frame >= 0
                && it->second.state_in)
                ++dangerous;

        CountStats& st = stats_;
        ++st.frames;
        st.total_legacy_count += n_dets;
        st.total_spatial_count += n_spatial;
        st.total_dedup_count += n;
        if (n_dets > st.max_legacy_count) st.max_legacy_count = n_dets;
        if (n > st.max_dedup_count) st.max_dedup_count = n;
        if (n_dets >= cfg_.min_danger_n) ++st.frames_legacy_gate_open;
        if (n >= cfg_.min_danger_n) ++st.frames_dedup_gate_open;

        res.frame_id = frame_id;
        res.n = n;
        res.n_detections = n_dets;
        res.n_spatial_dedup = n_spatial;
        res.n_tracks = n_tracks;
        res.n_suppressed = n_suppressed;
        res.events = events;
        res.dangerous_now = dangerous;
        res.visitors_total = visitors_total_;
        res.gate_n = n;
        res.gate_ok = n >= cfg_.min_danger_n;
        res.legacy_gate_ok = n_dets >= cfg_.min_danger_n;
        res.stats = st;
        return res;
    }

private:
    // ---- 归一化后的观测（等价 Python 的 dict） ----
    struct NObs {
        int track_id;
        double x1, y1, x2, y2;
        double score;
        bool confirmed;
        int misses;
        bool observed;      // 本帧真正被关联上（misses == 0）
        double foot_x, foot_y;
        double roi_x, roi_y;
    };

    // ---- 内部身份记录 ----
    struct Identity {
        int identity_id;
        int last_frame;
        double last_foot_x, last_foot_y;
        double last_roi_x, last_roi_y;
        double x1, y1, x2, y2;
        double last_score;
        int last_track_id;
        int last_observed;  // 最近一次"真正被关联上"（misses==0）的帧号
        bool state_in;      // 已提交的 ROI 状态
        int dwell;          // 当前状态连续帧数
        bool pending_in;    // 待提交状态
        int pending_cnt;    // 待提交已连续帧数
        int since_frame;    // 待提交序列起始帧

        explicit Identity(int ident)
            : identity_id(ident), last_frame(-1), last_foot_x(0.0),
              last_foot_y(0.0), last_roi_x(0.0), last_roi_y(0.0),
              x1(0.0), y1(0.0), x2(0.0), y2(0.0), last_score(0.0),
              last_track_id(-1), last_observed(-1), state_in(false), dwell(0),
              pending_in(false), pending_cnt(0), since_frame(-1) {}
        Identity() : identity_id(-1), last_frame(-1), last_foot_x(0.0),
              last_foot_y(0.0), last_roi_x(0.0), last_roi_y(0.0),
              x1(0.0), y1(0.0), x2(0.0), y2(0.0), last_score(0.0),
              last_track_id(-1), last_observed(-1), state_in(false), dwell(0),
              pending_in(false), pending_cnt(0), since_frame(-1) {}
    };

    /** 规范身份选择：置信度高者优先，并列取 identity_id 小者。 */
    struct OrderByScore {
        const std::map<int, NObs>* claims;
        explicit OrderByScore(const std::map<int, NObs>& c) : claims(&c) {}
        bool operator()(int a, int b) const {
            const double sa = claims->find(a)->second.score;
            const double sb = claims->find(b)->second.score;
            if (sa != sb) return sa > sb;
            return a < b;
        }
    };

    /** 同身份代表观测选择：已确认 > 本帧真关联上 > 置信度高 > track_id 小。
     *  "本帧真关联上"（observed）优先很关键：一个身份下同时有滑行轨迹和外推轨迹时，
     *  代表观测必须取真正看到的那条，否则 misses / 位置会用到过期值。 */
    static bool better(const NObs& a, const NObs& b) {
        if (a.confirmed != b.confirmed) return a.confirmed;
        if (a.observed != b.observed) return a.observed;
        if (a.score != b.score) return a.score > b.score;
        return a.track_id < b.track_id;
    }

    static bool finite(double v) {
        return v == v && v != std::numeric_limits<double>::infinity()
               && v != -std::numeric_limits<double>::infinity()
               && std::isfinite(v);
    }

    bool normalize(const Obs& t, NObs& o) const {
        if (t.track_id < 0) return false;
        if (!finite(t.x1) || !finite(t.y1) || !finite(t.x2) || !finite(t.y2))
            return false;
        o.track_id = t.track_id;
        o.x1 = t.x1; o.y1 = t.y1; o.x2 = t.x2; o.y2 = t.y2;
        o.score = finite(t.score) ? t.score : 0.0;
        o.confirmed = t.confirmed;
        o.misses = t.misses;
        o.observed = t.misses <= 0;
        if (t.has_foot) { o.foot_x = t.foot_x; o.foot_y = t.foot_y; }
        else { o.foot_x = (t.x1 + t.x2) * 0.5; o.foot_y = t.y2; }
        if (t.has_roi) { o.roi_x = t.roi_x; o.roi_y = t.roi_y; }
        else { o.roi_x = (t.x1 + t.x2) * 0.5; o.roi_y = (t.y1 + t.y2) * 0.5; }
        if (!finite(o.foot_x) || !finite(o.foot_y) || !finite(o.roi_x)
            || !finite(o.roi_y))
            return false;
        return true;
    }

    /** 同帧两条轨迹是否"同一个人"（三条判据取或，理由见文件头语义 5）。 */
    static bool samePersonInFrame(const NObs& a, const NObs& b, const Config& cfg) {
        const double dx = a.foot_x - b.foot_x;
        const double dy = a.foot_y - b.foot_y;
        const double d2 = dx * dx + dy * dy;
        if (cfg.dedup_tol > 0.0 && d2 <= cfg.dedup_tol * cfg.dedup_tol) return true;
        if (cfg.dup_foot_max > 0.0 && d2 <= cfg.dup_foot_max * cfg.dup_foot_max)
            return true;
        if (cfg.dup_iou_min <= 0.0) return false;
        const double ha = a.y2 - a.y1;
        const double hb = b.y2 - b.y1;
        if (ha <= 0.0 || hb <= 0.0) return false;
        const double ratio = (ha >= hb) ? (ha / hb) : (hb / ha);
        if (ratio > cfg.dup_height_ratio) return false;
        const double ix1 = std::max(a.x1, b.x1);
        const double iy1 = std::max(a.y1, b.y1);
        const double ix2 = std::min(a.x2, b.x2);
        const double iy2 = std::min(a.y2, b.y2);
        const double inter = std::max(0.0, ix2 - ix1) * std::max(0.0, iy2 - iy1);
        const double area_a = std::max(0.0, a.x2 - a.x1) * std::max(0.0, a.y2 - a.y1);
        const double area_b = std::max(0.0, b.x2 - b.x1) * std::max(0.0, b.y2 - b.y1);
        const double min_area = (area_a <= area_b) ? area_a : area_b;
        if (min_area <= 0.0) return false;
        if (inter / min_area < cfg.dup_iou_min) return false;
        const double cax = (a.x1 + a.x2) * 0.5, cay = (a.y1 + a.y2) * 0.5;
        const double cbx = (b.x1 + b.x2) * 0.5, cby = (b.y1 + b.y2) * 0.5;
        const bool in_a = (b.x1 <= cax && cax <= b.x2 && b.y1 <= cay && cay <= b.y2);
        const bool in_b = (a.x1 <= cbx && cbx <= a.x2 && a.y1 <= cby && cby <= a.y2);
        return in_a && in_b;
    }

    /** 只做同帧 dedup_tol 空间去重（不跨帧）的数量 —— 中间口径，用于消融对比。 */
    static int spatialDedupCount(const std::vector<NObs>& obs, double tol) {
        if (tol <= 0.0 || obs.size() <= 1) return static_cast<int>(obs.size());
        std::vector<const NObs*> order;
        order.reserve(obs.size());
        for (std::size_t i = 0; i < obs.size(); ++i) order.push_back(&obs[i]);
        std::sort(order.begin(), order.end(), ObsByScore());
        std::vector<std::pair<double, double> > kept;
        const double r2 = tol * tol;
        for (std::size_t i = 0; i < order.size(); ++i) {
            bool dup = false;
            for (std::size_t j = 0; j < kept.size(); ++j) {
                const double dx = order[i]->foot_x - kept[j].first;
                const double dy = order[i]->foot_y - kept[j].second;
                if (dx * dx + dy * dy <= r2) { dup = true; break; }
            }
            if (!dup) kept.push_back(std::make_pair(order[i]->foot_x, order[i]->foot_y));
        }
        return static_cast<int>(kept.size());
    }

    struct ObsByScore {
        bool operator()(const NObs* a, const NObs* b) const {
            if (a->score != b->score) return a->score > b->score;
            return a->track_id < b->track_id;
        }
    };

    int canonical(int ident) const {
        int guard = 0;
        std::map<int, int>::const_iterator it = merge_.find(ident);
        while (it != merge_.end()) {
            ident = it->second;
            if (++guard > 64) break;      // 防御：合并链异常时截断
            it = merge_.find(ident);
        }
        return ident;
    }

    int newIdentity() {
        const int ident = next_identity_++;
        ids_[ident] = Identity(ident);
        return ident;
    }

    /** 为没有映射的新 track_id 找一个"刚丢失的"身份（ID 切换合并）。 */
    int reidMatch(const NObs& o, const std::map<int, NObs>& claims, int frame_id) {
        if (cfg_.reid_radius <= 0.0 || cfg_.reid_max_gap <= 0) return -1;
        int best_id = -1;
        double best_d2 = 0.0;
        const double r2 = cfg_.reid_radius * cfg_.reid_radius;
        for (std::map<int, Identity>::const_iterator it = ids_.begin();
             it != ids_.end(); ++it) {
            const int ident = it->first;
            if (canonical(ident) != ident) continue;
            if (claims.find(ident) != claims.end()) continue;   // 本帧已被占用
            const Identity& rec = it->second;
            if (rec.last_frame < 0) continue;
            const int gap = frame_id - rec.last_frame;
            if (gap <= 0 || gap > cfg_.reid_max_gap) continue;
            const double dx = o.foot_x - rec.last_foot_x;
            const double dy = o.foot_y - rec.last_foot_y;
            const double d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            if (best_id < 0 || d2 < best_d2 || (d2 == best_d2 && ident < best_id)) {
                best_id = ident;
                best_d2 = d2;
            }
        }
        if (best_id >= 0) ++stats_.id_switch_merges;
        return best_id;
    }

    /** 把 absorbed 身份并入 canon（粘性）。ROI 状态取更接近 IN 的一侧。 */
    void mergeIdentity(int absorbed, int canon) {
        if (absorbed == canon) return;
        Identity& a = ids_[absorbed];
        Identity& c = ids_[canon];
        if (a.state_in && !c.state_in) {
            c.state_in = true;
            c.dwell = a.dwell;
            c.pending_in = a.pending_in;
            c.pending_cnt = a.pending_cnt;
            c.since_frame = a.since_frame;
        } else if (a.state_in && c.state_in && a.dwell > c.dwell) {
            c.dwell = a.dwell;
        }
        if (a.last_frame > c.last_frame) {
            c.last_frame = a.last_frame;
            c.last_foot_x = a.last_foot_x;
            c.last_foot_y = a.last_foot_y;
            c.last_roi_x = a.last_roi_x;
            c.last_roi_y = a.last_roi_y;
        }
        merge_[absorbed] = canon;
        // 已登记到被吸收身份的轨迹改指规范身份
        for (std::map<int, int>::iterator it = t2i_.begin(); it != t2i_.end(); ++it)
            if (canonical(it->second) == canon) it->second = canon;
    }

    /** 单身份 ROI 状态机（滞回 + 最短驻留）。 */
    void stepRoi(Identity& rec, double sd, int frame_id,
                 std::vector<RoiEvent>& events) {
        const bool raw_in = rec.state_in ? (sd > -cfg_.roi_hysteresis)
                                         : (sd >= cfg_.roi_hysteresis);
        if (raw_in == rec.state_in) {
            ++rec.dwell;
            rec.pending_cnt = 0;
            return;
        }
        if (rec.pending_cnt > 0 && rec.pending_in == raw_in) {
            ++rec.pending_cnt;
        } else {
            rec.pending_in = raw_in;
            rec.pending_cnt = 1;
            rec.since_frame = frame_id;
        }
        if (rec.pending_cnt >= cfg_.min_dwell_frames) {
            rec.state_in = raw_in;
            rec.dwell = rec.pending_cnt;
            rec.pending_cnt = 0;
            if (raw_in) ++visitors_total_;
            RoiEvent e;
            e.frame_id = frame_id;
            e.identity_id = rec.identity_id;
            e.track_id = rec.last_track_id;
            e.entering = raw_in;
            e.since_frame = rec.since_frame;
            e.signed_distance = sd;
            events.push_back(e);
        }
    }

    void prune(int frame_id) {
        const int ttl = cfg_.identity_ttl;
        if (ttl <= 0) return;
        std::vector<int> alias_targets;
        for (std::map<int, int>::const_iterator it = merge_.begin();
             it != merge_.end(); ++it)
            alias_targets.push_back(it->second);
        std::vector<int> dead;
        for (std::map<int, Identity>::const_iterator it = ids_.begin();
             it != ids_.end(); ++it) {
            const int ident = it->first;
            if (canonical(ident) != ident) continue;
            if (std::find(alias_targets.begin(), alias_targets.end(), ident)
                != alias_targets.end())
                continue;
            const Identity& rec = it->second;
            if (rec.state_in || rec.pending_cnt > 0) continue;
            if (rec.last_frame >= 0 && frame_id - rec.last_frame <= ttl) continue;
            dead.push_back(ident);
        }
        for (std::size_t i = 0; i < dead.size(); ++i) {
            const int ident = dead[i];
            ids_.erase(ident);
            for (std::map<int, int>::iterator it = t2i_.begin(); it != t2i_.end();) {
                if (canonical(it->second) == ident) {
                    std::map<int, int>::iterator nxt = it;
                    ++nxt;
                    t2i_.erase(it);
                    it = nxt;
                } else {
                    ++it;
                }
            }
            merge_.erase(ident);
        }
    }

    Config cfg_;
    std::vector<std::pair<double, double> > poly_;
    int next_identity_;
    std::map<int, int> t2i_;       // track_id -> identity_id
    std::map<int, Identity> ids_;  // identity_id -> 记录（按 id 升序迭代）
    std::map<int, int> merge_;     // absorbed -> canonical
    int frame_id_;
    int visitors_total_;
    CountStats stats_;
};

}  // namespace track_count

#endif  // PEDESTRIAN_TRACK_COUNTER_H
