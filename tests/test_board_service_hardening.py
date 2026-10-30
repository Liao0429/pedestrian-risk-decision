"""板卡服务加固回归测试（无需硬件，不在板上执行任何命令）。

任务: T10 板卡服务加固与永久停机根因（防复发）
报告: docs/review/20261030_round3/board_service_hardening.md
实测证据: deploy/probe/probe_results.txt

覆盖范围
  1. deploy/systemd/pedestrian-risk.service 的加固关键项，且旧版危险配置不得回潮
  2. deploy/systemd/pedestrian-watchdog.{service,timer} 与看门狗脚本的改进点
  3. scripts/board_maintenance.sh / scripts/board_recover.sh 的语法与行为要点
  4. 审计报告与实测证据文件存在且包含关键结论
shell 语法用本机可用的 POSIX shell（bash/sh/ash）执行 -n 检查；找不到时 skip（可用 PED_SH 指定）。
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
UNIT_DIR = REPO / "deploy" / "systemd"
SCRIPTS = REPO / "scripts"
PROBE_DIR = REPO / "deploy" / "probe"
REPORT = REPO / "docs" / "review" / "20261030_round3" / "board_service_hardening.md"
PROBE_RESULTS = PROBE_DIR / "probe_results.txt"

RISK_UNIT = UNIT_DIR / "pedestrian-risk.service"
WD_UNIT = UNIT_DIR / "pedestrian-watchdog.service"
WD_TIMER = UNIT_DIR / "pedestrian-watchdog.timer"
WD_SH = UNIT_DIR / "pedestrian-watchdog.sh"
MAINT = SCRIPTS / "board_maintenance.sh"
RECOVER = SCRIPTS / "board_recover.sh"

MI_B = 1024 * 1024


# ---------------------------------------------------------------- helpers
def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def code_only(text: str) -> str:
    """去掉整行注释与行内注释，只留下可执行代码（断言“代码里没有 X”，而不是“文件里没写过 X”）。"""
    kept = []
    for line in text.splitlines():
        if line.strip().startswith("#"):
            continue
        kept.append(re.split(r"\s+#", line, maxsplit=1)[0])
    return "\n".join(kept)


def unit_kv(path: Path) -> "dict[str, str]":
    """解析 systemd unit 的 key=value（同 key 取最后一条，与 systemd 语义一致）。"""
    out: "dict[str, str]" = {}
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line[0] in "#;[" or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key.strip()] = value.strip()
    return out


def unit_sections(path: Path) -> "set[str]":
    return set(re.findall(r"^\[([A-Za-z]+)\]", read(path), flags=re.M))


def parse_size(text: str) -> int:
    """把 systemd 的 160M / 512K / 1024 解析为字节数。"""
    m = re.fullmatch(r"\s*(\d+)\s*([KMGT]?)\s*", text or "")
    assert m, "无法解析大小: %r" % text
    n = int(m.group(1))
    unit = m.group(2)
    return n * {"": 1, "K": 1024, "M": MI_B, "G": 1024 * MI_B, "T": 1024 * MI_B * 1024}[unit]


def _posix_shell() -> "str | None":
    cands = []
    env = os.environ.get("PED_SH")
    if env:
        cands.append(env)
    for name in ("bash.exe", "bash", "sh.exe", "sh", "ash.exe", "dash"):
        found = shutil.which(name)
        if found:
            cands.append(found)
    cands += [
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files\Git\usr\bin\sh.exe",
        r"X:\DevTools\git\bin\bash.exe",
        r"X:\DevTools\git\usr\bin\sh.exe",
        r"G:\DevTools\mingw\w64devkit\bin\sh.exe",
        r"G:\DevTools\mingw\w64devkit\bin\ash.exe",
    ]
    for cand in cands:
        if not cand or not os.path.isfile(cand):
            continue
        if os.name == "nt" and not cand.lower().endswith(".exe"):
            continue  # bash.cmd 之类需要 cmd.exe 才能启动，跳过
        try:
            ok = subprocess.run([cand, "-c", "exit 0"], capture_output=True, timeout=30)
        except Exception:
            continue
        if ok.returncode == 0:
            return cand
    return None


SHELL = _posix_shell()


# ---------------------------------------------------------------- 交付物存在
def test_artifacts_exist():
    for path in (RISK_UNIT, WD_UNIT, WD_TIMER, WD_SH, MAINT, RECOVER, REPORT, PROBE_RESULTS):
        assert path.is_file(), "缺少交付物: %s" % path


@pytest.mark.parametrize("path", [RISK_UNIT, WD_UNIT, WD_TIMER, WD_SH, MAINT, RECOVER], ids=lambda p: p.name)
def test_artifacts_are_lf_only(path: Path):
    data = path.read_bytes()
    assert b"\r\n" not in data, "%s 必须是 LF 行尾（CRLF 会让板端 #!/bin/sh 与 unit 解析出问题）" % path.name
    assert not data.startswith(b"\xef\xbb\xbf"), "%s 不得带 UTF-8 BOM" % path.name


# ---------------------------------------------------------------- 主服务 unit
def test_risk_unit_is_well_formed():
    assert unit_sections(RISK_UNIT) >= {"Unit", "Service", "Install"}
    kv = unit_kv(RISK_UNIT)
    assert kv["ExecStart"].endswith("/luckfox_pico_rtsp_yolov5")
    assert kv["WorkingDirectory"].startswith("/home/pico/daima/")


@pytest.mark.parametrize(
    "key,expected",
    [
        ("Restart", "always"),
        ("RestartSec", "3"),
        ("StartLimitIntervalSec", "0"),
        ("LimitCORE", "0"),
        ("OOMPolicy", "continue"),
        ("TimeoutStopSec", "15"),
        ("KillMode", "mixed"),
        ("Type", "simple"),
    ],
)
def test_risk_unit_hardening_keys(key: str, expected: str):
    kv = unit_kv(RISK_UNIT)
    assert kv.get(key) == expected, "pedestrian-risk.service 的 %s 应为 %s，实际 %r" % (key, expected, kv.get(key))


def test_risk_unit_log_rate_limit_present():
    kv = unit_kv(RISK_UNIT)
    assert kv.get("LogRateLimitIntervalSec") == "30s"
    assert int(kv.get("LogRateLimitBurst", "0")) >= 100


@pytest.mark.parametrize(
    "key,legacy",
    [("Restart", "on-failure"), ("StartLimitIntervalSec", "30"), ("MemoryMax", "200M")],
)
def test_risk_unit_legacy_hazards_do_not_come_back(key: str, legacy: str):
    kv = unit_kv(RISK_UNIT)
    assert kv.get(key) != legacy, "旧版危险配置 %s=%s 不得回潮（见报告 §3）" % (key, legacy)


def test_risk_unit_start_limit_burst_not_set():
    # Interval=0 已禁用限流；再写 StartLimitBurst 只会造成误读
    assert "StartLimitBurst" not in unit_kv(RISK_UNIT)


def test_risk_unit_memory_cap_reviewed_for_211mb_board():
    kv = unit_kv(RISK_UNIT)
    mmax = parse_size(kv["MemoryMax"])
    mhigh = parse_size(kv["MemoryHigh"])
    assert 128 * MI_B <= mmax <= 176 * MI_B, "211MB 板子上 MemoryMax 应留出系统余量（当前 %s）" % kv["MemoryMax"]
    assert mhigh < mmax, "MemoryHigh 必须低于 MemoryMax"


# ---------------------------------------------------------------- 看门狗
def test_watchdog_units_are_timer_driven():
    wd = unit_kv(WD_UNIT)
    assert wd.get("Type") == "oneshot"
    assert wd.get("ExecStart") == "/usr/local/bin/pedestrian-watchdog.sh"
    assert wd.get("StartLimitIntervalSec") == "0", "看门狗自身不得被启动限流（否则守护者会永久死亡）"
    assert "TimeoutStartSec" in wd, "单次运行必须有界，避免卡在 systemctl 上"
    assert wd.get("LimitCORE") == "0"

    timer = unit_kv(WD_TIMER)
    assert unit_sections(WD_TIMER) >= {"Unit", "Timer", "Install"}
    assert timer.get("Unit") == "pedestrian-watchdog.service"
    assert "OnUnitActiveSec" in timer and "OnBootSec" in timer


def test_watchdog_script_replaces_naive_loop():
    raw = read(WD_SH)
    body = code_only(raw)  # 头部注释里说明了旧实现，断言只看代码
    assert not re.search(r"while\s+true", body), "不得再用 while true 常驻循环（脚本一死守护者就永久死亡）"
    assert "sleep 15" not in body, "旧的 15s 常驻节奏必须移除"
    assert raw.startswith("#!/bin/sh")
    assert "set -u" in body


def test_watchdog_script_does_not_fight_auto_restart():
    body = read(WD_SH)
    assert "ActiveState" in body and "SubState" in body, "必须区分 failed 与 activating(auto-restart)"
    assert "activating)" in body, "必须显式不插手 systemd 的 auto-restart 窗口"
    assert "reset-failed" in body, "StartLimit 命中后必须先 reset-failed 才能再 start"


def test_watchdog_script_reports_command_results():
    body = read(WD_SH)
    assert "systemctl is-active --quiet" in body
    assert "$?" in body, "必须检查并上报 systemctl 的返回码（旧版失败也照样记一句“尝试重启”）"
    assert "STALE_SEC" in body
    assert "stat -c %Y" in body


# ---------------------------------------------------------------- shell 语法
@pytest.mark.skipif(SHELL is None, reason="本机未找到可用 POSIX shell，可用 PED_SH 指定")
@pytest.mark.parametrize("path", [MAINT, RECOVER, WD_SH], ids=lambda p: p.name)
def test_shell_scripts_have_valid_syntax(path: Path):
    proc = subprocess.run([SHELL, "-n", path.as_posix()], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    assert proc.returncode == 0, "%s 语法错误: %s%s" % (path.name, proc.stdout, proc.stderr)


# ---------------------------------------------------------------- 维护脚本
def test_maintenance_script_covers_required_actions():
    body = read(MAINT)
    for token in (
        "core.[0-9]*",
        "journalctl --vacuum-size",
        "df -h /",
        "free -m",
        "/proc/loadavg",
        "thermal_zone",
        "systemctl is-active",
        "du -k",
    ):
        assert token in body, "board_maintenance.sh 缺少必需动作: %s" % token


def test_maintenance_script_is_scoped_and_idempotent():
    body = read(MAINT)
    assert "--dry-run" in body, "必须支持 --dry-run（幂等/可预演）"
    assert "set -u" in body
    assert "set -e" not in body, "逐步失败不应中断整个维护（幂等要求）"
    assert "rm -rf /" not in body
    assert 'rm -f -- "$f"' in body, "删除必须是精确的单文件删除"
    assert '"$DEMO_DIR"/core.[0-9]*' in body, "删除范围必须限定在 DEMO_DIR 顶层的 core.<pid>"
    assert "DEMO_DIR 不能是 /" in body, "必须拒绝 DEMO_DIR=/"


# ---------------------------------------------------------------- 恢复脚本
def test_recover_script_flow():
    body = read(RECOVER)
    for token in (
        "systemctl daemon-reload",
        "reset-failed",
        "systemctl restart pedestrian-risk.service",
        "systemctl enable --now pedestrian-watchdog.timer",
        "systemctl is-active pedestrian-risk.service",
        "--put",
        "md5sum",
    ):
        assert token in body, "board_recover.sh 缺少步骤: %s" % token


def test_recover_script_transport_and_overrides():
    body = read(RECOVER)
    assert "sshpass" in body, "必须支持构建机 sshpass 直连"
    assert "ssh_exec.py" in body, "必须支持 Windows 侧 paramiko helper"
    assert "BOARD_PASS=${BOARD_PASS:-" in body, "凭据必须可用环境变量覆盖"
    assert "SSH_WAIT_SEC" in body, "必须等待板子 SSH 就绪（当前板子 TCP22 可连但无 banner）"
    assert "CHECK_ONLY" in body


def test_recover_script_supports_nonroot_login_with_sudo():
    """板端 root 直登已不可用（01:49 实测 Authentication failed），实际是 pico + sudo。"""
    body = read(RECOVER)
    assert "BOARD_USER=${BOARD_USER:-pico}" in body, "默认登录用户应为 pico（root 直登失败）"
    assert "sudo -S -p ''" in body, "非 root 登录必须用 sudo -S 转发"
    assert "RSUDO" in body, "必须有 sudo 包装开关"
    assert "含单引号" in body, "必须有引号守卫：payload 含单引号时直接失败，避免嵌套引号被破"


def test_recover_script_does_not_touch_the_board_unconditionally():
    body = read(RECOVER)
    assert "die 3" in body, "板子不可达时必须明确失败退出"
    assert "cp -a /etc/systemd/system/pedestrian-*.service" in body, "安装前必须备份板上原有 unit"
    assert "EVIDENCE=" in body, "必须导出 journal 取证并回报证据目录"


# ---------------------------------------------------------------- 报告与证据
def test_report_contains_key_conclusions():
    body = read(REPORT)
    for token in (
        "Start request repeated too quickly",
        "reset-failed",
        "StartLimitIntervalSec=0",
        "OOMPolicy",
        "MemoryMax",
        "38",
        "board_recover.sh",
        "未验证",
    ):
        assert token in body, "报告缺少关键结论/内容: %s" % token


def test_report_watchdog_journal_claim_is_traceable():
    """看门狗 journal 结论必须有出处（文件名/行号/命令/时间归属），防止被当成一手实证过度引用。"""
    body = read(REPORT)
    for token in (
        "o_board_forensics2.txt",
        "第 194~222 行",
        "journalctl -u pedestrian-watchdog -n 40",
        "Oct 27 01:20:23",
        "27 条",
        "不是 2026-10-31 00:55 的失联窗口",
        "推断",
    ):
        assert token in body, "报告缺少 journal 取证出处/边界说明: %s" % token


def test_probe_evidence_is_committed_and_sanitized():
    assert PROBE_RESULTS.is_file()
    body = read(PROBE_RESULTS)
    for token in ("Start request repeated too quickly", "ActiveState=failed", "NRestarts"):
        assert token in body, "实测证据缺少: %s" % token
    for probe in sorted(PROBE_DIR.glob("*.sh")):
        text = read(probe)
        assert "123456" not in text, "%s 不得包含构建机口令" % probe.name
        assert "PW=${PW:-}" in text, "%s 的口令必须来自环境变量" % probe.name


def test_watchdog_and_recover_agree_on_hold_path():
    """抑制文件路径是看门狗与部署脚本之间的契约，必须一致。"""
    assert "/run/pedestrian-risk.hold" in read(WD_SH)
    assert "HOLD=/run/pedestrian-risk.hold" in read(RECOVER)


def test_report_documents_why_evidence_is_unavailable():
    """"证据不可得的原因"必须写在报告里，免得读者以为我们没查。"""
    body = read(REPORT)
    for token in ("证据不可得的原因", "journald", "损坏", "不可回溯", "不存在"):
        assert token in body, "报告缺少证据不可得的说明: %s" % token


def test_probe_scripts_have_valid_syntax():
    if SHELL is None:
        pytest.skip("本机未找到可用 POSIX shell")
    for probe in sorted(PROBE_DIR.glob("*.sh")):
        proc = subprocess.run([SHELL, "-n", probe.as_posix()], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        assert proc.returncode == 0, "%s 语法错误: %s%s" % (probe.name, proc.stdout, proc.stderr)

# ---------------------------------------------------------------- 看门狗行为（桩 systemctl，无需硬件）
WATCHDOG_STUB = """#!/bin/sh
# 测试桩：记录调用，并按 FAKE_ACTIVE/FAKE_SUB 回答 show / is-active
echo "systemctl $*" >> "$STUB_LOG"
cmd=$1
case "$cmd" in
  show)
    key=$3
    case "$key" in
      ActiveState) echo "$FAKE_ACTIVE" ;;
      SubState) echo "$FAKE_SUB" ;;
      *) echo "" ;;
    esac
    ;;
  is-active)
    if [ "$FAKE_ACTIVE" = "active" ]; then echo active; exit 0; fi
    echo "$FAKE_ACTIVE"; exit 3
    ;;
  *) exit 0 ;;
esac
"""


def _run_watchdog(tmp_path, active="inactive", sub="dead", extra_env=None, log_age_sec=None, hold=False):
    """用桩 systemctl 真跑一次看门狗脚本，返回 (rc, systemctl 调用记录, 输出)。"""
    if SHELL is None:
        pytest.skip("本机未找到可用 POSIX shell")
    bindir = tmp_path / "bin"
    bindir.mkdir()
    stub = bindir / "systemctl"
    stub.write_text(WATCHDOG_STUB, encoding="utf-8")
    os.chmod(stub, 0o755)

    logf = tmp_path / "enhanced_latest.log"
    logf.write_text("x\n", encoding="utf-8")
    if log_age_sec is not None:
        old = time.time() - log_age_sec
        os.utime(logf, (old, old))

    holdf = tmp_path / "hold"
    if hold:
        holdf.write_text("", encoding="utf-8")

    calllog = tmp_path / "calls.log"
    env = dict(os.environ)
    env.update(
        {
            "PATH": bindir.as_posix() + os.pathsep + env.get("PATH", ""),
            "STUB_LOG": calllog.as_posix(),
            "FAKE_ACTIVE": active,
            "FAKE_SUB": sub,
            "SERVICE": "pedestrian-risk.service",
            "LOG": logf.as_posix(),
            "HOLD_FILE": holdf.as_posix(),
            "STALE_SEC": "60",
            "RECOVER_SETTLE_SEC": "0",
        }
    )
    if extra_env:
        env.update(extra_env)

    proc = subprocess.run([SHELL, WD_SH.as_posix()], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120, env=env)
    calls = calllog.read_text(encoding="utf-8") if calllog.exists() else ""
    return proc.returncode, calls, proc.stdout + proc.stderr


def test_watchdog_does_not_start_deliberately_stopped_service(tmp_path):
    """inactive（部署/运维 stop 后的典型状态）默认不得自动拉起。

    这正是"上传 5MB 窗口内旧看门狗把旧二进制拉起、随后 mv 覆盖"的根因。
    """
    rc, calls, out = _run_watchdog(tmp_path, active="inactive", sub="dead")
    assert "systemctl start" not in calls, "inactive 时默认不得 start（部署窗口会把旧二进制拉起）"
    assert "reset-failed" not in calls
    assert rc == 0


def test_watchdog_starts_on_inactive_only_when_explicitly_enabled(tmp_path):
    _, calls, _ = _run_watchdog(tmp_path, active="inactive", sub="dead",
                                extra_env={"WATCHDOG_START_IF_INACTIVE": "1"})
    assert "reset-failed" in calls and "systemctl start" in calls


def test_watchdog_recovers_failed_unit_reset_failed_first(tmp_path):
    _, calls, _ = _run_watchdog(tmp_path, active="failed", sub="failed")
    assert "reset-failed" in calls, "failed 必须先 reset-failed，否则限流窗口内 start 会被拒"
    assert calls.index("reset-failed") < calls.index("systemctl start")


def test_watchdog_ignores_systemd_auto_restart_window(tmp_path):
    _, calls, _ = _run_watchdog(tmp_path, active="activating", sub="auto-restart")
    assert "systemctl start" not in calls and "reset-failed" not in calls


def test_watchdog_honors_hold_file(tmp_path):
    rc, calls, out = _run_watchdog(tmp_path, active="failed", sub="failed", hold=True)
    assert "systemctl start" not in calls, "存在抑制文件时（部署窗口）必须完全不动作"
    assert rc == 0
    assert "hold" in out or "抑制" in out


def test_watchdog_restarts_hung_service_on_stale_log(tmp_path):
    _, calls, _ = _run_watchdog(tmp_path, active="active", sub="running", log_age_sec=600)
    assert "reset-failed" in calls and "systemctl start" in calls, "主循环挂死（日志陈旧）必须被拉起"


def test_watchdog_healthy_service_is_left_alone(tmp_path):
    _, calls, _ = _run_watchdog(tmp_path, active="active", sub="running", log_age_sec=5)
    assert "systemctl start" not in calls and "reset-failed" not in calls


# ---------------------------------------------------------------- 部署窗口安全（board_recover.sh）
def test_recover_stops_watchdog_before_any_upload():
    body = read(RECOVER)
    assert "touch /run/pedestrian-risk.hold" in body, "部署前必须置抑制文件"
    assert "systemctl stop pedestrian-watchdog.timer" in body, "部署前必须停掉新看门狗 timer"
    stop_at = body.index("systemctl stop pedestrian-watchdog.timer")
    put_at = body.index("bput ")
    assert stop_at < put_at, "必须在任何上传之前停看门狗（否则上传窗口内会拉起旧二进制）"


def test_recover_reports_nrestarts_before_and_after():
    body = read(RECOVER)
    assert "NR_BEFORE" in body and "NR_AFTER" in body
    assert "NRestarts: 安装前=" in body, "必须打印 NRestarts 前后值作为佐证"


def test_recover_releases_watchdog_only_after_service_active():
    body = read(RECOVER)
    assert body.count("release_watchdog") >= 3, "应有定义 + PASS/PARTIAL 两处调用"
    fail_at = body.index('say "FAIL：pedestrian-risk 未 active')
    assert "release_watchdog" not in body[fail_at:], "FAIL 时不得放开看门狗/删除抑制文件"


def test_recover_binary_replace_stops_service_before_upload():
    body = read(RECOVER)
    assert "BINARY_DST" in body and "binary_verified" in body
    seg = body[body.index('if [ -n "$BINARY" ]'):]
    assert seg.index("systemctl stop pedestrian-risk.service") < seg.index('bput "$BINARY"'), \
        "替换二进制必须先停服务再上传，避免旧进程+新文件"

