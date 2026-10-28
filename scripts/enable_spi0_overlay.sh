#!/bin/bash
# =============================================================================
# enable_spi0_overlay.sh — 在 Luckfox Pico（RV1106）上启用 SPI0 控制器
# -----------------------------------------------------------------------------
# 目标：让 /dev/spidev0.0 出现在用户空间（ILI9341 显示 + XPT2046 触摸共用）。
#
# 原理（2026-10-28 开发板实测）：
#   - 出厂固件 spi@ff500000(SPI0) status=disabled，但 spidev@0 子节点已预置
#     （compatible=rockchip,spidev，50MHz），内核 spidev 驱动已内置；
#   - Luckfox 固件内置 overlay 机制（/etc/init.d/S99luckfoxcustomoverlay +
#     /mnt/cfg/dtbo/*.dts + configfs），启动时自动编译并加载 overlay，
#     无需重编内核 / 重打包 boot.img，可逆（删除 dts 重启即恢复出厂）；
#   - 注意：SPI0 下还有官方 fbtft@0（ST7789V）节点，与 spidev@0 同片选，
#     必须同时禁用，否则 spidev@0 注册失败。
#
# 需要：root 权限（sudo 或以 root 用户运行）。
# 用法：sudo ./scripts/enable_spi0_overlay.sh
# 验证：ls -l /dev/spidev0.0
#       然后编译运行 hardware_selftest 观察 SPI_LCD_ID 项
# 可逆：rm /mnt/cfg/dtbo/spi0.dts && 重启，即恢复出厂设备树。
# =============================================================================
set -euo pipefail

DTBO_DIR="/mnt/cfg/dtbo"
DTBO_NAME="spi0"
DTS_PATH="${DTBO_DIR}/${DTBO_NAME}.dts"

echo "==> [1/5] 检查 root 权限"
if [ "$(id -u)" -ne 0 ]; then
    echo "错误：需要 root 权限。请使用 sudo 或以 root 用户运行。" >&2
    exit 1
fi

echo "==> [2/5] 检查依赖（dtc / python3）"
command -v dtc >/dev/null || { echo "缺少 dtc，无法编译 overlay" >&2; exit 1; }
command -v python3 >/dev/null || { echo "缺少 python3，无法生成 dts" >&2; exit 1; }

echo "==> [3/5] 生成 overlay 设备树（动态读取 SPI0 引脚复用 phandle）"
mkdir -p "${DTBO_DIR}"
python3 - "${DTS_PATH}" <<'PY'
import sys
BASE = "/proc/device-tree"

def read_attr(node, attr):
    with open(f"{BASE}/{node}/{attr}", "rb") as f:
        return f.read()

def phandle(node):
    return int.from_bytes(read_attr(node, "phandle"), "big")

clk  = phandle("pinctrl/spi0/spi0m0-clk")
miso = phandle("pinctrl/spi0/spi0m0-miso")
mosi = phandle("pinctrl/spi0/spi0m0-mosi")
cs0  = phandle("pinctrl/spi0/spi0m0-cs0")

dts = f"""/dts-v1/;
/plugin/;

&{{/spi@ff500000}} {{
    status = "okay";
    pinctrl-0 = <0x{clk:x} 0x{miso:x} 0x{mosi:x} 0x{cs0:x}>;

    fbtft@0 {{
        status = "disabled";
    }};
}};
"""
out = sys.argv[1]
with open(out, "w") as f:
    f.write(dts)
print(f"已生成 {out}（phandle clk={clk} miso={miso} mosi={mosi} cs0={cs0}）")
PY

echo "==> [4/5] 通过 Luckfox overlay 机制加载（立即生效，无需重启）"
/etc/init.d/S99luckfoxcustomoverlay start

echo "==> [5/5] 验证 /dev/spidev0.0"
if [ -e /dev/spidev0.0 ]; then
    echo "OK：/dev/spidev0.0 已生成"
    ls -l /dev/spidev0.0
else
    echo "警告：/dev/spidev0.0 未出现。检查以下线索："
    echo "  - /sys/bus/spi/devices/ 下是否有 spi0.0："
    ls /sys/bus/spi/devices/ 2>/dev/null || true
    echo "  - 内核日志（spi / spidev 相关）："
    dmesg | grep -i -E "spi|spidev|ff500000" | tail -20 || true
    exit 1
fi