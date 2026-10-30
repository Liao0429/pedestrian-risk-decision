// cpp_parity_harness.cc - C++/Python 数值一致性交叉验证工具（仅主机测试，不进板端构建）
//
// 目的：把 src/algorithms/*.h（板端 C++ 实现）与 src/algorithms/*.py（Python 原型）
// 放在同一组输入上运行并逐值比较，暴露两套实现的数值 / 语义分歧。
// 由 tests/test_cpp_python_parity.py 调用：stdin 喂命令，stdout 读结果。
//
// 命令格式（每行一条，token 以空白分隔）：
//   LAP    <edge_dist> <n> x1 y1 ... xn yn
//   DBSCAN <eps> <min_samples> <n> x1 y1 ... xn yn
//   KAL    <dt> <sig_a> <sig_z> <n_upd> x0 y0 [zx zy]*
//   EMA    <alpha> <use_ts:0|1> <tau> <n> v1 t1 v2 t2 ...
//   FSM    <n> [n density rgcc fiedler trend now_ms]*
//   BT     <track_thresh> <det_thresh> <match_thresh> <max_lost> <min_hits> <F>
//          其后紧跟 F 行，每行 "<k> x1 y1 x2 y2 conf ..."（k 为当帧检测数）
//   TC     <F> <dedup_tol> <dup_foot_max> <dup_iou_min> <dup_height_ratio> <reid_radius>
//          <reid_max_gap> <identity_ttl> <min_dwell> <min_danger_n> <hyst>
//          <npoly> x1 y1 ... xnpoly ynpoly
//          其后紧跟 F 行，每行 "<k> <n_det> [tid x1 y1 x2 y2 score conf misses footx footy roix roiy]*"
//          （k=当帧轨迹数，n_det<0 表示用 k；每帧输出一行，共 F 行）
//   '#' 开头为注释
// 输出统一 %.12g，便于 Python 端逐值比较。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "graph_laplacian.h"
#include "crowd_dbscan.h"
#include "risk_fsm.h"
#include "kalman_filter.h"
#include "ema_smoother.h"
#include "bytetrack_tracker.h"
#include "track_counter.h"

static void pr(double v) { std::printf("%.12g", v); }

// ---------------- LAP ----------------
static void cmdLap(const std::vector<double>& a) {
    const double edge = a[0];
    const int n = (int)a[1];
    GraphLaplacian gl(edge);
    for (int i = 0; i < n; ++i) gl.addPoint((float)a[2 + 2 * i], (float)a[3 + 2 * i]);
    const SpectralFeatures f = gl.computeFeatures();
    std::printf("n=%d m=%d density=", f.n, f.m); pr(f.density);
    std::printf(" r_gcc="); pr(f.r_gcc);
    std::printf(" clustering="); pr(f.clustering);
    std::printf(" fiedler="); pr(f.fiedler);
    std::printf(" spectral_radius="); pr(f.spectral_radius);
    std::printf(" eig_entropy="); pr(f.eig_entropy);
    std::printf(" converged=%d\n", f.converged ? 1 : 0);
}

// ---------------- DBSCAN ----------------
static void cmdDbscan(const std::vector<double>& a) {
    const double eps = a[0];
    const int ms = (int)a[1];
    const int n = (int)a[2];
    CrowdDBSCAN db(eps, ms);
    for (int i = 0; i < n; ++i) db.addPoint((float)a[3 + 2 * i], (float)a[4 + 2 * i]);
    const CrowdClusterFeatures f = db.compute();
    std::printf("n=%d n_clusters=%d sizes=", f.n, f.n_clusters);
    for (std::size_t i = 0; i < f.cluster_sizes.size(); ++i) {
        if (i) std::printf(",");
        std::printf("%d", f.cluster_sizes[i]);
    }
    std::printf(" centers=");
    for (std::size_t i = 0; i < f.cluster_centers.size(); ++i) {
        if (i) std::printf(";");
        pr(f.cluster_centers[i].first); std::printf(","); pr(f.cluster_centers[i].second);
    }
    std::printf(" largest_ratio="); pr(f.largest_cluster_ratio);
    std::printf(" largest=%d n_noise=%d n_clustered=%d labels=",
                f.largest_cluster_size, f.n_noise, f.n_clustered);
    for (std::size_t i = 0; i < f.labels.size(); ++i) {
        if (i) std::printf(",");
        std::printf("%d", f.labels[i]);
    }
    std::printf("\n");
}

// ---------------- KALMAN ----------------
static void cmdKal(const std::vector<double>& a) {
    KalmanFilter kf(a[0], a[1], a[2]);
    const int nUpd = (int)a[3];
    kf.init(a[4], a[5]);
    for (int i = 0; i < nUpd; ++i) kf.update(a[6 + 2 * i], a[7 + 2 * i]);
    std::printf("x="); pr(kf.x()); std::printf(" y="); pr(kf.y());
    std::printf(" vx="); pr(kf.vx()); std::printf(" vy="); pr(kf.vy());
    std::printf(" P=");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            if (i || j) std::printf(",");
            pr(kf.covariance(i, j));
        }
    std::printf("\n");
}

// ---------------- EMA ----------------
static void cmdEma(const std::vector<double>& a) {
    const double alpha = a[0];
    const bool useTs = a[1] != 0.0;
    const double tau = a[2];
    const int n = (int)a[3];
    pedestrian::EmaSmoother ema(alpha, 1, useTs, tau);
    std::printf("out=");
    for (int i = 0; i < n; ++i) {
        const double y = ema.update(a[4 + 2 * i], (int64_t)a[5 + 2 * i]);
        if (i) std::printf(",");
        pr(y);
    }
    std::printf("\n");
}

// ---------------- FSM ----------------
static void cmdFsm(const std::vector<double>& a) {
    const int n = (int)a[0];
    pedestrian::RiskFSM fsm;
    std::string lv, sc;
    char buf[64];
    for (int i = 0; i < n; ++i) {
        pedestrian::FsmFeatures f;
        f.n = (int)a[1 + 6 * i];
        f.density = a[2 + 6 * i];
        f.r_gcc = a[3 + 6 * i];
        f.fiedler = a[4 + 6 * i];
        f.trend = a[5 + 6 * i];
        const int64_t nowMs = (int64_t)a[6 + 6 * i];
        const pedestrian::RiskLevel l = fsm.update(f, nowMs);
        if (i) { lv += ","; sc += ","; }
        lv += pedestrian::RiskFSM::levelName(l);
        std::snprintf(buf, sizeof buf, "%.12g", fsm.score());
        sc += buf;
    }
    std::printf("levels=%s scores=%s\n", lv.c_str(), sc.c_str());
}

// ---------------- BYTETRACK ----------------
static void cmdBt(std::istream& in, double tb, double db, double mb, int ml, int mh, int frames) {
    ByteTrack bt(tb, db, mb, ml, mh, 1.0, false);
    std::printf("frames=");
    for (int fi = 0; fi < frames; ++fi) {
        int k = 0;
        if (!(in >> k)) { std::printf("\n"); return; }
        std::vector<std::vector<double>> dets;
        for (int i = 0; i < k; ++i) {
            std::vector<double> d(5, 0.0);
            for (int j = 0; j < 5; ++j) in >> d[j];
            dets.push_back(d);
        }
        const std::vector<TrackResult> res = bt.update(dets, -1);
        if (fi) std::printf("|");
        for (std::size_t i = 0; i < res.size(); ++i) {
            if (i) std::printf(";");
            std::printf("%d:", res[i].id);
            pr(res[i].x1); std::printf(","); pr(res[i].y1); std::printf(",");
            pr(res[i].x2); std::printf(","); pr(res[i].y2); std::printf(",");
            pr(res[i].score);
        }
    }
    std::printf("\n");
}

// ---------------- TRACK COUNTER ----------------
// 逐帧输出一行，字段顺序与 tests/test_cpp_python_parity.py 的 parse_tc 一一对应。
static void printTcFrame(const track_count::Result& r) {
    std::printf("n=%d det=%d spat=%d trk=%d supp=%d dang=%d vis=%d gate=%d lgate=%d ",
                r.n, r.n_detections, r.n_spatial_dedup, r.n_tracks, r.n_suppressed,
                r.dangerous_now, r.visitors_total, r.gate_ok ? 1 : 0,
                r.legacy_gate_ok ? 1 : 0);
    std::printf("stats=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d ", r.stats.frames,
                r.stats.duplicates_suppressed, r.stats.id_switch_merges,
                r.stats.total_legacy_count, r.stats.total_spatial_count,
                r.stats.total_dedup_count, r.stats.max_legacy_count,
                r.stats.max_dedup_count, r.stats.frames_legacy_gate_open,
                r.stats.frames_dedup_gate_open);
    std::printf("persons=");
    if (r.persons.empty()) {
        std::printf("-");
    } else {
        for (std::size_t i = 0; i < r.persons.size(); ++i) {
            const track_count::PersonState& p = r.persons[i];
            if (i) std::printf(";");
            std::printf("%d|", p.identity_id);
            for (std::size_t k = 0; k < p.track_ids.size(); ++k) {
                if (k) std::printf(",");
                std::printf("%d", p.track_ids[k]);
            }
            std::printf("|");
            pr(p.x1); std::printf(","); pr(p.y1); std::printf(",");
            pr(p.x2); std::printf(","); pr(p.y2);
            std::printf("|"); pr(p.score);
            std::printf("|"); pr(p.foot_x); std::printf(","); pr(p.foot_y);
            std::printf("|"); pr(p.roi_x); std::printf(","); pr(p.roi_y);
            std::printf("|%d|%d|%d", p.in_roi ? 1 : 0, p.dwell, p.confirmed ? 1 : 0);
        }
    }
    std::printf(" events=");
    if (r.events.empty()) {
        std::printf("-");
    } else {
        for (std::size_t i = 0; i < r.events.size(); ++i) {
            const track_count::RoiEvent& e = r.events[i];
            if (i) std::printf(";");
            std::printf("%d|%d|%d|%d|", e.identity_id, e.track_id,
                        e.entering ? 1 : 0, e.since_frame);
            pr(e.signed_distance);
        }
    }
    std::printf("\n");
}

static void cmdTc(std::istream& in, std::istringstream& iss) {
    int frames = 0, reid_gap = 0, ttl = 0, dwell = 0, dangn = 0, npoly = 0;
    double dedup = 0.0, footMax = 0.0, iouMin = 0.0, hratio = 0.0, reidR = 0.0, hyst = 0.0;
    iss >> frames >> dedup >> footMax >> iouMin >> hratio >> reidR
        >> reid_gap >> ttl >> dwell >> dangn >> hyst >> npoly;
    track_count::Config cfg;
    cfg.dedup_tol = dedup;
    cfg.dup_foot_max = footMax;
    cfg.dup_iou_min = iouMin;
    cfg.dup_height_ratio = hratio;
    cfg.reid_radius = reidR;
    cfg.reid_max_gap = reid_gap;
    cfg.identity_ttl = ttl;
    cfg.min_dwell_frames = dwell;
    cfg.min_danger_n = dangn;
    cfg.roi_hysteresis = hyst;
    for (int i = 0; i < npoly; ++i) {
        double px = 0.0, py = 0.0;
        iss >> px >> py;
        cfg.roi_polygon.push_back(std::make_pair(px, py));
    }
    track_count::TrackCounter tc(cfg);
    for (int fi = 0; fi < frames; ++fi) {
        int k = 0, ndet = -1;
        if (!(in >> k >> ndet)) { std::printf("ERR TC_EOF\n"); return; }
        track_count::FrameObs fo;
        fo.frame_id = fi + 1;
        fo.n_detections = ndet;
        for (int i = 0; i < k; ++i) {
            track_count::Obs o;
            int conf = 1, miss = 0;
            in >> o.track_id >> o.x1 >> o.y1 >> o.x2 >> o.y2 >> o.score
               >> conf >> miss >> o.foot_x >> o.foot_y >> o.roi_x >> o.roi_y;
            o.confirmed = conf != 0;
            o.misses = miss;
            o.has_foot = true;
            o.has_roi = true;
            fo.tracks.push_back(o);
        }
        printTcFrame(tc.update(fo));
    }
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "BT") {
            double tb, db, mb; int ml, mh, frames;
            iss >> tb >> db >> mb >> ml >> mh >> frames;
            cmdBt(std::cin, tb, db, mb, ml, mh, frames);
            continue;
        }
        if (cmd == "TC") {
            cmdTc(std::cin, iss);
            continue;
        }
        std::vector<double> a;
        double v;
        while (iss >> v) a.push_back(v);
        if (cmd == "LAP" && a.size() >= 2) cmdLap(a);
        else if (cmd == "DBSCAN" && a.size() >= 3) cmdDbscan(a);
        else if (cmd == "KAL") cmdKal(a);
        else if (cmd == "EMA" && a.size() >= 4) cmdEma(a);
        else if (cmd == "FSM" && a.size() >= 1) cmdFsm(a);
        else std::printf("ERR %s\n", cmd.c_str());
    }
    return 0;
}
