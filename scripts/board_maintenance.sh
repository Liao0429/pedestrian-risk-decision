#!/bin/sh
# board_maintenance.sh — 板端例行维护与健康快照（安全、幂等、可重复执行）
#
# 背景（20261030 实测）：根分区 80% 满；demo 目录 38 个 core dump，
#   表观 3.52GiB 而因稀疏写实际占用约 477MiB。
# 做：清 core dump -> 轮转过大的日志 -> vacuum journal -> 打印健康快照。
# 不做：不动 config/ 与二进制、不重启服务（重启交给看门狗与 board_recover.sh）。
#
# 用法：
#   sh board_maintenance.sh                 # 实际执行
#   sh board_maintenance.sh --dry-run       # 只打印将要执行的动作
#   KEEP_CORES=3 sh board_maintenance.sh    # 保留最新 3 个 core
# 可覆盖的环境变量：DEMO_DIR APP_LOG KEEP_CORES JOURNAL_KEEP LOG_MAX_BYTES LOG_KEEP_TAIL DRY_RUN
set -u

DRY_RUN=${DRY_RUN:-0}
KEEP_CORES=${KEEP_CORES:-0}
DEMO_DIR=${DEMO_DIR:-/home/pico/daima/luckfox_pico_rtsp_yolov5_demo}
APP_LOG=${APP_LOG:-/tmp/enhanced_latest.log}
JOURNAL_KEEP=${JOURNAL_KEEP:-32M}
LOG_MAX_BYTES=${LOG_MAX_BYTES:-33554432}
LOG_KEEP_TAIL=${LOG_KEEP_TAIL:-1048576}

for a in "$@"; do
    case "$a" in
        --dry-run|-n) DRY_RUN=1 ;;
        --help|-h) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "[maintenance] 未知参数: $a" >&2; exit 2 ;;
    esac
done

say() { echo "[maintenance] $*"; }

say "===== 维护开始 $(date '+%F %T' 2>/dev/null) ====="
say "DEMO_DIR=$DEMO_DIR  APP_LOG=$APP_LOG  DRY_RUN=$DRY_RUN"

# 权限自检（2026-10-31 补）：板上 SSH 登录是 pico + sudo，本脚本若以 pico 身份直接跑，
# 清 core 仍可完成（DEMO_DIR 属主是 pico，目录可写即可 unlink），但：
#   - 日志轮转：/tmp/enhanced_latest.log 是 root 所有 -> 截断会失败
#   - journal vacuum：需要 root -> 会报 Access denied
# 这里先把结论说清楚，避免"跑完了以为清干净了"。
if [ "$(id -u 2>/dev/null || echo 1)" != "0" ]; then
    say "警告: 当前不是 root（uid=$(id -u 2>/dev/null)）。journal vacuum 与日志轮转需要 root，"
    say "      若未提权，这两步会被跳过或在下面明确报失败；core 清理仍可完成。建议加 sudo 运行："
    say "      echo '<口令>' | sudo -S -p '' sh $0"
    NOT_ROOT=1
else
    NOT_ROOT=0
fi
say "磁盘(前): $(df -h / 2>/dev/null | tail -n 1)"
say "磁盘(前, kB): $(df -k / 2>/dev/null | tail -n 1)"

# ---------- 1. 清 core dump：删除范围严格限制在 DEMO_DIR 顶层的 core.<pid> ----------
if [ ! -d "$DEMO_DIR" ]; then
    say "core 清理: 跳过（目录不存在）"
elif [ "$DEMO_DIR" = "/" ]; then
    say "core 清理: 拒绝执行（DEMO_DIR 不能是 /）"
else
    CORES=$(find "$DEMO_DIR" -maxdepth 1 -type f -name 'core.[0-9]*' -printf '%T@ %p\n' 2>/dev/null | sort -n | awk '{print $2}')
    N=$(printf '%s\n' "$CORES" | grep -c . || true)
    if [ "$N" -eq 0 ]; then
        say "core 清理: 未发现 core.<pid>"
    else
        KB=$(printf '%s\n' "$CORES" | grep . | while IFS= read -r f; do du -k "$f" 2>/dev/null | awk '{print $1}'; done | awk '{s+=$1} END{print s+0}')
        APPARENT=$(printf '%s\n' "$CORES" | grep . | while IFS= read -r f; do wc -c < "$f" 2>/dev/null; done | awk '{s+=$1} END{print s+0}')
        say "core 清理: $N 个，实占 $((KB / 1024)) MiB，表观 $((APPARENT / 1048576)) MiB，保留最新 $KEEP_CORES 个"
        if [ "$KEEP_CORES" -gt 0 ]; then
            DELETE=$(printf '%s\n' "$CORES" | grep . | head -n -"$KEEP_CORES")
        else
            DELETE=$(printf '%s\n' "$CORES" | grep .)
        fi
        D=$(printf '%s\n' "$DELETE" | grep -c . || true)
        if [ "$D" -eq 0 ]; then
            say "core 清理: 无需删除"
        elif [ "$DRY_RUN" = "1" ]; then
            printf '%s\n' "$DELETE" | grep . | sed 's/^/[dry-run] 将删除 /'
        else
            printf '%s\n' "$DELETE" | grep . | while IFS= read -r f; do
                case "$f" in
                    "$DEMO_DIR"/core.[0-9]*) rm -f -- "$f" && say "已删除 $f" ;;
                    *) say "跳过（越界）: $f" ;;
                esac
            done
            say "core 清理: 完成"
        fi
    fi
fi

# ---------- 2. 轮转主日志（只保留末段，服务以 O_APPEND 打开，截断安全） ----------
if [ ! -f "$APP_LOG" ]; then
    say "日志轮转: $APP_LOG 不存在，跳过"
else
    SZ=$(wc -c < "$APP_LOG" 2>/dev/null || echo 0)
    if [ "$SZ" -gt "$LOG_MAX_BYTES" ]; then
        say "日志轮转: $SZ B > $LOG_MAX_BYTES B，保留末 $LOG_KEEP_TAIL B"
        if [ "$DRY_RUN" = "1" ]; then
            echo "[dry-run] tail -c $LOG_KEEP_TAIL $APP_LOG > $APP_LOG.1 && : > $APP_LOG"
        elif [ "$NOT_ROOT" = "1" ] && [ ! -w "$APP_LOG" ]; then
            say "日志轮转: 跳过（$APP_LOG 非当前用户可写，需要 root）"
        else
            if tail -c "$LOG_KEEP_TAIL" "$APP_LOG" > "$APP_LOG.1" 2>/dev/null; then
                if : > "$APP_LOG" 2>/dev/null; then
                    say "日志轮转: 完成（旧内容在 $APP_LOG.1）"
                else
                    say "日志轮转: 截断失败（旧内容在 $APP_LOG.1，原日志保持原样）"
                fi
            else
                say "日志轮转: 失败（保留原日志）"
            fi
        fi
    else
        say "日志轮转: $APP_LOG $SZ B，未超阈值 $LOG_MAX_BYTES B"
    fi
fi

# ---------- 3. vacuum journal（限制 systemd 日志占用） ----------
if command -v journalctl >/dev/null 2>&1; then
    say "journal: $(journalctl --disk-usage 2>/dev/null | tail -n 1)"
    # 警告: 10-30~10-31 的板端行为证据（看门狗是否动作、崩溃序列）只存在 journal 里。
    # 若尚未取证，请先跑 scripts/board_recover.sh 第 2 步（它会在任何 vacuum 之前导出）。
    say "警告: vacuum 会丢弃旧 journal。若还没导出 10-30~10-31 的证据，先跑 board_recover.sh（或本次用 DRY_RUN=1 跳过）。"
    if [ "$DRY_RUN" = "1" ]; then
        echo "[dry-run] journalctl --vacuum-size=$JOURNAL_KEEP"
    elif [ "$NOT_ROOT" = "1" ]; then
        say "journal: 跳过 vacuum（需要 root；非 root 下 journalctl 会报 Access denied）"
        say "journal: 当前用户可见用量: $(journalctl --disk-usage 2>&1 | tail -n 1)"
    else
        journalctl --vacuum-size="$JOURNAL_KEEP" 2>&1 | tail -n 3 | sed 's/^/[maintenance] journal: /'
    fi
else
    say "journal: 无 journalctl，跳过"
fi

# ---------- 4. 健康快照 ----------
say "----- 健康快照 -----"
say "时间/负载: $(date '+%F %T' 2>/dev/null)  $(cat /proc/loadavg 2>/dev/null)"
say "uptime: $(uptime 2>/dev/null | sed 's/^ *//')"
say "内存:"; free -m 2>/dev/null || head -n 3 /proc/meminfo 2>/dev/null

TEMP_SEEN=0
for tz in /sys/class/thermal/thermal_zone*; do
    [ -r "$tz/temp" ] || continue
    t=$(cat "$tz/temp" 2>/dev/null)
    name=$(cat "$tz/type" 2>/dev/null || echo thermal)
    case "$t" in ''|*[!0-9-]*) continue ;; esac
    say "温度 $name: $(awk -v v="$t" 'BEGIN{printf "%.1f", v/1000}') C"
    TEMP_SEEN=1
done
[ "$TEMP_SEEN" = "1" ] || say "温度: 无 /sys/class/thermal 节点可读（RV1106 未暴露），无法读取"
for f in /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/class/devfreq/*/cur_freq; do
    [ -r "$f" ] && say "频率 $f: $(cat "$f" 2>/dev/null) Hz"
done

say "服务状态:"
for s in pedestrian-risk.service pedestrian-watchdog.service pedestrian-watchdog.timer go2rtc.service ssh.service; do
    if systemctl cat "$s" >/dev/null 2>&1; then
        say "  $s: $(systemctl is-active "$s" 2>/dev/null)/$(systemctl is-enabled "$s" 2>/dev/null)"
    else
        say "  $s: 未安装"
    fi
done
say "pedestrian-risk 关键属性:"
systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p MemoryCurrent -p MemoryMax -p OOMPolicy -p LimitCORE pedestrian-risk.service 2>/dev/null | sed 's/^/[maintenance]   /'
say "core_pattern: $(cat /proc/sys/kernel/core_pattern 2>/dev/null)"
# 崩溃风暴告警（持久落盘，断电重启后仍可见；见 pedestrian-risk-alert.sh）
if [ -r /var/lib/pedestrian-risk/storm.log ]; then
    say "崩溃风暴告警记录（末 10 行）:"
    tail -n 10 /var/lib/pedestrian-risk/storm.log 2>/dev/null | sed 's/^/[maintenance]   /'
else
    say "崩溃风暴告警记录: 无（/var/lib/pedestrian-risk/storm.log 不存在，说明未触发过 OnFailure 告警）"
fi
say "内存 Top5 (RSS kB):"
ps -eo rss,pid,comm --sort=-rss 2>/dev/null | head -n 6 | sed 's/^/[maintenance]   /' || ps -eo rss,pid,comm 2>/dev/null | head -n 6 | sed 's/^/[maintenance]   /'
if [ -d "$DEMO_DIR" ]; then
    say "core 清理后剩余: $(find "$DEMO_DIR" -maxdepth 1 -type f -name 'core.[0-9]*' 2>/dev/null | grep -c .) 个"
fi
say "磁盘(后): $(df -h / 2>/dev/null | tail -n 1)"
say "磁盘(后, kB): $(df -k / 2>/dev/null | tail -n 1)"
say "===== 维护结束（本脚本幂等，可重复执行）====="
exit 0
