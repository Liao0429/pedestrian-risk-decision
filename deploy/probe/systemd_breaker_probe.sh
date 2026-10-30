#!/bin/bash
# probe6：验证「限流断路器 + OnFailure 告警」语义（崩溃风暴的封顶与可见性）
PW=${PW:-}   # 构建机 sudo 口令: 执行前 export PW=...（不入库）
S() { [ -n "$PW" ] || { echo 'ERROR: 请先 export PW=<构建机sudo口令>'; exit 9; }; echo "$PW" | sudo -S -p '' "$@"; }
echo "$PW" | sudo -S -p '' -v 2>/dev/null || { echo SUDO_FAIL; exit 9; }
U=/run/systemd/system
S rm -rf /tmp/dsh-alert; mkdir -p /tmp/dsh-alert

cat > /tmp/dsh-alert.service <<'EOF'
[Unit]
Description=DSH alert marker writer
StartLimitIntervalSec=0
[Service]
Type=oneshot
ExecStart=/bin/sh -c 'date "+%F %T" >> /tmp/dsh-alert/marker.log; /bin/systemctl show -p Result -p NRestarts dsh-probe-a.service >> /tmp/dsh-alert/marker.log 2>/dev/null; echo "---" >> /tmp/dsh-alert/marker.log; logger -t dsh-alert "unit failed, marker written"'
EOF
cat > /tmp/dsh-a.service <<'EOF'
[Unit]
Description=DSH breaker probe A (5/30s + OnFailure)
StartLimitIntervalSec=30
StartLimitBurst=5
OnFailure=dsh-alert.service
[Service]
Type=simple
ExecStart=/bin/false
Restart=always
RestartSec=2
EOF
cat > /tmp/dsh-b.service <<'EOF'
[Unit]
Description=DSH breaker probe B (10/300s + OnFailure)
StartLimitIntervalSec=300
StartLimitBurst=10
OnFailure=dsh-alert.service
[Service]
Type=simple
ExecStart=/bin/false
Restart=always
RestartSec=2
EOF
S install -o root -g root -m 644 /tmp/dsh-a.service $U/dsh-probe-a.service
S install -o root -g root -m 644 /tmp/dsh-b.service $U/dsh-probe-b.service
S install -o root -g root -m 644 /tmp/dsh-alert.service $U/dsh-alert.service
S systemctl daemon-reload
S systemctl reset-failed dsh-probe-a.service dsh-probe-b.service 2>/dev/null

echo "===== [A] 5 次/30s：爆发后是否 failed，OnFailure 是否触发 ====="
S systemctl start dsh-probe-a.service
sleep 22
S systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-a.service
echo "-- alert marker --"; ls -l /tmp/dsh-alert/ 2>&1; head -12 /tmp/dsh-alert/marker.log 2>/dev/null
echo "-- journal A --"; S journalctl -u dsh-probe-a.service --no-pager -n 4 -o short-iso | sed 's/liao-TV //'
echo "-- journal alert --"; S journalctl -u dsh-alert.service --no-pager -n 4 -o short-iso | sed 's/liao-TV //'

echo "===== [B] 10 次/300s：风暴是否被封顶 ====="
S systemctl start dsh-probe-b.service
sleep 32
echo "T+32s:"; S systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-b.service
sleep 14
echo "T+46s:"; S systemctl show -p ActiveState -p Result -p NRestarts dsh-probe-b.service
echo "-- journal B --"; S journalctl -u dsh-probe-b.service --no-pager -n 4 -o short-iso | sed 's/liao-TV //'

echo "===== cleanup ====="
S systemctl stop dsh-probe-a.service dsh-probe-b.service 2>/dev/null
S rm -f $U/dsh-probe-a.service $U/dsh-probe-b.service $U/dsh-alert.service /tmp/dsh-a.service /tmp/dsh-b.service /tmp/dsh-alert.service
S systemctl daemon-reload
S systemctl reset-failed dsh-probe-a.service dsh-probe-b.service dsh-alert.service 2>/dev/null
S systemctl list-units --all 'dsh-probe*' --no-pager | tail -2
echo done