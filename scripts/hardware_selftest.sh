#!/bin/bash
# ============================================================
# hardware_selftest.sh — 开发板硬件自检入口（开机自检 + 演示门禁）
# ------------------------------------------------------------
# 用途：
#   1. systemd 开机自检调用（hardware-selftest.service）；
#   2. 手动运行：sudo bash hardware_selftest.sh
# 行为：
#   - 调用已编译的 hardware_selftest 二进制逐项检测：
#     GPIO 写读回 / PWM 节点 / SPI 屏幕 ID 0x9341 / USB 声卡 /
#     WiFi 连接 / 按键电平，每项输出 PASS/FAIL；
#   - 完整报告写入 /tmp/hardware_selftest.log；
#   - 全部 PASS 时创建 /tmp/hardware_selftest.pass，
#     start_demo.sh 只有看到该标记才进入演示模式（可 --force 跳过）；
#   - 退出码：0=全部通过，1=存在 FAIL。
#
# 参数：
#   $1 自检二进制路径（默认 /home/pico/daima/luckfox_pico_rtsp_yolov5_demo/hardware_selftest）
#   其余参数透传给二进制（如 -g/-b/-c/-p）
# ============================================================

DAIMA=/home/pico/daima/luckfox_pico_rtsp_yolov5_demo
BIN=$DAIMA/hardware_selftest
# 若第一个参数是可执行文件，则作为二进制路径
if [ $# -gt 0 ] && [ -x "$1" ]; then
  BIN=$1
  shift
fi
LOG=/tmp/hardware_selftest.log
PASS_MARK=/tmp/hardware_selftest.pass

echo "==============================================" | tee "$LOG"
echo " 硬件自检 hardware selftest $(date '+%F %T')" | tee -a "$LOG"
echo "==============================================" | tee -a "$LOG"

if [ -x "$BIN" ]; then
  "$BIN" "$@" | tee -a "$LOG"
  RC=${PIPESTATUS[0]}
else
  echo "[ERROR] 未找到自检二进制: $BIN" | tee -a "$LOG"
  echo "  请先交叉编译并部署：make hardware-selftest 产物拷到 $DAIMA/" | tee -a "$LOG"
  RC=1
fi

echo "----------------------------------------------" | tee -a "$LOG"
if [ "$RC" -eq 0 ]; then
  echo "自检结果: 全部 PASS，允许进入演示模式" | tee -a "$LOG"
  touch "$PASS_MARK"
else
  echo "自检结果: 存在 FAIL，已阻止进入演示模式（详见上方报告）" | tee -a "$LOG"
  rm -f "$PASS_MARK"
fi
echo "日志: $LOG" | tee -a "$LOG"

exit "$RC"