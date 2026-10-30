// bt_sequence_probe.cc - ByteTrack 真实序列差分探针（仅主机测试，不进板端构建）
//
// 用途：把 tests/test_bytetrack_sequences.py 构造的真实多人序列（交叉 / 遮挡重现 /
// ID 切换 / 重复框 / 低分二次关联）喂给 src/algorithms/bytetrack_tracker.h 的真实
// 编译产物，按 %.17g 打印逐帧轨迹（含 trackInfo 生命周期字段），与 Python 参考实现
// 逐值比较；同时提供输入防御与参数校验的自检命令。
//
// 命令格式（每行一条，token 以空白分隔；'#' 开头为注释）：
//   SEQ <track_thresh> <det_thresh> <match_thresh> <max_lost> <min_hits> <n_frames>
//       其后紧跟 n_frames 行，每行 "<k> x1 y1 x2 y2 score ..."（k=当帧检测数，0=空帧）
//   PARAMS <track_thresh> <det_thresh> <match_thresh> <max_lost> <min_hits>
//       输出 valid=0/1（校验条件应与 Python 版 ValueError 一致）
//   BAD  注入长度不足 / NaN 的检测行，验证不崩溃且坏行被丢弃
// 输出浮点统一 %.17g，便于与 Python 端逐值比较。

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "bytetrack_tracker.h"

static void pr(double v) { std::printf("%.17g", v); }

// ---------------- SEQ ----------------
static void runSeq(std::istream& in, double tb, double db, double mb, int ml,
                   int mh, int frames) {
    ByteTrack bt(tb, db, mb, ml, mh, 1.0, false);
    for (int fi = 0; fi < frames; ++fi) {
        int k = 0;
        if (!(in >> k)) { std::printf("\n"); return; }
        std::vector<std::vector<double> > dets;
        for (int i = 0; i < k; ++i) {
            std::vector<double> d(5, 0.0);
            for (int j = 0; j < 5; ++j) in >> d[j];
            dets.push_back(d);
        }
        const std::vector<TrackResult> res = bt.update(dets, -1);
        const std::vector<TrackInfo> info = bt.trackInfo();
        if (fi) std::printf("|");
        for (std::size_t i = 0; i < res.size(); ++i) {
            if (i) std::printf(";");
            std::printf("%d,", res[i].id);
            pr(res[i].x1); std::printf(","); pr(res[i].y1); std::printf(",");
            pr(res[i].x2); std::printf(","); pr(res[i].y2); std::printf(",");
            pr(res[i].score); std::printf(",");
            // states() 与 trackInfo() 同序（m_tracks 恒按 ID 升序，prune 保序压缩）
            const int conf = (i < info.size() && info[i].confirmed) ? 1 : 0;
            const int hits = (i < info.size()) ? info[i].hits : -1;
            const int misses = (i < info.size()) ? info[i].misses : -1;
            const int age = (i < info.size()) ? info[i].age : -1;
            std::printf("%d,%d,%d,%d", conf, hits, misses, age);
        }
        if (res.empty()) std::printf("-");
    }
    std::printf("\n");
}

// ---------------- PARAMS ----------------
static void runParams(double tb, double db, double mb, int ml, int mh) {
    std::string err;
    const bool ok = ByteTrack::paramsValid(tb, db, mb, ml, mh, &err);
    std::printf("valid=%d\n", ok ? 1 : 0);
}

// ---------------- BAD ----------------
// 长度 <5 与 NaN 的行必须被丢弃且不崩溃；随后正常帧照常建轨。
static void runBad() {
    ByteTrack bt(0.5, 0.1, 0.8, 30, 3, 1.0, false);

    std::vector<std::vector<double> > empty_frame;
    bt.update(empty_frame, -1);

    std::vector<std::vector<double> > bad;
    bad.push_back(std::vector<double>(3, 1.0));            // 长度不足
    std::vector<double> nan_row(5, 0.0);
    nan_row[0] = std::numeric_limits<double>::quiet_NaN();
    nan_row[4] = 0.9;
    bad.push_back(nan_row);                                // NaN 坐标
    std::vector<double> good(5, 0.0);
    good[0] = 100.0; good[1] = 100.0; good[2] = 140.0; good[3] = 300.0; good[4] = 0.9;
    bad.push_back(good);
    bt.update(bad, -1);
    std::printf("after_bad count=%d\n", bt.trackCount());

    std::vector<std::vector<double> > d3;
    std::vector<double> g2(5, 0.0);
    g2[0] = 300.0; g2[1] = 100.0; g2[2] = 340.0; g2[3] = 300.0; g2[4] = 0.9;
    d3.push_back(g2);
    const std::vector<TrackResult> res = bt.update(d3, -1);
    std::printf("after_good count=%d ids=", bt.trackCount());
    for (std::size_t i = 0; i < res.size(); ++i) {
        if (i) std::printf(",");
        std::printf("%d", res[i].id);
    }
    std::printf("\n");
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "SEQ") {
            double tb = 0.0, db = 0.0, mb = 0.0;
            int ml = 0, mh = 0, frames = 0;
            iss >> tb >> db >> mb >> ml >> mh >> frames;
            runSeq(std::cin, tb, db, mb, ml, mh, frames);
        } else if (cmd == "PARAMS") {
            double tb = 0.0, db = 0.0, mb = 0.0;
            int ml = 0, mh = 0;
            iss >> tb >> db >> mb >> ml >> mh;
            runParams(tb, db, mb, ml, mh);
        } else if (cmd == "BAD") {
            runBad();
        } else {
            std::printf("ERR %s\n", cmd.c_str());
        }
    }
    return 0;
}
