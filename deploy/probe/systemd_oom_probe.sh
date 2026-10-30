#!/bin/bash
PW=${PW:-}          # 构建机 sudo 口令: 执行前 export PW=...（不入库）
SUDO() { [ -n "$PW" ] || { echo 'ERROR: 请先 export PW=<构建机sudo口令>'; exit 9; }; echo "$PW" | sudo -S -p '' "$@"; }
echo "$PW" | sudo -S -p '' -v 2>/dev/null || { echo SUDO_FAIL; exit 9; }
cat > /tmp/dsh_oom.py <<'PYEOF'
import time
chunks = []
while True:
    chunks.append(b'x' * (1024 * 1024))
    time.sleep(0.05)

PYEOF

mk() {
  cat > /tmp/dsh_$1.service <<EOF
[Unit]
Description=DSH OOM probe $1 (OOMPolicy=$2)
StartLimitIntervalSec=0

[Service]
Type=simple
MemoryMax=32M
OOMPolicy=$2
Restart=always
RestartSec=3
ExecStart=/usr/bin/python3 /tmp/dsh_oom.py
EOF
  SUDO install -o root -g root -m 644 /tmp/dsh_$1.service /run/systemd/system/dsh-probe-$1.service
}

echo "===== [C] OOMPolicy=stop + MemoryMax=32M + Restart=always ====="
mk c stop
SUDO systemctl daemon-reload; SUDO systemctl reset-failed dsh-probe-c.service 2>/dev/null
SUDO systemctl start dsh-probe-c.service
sleep 25
echo "-- show C --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p OOMPolicy dsh-probe-c.service
SUDO systemctl is-active dsh-probe-c.service; echo "is-active rc=$?"
sleep 8
echo "-- show C after +8s (did Restart=always revive it?) --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-c.service
echo "-- journal C --"
SUDO journalctl -u dsh-probe-c.service --no-pager -n 14 -o short-iso | sed 's/liao-TV //'
echo "-- kernel OOM lines --"
echo "$PW" | sudo -S -p '' dmesg 2>/dev/null | grep -i -E "oom|killed process" | tail -4
SUDO systemctl stop dsh-probe-c.service

echo "===== [D] OOMPolicy=continue (proposed) ====="
mk d continue
SUDO systemctl daemon-reload; SUDO systemctl reset-failed dsh-probe-d.service 2>/dev/null
SUDO systemctl start dsh-probe-d.service
sleep 25
echo "-- show D --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p OOMPolicy dsh-probe-d.service
SUDO systemctl stop dsh-probe-d.service

echo "===== [E] retry window: can a 15s-cadence watchdog revive a start-limit-hit unit? ====="
cat > /tmp/dsh_e.service <<'EOF'
[Unit]
Description=DSH probe E (board config, fast cadence)
StartLimitIntervalSec=30
StartLimitBurst=5

[Service]
Type=simple
ExecStart=/bin/false
Restart=on-failure
RestartSec=1
EOF
SUDO install -o root -g root -m 644 /tmp/dsh_e.service /run/systemd/system/dsh-probe-e.service
SUDO systemctl daemon-reload; SUDO systemctl reset-failed dsh-probe-e.service 2>/dev/null
SUDO systemctl start dsh-probe-e.service
sleep 9
echo "-- state after burst --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-e.service
echo "-- T+9s: explicit restart INSIDE the 30s window --"
echo "$PW" | sudo -S -p '' systemctl restart dsh-probe-e.service; echo "restart(inside window) rc=$?"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-e.service
echo "-- T+11s: is-active (what the watchdog tests) --"
SUDO systemctl is-active dsh-probe-e.service; echo "is-active rc=$?"
echo "-- T+34s: explicit restart AFTER the interval has passed --"
sleep 23
echo "$PW" | sudo -S -p '' systemctl restart dsh-probe-e.service; echo "restart(after window) rc=$?"
sleep 3
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-e.service
echo "-- journal E (tail 8) --"
SUDO journalctl -u dsh-probe-e.service --no-pager -n 8 -o short-iso | sed 's/liao-TV //'

echo "===== cleanup ====="
SUDO systemctl stop dsh-probe-e.service 2>/dev/null
SUDO rm -f /run/systemd/system/dsh-probe-c.service /run/systemd/system/dsh-probe-d.service /run/systemd/system/dsh-probe-e.service /tmp/dsh_c.service /tmp/dsh_d.service /tmp/dsh_e.service /tmp/dsh_oom.py
SUDO systemctl daemon-reload
SUDO systemctl reset-failed dsh-probe-c.service dsh-probe-d.service dsh-probe-e.service 2>/dev/null
SUDO systemctl list-units --all 'dsh-probe*' --no-pager | tail -2
echo done