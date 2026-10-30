#!/bin/bash
PW=${PW:-}          # 构建机 sudo 口令: 执行前 export PW=...（不入库）
SUDO() { [ -n "$PW" ] || { echo 'ERROR: 请先 export PW=<构建机sudo口令>'; exit 9; }; echo "$PW" | sudo -S -p '' "$@"; }
echo "$PW" | sudo -S -p '' -v 2>/dev/null || { echo SUDO_FAIL; exit 9; }
cat > /tmp/dsh_oom.py <<'PYEOF'
import time
chunks = []
while True:
    chunks.append(b'x' * (1024 * 1024))
    time.sleep(0.02)

PYEOF

echo "===== [G] raw cgroup numbers: is MemoryMax enforced? ====="
cat > /tmp/dsh_g.service <<'EOF'
[Unit]
Description=DSH probe G memory
StartLimitIntervalSec=0
[Service]
Type=simple
MemoryMax=32M
OOMPolicy=stop
Restart=always
RestartSec=3
ExecStart=/usr/bin/python3 /tmp/dsh_oom.py
EOF
SUDO install -o root -g root -m 644 /tmp/dsh_g.service /run/systemd/system/dsh-probe-g.service
SUDO systemctl daemon-reload; SUDO systemctl reset-failed dsh-probe-g.service 2>/dev/null
SUDO systemctl start dsh-probe-g.service
sleep 12
echo "-- show G --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p MemoryMax -p MemoryCurrent -p MainPID dsh-probe-g.service
CG=/sys/fs/cgroup/system.slice/dsh-probe-g.service
echo "-- cgroup files --"; ls $CG 2>&1 | head -5
for f in memory.max memory.current memory.peak memory.events memory.oom.group; do
  printf '%s = ' "$f"; SUDO cat $CG/$f 2>&1 | tr '\n' ' '; echo
done
PID=$(SUDO systemctl show -p MainPID --value dsh-probe-g.service)
echo "MainPID=$PID"
[ -n "$PID" ] && [ "$PID" != "0" ] && SUDO grep -E 'VmRSS|VmSize' /proc/$PID/status
echo "-- journal G --"; SUDO journalctl -u dsh-probe-g.service --no-pager -n 6 -o short-iso | sed 's/liao-TV //'
echo "-- kernel oom --"; echo "$PW" | sudo -S -p '' dmesg 2>/dev/null | grep -iE 'oom|Killed process' | tail -3
SUDO systemctl stop dsh-probe-g.service

echo "===== [H] watchdog cadence: retry every 15s against a start-limit-hit unit ====="
cat > /tmp/dsh_h.service <<'EOF'
[Unit]
Description=DSH probe H retry cadence
StartLimitIntervalSec=30
StartLimitBurst=5
[Service]
Type=simple
ExecStart=/bin/false
Restart=on-failure
RestartSec=1
EOF
SUDO install -o root -g root -m 644 /tmp/dsh_h.service /run/systemd/system/dsh-probe-h.service
SUDO systemctl daemon-reload; SUDO systemctl reset-failed dsh-probe-h.service 2>/dev/null
SUDO systemctl start dsh-probe-h.service
sleep 8
for i in 1 2 3 4; do
  echo "$PW" | sudo -S -p '' systemctl restart dsh-probe-h.service >/dev/null 2>&1; rc=$?
  st=$(SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-h.service | tr '\n' ' ')
  echo "T+$(date +%H:%M:%S) attempt#$i rc=$rc  $st"
  sleep 15
done
echo "-- journal H (tail 6) --"; SUDO journalctl -u dsh-probe-h.service --no-pager -n 6 -o short-iso | sed 's/liao-TV //'

echo "===== cleanup ====="
SUDO systemctl stop dsh-probe-h.service 2>/dev/null
SUDO rm -f /run/systemd/system/dsh-probe-g.service /run/systemd/system/dsh-probe-h.service /tmp/dsh_g.service /tmp/dsh_h.service /tmp/dsh_oom.py
SUDO systemctl daemon-reload
SUDO systemctl reset-failed dsh-probe-g.service dsh-probe-h.service 2>/dev/null
SUDO systemctl list-units --all 'dsh-probe*' --no-pager | tail -2
echo done