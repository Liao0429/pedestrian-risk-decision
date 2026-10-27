#!/usr/bin/env bash
# =============================================================================
# cross_build_lvgl.sh — LVGL v8 交叉编译脚本（Luckfox Pico / RV1106 目标）
#
# 用途：
#   - 在老电脑（Ubuntu，装有 Luckfox ARM 工具链）上交叉编译 LVGL v8 静态库
#     + 一个无显示驱动的冒烟测试程序（lvgl_test），验证工具链/配置可用。
#   - 产物可在开发板上直接运行（armv7l / armhf / glibc 2.35）。
#
# 已验证：
#   - 2026-10-27：gcc-arm-11.2-2022.02 + LVGL v8.3.11，编译 0 错误 0 警告，
#     产物 lvgl_test 在 192.168.0.136（Luckfox Pico Max）真机运行通过。
#
# 用法：
#   ./cross_build_lvgl.sh [工作目录] [LVGL 版本标签]
#     默认工作目录  /home/liao/lvgl_cross
#     默认版本标签  v8.3.11
#
# 环境：
#   - 工具链路径默认 /home/liao/luckfox/gcc-arm-11.2-2022.02-x86_64-arm-none-linux-gnueabihf/bin
#     可用环境变量 ARM_TOOLCHAIN_BIN 覆盖。
#   - 下载走 http_proxy/https_proxy（老电脑 mihomo 7890 已配系统代理）。
#
# 产物：
#   <workdir>/build/lvgl_test      —— ARM 冒烟测试程序（可拷到开发板运行）
#   <workdir>/build/lib/liblvgl.a  —— LVGL 静态库（后续与 lv_drivers/板端驱动链接）
# =============================================================================
set -euo pipefail

WORKDIR="${1:-/home/liao/lvgl_cross}"
LVGL_TAG="${2:-v8.3.11}"
TOOLCHAIN_BIN="${ARM_TOOLCHAIN_BIN:-/home/liao/luckfox/gcc-arm-11.2-2022.02-x86_64-arm-none-linux-gnueabihf/bin}"

# ---------- 0. 环境检查 ----------
if [ ! -x "${TOOLCHAIN_BIN}/arm-none-linux-gnueabihf-gcc" ]; then
    echo "[ERROR] 未找到交叉工具链：${TOOLCHAIN_BIN}/arm-none-linux-gnueabihf-gcc" >&2
    exit 1
fi
for cmd in curl cmake make tar; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "[ERROR] 缺少命令: $cmd" >&2; exit 1; }
done

# ---------- 1. 下载并解压 LVGL ----------
mkdir -p "$WORKDIR"
cd "$WORKDIR"
ARCHIVE="lvgl-${LVGL_TAG}.tar.gz"
if [ ! -f "$ARCHIVE" ]; then
    echo "[1/5] 下载 LVGL ${LVGL_TAG} ..."
    curl -sL --max-time 300 -o "$ARCHIVE" \
        "https://github.com/lvgl/lvgl/archive/refs/tags/${LVGL_TAG}.tar.gz"
fi
if [ ! -d lvgl ]; then
    echo "[2/5] 解压 ..."
    tar xzf "$ARCHIVE"
    mv "lvgl-${LVGL_TAG#v}" lvgl
fi

# ---------- 2. 生成 lv_conf.h（16bit 颜色、1MB 堆、关闭 examples/demos） ----------
if [ ! -f lvgl/lv_conf.h ]; then
    cp lvgl/lv_conf_template.h lvgl/lv_conf.h
    sed -i 's|#define LV_COLOR_DEPTH 16|#define LV_COLOR_DEPTH 16|' lvgl/lv_conf.h
    sed -i 's|#define LV_MEM_SIZE    (32U \* 1024U)|#define LV_MEM_SIZE    (1024U * 1024U)|' lvgl/lv_conf.h
    sed -i 's|#define LV_BUILD_EXAMPLES 1|#define LV_BUILD_EXAMPLES 0|' lvgl/lv_conf.h
fi

# ---------- 3. 生成工具链文件 / CMakeLists / main.c ----------
cat > toolchain-armhf.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TOOLCHAIN_PREFIX arm-none-linux-gnueabihf-)
set(CMAKE_C_COMPILER \${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER \${TOOLCHAIN_PREFIX}g++)
set(CMAKE_FIND_ROOT_PATH $TOOLCHAIN_BIN/../arm-none-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

cat > CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.13)
project(lvgl_cross LANGUAGES C)
add_subdirectory(lvgl)
add_executable(lvgl_test main.c)
target_link_libraries(lvgl_test PRIVATE lvgl)
EOF

cat > main.c <<'EOF'
#include "lvgl.h"
#include <stdio.h>

int main(void)
{
    lv_init();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("LVGL cross OK v%d.%d.%d, tick=%lu\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
           (unsigned long)lv_tick_get());
    printf("heap total=%lu free=%lu used=%lu%%\n",
           (unsigned long)mon.total_size, (unsigned long)mon.free_size,
           (unsigned long)mon.used_pct);
    lv_timer_handler();
    lv_deinit();
    printf("deinit OK\n");
    return 0;
}
EOF

# ---------- 4. 交叉编译 ----------
echo "[3/5] 交叉编译（cmake + make -j4）..."
export PATH="${TOOLCHAIN_BIN}:${PATH}"
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-armhf.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DLV_BUILD_EXAMPLES=OFF -DLV_BUILD_DEMOS=OFF > /tmp/lvgl_cmake.log 2>&1
make -j4 > /tmp/lvgl_make.log 2>&1

# ---------- 5. 结果 ----------
echo "[4/5] 构建产物："
file lvgl_test
ls -la lvgl_test lib/liblvgl.a
echo "[5/5] 部署到开发板（板端为 pico@<开发板IP>，需先改设备树/接好屏幕）："
echo "  scp ${WORKDIR}/build/lvgl_test pico@192.168.0.136:/tmp/"
echo "  开发板上执行：/tmp/lvgl_test"