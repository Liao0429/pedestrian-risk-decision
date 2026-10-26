#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""开发板健康快照采集（8h/24h 长稳数据点）。

每次运行通过 SSH（paramiko）无侵入读取 Luckfox Pico Max 开发板状态，
向 results/edge_board/long_stability.jsonl 追加一行 JSON 快照，并打印摘要。
用于长稳测试：定时/反复运行本脚本，累积数据后分析服务重启次数、日志错误、
FPS/耗时波动、内存占用随时间的变化。

用法:
    python scripts/board_health_snapshot.py
    python scripts/board_health_snapshot.py --fps-samples 0   # 跳过 25s 帧率采样

环境变量（可覆盖默认值）:
    BOARD_HOST  默认 192.168.0.136
    BOARD_USER  默认 pico
    BOARD_PASS  默认 luckfox（Luckfox 官方默认密码，仓库为私人仓库）

输出:
    results/edge_board/long_stability.jsonl   （追加，每行一个快照）
"""
import argparse
import json
import os
import pathlib
import sys
import time

try:
    import paramiko
except ImportError:
    sys.exit("需要 paramiko：python -m pip install paramiko")

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_OUT = PROJECT_ROOT / "results" / "edge_board" / "long_stability.jsonl"

HOST = os.environ.get("BOARD_HOST", "192.168.0.136")
USER = os.environ.get("BOARD_USER", "pico")
PASS = os.environ.get("BOARD_PASS", "luckfox")

SERVICE = "pedestrian-risk"
WATCHDOG = "pedestrian-watchdog"
LOG = "/tmp/enhanced_latest.log"
PROC_PATTERN = "[l]uckfox_pico_rtsp_yolov5"


def run(ssh, cmd, timeout=30, sudo=False):
    """执行命令，返回 stdout/stderr（去空白）。sudo 用 -S 从 stdin 喂密码。"""
    if sudo:
        cmd = f"echo '{PASS}' | sudo -S {cmd} 2>/dev/null"
    try:
        _in, _out, _err = ssh.exec_command(cmd, timeout=timeout)
        out = _out.read().decode("utf-8", "replace").strip()
        err = _err.read().decode("utf-8", "replace").strip()
        return out, err
    except Exception as exc:  # noqa: BLE001
        return "", f"EXEC_ERROR: {exc}"


def get_first_line(text, default=""):
    return text.splitlines()[0].strip() if text else default


def parse_profile_last(out):
    """从最后一行 [PROFILE] 提取 avg_ms 字段为 dict。"""
    lines = [ln for ln in out.splitlines() if ln.strip()]
    if not lines:
        return {}
    last = lines[-1]
    data = {}
    for tok in last.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                data[k] = float(v)
            except ValueError:
                data[k] = v
    return data


def collect(ssh, fps_samples=25):
    snap = {"ts_iso": time.strftime("%Y-%m-%d %H:%M:%S %z")}

    out, _ = run(ssh, "date '+%Y-%m-%d %H:%M:%S %z'")
    snap["board_time"] = get_first_line(out)
    out, _ = run(ssh, "uptime")
    snap["uptime"] = get_first_line(out)

    # systemd 服务状态
    out, _ = run(ssh, f"systemctl show {SERVICE} -p ActiveState -p SubState -p NRestarts -p ExecMainStartTimestamp -p ActiveEnterTimestamp", sudo=True)
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            snap[f"svc_{k}"] = v
    out, _ = run(ssh, f"systemctl show {WATCHDOG} -p ActiveState -p SubState -p NRestarts", sudo=True)
    for line in out.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            snap[f"wd_{k}"] = v

    # 主进程
    out, _ = run(ssh, f"pgrep -f '{PROC_PATTERN}' | head -1")
    pid = get_first_line(out)
    snap["pid"] = pid or ""
    if pid:
        out, _ = run(ssh, f"ps -o pid,rss,vsz,etime,comm -p {pid} --no-headers")
        fields = out.split()
        if len(fields) >= 5:
            snap["proc_pid"], snap["proc_rss_kb"], snap["proc_vsz_kb"], snap["proc_etime"], snap["proc_comm"] = fields[:5]
        out, _ = run(ssh, f"echo '{PASS}' | sudo -S readlink /proc/{pid}/exe 2>/dev/null")
        snap["proc_exe"] = get_first_line(out)

    # 内存
    out, _ = run(ssh, "free -m")
    for line in out.splitlines():
        if line.startswith("Mem:"):
            parts = line.split()
            if len(parts) >= 7:
                snap["mem_total_mb"], snap["mem_used_mb"], snap["mem_free_mb"], snap["mem_shared_mb"], snap["mem_buff_cache_mb"], snap["mem_avail_mb"] = parts[1:7]
        elif line.startswith("Swap:"):
            parts = line.split()
            if len(parts) >= 4:
                snap["swap_total_mb"], snap["swap_used_mb"], snap["swap_free_mb"] = parts[1:4]

    # 日志健康
    out, _ = run(ssh, f"stat -c '%Y' {LOG}")
    log_mtime = 0
    try:
        log_mtime = int(get_first_line(out))
    except ValueError:
        pass
    snap["log_mtime_age_s"] = int(time.time()) - log_mtime if log_mtime else -1
    out, _ = run(ssh, f"grep -c 'rknn_run fail' {LOG}; grep -c '\\[PROFILE\\]' {LOG}; grep -c '\\[PREPROCESS\\]' {LOG}; grep -c 'EnhancedDecision' {LOG}")
    counts = out.splitlines()
    snap["log_fail_count"] = int(counts[0]) if len(counts) > 0 and counts[0].isdigit() else -1
    snap["log_profile_count"] = int(counts[1]) if len(counts) > 1 and counts[1].isdigit() else -1
    snap["log_preprocess_count"] = int(counts[2]) if len(counts) > 2 and counts[2].isdigit() else -1
    snap["log_enhanced_count"] = int(counts[3]) if len(counts) > 3 and counts[3].isdigit() else -1

    # 最近 PROFILE 性能
    out, _ = run(ssh, f"grep '\\[PROFILE\\]' {LOG} | tail -3")
    snap["profile_last"] = parse_profile_last(out)

    # 帧率采样（可选）
    if fps_samples > 0:
        out, _ = run(ssh, f"grep -c 'EnhancedDecision' {LOG}")
        try:
            c1 = int(get_first_line(out))
        except ValueError:
            c1 = 0
        time.sleep(fps_samples)
        out, _ = run(ssh, f"grep -c 'EnhancedDecision' {LOG}")
        try:
            c2 = int(get_first_line(out))
        except ValueError:
            c2 = c1
        snap["fps_sample"] = round((c2 - c1) / fps_samples, 3) if fps_samples else None

    # 错误日志（仅当前运行进程，避免统计部署迭代期的历史噪音）
    if pid:
        out, _ = run(ssh, f"journalctl -u {SERVICE} _PID={pid} --no-pager 2>/dev/null | grep -icE 'fail|error|exception|crash'", sudo=True)
        snap["journal_error_count"] = int(out) if out.isdigit() else -1
    else:
        snap["journal_error_count"] = -1

    return snap


def main():
    parser = argparse.ArgumentParser(description="开发板健康快照（长稳数据点）")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="输出 JSONL 路径")
    parser.add_argument("--fps-samples", type=int, default=25, help="帧率采样秒数，0 跳过")
    args = parser.parse_args()

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, 22, username=USER, password=PASS, timeout=10)
    try:
        snap = collect(ssh, fps_samples=args.fps_samples)
    finally:
        ssh.close()

    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(snap, ensure_ascii=False) + "\n")

    print(f"快照已追加: {out_path}")
    print(json.dumps(snap, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()