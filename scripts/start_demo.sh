#!/bin/bash
# ============================================================
# 开发板一键启动脚本（答辩现场用）
# 自动拉起：行人检测 RTSP 源 + go2rtc 网页观看，并打印访问地址
# 用法：sudo bash start_demo.sh
# 说明：需先部署好 luckfox_pico_rtsp_yolov5（daima目录）和 go2rtc（/home/pico）
# ============================================================

DAIMA=/home/pico/daima/luckfox_pico_rtsp_yolov5_demo
GO2RTC_DIR=/home/pico
GO2RTC_BIN=$GO2RTC_DIR/go2rtc
GO2RTC_CFG=$GO2RTC_DIR/go2rtc.yaml

echo "=============================================="
echo " 毕设演示：边缘端行人感知与风险决策系统"
echo "=============================================="

# --- 停旧进程 ---
echo "[1/4] 停止旧进程..."
pkill -9 -f luckfox_pico_rtsp_yolov5 2>/dev/null || true
pkill -9 -f go2rtc 2>/dev/null || true
sleep 1

# --- 启动检测程序（RTSP 源） ---
echo "[2/4] 启动行人检测 RTSP 源..."
cd "$DAIMA"
LD_LIBRARY_PATH=./lib setsid ./luckfox_pico_rtsp_yolov5 > /tmp/demo_rtsp.log 2>&1 < /dev/null &

RTSP_OK=0
for i in $(seq 1 40); do
  if ss -tlnp 2>/dev/null | grep -q ':554'; then RTSP_OK=1; echo "      RTSP 源已就绪 (:554)"; break; fi
  sleep 1
done
if [ "$RTSP_OK" != "1" ]; then
  echo "  !! 警告：RTSP 源未在 40 秒内就绪，请检查 /tmp/demo_rtsp.log"
fi

# --- 启动 go2rtc（网页观看） ---
echo "[3/4] 启动网页观看 go2rtc..."
cd "$GO2RTC_DIR"
setsid ./go2rtc -config go2rtc.yaml > /tmp/demo_go2rtc.log 2>&1 < /dev/null &

WEB_OK=0
for i in $(seq 1 30); do
  if ss -tlnp 2>/dev/null | grep -q ':1984'; then WEB_OK=1; echo "      网页服务已就绪 (:1984)"; break; fi
  sleep 1
done
if [ "$WEB_OK" != "1" ]; then
  echo "  !! 警告：go2rtc 未就绪，请检查 /tmp/demo_go2rtc.log"
fi

# --- 获取本机 IP（WiFi 优先，其次网线） ---
echo "[4/4] 获取访问地址..."
IP=""
for iface in wlan0 eth0; do
  IP=$(ip -4 addr show "$iface" 2>/dev/null | grep inet | awk '{print $2}' | cut -d/ -f1 | head -1)
  [ -n "$IP" ] && break
done
[ -z "$IP" ] && IP=$(hostname -I 2>/dev/null | awk '{print $1}')

echo ""
echo "=============================================="
echo " 演示地址（用手机/电脑浏览器打开）:"
echo "   网页观看 : http://$IP:1984"
echo "   RTSP    : rtsp://$IP:554/live/0"
echo "=============================================="
echo " 完成。检测日志: /tmp/demo_rtsp.log"