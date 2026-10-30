#!/bin/bash
# DSH controlled experiment: verify systemd StartLimit semantics (build host, Ubuntu 22.04 / systemd 249)
PW=${PW:-}          # 构建机 sudo 口令: 执行前 export PW=...（不入库）
SUDO() { [ -n "$PW" ] || { echo 'ERROR: 请先 export PW=<构建机sudo口令>'; exit 9; }; echo "$PW" | sudo -S -p '' "$@"; }
if ! echo "$PW" | sudo -S -p '' -v 2>/dev/null; then echo "SUDO_VALIDATE_FAILED"; exit 9; fi
echo "sudo ok: $(echo "$PW" | sudo -S -p '' id -un 2>/dev/null)"

cat > /tmp/dsh_a.service <<'EOF'
[Unit]
Description=DSH StartLimit probe A (reproduces board config)
StartLimitIntervalSec=30
StartLimitBurst=5

[Service]
Type=simple
ExecStart=/bin/false
Restart=on-failure
RestartSec=5
EOF
cat > /tmp/dsh_b.service <<'EOF'
[Unit]
Description=DSH StartLimit probe B (hardened: rate limit disabled)
StartLimitIntervalSec=0

[Service]
Type=simple
ExecStart=/bin/false
Restart=always
RestartSec=3
EOF

echo "===== [1] board config reproduced: Restart=on-failure RestartSec=5 StartLimitIntervalSec=30 StartLimitBurst=5 ====="
SUDO install -o root -g root -m 644 /tmp/dsh_a.service /run/systemd/system/dsh-probe-a.service
SUDO systemctl reset-failed dsh-probe-a.service 2>/dev/null
SUDO systemctl daemon-reload
echo "-- effective config --"
SUDO systemctl show -p StartLimitIntervalUSec -p StartLimitBurst -p Restart -p RestartUSec dsh-probe-a.service
date +'%H:%M:%S start'
SUDO systemctl start dsh-probe-a.service; echo "start rc=$?"
sleep 40
date +'%H:%M:%S after 40s'
echo "-- show A --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p ExecMainStatus dsh-probe-a.service
echo "-- is-active A (what the watchdog tests) --"; SUDO systemctl is-active dsh-probe-a.service; echo "is-active rc=$?"
echo "-- journal A --"
SUDO journalctl -u dsh-probe-a.service --no-pager -n 30 -o short-iso | sed 's/liao-TV //'

echo "===== [2] can the watchdog rescue it? explicit restart/start while start-limit-hit ====="
echo "$PW" | sudo -S -p '' systemctl restart dsh-probe-a.service; echo "restart rc=$?"
echo "$PW" | sudo -S -p '' systemctl start dsh-probe-a.service; echo "start rc=$?"
echo "$PW" | sudo -S -p '' systemctl is-failed dsh-probe-a.service; echo "is-failed rc=$?"

echo "===== [3] recovery requires reset-failed ====="
SUDO systemctl reset-failed dsh-probe-a.service
SUDO systemctl start dsh-probe-a.service; echo "start after reset rc=$?"
sleep 4
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts dsh-probe-a.service
SUDO systemctl stop dsh-probe-a.service
SUDO systemctl reset-failed dsh-probe-a.service

echo "===== [4] proposed fix: StartLimitIntervalSec=0 + Restart=always + RestartSec=3 ====="
SUDO install -o root -g root -m 644 /tmp/dsh_b.service /run/systemd/system/dsh-probe-b.service
SUDO systemctl daemon-reload
SUDO systemctl start dsh-probe-b.service; echo "start B rc=$?"
sleep 45
echo "-- show B after 45s of instant failure (A would already be dead) --"
SUDO systemctl show -p ActiveState -p SubState -p Result -p NRestarts -p StartLimitIntervalUSec dsh-probe-b.service
echo "-- journal B (last 6) --"
SUDO journalctl -u dsh-probe-b.service --no-pager -n 6 -o short-iso | sed 's/liao-TV //'

echo "===== [5] cleanup ====="
SUDO systemctl stop dsh-probe-b.service
SUDO rm -f /run/systemd/system/dsh-probe-a.service /run/systemd/system/dsh-probe-b.service /tmp/dsh_a.service /tmp/dsh_b.service
SUDO systemctl daemon-reload
SUDO systemctl reset-failed dsh-probe-a.service dsh-probe-b.service 2>/dev/null
SUDO systemctl list-units --all 'dsh-probe*' --no-pager | tail -3
echo cleanup done