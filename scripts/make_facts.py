# -*- coding: utf-8 -*-
"""生成 docs/FACTS.md —— 从真实结果文件抽取可引用事实（禁止估算）。"""
import json, os, sys, re
sys.stdout.reconfigure(encoding="utf-8")

ROOT = r"G:\PedestrianRiskDecision"
OUT = os.path.join(ROOT, "docs", "FACTS.md")
rows = []

def rel(p): return os.path.relpath(p, ROOT).replace("\\", "/")
def add(name, value, src, how): rows.append((name, str(value), rel(src), how))
def load(p):
    try:
        with open(p, encoding="utf-8") as f: return json.load(f)
    except Exception: return None

def flat(obj, prefix="", depth=0, maxdepth=2):
    out = []
    if isinstance(obj, dict):
        for k, v in obj.items():
            key = f"{prefix}.{k}" if prefix else k
            if isinstance(v, (dict, list)) and depth < maxdepth: out.extend(flat(v, key, depth+1, maxdepth))
            elif isinstance(v, (dict, list)): out.append((key, json.dumps(v, ensure_ascii=False)[:200]))
            else: out.append((key, v))
    elif isinstance(obj, list): out.append((prefix, f"list[{len(obj)}]"))
    return out

p = os.path.join(ROOT, "results", "images500_gt_eval", "summary.json"); d = load(p)
if d:
    for k, lab in [("images_total","图片总数"),("gt_total","GT 目标总数"),("det_total","检出目标总数"),
                   ("overall_detection_rate_pct","整体检出率(%)"),("mae","计数 MAE"),("rmse","计数 RMSE"),
                   ("pearson_r","Pearson r"),("generated_at","生成时间")]:
        add(f"图片评测 · {lab}", d.get(k,"未采集"), p, k)
    for lv,n in (d.get("level_counts") or {}).items(): add(f"图片评测 · 风险等级 {lv} 图片数", n, p, "level_counts")
    for bk,st in (d.get("bucket_stats") or {}).items():
        if isinstance(st,dict):
            add(f"图片评测 · GT 分桶 {bk} 图片数", st.get("images","未采集"), p, f"bucket_stats[{bk}].images")
            add(f"图片评测 · GT 分桶 {bk} 检出率(%)", st.get("rate_pct","未采集"), p, f"bucket_stats[{bk}].rate_pct")
else: add("图片评测 · summary.json","未采集（文件缺失）",p,"—")

p = os.path.join(ROOT,"results","images500_gt_eval","tiny_gt_summary.json"); d = load(p)
if d:
    for k,v in flat(d,maxdepth=1): add(f"tinyGT · {k}", v, p, "tiny_gt_summary.json")

for fn in ["_aggregate.json","_aggregate_recomputed.json"]:
    p = os.path.join(ROOT,"results","rerun",fn); d = load(p)
    if d:
        for k,v in flat(d,maxdepth=1): add(f"视频聚合[{fn}] · {k}", v, p, fn)

for sub in ["video_enhanced","video_baseline"]:
    p = os.path.join(ROOT,"results","rerun",sub,"_summary.json"); d = load(p)
    if d:
        for k,v in flat(d,maxdepth=1): add(f"视频汇总[{sub}] · {k}", v, p, f"{sub}/_summary.json")

for fn,lab in [("_comparison_summary.json","对比汇总"),("_ablation.json","消融"),("_spectral_fsm_experiment.json","谱特征FSM实验")]:
    p = os.path.join(ROOT,"results","rerun",fn); d = load(p)
    if d:
        for k,v in flat(d,maxdepth=2): add(f"{lab} · {k}", v, p, fn)

p = os.path.join(ROOT,"results","scenarios","scenario_test.json"); d = load(p)
if d:
    for k,v in flat(d,maxdepth=2): add(f"场景测试 · {k}", v, p, "scenario_test.json")

p = os.path.join(ROOT,"results","experiment_summary.json"); d = load(p)
if d:
    for k,v in flat(d,maxdepth=2): add(f"实验总摘要 · {k}", v, p, "experiment_summary.json")

p = os.path.join(ROOT,"results","edge_board","default_config.json"); d = load(p)
if d:
    for k,v in flat(d,maxdepth=2): add(f"真机配置 · {k}", v, p, "edge_board/default_config.json")

p = os.path.join(ROOT,"results","edge_board","EDGE_TEST_REPORT.md")
if os.path.exists(p):
    txt = open(p, encoding="utf-8", errors="ignore").read()
    for pat,label in [(r"(\d+(?:\.\d+)?)\s*FPS","FPS"),(r"(\d+(?:\.\d+)?)\s*ms","ms 耗时"),(r"(\d+(?:\.\d+)?)\s*MB","MB 内存")]:
        hits = re.findall(pat, txt)
        if hits: add(f"真机报告 · {label}（原文出现值）", " / ".join(dict.fromkeys(hits))[:120], p, f"正则 {pat}")

p = os.path.join(ROOT,"results","edge_board","long_stability.jsonl")
if os.path.exists(p):
    lines = [l for l in open(p, encoding="utf-8", errors="ignore") if l.strip()]
    add("真机长稳 · 采样记录条数", len(lines), p, "非空行计数")
    try:
        first = json.loads(lines[0]); last = json.loads(lines[-1])
        for key in ("ts","timestamp","time","elapsed_s","uptime_s"):
            if key in first and key in last:
                add(f"真机长稳 · 起止 {key}", f"{first[key]} -> {last[key]}", p, "首末行对比"); break
        for key in ("fps","FPS"):
            if key in first:
                vals = []
                for l in lines:
                    try:
                        j = json.loads(l)
                        if key in j: vals.append(float(j[key]))
                    except Exception: pass
                if vals: add("真机长稳 · FPS 最小/最大/均值", f"{min(vals):.2f} / {max(vals):.2f} / {sum(vals)/len(vals):.2f}", p, f"全部 {len(vals)} 条 {key} 统计")
    except Exception: pass

src = os.path.join(ROOT,"src")
if os.path.isdir(src):
    for sub in sorted(os.listdir(src)):
        sp = os.path.join(src, sub)
        if os.path.isdir(sp):
            n = sum(1 for _,_,fs in os.walk(sp) for f in fs)
            add(f"代码 · src/{sub} 文件数", n, sp, "目录递归计数")
    cpp = [f for _,_,fs in os.walk(src) for f in fs if f.endswith((".cc",".cpp",".c",".h",".hpp"))]
    pyf = [f for _,_,fs in os.walk(src) for f in fs if f.endswith(".py")]
    add("代码 · C/C++ 源文件数（src/）", len(cpp), src, "后缀统计")
    add("代码 · Python 文件数（src/）", len(pyf), src, "后缀统计")

for sub in ["tests","scripts","deploy","tools","config","dataset"]:
    sp = os.path.join(ROOT, sub)
    if os.path.isdir(sp):
        n = sum(1 for _,_,fs in os.walk(sp) for f in fs)
        add(f"代码 · {sub}/ 文件数", n, sp, "目录递归计数")

tp = os.path.join(ROOT,"tests")
if os.path.isdir(tp):
    cnt = 0
    for f in os.listdir(tp):
        if f.endswith(".py"):
            t = open(os.path.join(tp,f), encoding="utf-8", errors="ignore").read()
            cnt += len(re.findall(r"^def test_", t, re.M))
    add("代码 · pytest 用例数（tests/*.py 中 def test_）", cnt, tp, "正则计数")

hw = os.path.join(ROOT,"docs","hardware")
if os.path.isdir(hw):
    files = sorted(os.listdir(hw))
    add("硬件 · docs/hardware 文件数", len(files), hw, "目录计数")
    for f in files:
        if f.endswith(".md"): add(f"硬件 · 文档 {f}", "存在", os.path.join(hw,f), "文件存在")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("# FACTS · 项目可引用真实事实表\n\n")
    f.write("> 自动生成，禁止手工估算。每行注明来源文件与提取方式；取不到的写「未采集」。\n")
    f.write("> 生成脚本：`tmp/_make_facts.py`（可重复运行）\n\n")
    f.write(f"共 **{len(rows)}** 条事实。\n\n")
    f.write("| # | 指标名 | 数值 | 来源文件 | 提取方式 |\n|---|---|---|---|---|\n")
    for i,(n,v,s,h) in enumerate(rows,1):
        v = str(v).replace("|","\\|")[:300]
        f.write(f"| {i} | {n} | {v} | `{s}` | {h} |\n")

print("OK 写入", OUT)
print("事实条目数:", len(rows))
print("未采集项:", sum(1 for r in rows if "未采集" in str(r[1])))
