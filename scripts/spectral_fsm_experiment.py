#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spectral_fsm_experiment.py - 谱特征接入 FSM 的离线实验裁决（评审 P1 遗留）
=================================================================

研究问题（Agent3/交叉A 裁决遗留）：
    现有 FSM 评分 score = w1*norm(n) + w2*density + w3*r_gcc，谱特征
    （fiedler/spectral_radius/eig_entropy）只计算并显示，从未进入决策。
    消融 C 组=B 组正是因此。谱特征接入是否能改善风险判断？

假设：
    Fiedler 值（代数连通度）能区分"链状伪聚集"（r_gcc=1 但 fiedler 小，
    如 10 人 1.5m 串排）与"紧密真实聚集"（r_gcc=1 且 fiedler 大）。
    把谱特征作为修正项接入 FSM 评分可降低链状误报，同时不破坏紧密聚集。

方法（完全离线、可复现，不改生产代码）：
    1. 场景判别：对 scenario_test 的合成场景，用谱修正 FSM 重算最终等级，
       检验 n10_chain 是否 DANGER->WARNING 而 n10_cluster 保持 DANGER。
    2. 111 条视频离线重算：读取 results/rerun/video_enhanced/*/timeline.json，
       先用原配置重算（M0 一致性校验），再用谱修正模式 M1/M2 x w4 扫描，
       统计 DANGER 帧、切换次数、DANGER 视频数等全局指标。

谱修正模式：
    M0 对照：score 原式（无谱项）
    M1 正反馈：score += w4 * fiedler_norm          （谱连通度越高风险越高）
    M2 抑制模式：score -= w4 * (1-fiedler_norm) * r_gcc （高 r_gcc 但低 fiedler
                → 链状伪聚集被降分；紧密团 fiedler_norm 大，几乎不惩罚）

    fiedler_norm = fiedler / (fiedler + 1.0)   # 单调映射 [0,1)
    fiedler<=0 视为谱特征不可用：M1 不加分，M2 用 0.5 中性值不惩罚。

输出：results/rerun/_spectral_fsm_experiment.json + 控制台表格
测试：pytest tests/test_spectral_fsm_experiment.py
"""
import json, math, os, sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(PROJECT_ROOT / "src"))
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))

from algorithms.graph_laplacian import compute_laplacian_features  # noqa: E402
from algorithms.risk_fsm import RiskFSM, RiskFSMConfig  # noqa: E402
from crowd_test import load_config  # noqa: E402

EDGE_DIST = 2.0

def fiedler_norm(fiedler):
    """谱连通度归一化：[0,1)。fiedler<=0 视为不可用返回 None。"""
    if fiedler is None or fiedler <= 0:
        return None
    return fiedler / (fiedler + 1.0)

class SpectralRiskFSM(RiskFSM):
    """带谱修正的 FSM（仅实验用，不修改生产 risk_fsm.py）。"""
    def __init__(self, config=None, mode="M2", w4=0.1):
        # 复制配置并禁用父类谱项（w4=0）：M1/M2 是独立的实验谱修正，避免双重计算
        cfg_copy = RiskFSMConfig(**vars(config)) if isinstance(config, RiskFSMConfig) else config
        if cfg_copy is not None:
            cfg_copy.w4 = 0.0
        super().__init__(cfg_copy)
        self.mode = mode
        self.w4 = w4

    def _compute_score(self, features):
        score = super()._compute_score(features)
        fiedler = features.get("fiedler", 0.0) or 0.0
        r_gcc = features.get("r_gcc", 0.0) or 0.0
        fn = fiedler_norm(fiedler)
        if fn is None:
            fn = 0.5  # 谱特征不可用：中性，不惩罚也不加分
        if self.mode == "M1":
            score += self.w4 * fn
        elif self.mode == "M2":
            score -= self.w4 * (1.0 - fn) * r_gcc
        return max(0.0, min(score, 1.0))

# ---------- 1. 场景判别 ----------
def grid_points(n, spacing):
    side = math.ceil(math.sqrt(n))
    return [((i % side) * spacing, (i // side) * spacing) for i in range(n)]

def pair_points(n_pairs, gap_between_pairs):
    pts = []
    for i in range(n_pairs):
        base_x = i * gap_between_pairs
        pts.append((base_x, 0.0))
        pts.append((base_x + 0.5, 0.0))
    return pts

def chain_points(n, spacing):
    return [(i * spacing, 0.0) for i in range(n)]

SCENARIOS = [
    ("n8_dispersed",      "8 人完全分散（间距3m）", grid_points(8, 3.0)),
    ("n8_pairs",          "8 人=4个二人组（组间3m）", pair_points(4, 3.0)),
    ("n8_cluster",        "8 人紧密网格（0.8m）", grid_points(8, 0.8)),
    ("n10_dispersed",     "10 人完全分散（间距3m）", grid_points(10, 3.0)),
    ("n10_two_clusters",  "10 人=两个5人簇（簇距4m）", grid_points(5, 0.8) + [(x + 6.0, y) for x, y in grid_points(5, 0.8)]),
    ("n10_cluster",       "10 人紧密网格（0.8m）", grid_points(10, 0.8)),
    ("n10_chain",          "10 人链状（1.5m 间距）", chain_points(10, 1.5)),
    ("n12_dispersed",     "12 人完全分散（间距3m）", grid_points(12, 3.0)),
    ("n12_cluster",       "12 人紧密网格（0.8m）", grid_points(12, 0.8)),
    ("n12_two_clusters",  "12 人=两个6人簇（簇距5m）", grid_points(6, 0.8) + [(x + 7.0, y) for x, y in grid_points(6, 0.8)]),
]

def fsm_final_level(cfg, feat, mode="M0", w4=0.1):
    kwargs = dict(cfg.get("fsm", {})) if cfg else {}
    if mode == "M0":
        kwargs["w4"] = 0.0   # M0 = 无谱特征基线对照
        fsm = RiskFSM(RiskFSMConfig(**kwargs))
    else:
        fsm = SpectralRiskFSM(RiskFSMConfig(**kwargs), mode=mode, w4=w4)
    last = "SAFE"
    for t in range(20):
        feats = {"n": feat["n"], "density": feat["density"], "r_gcc": feat["r_gcc"],
                 "clustering": feat["clustering"], "fiedler": feat.get("fiedler", 0.0)}
        last = fsm.update(feats, now_ms=t * 100)
    return last

def run_scenarios(cfg):
    rows = []
    for name, desc, pts in SCENARIOS:
        feat = compute_laplacian_features(pts, EDGE_DIST)
        row = {"name": name, "desc": desc, "n": feat["n"], "density": round(feat["density"], 4),
               "r_gcc": round(feat["r_gcc"], 4), "clustering": round(feat["clustering"], 4),
               "fiedler": round(feat["fiedler"], 4),
               "levels": {"M0": fsm_final_level(cfg, feat, "M0")}}
        for mode in ("M1", "M2"):
            for w4 in (0.05, 0.1, 0.2):
                row["levels"][f"{mode}_{w4}"] = fsm_final_level(cfg, feat, mode, w4)
        rows.append(row)
    return rows

# ---------- 2. 111 条视频离线重算 ----------
def load_timelines():
    base = PROJECT_ROOT / "results" / "rerun" / "video_enhanced"
    timelines = []
    for d in sorted(base.iterdir()):
        tl = d / f"{d.name}_timeline.json"
        if tl.is_file():
            data = json.loads(tl.read_text(encoding="utf-8"))
            timelines.append((d.name, data))
    return timelines

def replay(timeline, cfg, mode="M0", w4=0.1, now_ms_mode="frame", m0_spectral=False, fps=None):
    """从特征重算等级序列，返回 (levels, switches, dist)。
    m0_spectral=True 时 M0 使用生产配置（含 w4 谱项），用于验证离线重算与真实推理一致；
    默认 False 保持 M0 为无谱特征基线对照（离线实验语义）。
    now_ms_mode="fps" 时按真实帧率计算时间戳（与在线推理一致）。"""
    kwargs = dict(cfg.get("fsm", {})) if cfg else {}
    if mode == "M0":
        if not m0_spectral:
            kwargs["w4"] = 0.0   # M0 = 无谱特征基线对照
        fsm = RiskFSM(RiskFSMConfig(**kwargs))
    else:
        fsm = SpectralRiskFSM(RiskFSMConfig(**kwargs), mode=mode, w4=w4)
    levels = []
    for i, it in enumerate(timeline):
        feats = {"n": it.get("n", 0), "density": it.get("density", 0.0),
                 "r_gcc": it.get("r_gcc", 0.0), "fiedler": it.get("fiedler", 0.0)}
        if now_ms_mode == "frame":
            now_ms = None  # 仅帧计数保持（danger_hold_frames）
        elif now_ms_mode == "fps":
            now_ms = int(i * 1000.0 / fps) if fps else i * 33
        else:
            now_ms = i * 33  # 近似 30fps（danger_hold_ms 保持）
        levels.append(fsm.update(feats, now_ms))
    dist = {}
    for lv in levels:
        dist[lv] = dist.get(lv, 0) + 1
    switches = sum(1 for i in range(1, len(levels)) if levels[i] != levels[i-1])
    return levels, switches, dist

def run_videos(cfg, timelines):
    """返回 (一致性统计, 各模式汇总)。"""
    # 先选一致性更高的 now_ms 模式
    best_mode, best_rate = "frame", -1.0
    for nm in ("frame", "ms33"):
        total_same = total_frames = 0
        for _, tl in timelines:
            levels, _, _ = replay(tl, cfg, "M0", 0.0, now_ms_mode=nm)
            same = sum(1 for a, b in zip(levels, [x["level"] for x in tl]) if a == b)
            total_same += same
            total_frames += len(tl)
        rate = total_same / total_frames
        print(f"  [M0 一致性] now_ms={nm}: {rate:.4%} ({total_same}/{total_frames})")
        if rate > best_rate:
            best_rate, best_mode = rate, nm

    modes = [("M0", 0.0)] + [(m, w) for m in ("M1", "M2") for w in (0.05, 0.1, 0.2)]
    summary = {}
    for mode, w4 in modes:
        key = mode if w4 == 0.0 else f"{mode}_{w4}"
        total_frames = 0
        danger_frames = warning_frames = safe_frames = 0
        total_switches = 0
        danger_videos = 0
        per_video = {}
        for name, tl in timelines:
            _, switches, dist = replay(tl, cfg, mode, w4, now_ms_mode=best_mode)
            n = len(tl)
            total_frames += n
            danger_frames += dist.get("DANGER", 0)
            warning_frames += dist.get("WARNING", 0)
            safe_frames += dist.get("SAFE", 0)
            total_switches += switches
            if dist.get("DANGER", 0) > 0:
                danger_videos += 1
            per_video[name] = {"frames": n, "switches": switches,
                               "DANGER": dist.get("DANGER", 0),
                               "WARNING": dist.get("WARNING", 0),
                               "SAFE": dist.get("SAFE", 0)}
        summary[key] = {
            "mode": mode, "w4": w4,
            "frames": total_frames,
            "DANGER_frames": danger_frames,
            "WARNING_frames": warning_frames,
            "SAFE_frames": safe_frames,
            "switches_total": total_switches,
            "switches_avg_per_video": round(total_switches / max(len(timelines), 1), 2),
            "DANGER_videos": danger_videos,
            "now_ms_mode": best_mode,
        }
    return summary, best_mode



def analyze_m2_vs_m0(cfg, timelines, now_ms_mode="ms33"):
    """M2_0.1 vs M0 逐帧差异分析：检验谱抑制是否特异作用于低 fiedler 帧。"""
    downgrade_D = upgrade_D = downgrade_W = upgrade_W = 0
    m0_danger_f, m2_still_f, m2_lost_f = [], [], []
    video_diff = []
    for name, tl in timelines:
        lv0, _, _ = replay(tl, cfg, "M0", 0.0, now_ms_mode=now_ms_mode)
        lv2, _, _ = replay(tl, cfg, "M2", 0.1, now_ms_mode=now_ms_mode)
        d0 = sum(1 for a, b in zip(lv0, lv2) if a == "DANGER" and b != "DANGER")
        d1 = sum(1 for a, b in zip(lv0, lv2) if a != "DANGER" and b == "DANGER")
        downgrade_D += d0; upgrade_D += d1
        downgrade_W += sum(1 for a, b in zip(lv0, lv2) if a == "WARNING" and b == "SAFE")
        upgrade_W  += sum(1 for a, b in zip(lv0, lv2) if a == "SAFE" and b in ("WARNING", "DANGER"))
        for it, a, b in zip(tl, lv0, lv2):
            f = it.get("fiedler", 0.0)
            if a == "DANGER":
                m0_danger_f.append(f)
                if b == "DANGER":
                    m2_still_f.append(f)
                else:
                    m2_lost_f.append(f)
        if d0 > 0 or d1 > 0:
            video_diff.append({"video": name, "D降级帧": d0, "D新增帧": d1,
                             "M0_D": sum(1 for x in lv0 if x == "DANGER"),
                             "M2_D": sum(1 for x in lv2 if x == "DANGER")})
    def fstats(xs):
        if not xs:
            return {"N": 0}
        xs_s = sorted(xs)
        med = xs_s[len(xs_s)//2]
        return {"N": len(xs), "mean": round(sum(xs)/len(xs), 4), "median": round(med, 4)}
    return {
        "diff": {"D降级为非D": downgrade_D, "非D升级为D": upgrade_D,
                 "W降级为S": downgrade_W, "S升级为W/D": upgrade_W},
        "fiedler_stats": {
            "M0_DANGER帧": fstats(m0_danger_f),
            "M2仍DANGER帧": fstats(m2_still_f),
            "M2被降级(原D)帧": fstats(m2_lost_f),
        },
        "video_diff": sorted(video_diff, key=lambda v: -v["D降级帧"])[:20],
    }

def main():
    cfg = load_config(str(PROJECT_ROOT / "config" / "default_config.json"))
    print("=" * 70)
    print("场景判别（FSM 20 帧稳定等级；目标：n10_chain 被 M2 降级且 cluster 保持 DANGER）")
    print("=" * 70)
    scen_rows = run_scenarios(cfg)
    hdr = f"{'场景':<18}{'n':>3}{'rho':>7}{'gcc':>7}{'fiedler':>9}  " + " ".join(
        f"{k:>10}" for k in ("M0","M1_0.1","M2_0.05","M2_0.1","M2_0.2"))
    print(hdr)
    for r in scen_rows:
        lv = r["levels"]
        print(f"{r['name']:<18}{r['n']:>3}{r['density']:>7}{r['r_gcc']:>7}{r['fiedler']:>9}  "
              f"{lv['M0']:>10}{lv['M1_0.1']:>10}{lv['M2_0.05']:>10}{lv['M2_0.1']:>10}{lv['M2_0.2']:>10}")

    print()
    print("=" * 70)
    print("111 条视频离线重算（M0 一致性校验 + M1/M2 谱修正扫描）")
    print("=" * 70)
    timelines = load_timelines()
    print(f"加载 timeline：{len(timelines)} 条视频")
    summary, best_mode = run_videos(cfg, timelines)

    hdr2 = f"{'模式':<10}{'DANGER帧':>10}{'WARN帧':>9}{'SAFE帧':>9}{'切换总':>8}{'切换均/视频':>11}{'DANGER视频':>11}"
    print(hdr2)
    for key, s in summary.items():
        print(f"{key:<10}{s['DANGER_frames']:>10}{s['WARNING_frames']:>9}{s['SAFE_frames']:>9}"
              f"{s['switches_total']:>8}{s['switches_avg_per_video']:>11}{s['DANGER_videos']:>11}")

    m2_analysis = analyze_m2_vs_m0(cfg, timelines, best_mode)

    out = {
        "method": "离线重算：results/rerun/video_enhanced/*/timeline.json；M0=原配置（校验），M1=谱正反馈，M2=谱抑制",
        "fiedler_norm": "fiedler/(fiedler+1)；fiedler<=0 视为不可用（中性 0.5）",
        "now_ms_mode": best_mode,
        "scenarios": scen_rows,
        "video_summary": summary,
        "m2_vs_m0_analysis": m2_analysis,
    }
    out_path = PROJECT_ROOT / "results" / "rerun" / "_spectral_fsm_experiment.json"
    out_path.write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print()
    print("已保存:", out_path)

if __name__ == "__main__":
    main()
