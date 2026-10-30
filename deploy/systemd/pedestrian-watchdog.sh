#!/bin/sh
# pedestrian-watchdog.sh — 板端看门狗（单次执行，由 pedestrian-watchdog.timer 每 30s 触发）
#
# 与旧版（20261030 现场取证可见 while true + sleep 15 常驻循环）的差异：
#   1) 判活只认 ActiveState，绝不插手 activating(auto-restart)：
#      旧版用 systemctl is-active 判活，该命令在 systemd 自动重启窗口同样返回 3，
#      于是每 15s 发一次 systemctl restart —— 每次 restart 都是一次 start，
#      反而把 StartLimit 窗口喂满（实测：窗口内 restart 被拒 rc=1，见报告 §3.1 实验 E/H）。
#   2) inactive 默认不再自动拉起（2026-10-31 新增，见报告 §4.6 部署窗口缺陷）：
#      旧版遇到 inactive 就 start，而"部署/替换二进制"本身就是
#      stop -> 上传 5MB（40~60s）-> mv -> start；这几十秒内服务是 inactive，
#      看门狗把旧二进制拉起来，随后 mv 覆盖文件 -> 出现"旧进程+新文件"、
#      额外 NRestarts、甚至崩溃。现在 inactive 只记录不动手，除非显式
#      WATCHDOG_START_IF_INACTIVE=1（给确实需要"无序必拉起"的场景留口子）。
#   3) 维护抑制文件 /run/pedestrian-risk.hold：存在即完全不动作（部署脚本会先 touch 它）。
#   4) failed 先 reset-failed 再 start：StartLimit 命中/崩溃退出后单元停在 failed，
#      窗口内任何 start/restart 都会被拒（journal: "Start request repeated too quickly"）。
#   5) 检查并上报每条命令的返回码；旧版失败也照样记一句"尝试重启"，看起来像在救火。
#   6) 日志文件缺失时给出明确结论；旧版 if [ -f ] 无 else，静默跳过陈旧检查。
#   7) 陈旧阈值默认 60s（旧版 30s）：7fps 下 30s 太短，正常抖动会被误判为挂死。
#
# 环境变量：SERVICE / LOG / STALE_SEC / DRY_RUN / HOLD_FILE /
#           WATCHDOG_START_IF_INACTIVE / RECOVER_SETTLE_SEC
set -u

SERVICE=${SERVICE:-pedestrian-risk.service}
LOG=${LOG:-/tmp/enhanced_latest.log}
STALE_SEC=${STALE_SEC:-60}
DRY_RUN=${DRY_RUN:-0}
HOLD_FILE=${HOLD_FILE:-/run/pedestrian-risk.hold}
START_IF_INACTIVE=${WATCHDOG_START_IF_INACTIVE:-0}
RECOVER_SETTLE_SEC=${RECOVER_SETTLE_SEC:-5}

note() { echo "[watchdog] $*"; logger -t pedestrian-watchdog "$*" 2>/dev/null || true; }

show_state() {
    ACTIVE=$(systemctl show -p ActiveState --value "$SERVICE" 2>/dev/null || echo unknown)
    SUB=$(systemctl show -p SubState --value "$SERVICE" 2>/dev/null || echo unknown)
}

recover() {
    reason=$1
    if [ -f "$HOLD_FILE" ]; then
        note "存在抑制文件 $HOLD_FILE（维护/部署窗口），放弃恢复动作"
        return 0
    fi
    note "$SERVICE state=$ACTIVE sub=$SUB 原因: $reason"
    if [ "$DRY_RUN" = "1" ]; then
        note "[dry-run] systemctl reset-failed $SERVICE && systemctl start $SERVICE"
        return 0
    fi
    if systemctl reset-failed "$SERVICE"; then
        note "reset-failed 成功（清除 StartLimit/OOM 命中状态）"
    else
        note "reset-failed 返回非零，仍继续尝试启动"
    fi
    if systemctl start "$SERVICE"; then
        note "start 已下发"
    else
        rc=$?
        note "start 失败 rc=$rc（start-limit 命中时需等限流窗口过去）"
    fi
    sleep "$RECOVER_SETTLE_SEC"
    if systemctl is-active --quiet "$SERVICE"; then
        note "恢复成功"
        return 0
    fi
    note "下发后仍未 active，交给下一个 timer 周期"
    return 1
}

stale_seconds() {
    [ -f "$LOG" ] || { echo ""; return; }
    now=$(date +%s)
    mtime=$(stat -c %Y "$LOG" 2>/dev/null || echo 0)
    [ "$mtime" = "0" ] && { echo ""; return; }
    echo $((now - mtime))
}

# 维护/部署抑制：存在即完全不动作（部署脚本在替换二进制/unit 前会 touch 它）
if [ -f "$HOLD_FILE" ]; then
    note "检测到抑制文件 $HOLD_FILE（维护/部署窗口），本次不动作"
    exit 0
fi

show_state
note "检查: state=$ACTIVE sub=$SUB"

case "$ACTIVE" in
    active)
        age=$(stale_seconds)
        if [ -z "$age" ]; then
            note "警告: 日志 $LOG 不存在，无法做陈旧判定"
            exit 0
        fi
        if [ "$age" -gt "$STALE_SEC" ]; then
            recover "日志已 ${age} 秒未更新（阈值 ${STALE_SEC} 秒），主循环疑似挂死"
            exit $?
        fi
        note "健康: 日志 ${age} 秒前更新（阈值 ${STALE_SEC} 秒）"
        exit 0
        ;;
    activating)
        note "systemd 正在自行处理（sub=$SUB），看门狗不介入"
        exit 0
        ;;
    failed)
        recover "单元处于 failed（StartLimit 命中 / OOM / 崩溃退出）"
        exit $?
        ;;
    inactive)
        if [ "$START_IF_INACTIVE" = "1" ]; then
            recover "单元未运行（WATCHDOG_START_IF_INACTIVE=1 显式开启自动拉起）"
            exit $?
        fi
        note "单元 inactive（正常运维/部署窗口，或尚未启动）；默认不自动拉起，避免在部署窗口把旧二进制拉起"
        exit 0
        ;;
    *)
        note "状态无法判定（ACTIVE=$ACTIVE），只记录不动作"
        exit 0
        ;;
esac
