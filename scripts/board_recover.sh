#!/bin/sh
# board_recover.sh — 把 deploy/systemd/ 的加固 unit 安装到开发板并重启验证。
#
# 前置条件：板子 192.168.0.136 需已断电重启。
#   2026-10-31 00:55 现场：TCP22 可连但 12s 内不出 banner、554 已关闭、go2rtc 仍在 -> SSH 不可用。
#   本脚本会先等待 SSH 就绪，就绪后才动手；等待超时则明确失败，不做任何破坏性操作。
#
# 传输方式（自动探测，二选一）：
#   1) 本机有 sshpass        -> sshpass -p "$BOARD_PASS" ssh / scp
#   2) 否则用 paramiko helper -> "$PED_PYTHON" "$PED_HELPER"（默认 $REPO_ROOT/tmp/net/ssh_exec.py）
#   也可直接在构建机 192.168.0.63（装了 sshpass）上执行同一脚本。
#
# 用法：
#   BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh
#   CHECK_ONLY=1 BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh    # 只体检，不安装不重启
#   PED_PYTHON='X:/py311_gpu/Scripts/python.exe' PED_HELPER='G:/PedestrianRiskDecision/tmp/net/ssh_exec.py' \
#     BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh
#   BINARY=/path/to/luckfox_pico_rtsp_yolov5 BINARY_MD5=<期望md5> BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh
#       # 顺带替换主二进制：先 stop 服务 -> 上传 -> md5 校验 -> install，避免"旧进程+新文件"
# 登录：默认 pico + sudo（板上 root 直登已不可用）；任何替换二进制的流程都必须在
#       窗口内停看门狗并置 /run/pedestrian-risk.hold，本脚本已内置该顺序。
# 退出码：0 通过 / 1 验证未通过 / 2 缺少传输手段或前置文件 / 3 板子不可达
set -u

BOARD_HOST=${BOARD_HOST:-192.168.0.136}
# 板端 root 的 SSH 直登不可用（2026-10-31 01:49 实测 root -> Authentication failed）；
# 实际可用的是 pico 登录 + sudo（已验证：echo "$BOARD_PASS" | sudo -S -p '' id -un -> root）。
BOARD_USER=${BOARD_USER:-pico}
# 安全：**不提供默认口令**。口令必须由环境变量或 $BOARD_PASS_FILE 注入，
# 以免真实凭据进入版本库（2026-10-31 安全整改：旧版本曾把默认口令写死并被推到公开仓库）。
BOARD_PASS=${BOARD_PASS:-}
if [ -z "$BOARD_PASS" ] && [ -n "${BOARD_PASS_FILE:-}" ] && [ -f "$BOARD_PASS_FILE" ]; then
    BOARD_PASS=$(cat "$BOARD_PASS_FILE")
fi
if [ -z "$BOARD_PASS" ]; then
    echo "[recover] 错误：未提供 BOARD_PASS，也未提供可读的 BOARD_PASS_FILE。" >&2
    echo "[recover] 本脚本不提供默认口令（旧版本把默认口令写死并被推到公开仓库，2026-10-31 整改）。" >&2
    exit 2
fi
SSH_WAIT_SEC=${SSH_WAIT_SEC:-180}
CHECK_ONLY=${CHECK_ONLY:-0}
# 维护抑制文件：部署窗口内让看门狗完全不动作（看门狗脚本会检查它）
HOLD=/run/pedestrian-risk.hold
# 可选：同时替换主二进制（解决"部署期间看门狗把旧二进制拉起"的根因流程）
BINARY=${BINARY:-}
BINARY_DST=${BINARY_DST:-/home/pico/daima/luckfox_pico_rtsp_yolov5_demo/luckfox_pico_rtsp_yolov5}
BINARY_MD5=${BINARY_MD5:-}

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UNIT_DIR="$REPO_ROOT/deploy/systemd"

say() { echo "[recover] $*"; }
die() { rc=$1; shift; echo "[recover] 失败: $*" >&2; exit "$rc"; }

# ---------- 传输层 ----------
# raw() = 纯传输原语；bsh() 在其上加 sudo 包装（非 root 登录时）。
SSH_OPTS='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8'
if command -v sshpass >/dev/null 2>&1 && command -v ssh >/dev/null 2>&1; then
    MODE=sshpass
    raw() { sshpass -p "$BOARD_PASS" ssh $SSH_OPTS "$BOARD_USER@$BOARD_HOST" "$1"; }
    bput() { sshpass -p "$BOARD_PASS" scp $SSH_OPTS "$1" "$BOARD_USER@$BOARD_HOST:$2" >/dev/null; }
elif [ -f "${PED_HELPER:-$REPO_ROOT/tmp/net/ssh_exec.py}" ]; then
    MODE=helper
    PED_PYTHON=${PED_PYTHON:-python}
    PED_HELPER=${PED_HELPER:-$REPO_ROOT/tmp/net/ssh_exec.py}
    raw() { "$PED_PYTHON" "$PED_HELPER" "$BOARD_HOST" "$BOARD_USER" "$BOARD_PASS" -- "$1"; }
    bput() { "$PED_PYTHON" "$PED_HELPER" "$BOARD_HOST" "$BOARD_USER" "$BOARD_PASS" --put "$1" "$2" >/dev/null; }
else
    die 2 "既无 sshpass，也未找到 helper（${PED_HELPER:-$REPO_ROOT/tmp/net/ssh_exec.py}）。请安装 sshpass，或用 PED_PYTHON/PED_HELPER 指定。"
fi

# 非 root 登录 -> 用 sudo -S 把整条远程命令交给 root 的 sh 执行
# 注意：payload 不得含单引号（否则嵌套引号会破），由下面的 case 直接拦下
RSUDO=""
[ "$BOARD_USER" = "root" ] || RSUDO=1
bsh() {
    case "$1" in
        *"'"*) die 2 "内部错误：远程命令含单引号，无法安全经 sudo 转发" ;;
    esac
    if [ -n "$RSUDO" ]; then
        raw "echo '$BOARD_PASS' | sudo -S -p '' sh -c '$1'"
    else
        raw "$1"
    fi
}
say "传输方式: $MODE  目标: $BOARD_USER@$BOARD_HOST  sudo包装=$([ -n "$RSUDO" ] && echo yes || echo no)"

for f in pedestrian-risk.service pedestrian-watchdog.service pedestrian-watchdog.timer pedestrian-watchdog.sh pedestrian-risk-alert.service pedestrian-risk-alert.sh; do
    [ -f "$UNIT_DIR/$f" ] || die 2 "缺少前置文件 $UNIT_DIR/$f"
done

# ---------- 0. 等 SSH ----------
say "== 0/7 等待板子 SSH 就绪（上限 ${SSH_WAIT_SEC} 秒）=="
waited=0; ok=0
while [ "$waited" -lt "$SSH_WAIT_SEC" ]; do
    if bsh 'true' >/dev/null 2>&1; then ok=1; say "SSH 就绪（等待 ${waited} 秒）"; break; fi
    sleep 10; waited=$((waited + 10))
    say "  仍不可用（${waited} 秒）；TCP22 可连但无 banner 是已知症状，需断电重启"
done
[ "$ok" = "1" ] || die 3 "板子 SSH 不可用（等待 ${SSH_WAIT_SEC} 秒），先断电重启再执行本脚本"

# ---------- 1. 预检 ----------
say "== 1/7 预检 =="
bsh 'uname -a; systemctl --version | head -n 1; systemctl is-system-running; free -m | head -n 2; df -h / | tail -n 1'

# ---------- 2. 备份现有 unit + 导出 journal 取证（只读） ----------
# 关键：本步刻意放在 CHECK_ONLY 分支之前 —— 取证只读、不重启、不改配置，
# 因此即使当前正被别的任务占用（例如 T6/T8 上板测量），也能安全地把证据抢下来；
# 且必须早于任何 journalctl --vacuum（board_maintenance.sh 会 vacuum）。
# 说明：上一 boot 起始时钟是 2023-09-20（RTC 未同步），按时间过滤会漏掉早段，
#       因此同时保存 --boot -1（整个上一 boot，与时钟无关）与无时间过滤的 last3000。
say "== 2/7 备份现有 unit + 导出 journal 取证（只读，先于任何 vacuum/安装）=="
# 写 /tmp（任何用户可写）再拷贝一份到 /root（root-only，防 /tmp 被清）；两处都留证据
bsh 'TS=$(date +%Y%m%d-%H%M%S); BK=/tmp/ped-evidence-$TS; mkdir -p $BK; cp -a /etc/systemd/system/pedestrian-*.service /etc/systemd/system/pedestrian-*.timer $BK/ 2>/dev/null; journalctl --list-boots --no-pager > $BK/journal_boots.txt 2>&1; journalctl --disk-usage > $BK/journal_disk_usage.txt 2>&1; journalctl -b -1 -n 3000 --no-pager > $BK/journal_prevboot_last3000.txt 2>/dev/null; journalctl -b -1 -u pedestrian-risk -u pedestrian-watchdog --no-pager > $BK/journal_prevboot_units.txt 2>/dev/null; journalctl -u pedestrian-risk -u pedestrian-watchdog -n 3000 --no-pager > $BK/journal_last3000_units.txt 2>/dev/null; journalctl --since "2026-10-30 20:00" --until "2026-10-31 01:40" --no-pager > $BK/journal_1030_1031_window.txt 2>/dev/null; mkdir -p /root/ped-evidence-$TS; cp -a $BK/. /root/ped-evidence-$TS/; echo "EVIDENCE=$BK"; echo "ROOT_COPY=/root/ped-evidence-$TS"; wc -l $BK/*.txt; echo "--- unit 备份 ---"; ls -l $BK'

# 安装前基线（用于事后对比 NRestarts 是否被无谓地抬高）
NR_BEFORE=$(bsh 'systemctl show -p NRestarts --value pedestrian-risk.service' 2>/dev/null | tr -d '\r' | tail -n 1)
ACT_BEFORE=$(bsh 'systemctl is-active pedestrian-risk.service' 2>/dev/null | tr -d '\r' | tail -n 1)
say "安装前基线: is-active=${ACT_BEFORE:-?} NRestarts=${NR_BEFORE:-?}"

if [ "$CHECK_ONLY" = "1" ]; then
    say "CHECK_ONLY=1：只体检，不安装不重启（上面的 journal 取证已完成）"
    bsh 'systemctl is-active pedestrian-risk.service; systemctl show -p NRestarts -p Result -p OOMPolicy -p MemoryMax -p StartLimitIntervalUSec pedestrian-risk.service; echo "--- 上一 boot 最后 12 条 ---"; journalctl -b -1 -n 12 --no-pager'
    exit 0
fi

# ---------- 3. 先停看门狗 + 置抑制文件（关键：部署窗口内不得有任何东西拉起服务） ----------
# 背景（board-perf 2026-10-31 核到）：旧部署流程是 stop -> 上传 5MB(40~60s) -> mv -> start，
# 而旧看门狗在服务 inactive 后会 systemctl restart，于是上传窗口内把**旧二进制**拉起来，
# 随后 mv 覆盖文件 -> "旧进程 + 新文件"，并抬高 NRestarts。这里用两道保险：
#   ① 停掉 watchdog（timer 与旧的常驻 service 都停）  ② touch 抑制文件，让脚本也不动作
say "== 3/7 停看门狗 + 置抑制文件（先于任何上传/替换）=="
bsh 'touch /run/pedestrian-risk.hold; systemctl stop pedestrian-watchdog.timer 2>/dev/null; systemctl stop pedestrian-watchdog.service 2>/dev/null; echo "watchdog_states=$(systemctl is-active pedestrian-watchdog.timer pedestrian-watchdog.service 2>/dev/null | tr "\n" " ")"; ls -l /run/pedestrian-risk.hold'

# ---------- 4. 上传并安装 ----------
say "== 4/7 上传并安装（unit 「$BINARY」）=="
for f in pedestrian-risk.service pedestrian-watchdog.service pedestrian-watchdog.timer pedestrian-watchdog.sh pedestrian-risk-alert.service pedestrian-risk-alert.sh; do
    bput "$UNIT_DIR/$f" "/tmp/$f" || die 2 "上传 $f 失败"
    say "  已上传 $f"
done
if [ -n "$BINARY" ]; then
    [ -f "$BINARY" ] || die 2 "BINARY 指定但文件不存在: $BINARY"
    say "  替换二进制: $BINARY -> $BINARY_DST"
    # 先停服务，再上传 5MB，再校验，最后才 install（避免"旧进程+新文件"）
    bsh 'systemctl stop pedestrian-risk.service 2>/dev/null; echo "service_after_stop=$(systemctl is-active pedestrian-risk.service)"'
    bput "$BINARY" "/tmp/ped_binary_new" || die 2 "上传二进制失败"
    bsh 'src=/tmp/ped_binary_new; dst='"$BINARY_DST"'; ls -l $src; test -s $src || exit 1; md5sum $src; install -m 755 $src $dst; md5sum $dst; test -s $dst || exit 1; echo binary_verified'
    if [ -n "$BINARY_MD5" ]; then
        GOT=$(bsh "md5sum $BINARY_DST" 2>/dev/null | awk '{print $1}' | tail -n 1 | tr -d '\r')
        [ "$GOT" = "$BINARY_MD5" ] || die 2 "二进制 md5 校验失败: 期望 $BINARY_MD5 实际 $GOT"
        say "  二进制 md5 校验通过: $GOT"
    fi
fi
bsh 'install -m 644 /tmp/pedestrian-risk.service /etc/systemd/system/pedestrian-risk.service; install -m 644 /tmp/pedestrian-watchdog.service /etc/systemd/system/pedestrian-watchdog.service; install -m 644 /tmp/pedestrian-watchdog.timer /etc/systemd/system/pedestrian-watchdog.timer; install -m 755 /tmp/pedestrian-watchdog.sh /usr/local/bin/pedestrian-watchdog.sh; install -m 644 /tmp/pedestrian-risk-alert.service /etc/systemd/system/pedestrian-risk-alert.service; install -m 755 /tmp/pedestrian-risk-alert.sh /usr/local/bin/pedestrian-risk-alert.sh; echo installed; md5sum /etc/systemd/system/pedestrian-risk.service /etc/systemd/system/pedestrian-risk-alert.service /usr/local/bin/pedestrian-watchdog.sh /usr/local/bin/pedestrian-risk-alert.sh' || die 2 "安装失败"

say "单元语法自检（systemd-analyze verify，仅供参考，非致命）:"
bsh 'command -v systemd-analyze >/dev/null 2>&1 && systemd-analyze verify /etc/systemd/system/pedestrian-risk.service /etc/systemd/system/pedestrian-watchdog.timer 2>&1 | head -n 20; echo verify-rc=$?'

# ---------- 5. daemon-reload + reset-failed + 启用 + 重启（看门狗仍保持停止） ----------
say "== 5/7 daemon-reload + reset-failed + 启用 + 重启 =="
bsh 'systemctl daemon-reload; systemctl reset-failed pedestrian-risk.service; systemctl disable --now pedestrian-watchdog.service 2>/dev/null; systemctl enable pedestrian-risk.service; systemctl restart pedestrian-risk.service; echo restart-rc=$?'
say "看门狗此刻仍停止（避免干扰判定）:"
bsh 'echo "watchdog_timer=$(systemctl is-active pedestrian-watchdog.timer)"; test -f /run/pedestrian-risk.hold && echo "hold=present" || echo "hold=absent"'

# ---------- 6. 等待并验证 ----------
say "== 6/7 等待 25 秒后验证 =="
sleep 25
bsh 'systemctl is-active pedestrian-risk.service; systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p OOMPolicy -p MemoryMax -p MemoryHigh -p StartLimitIntervalUSec -p LimitCORE -p TimeoutStopUSec pedestrian-risk.service; echo "NR_AFTER=$(systemctl show -p NRestarts --value pedestrian-risk.service)"; systemctl is-active pedestrian-watchdog.timer'
bsh 'echo "--- 应用日志尾部 ---"; tail -n 5 /tmp/enhanced_latest.log 2>/dev/null; echo "--- journal ---"; journalctl -u pedestrian-risk.service -n 10 --no-pager'
bsh 'echo "--- 端口 ---"; (ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -E ":(554|8554|1984|22)" ; echo "--- 资源 ---"; df -h / | tail -n 1; cat /proc/loadavg'

# ---------- 7. 判定 + 收尾（确认服务 OK 后才放开看门狗） ----------
say "== 7/7 判定与放开看门狗 =="
ACT=$(bsh 'systemctl is-active pedestrian-risk.service' 2>/dev/null | tr -d '\r' | tail -n 1)
NR=$(bsh 'systemctl show -p NRestarts --value pedestrian-risk.service' 2>/dev/null | tr -d '\r' | tail -n 1)
say "is-active=$ACT  NRestarts: 安装前=${NR_BEFORE:-?} -> 安装后=${NR:-?}"
if [ -n "$NR_BEFORE" ] && [ -n "$NR" ] && [ "$NR_BEFORE" != "$NR" ]; then
    say "佐证：安装期间 NRestarts 从 $NR_BEFORE 变为 $NR。本次安装已先停看门狗 + 置抑制文件，若仍增长，说明还有别的东西在拉起服务（旧看门狗的典型症状）"
fi

release_watchdog() {
    bsh 'rm -f /run/pedestrian-risk.hold; systemctl enable --now pedestrian-watchdog.timer; systemctl list-timers pedestrian-watchdog.timer --no-pager | head -n 3; echo "hold=$(test -f /run/pedestrian-risk.hold && echo present || echo absent)"'
}

if [ "$ACT" = "active" ] && [ "${NR:-0}" = "0" ]; then
    say "PASS：服务 active 且无自动重启；现在放开看门狗"
    release_watchdog
    say "继续观察 30 分钟确认无新 core"
    exit 0
fi
if [ "$ACT" = "active" ]; then
    say "PARTIAL：服务 active，但 NRestarts=${NR:-?}；现在放开看门狗并观察是否持续增长"
    release_watchdog
    exit 0
fi
say "FAIL：pedestrian-risk 未 active（「$ACT]）；**看门狗保持停止、抑制文件保留**，请看上面的 journal 与 systemd-analyze 输出后再决定"
exit 1
