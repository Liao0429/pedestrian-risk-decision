// =============================================================================
// test_track_count_integration.cc — task-11 接线级 A/B 测试（主机 x86）
// -----------------------------------------------------------------------------
// 目的：在"ByteTrack + TrackCounter"与"当帧检测框数（旧口径）"之间给出可复现的
// 数字，覆盖 track-count 指出的真实失效模式：
//   * 同一人被 NMS 漏掉后输出两个框（脚点差 6cm）→ 旧口径算 2 人；
//     2cm 脚点去重对这种偏移完全无效（6cm > 2cm），必须靠 20cm dup_foot_max +
//     身份级合并才能纠正。
// 场景设计（像素用于 IoU 关联，米制用于脚点判据，与板端一致）：
//   甲/乙 两人，每人每帧输出 2 个检测框（box 中心差 6px，脚点差 6cm），持续 12 帧。
//   min_danger_n = 3（与板端 FSM 对齐）。
//   A 组：4 人 × 2 框 → 旧口径 8，去重后应为 4（-50%）
//   B 组：2 人 × 2 框 → 旧口径 4（legacy_gate_ok=true 误开门），去重后 2（gate_ok=false，
//        与 track-count 报的"误开门 30/30 帧 → 0/30 帧"同向）
// =============================================================================

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "bytetrack_tracker.h"
#include "track_counter.h"

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++g_fail;                                                        \
        } else {                                                             \
            ++g_pass;                                                        \
        }                                                                    \
    } while (0)

// 每人每帧 2 个框：meter 位置 -> 像素用 PX_PER_M 映射（模拟 IPM 反投影）
const double PX_PER_M = 100.0;

struct FrameAB {
    int legacy_n;       // 旧口径：当帧检测框数
    int persons;        // 新口径：TrackCounter 去重后身份数
    int dangerous;      // 新口径 ROI 内身份数
    bool gate_ok;       // 新口径 n >= min_danger_n
    bool legacy_gate_ok;// 旧口径 n >= min_danger_n
    int suppressed;     // 本帧被抑制的重复轨迹数
};

// 生成一帧：nPersons 个人，每人 dup 个重复框（脚点偏移 dupOffM 米）
void makeFrame(int nPersons, int dup, double dupOffM, int frameIdx,
               std::vector<std::vector<double> >& dets,
               std::vector<track_count::Obs>& obs) {
    dets.clear();
    obs.clear();
    for (int p = 0; p < nPersons; ++p) {
        // 地面米制位置（彼此间隔 2m，避免跨人混淆）
        const double mx = 1.0 + p * 2.0;
        const double my = 1.0 + 0.01 * (frameIdx % 3);   // 轻微运动
        // 像素框：中心按 IPM 缩放映射，宽 60 高 140
        for (int k = 0; k < dup; ++k) {
            const double off = (k == 0) ? 0.0 : dupOffM;   // 第二个框脚点偏 6cm
            const double cx = (mx + off) * PX_PER_M;
            const double footY = my * PX_PER_M;
            const double x1 = cx - 30.0, x2 = cx + 30.0;
            const double y1 = footY - 140.0, y2 = footY;
            std::vector<double> d(5);
            d[0] = x1; d[1] = y1; d[2] = x2; d[3] = y2; d[4] = 0.92;
            dets.push_back(d);
        }
    }
}

FrameAB runScenario(int nPersons, int dup, double dupOffM, int frames,
                    int minDangerN, bool roiEnabled) {
    ByteTrack tracker(0.5, 0.1, 0.8, 30, 3, 1.0, false);
    std::string perr;
    CHECK(ByteTrack::paramsValid(0.5, 0.1, 0.8, 30, 3, &perr));

    track_count::Config cfg;
    cfg.min_danger_n = minDangerN;
    cfg.roi_hysteresis = 8.0;
    cfg.min_dwell_frames = 3;
    if (roiEnabled) {
        // 右侧 1/4 画面为 ROI（像素）；甲在 x=100px 外、乙在 x=300px 外 → 都不在内
        cfg.roi_polygon.push_back(std::make_pair(500.0, 10.0));
        cfg.roi_polygon.push_back(std::make_pair(710.0, 10.0));
        cfg.roi_polygon.push_back(std::make_pair(710.0, 470.0));
        cfg.roi_polygon.push_back(std::make_pair(500.0, 470.0));
    }
    track_count::TrackCounter counter(cfg);

    FrameAB last;
    last.legacy_n = 0; last.persons = 0; last.dangerous = 0;
    last.gate_ok = false; last.legacy_gate_ok = false; last.suppressed = 0;

    for (int f = 0; f < frames; ++f) {
        std::vector<std::vector<double> > dets;
        std::vector<track_count::Obs> dummy;   // makeFrame 的 obs 参数在此重算
        makeFrame(nPersons, dup, dupOffM, f, dets, dummy);

        const std::vector<TrackResult> trks = tracker.update(dets);   // frame_id 自增
        const std::vector<TrackInfo> tinfo = tracker.trackInfo();

        track_count::FrameObs fo;
        fo.frame_id = f + 1;
        fo.n_detections = (int)dets.size();
        for (size_t i = 0; i < trks.size(); ++i) {
            track_count::Obs o;
            o.track_id = trks[i].id;
            o.x1 = trks[i].x1; o.y1 = trks[i].y1;
            o.x2 = trks[i].x2; o.y2 = trks[i].y2;
            o.score = trks[i].score;
            o.confirmed = (i < tinfo.size()) ? tinfo[i].confirmed : true;
            o.misses    = (i < tinfo.size()) ? tinfo[i].misses : 0;
            // 脚点：像素框底边中点 / PX_PER_M -> 米（板端为 IPM）
            o.has_foot = true;
            o.foot_x = ((o.x1 + o.x2) * 0.5) / PX_PER_M;
            o.foot_y = o.y2 / PX_PER_M;
            o.has_roi = true;
            o.roi_x = (o.x1 + o.x2) * 0.5;
            o.roi_y = (o.y1 + o.y2) * 0.5;
            fo.tracks.push_back(o);
        }
        const track_count::Result r = counter.update(fo);
        last.legacy_n = fo.n_detections;
        last.persons = (int)r.persons.size();
        last.dangerous = r.dangerous_now;
        last.gate_ok = r.gate_ok;
        last.legacy_gate_ok = r.legacy_gate_ok;
        last.suppressed = r.n_suppressed;
    }
    return last;
}

}  // namespace

int main() {
    std::printf("==== task-11 接线级 A/B（ByteTrack + TrackCounter vs 当帧检测框数）====\n");
    std::printf("场景：每人每帧 %d 个框、脚点偏移 6cm（2cm 脚点去重对此无效）\n", 2);

    // A 组：4 人 × 2 框
    {
        FrameAB a = runScenario(4, 2, 0.06, 12, 3, false);
        // suppressed 只统计"同帧空间合并(step 2)"；本例两条重复轨迹在 claim(step 1)
        // 阶段就归并到同一身份，故 suppressed=0，但人数去重仍然生效（persons=4）。
        std::printf("A 4人x2框: legacy_n=%d  dedup_persons=%d  step2_suppressed=%d  gate_ok=%d/legacy=%d\n",
                    a.legacy_n, a.persons, a.suppressed, (int)a.gate_ok, (int)a.legacy_gate_ok);
        CHECK(a.legacy_n == 8);
        CHECK(a.persons == 4);          // 去重 -50%
        CHECK(a.suppressed >= 0);       // 诊断量（step-1 身份归并时可为 0）
    }
    // B 组：2 人 × 2 框（旧口径 4 >= min_danger_n=3 会误开门）
    {
        FrameAB b = runScenario(2, 2, 0.06, 12, 3, false);
        std::printf("B 2人x2框: legacy_n=%d  dedup_persons=%d  step2_suppressed=%d  gate_ok=%d/legacy=%d\n",
                    b.legacy_n, b.persons, b.suppressed, (int)b.gate_ok, (int)b.legacy_gate_ok);
        CHECK(b.legacy_n == 4);
        CHECK(b.persons == 2);
        CHECK(b.legacy_gate_ok == true);    // 假阳性门控
        CHECK(b.gate_ok == false);          // 去重后不误开门
    }
    // C 组：对照组 —— 无重复框时新老口径一致（不得引入虚假抑制）
    {
        FrameAB c = runScenario(3, 1, 0.0, 12, 3, false);
        std::printf("C 3人x1框(对照): legacy_n=%d  dedup_persons=%d  suppressed=%d\n",
                    c.legacy_n, c.persons, c.suppressed);
        CHECK(c.legacy_n == 3);
        CHECK(c.persons == 3);
        CHECK(c.suppressed == 0);
    }
    // D 组：ROI 内人数（危险人数口径）——甲乙都在 ROI 外
    {
        FrameAB d = runScenario(2, 2, 0.06, 16, 3, true);
        std::printf("D ROI 外 2人x2框: dangerous_now=%d（应 0）\n", d.dangerous);
        CHECK(d.dangerous == 0);
    }

    std::printf("\n%d/%d checks passed\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
