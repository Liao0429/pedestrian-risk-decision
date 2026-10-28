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

echo "==> [1/6] 检查 root 权限"
if [ "$(id -u)" -ne 0 ]; then
    echo "错误：需要 root 权限。请使用 sudo 或以 root 用户运行。" >&2
    exit 1
fi

echo "==> [2/6] 检查依赖（dtc / python3）"
command -v dtc >/dev/null || { echo "缺少 dtc，无法编译 overlay" >&2; exit 1; }
command -v python3 >/dev/null || { echo "缺少 python3，无法生成 dts" >&2; exit 1; }

echo "==> [3/6] 生成 overlay 设备树（动态读取 SPI0 引脚复用 phandle）"
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

echo "==> [4/5] 通过 configfs 加载 overlay（立即生效，无需重启）"
# 自包含加载：不直接调用 /etc/init.d/S99luckfoxcustomoverlay start，
# 该脚本存在官方 bug（rm 误用未定义变量 LLF_CUSTOM_DTS_PATH，报错但不影响加载），
# 这里复刻其核心逻辑并规避 bug，保证脚本退出码干净。
OVERLAY_SYS="/sys/kernel/config/device-tree/overlays/${DTBO_NAME}"
mkdir -p "${DTBO_DIR}"
dtc -I dts -O dtb "${DTS_PATH}" -o "${DTBO_DIR}/${DTBO_NAME}.dtbo"
if [ ! -d "${OVERLAY_SYS}" ]; then
    mkdir -p "${OVERLAY_SYS}"
fi
if [ ! -s "${OVERLAY_SYS}/dtbo" ]; then
    cat "${DTBO_DIR}/${DTBO_NAME}.dtbo" > "${OVERLAY_SYS}/dtbo"
    echo 1 > "${OVERLAY_SYS}/status"
    echo "overlay 已加载（configfs ${OVERLAY_SYS} status=1）"
else
    echo "overlay 已存在（${OVERLAY_SYS}），跳过写入（当前 status=$(cat ${OVERLAY_SYS}/status)）"
fi
rm -f "${DTBO_DIR}/${DTBO_NAME}.dtbo"

echo "==> [5/6] 放行用户态访问（udev 规则 + 立即 chmod）"
UDEV_RULE="/etc/udev/rules.d/99-luckfox-hardware.rules"
if [ ! -f "${UDEV_RULE}" ] || ! grep -q 'spidev' "${UDEV_RULE}" 2>/dev/null; then
    cat >> "${UDEV_RULE}" <<'EOF'
# spidev: user-space SPI access for hardware_selftest / user drivers
KERNEL=="spidev*", MODE="0666"
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
SUBSYSTEM=="pwm",  MODE="0660", GROUP="gpio"
EOF
    echo "已写入 ${UDEV_RULE}"
fi
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true
chmod 0666 /dev/spidev0.0 2>/dev/null || true

echo "==> [6/6] 验证 /dev/spidev0.0"
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