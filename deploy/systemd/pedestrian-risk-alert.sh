#!/bin/sh
# pedestrian-risk-alert.sh — 由 pedestrian-risk.service 的 OnFailure= 触发
#
# 实测（deploy/probe/probe6_breaker_results.txt、probe7）：
#   限流命中后 journal 出现 "Triggering OnFailure= dependencies."，本脚本被拉起并可写盘。
# 作用：把"主服务已进入 failed"落盘到持久目录（断电重启后仍可见）+ 一条 user.err 级日志。
#      这是"崩溃-重启风暴 -> 整机失联"链条上唯一的可见性来源。
#
# 环境变量：SERVICE / ALERT_DIR / STORM_ALERT_N
set -u

SERVICE=${SERVICE:-pedestrian-risk.service}
DIR=${ALERT_DIR:-/var/lib/pedestrian-risk}
STORM_ALERT_N=${STORM_ALERT_N:-2}

mkdir -p "$DIR" 2>/dev/null || DIR=/tmp/pedestrian-risk
mkdir -p "$DIR" 2>/dev/null || true

STAMP=$(date '+%F %T' 2>/dev/null || date)
STATE=$(systemctl show -p ActiveState -p Result -p NRestarts -p ExecMainStatus "$SERVICE" 2>/dev/null | tr '\n' ' ')
echo "[$STAMP] $SERVICE FAILED | $STATE" >> "$DIR/storm.log" 2>/dev/null

logger -t pedestrian-risk-alert -p user.err "$SERVICE 进入 failed（$STATE）；详见 $DIR/storm.log" 2>/dev/null || true

# 近 10 分钟失败次数（按 storm.log 行首时间戳粗略统计）
CUTOFF=$(date -d '10 min ago' '+%F %T' 2>/dev/null || echo "")
if [ -n "$CUTOFF" ]; then
    RECENT=$(awk -v c="$CUTOFF" 'substr($0,2,19) >= c' "$DIR/storm.log" 2>/dev/null | wc -l | tr -d ' ')
    if [ "${RECENT:-0}" -ge "$STORM_ALERT_N" ] 2>/dev/null; then
        echo "[$STAMP] 近 10 分钟 failed 次数 $RECENT >= $STORM_ALERT_N —— 崩溃风暴告警" >> "$DIR/storm.log"
        logger -t pedestrian-risk-alert -p user.err "近 10 分钟 $SERVICE 失败 $RECENT 次，疑似崩溃风暴" 2>/dev/null || true
    fi
fi
exit 0
