#!/bin/bash
# ============================================================
# go2rtc 本地视频 demo 流一键配置脚本（P2-1：演示兜底）
# ------------------------------------------------------------
# 用途：答辩/演示时现场镜头前无人走动，实时画面可能一直 SAFE。
#       本脚本向 go2rtc 添加一个本地视频文件流（demo），
#       手机浏览器打开 go2rtc 网页点击 demo 即可播放预录的
#       密集人群增强视频（DANGER 切换），保证现场演示效果。
#
# 依赖：bash + awk（开发板 Buildroot 自带，无需 python3）
#       已部署 go2rtc（/home/pico/go2rtc，参考 docs/WEB_VIEWER.md）
#
# 用法：
#   sudo bash setup_go2rtc_demo_stream.sh \
#       --video /userdata/demo/mixkit_13192_enhanced.mp4 [--stream demo] [--dry-run]
#
# 视频文件需先放到开发板（可用 scp 从 PC 拷贝，见 --video 缺省提示）。
# 执行后自动：备份 go2rtc.yaml → 添加/更新 demo 流 → 重启 go2rtc → 验证 API。
# ============================================================

set -u

GO2RTC_CFG="${GO2RTC_CFG:-/home/pico/go2rtc.yaml}"
GO2RTC_BIN="${GO2RTC_BIN:-/home/pico/go2rtc}"
DEFAULT_VIDEO="/userdata/demo/mixkit_13192_enhanced.mp4"
VIDEO=""
STREAM="demo"
DRY_RUN=0
PORT=1984

# --- 解析参数 ---
while [ $# -gt 0 ]; do
  case "$1" in
    --video) VIDEO="${2:-}"; shift 2 ;;
    --stream) STREAM="${2:-}"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) echo "!! 未知参数: $1"; exit 2 ;;
  esac
done
[ -z "$VIDEO" ] && VIDEO="$DEFAULT_VIDEO"

echo "=============================================="
echo " go2rtc 本地视频 demo 流配置（P2-1）"
echo "=============================================="
echo "  配置    : $GO2RTC_CFG"
echo "  流名称  : $STREAM"
echo "  视频    : $VIDEO"

# --- 检查视频文件 ---
if [ ! -f "$VIDEO" ]; then
  echo "!! 视频文件不存在: $VIDEO"
  echo ""
  echo "   请先从 PC 拷贝到开发板，例如："
  echo "   scp results/video_enhanced_fsm/mixkit_13192_enhanced.mp4 \\"
  echo "       pico@<开发板IP>:/userdata/demo/"
  echo "   （目录不存在先执行: sudo mkdir -p /userdata/demo）"
  exit 1
fi
echo "  [OK] 视频文件存在: $VIDEO"

# --- 检查 go2rtc 配置文件 ---
if [ ! -f "$GO2RTC_CFG" ]; then
  echo "!! 配置文件不存在: $GO2RTC_CFG"
  echo "   请先按 docs/WEB_VIEWER.md 部署 go2rtc 并生成配置。"
  exit 1
fi

# ============================================================
# 编辑函数：向 go2rtc.yaml 添加/更新 $STREAM 本地视频流
#  - 保留其他流（cam 等）
#  - 已存在且指向同一视频 → 不变（幂等）
#  - 已存在但指向旧视频 → 原位替换
#  - 无 streams 块 → 重建最小配置（streams + api）
# ============================================================
edit_yaml() {
  local cfg="$1" stream="$2" video="$3"

  # 幂等：已配置且指向同一视频 → 输出原内容
  if grep -qE "^  ${stream}:" "$cfg" && grep -qF "file://${video}" "$cfg"; then
    cat "$cfg"
    return 0
  fi

  # 无 streams 块 → 重建最小配置（保留原文件为备份由调用方负责）
  if ! grep -qE "^streams:" "$cfg"; then
    {
      echo "streams:"
      printf '  %s:\n' "$stream"
      printf '  - file://%s\n' "$video"
      if grep -qE "^api:" "$cfg"; then
        grep -A2 -E "^api:" "$cfg" | head -3
      else
        echo 'api:'
        echo '  listen: ":1984"'
      fi
    }
    return 0
  fi

  awk -v stream="$stream" -v video="$video" '
    BEGIN { in_streams = 0; inserted = 0; skip_entry = 0 }
    {
      is_top = ($0 !~ /^[ \t]/) && ($0 != "")
      if (!in_streams) {
        print $0
        if ($0 ~ /^streams:/) in_streams = 1
        next
      }
      # ---- 在 streams 块内 ----
      if (is_top) {
        # streams 块结束：插入新条目，然后打印本行
        printf "  %s:\n", stream
        printf "  - file://%s\n", video
        inserted = 1
        in_streams = 0
        print $0
        next
      }
      if (skip_entry) {
        # 跳过旧条目值行；遇到下一个流键时结束
        if ($0 ~ /^  [A-Za-z0-9._-]+:/) skip_entry = 0
        else next
      }
      if ($0 ~ ("^  " stream ":")) {
        skip_entry = 1
        next
      }
      print $0
    }
    END {
      if (in_streams && !inserted) {
        printf "  %s:\n", stream
        printf "  - file://%s\n", video
      }
    }
  ' "$cfg"
}

# --- 执行编辑 ---
TMP_CFG="${GO2RTC_CFG}.tmp.$$"
if [ "$DRY_RUN" = "1" ]; then
  echo ""
  echo "[dry-run] 编辑后的配置如下（不写文件、不重启）："
  echo "------------------------------------------------"
  edit_yaml "$GO2RTC_CFG" "$STREAM" "$VIDEO"
  echo "------------------------------------------------"
  exit 0
fi

BAK="${GO2RTC_CFG}.bak.$(date +%Y%m%d_%H%M%S)"
cp -f "$GO2RTC_CFG" "$BAK"
edit_yaml "$GO2RTC_CFG" "$STREAM" "$VIDEO" > "$TMP_CFG"
if grep -qE "^  ${STREAM}:" "$TMP_CFG"; then
  mv -f "$TMP_CFG" "$GO2RTC_CFG"
  echo "  [OK] 已更新 $GO2RTC_CFG（备份: $BAK）"
else
  rm -f "$TMP_CFG"
  echo "!! 配置编辑失败（未生成 $STREAM 流），已还原原配置"
  exit 1
fi

# --- 重启 go2rtc ---
echo "  [*] 重启 go2rtc..."
pkill -f go2rtc 2>/dev/null || true
sleep 1
cd "$(dirname "$GO2RTC_BIN")" || exit 1
setsid "$GO2RTC_BIN" -config "$GO2RTC_CFG" > /tmp/go2rtc_demo.log 2>&1 < /dev/null &

OK=0
for i in $(seq 1 20); do
  if ss -tlnp 2>/dev/null | grep -q ":$PORT"; then OK=1; break; fi
  sleep 1
done
if [ "$OK" != "1" ]; then
  echo "!! go2rtc 未在 20 秒内监听 :$PORT，请查看 /tmp/go2rtc_demo.log"
  exit 1
fi

# --- 验证 API ---
STREAMS=$(curl -s "http://127.0.0.1:$PORT/api/streams" 2>/dev/null || echo "")
if echo "$STREAMS" | grep -q "$STREAM"; then
  echo "  [OK] go2rtc API 已识别 $STREAM 流"
else
  echo "  !! 警告：API 未返回 $STREAM 流（返回: ${STREAMS:-空}）"
fi

# --- 打印访问地址（WiFi 优先，其次网线） ---
IP=""
for iface in wlan0 eth0; do
  IP=$(ip -4 addr show "$iface" 2>/dev/null | grep inet | awk '{print $2}' | cut -d/ -f1 | head -1)
  [ -n "$IP" ] && break
done
[ -z "$IP" ] && IP=$(hostname -I 2>/dev/null | awk '{print $1}')

echo ""
echo "=============================================="
echo " 演示地址："
echo "   网页观看 : http://$IP:$PORT   （点击 cam 看实时 / 点击 $STREAM 看预录视频）"
echo "   预录直链 : http://$IP:$PORT/api/stream.mjpeg?src=$STREAM"
echo "=============================================="
echo " 完成。go2rtc 日志: /tmp/go2rtc_demo.log"