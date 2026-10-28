# 板端 LVGL 移植准备（ILI9341 触摸屏接入草案）

> 文档定位：真机点亮前的**调查结论 + 实施草案**。ILI9341 2.8 寸触摸屏尚未采购，本
> 文档基于 2026-10-27/28 开发板（Luckfox Pico Max）实测，给出设备树修改、boot.img 处理、
> LVGL 交叉编译三条线的完整准备。**2026-10-28 新增：Luckfox 动态 overlay 方案
> （方案 C，推荐）——无需重编内核/boot.img，一条命令启用 `/dev/spidev0.0`。**
> 关联文档：[`lvgl_menu.md`](./lvgl_menu.md)（菜单状态机 + LVGL 移植调研）、
> [`selftest.md`](./selftest.md)（硬件自检，含 SPI 屏幕 ID 检测项）、
> [`DISPLAY_MENU_SYSTEM_PLAN.md`](./DISPLAY_MENU_SYSTEM_PLAN.md)。

---

## 1. 板端现状（2026-10-27 实测）

| 项目 | 实测结果 |
|---|---|
| 板型 | Luckfox Pico Max（compatible `rockchip,rv1103g-38x38-ipc-v10 rockchip,rv1106g3`） |
| 内核 | Linux 5.10.160 armv7l（TF 卡启动：`storagemedia=sd`，root=/dev/mmcblk1p7） |
| 显示 | **无 /dev/fb\***（`/sys/class/graphics` 仅有 `fbcon`），无 DRM 显示节点 |
| SPI 控制器 | `spi@ff500000`(SPI0) **disabled**；`spi@ff510000`(SPI1) disabled；`spi@ffac0000`(SFC 启动 flash) okay |
| SPI0 子节点 | **已同时存在** `spidev@0`（compatible `rockchip,spidev`，50MHz）与 `fbtft@0`（compatible `sitronix,st7789v`），父节点 disabled 导致均未生效 |
| fbtft 驱动 | 内核**已内置** `fb_st7735r`、`fb_st7789v` 平台驱动；**无 `fb_ili9341`** |
| spidev 驱动 | 内核**已内置**（`/proc/kallsyms` 存在 `spidev_open/read/write` 等符号） |
| 工具链匹配 | 板端 glibc 2.35（armhf 硬浮点），与老电脑 `gcc-arm-11.2-2022.02`（glibc 2.35）匹配 |
| boot 分区 | `/dev/mmcblk1p4`（32M），内容为 **FIT 镜像**（kernel + flat_dt + resource），带 `sha256,rsa2048` 签名（key-name-hint `dev`） |

关键结论：**内核侧无需新增驱动**——spidev 已内置，SPI0 的 spidev 节点也已存在。
唯一要改的是把 `spi@ff500000` 的 `status` 从 `"disabled"` 改为 `"okay"`。

---

## 2. 点亮路径对比

| 方案 | 内核改动 | 收益 | 成本/风险 | 结论 |
|---|---|---|---|---|
| **A：fbtft 内核驱动** | 需要 `CONFIG_FB_TFT_ILI9341=y`（当前内核无此驱动，需重编内核） | 得到 `/dev/fb0`，LVGL 走 `lv_drivers` fbdev | 拉完整内核源码 + 重编 + 重打包 boot.img，工程量最大 | 备选 |
| **B：spidev 用户态驱动（改 boot.img）** | 改 DTS（spi0 status=okay）后重编 boot.img 烧写 | `/dev/spidev0.0` 出现，ILI9341 初始化/绘帧由用户态驱动完成 | 需处理 FIT 签名；改动固化在 boot 分区，回退要重烧 | 备选 |
| **C：Luckfox 动态 overlay（2026-10-28 实测推荐）** | **不改内核、不重编 boot.img** | `/dev/spidev0.0` 立即出现（configfs 动态加载），重启自动生效 | 需 root 权限执行一次脚本；可逆（删 dts 重启恢复出厂） | **推荐** |

方案 B/C 的共同理由：
1. 内核已有 `spidev` 驱动与 `spidev@0` 节点，改动面最小；
2. 帧率可调（SPI0 50MHz，2.8 寸 320×240 RGB565 一帧 150KB，理论 ~13ms/帧，足够 LVGL 菜单）；
3. 出问题只影响用户态程序，不折腾内核与签名 boot.img 的兼容性。

---

## 3. 设备树修改草案（方案 B）

当前 `spi@ff500000` 节点原文（从板端 boot FIT 中提取的 DTS 实测）：

```dts
spi@ff500000 {
    compatible = "rockchip,rv1106-spi", "rockchip,rk3066-spi";
    reg = <0xff500000 0x1000>;
    interrupts = <0x00 0x17 0x04>;
    #address-cells = <0x01>;
    #size-cells = <0x00>;
    clocks = <0x02 0xcd 0x02 0xcc 0x02 0xce>;
    clock-names = "spiclk", "apb_pclk", "sclk_in";
    dmas = <0x3b 0x01 0x3b 0x00>;
    dma-names = "tx", "rx";
    pinctrl-names = "default";
    pinctrl-0 = <0x43 0x44 0x45 0x46>;   /* SPI0 四线 mux 已配好 */
    status = "disabled";                  /* ★ 仅此一处修改 */

    spidev@0 {
        compatible = "rockchip,spidev";
        spi-max-frequency = <0x2faf080>;  /* 50 MHz */
        reg = <0x00>;
    };

    fbtft@0 {                              /* 官方 1.3 寸 ST7789 屏节点，保留不动 */
        compatible = "sitronix,st7789v";
        reg = <0x00>;
        spi-max-frequency = <0x2faf080>;
        fps = <0x1e>;
        buswidth = <0x08>;
        debug = <0x07>;
        led-gpios    = <0x47 0x08 0x00>;   /* GPIO2_C0 背光 */
        dc-gpios     = <0x47 0x09 0x00>;  /* GPIO2_C1 DC */
        reset-gpios    = <0x35 0x13 0x01>;  /* GPIO1_C3 复位，低有效 */
    };
};
```

**最小修改**：

```dts
spi@ff500000 {
    status = "okay";    /* 由 disabled 改为 okay */
};
```

修改后重启应出现 `/dev/spidev0.0`，可用 `spidev_test` 回读 ILI9341 的 ID 寄存器（0xD3）验证 0x93 0x41。

---

## 3.5 方案 C：Luckfox 动态 overlay（2026-10-28 实测，推荐）

### 背景

Luckfox 出厂固件内置设备树 overlay 机制（`/etc/init.d/S99luckfoxcustomoverlay`）：
开机时自动把 `/mnt/cfg/dtbo/*.dts` 用 `dtc` 编译成 dtbo，写入
`/sys/kernel/config/device-tree/overlays/<name>/dtbo` 并置 status=1 动态加载。
**因此无需重编内核、无需重打包 boot.img**——只需提供一段 overlay 设备树。

### 实测结论（2026-10-28，开发板 armv7l）

- 内核 `spidev` 驱动**已内置**（`/sys/bus/spi/drivers/spidev` 存在）；
- SPI0 节点 `spi@ff500000` 的引脚复用 `pinctrl-0 = <0x43 0x44 0x45 0x46>`
  （= `spi0m0-clk/miso/mosi/cs0`，phandle 大端存储，可从 `/proc/device-tree` 动态读取）；
- ⚠️ **fbtft@0 必须禁用**：SPI0 下预置的官方 ST7789V 节点 `fbtft@0` 与
  `spidev@0` **同片选（reg 均为 0）且无 status=disabled**。只把控制器
  status 改 okay 会导致两者争用，`spidev@0` 可能注册失败。overlay 中须把
  `fbtft@0` 置为 disabled。
- 普通用户（`pico`）**无法**写 `/mnt/cfg` 与 configfs——执行需 root 权限。
  运行自检程序若需无 root 访问 GPIO/PWM，另见下方 udev 建议。

### 真机执行记录（2026-10-28 已完成 ✅）

- **`/dev/spidev0.0` 已生成并开机自动加载**：configfs overlay `spi0` status=1，
  `/sys/bus/spi/devices/spi0.0` 存在；`spidev` ioctl 实测 **mode=0 / 8bit / 1MHz**
  配置成功；重启后自动恢复（见下"开机自启"）。
- **权限**：`/etc/udev/rules.d/99-luckfox-hardware.rules` 已写入（spidev 0666 /
  gpio / pwm），`/dev/spidev0.0` 权限实测 `crw-rw-rw-`，`pico` 用户可直接访问。
- **selftest 联动**：`hardware_selftest` 的 SPI 项从「未找到设备节点」变为
  「LCD ID 不匹配 0x9341」——即 SPI 传输已实际执行（未接屏幕读到非 0x9341
  属预期 FAIL），待 ILI9341 到货接线后该项即可 PASS。

### 一键脚本

```bash
sudo ./scripts/enable_spi0_overlay.sh
```

脚本行为：① 检查 root；② 动态读取当前固件的 SPI0 引脚 phandle（兼容不同固件版本）；
③ 生成 `/mnt/cfg/dtbo/spi0.dts`；④ **自包含 configfs 加载 overlay**（编译 dtbo →
写入 `/sys/kernel/config/device-tree/overlays/spi0` → status=1，无需重启，规避官方
脚本 rm bug）；⑤ 写入 udev 规则并放行 `/dev/spidev0.0` 用户态访问；⑥ 验证
`/dev/spidev0.0` 是否生成并给出排障命令。板上已实测：`rc=0`，`/dev/spidev0.0`
权限 `crw-rw-rw-`。

**可逆**：`sudo rm /mnt/cfg/dtbo/spi0.dts` 后重启即恢复出厂设备树（若已安装自启服务，先 `sudo systemctl disable spi0-overlay.service`）。

### 开机自启（已实测）

Luckfox 官方 `S99luckfoxcustomoverlay.service` **默认未启用**（其 init.d 脚本缺 LSB
头，`systemctl enable` 会报 `Default-Start contains no runlevels`；且官方脚本本身有
`rm ${LLF_CUSTOM_DTS_PATH}/...` 变量名 bug，运行退出码为 1）。因此本项目改用**原生
systemd oneshot 服务**调用同一脚本，实测重启后 `/dev/spidev0.0` 自动出现：

```bash
# 板端已安装（2026-10-28），服务名：spi0-overlay.service
cat > /etc/systemd/system/spi0-overlay.service <<'EOF'
[Unit]
Description=Enable SPI0 overlay for ILI9341/XPT2046 (Luckfox configfs)
After=local-fs.target systemd-modules-load.service
Before=multi-user.target

[Service]
Type=oneshot
ExecStart=/bin/bash /usr/local/sbin/enable_spi0_overlay.sh
RemainAfterExit=yes
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
sudo cp /usr/local/sbin/enable_spi0_overlay.sh /usr/local/sbin/  # 脚本本体
sudo systemctl daemon-reload
sudo ln -sf /etc/systemd/system/spi0-overlay.service /etc/systemd/system/multi-user.target.wants/spi0-overlay.service
```

### overlay 内容（脚本自动生成，等价如下）

```dts
/dts-v1/;
/plugin/;

&{/spi@ff500000}{
    status = "okay";
    pinctrl-0 = <0x43 0x44 0x45 0x46>;   /* 动态读取，此处为 2026-03-14 固件实测值 */

    fbtft@0 {
        status = "disabled";             /* 与 spidev@0 同片选，必须禁用 */
    };
};
```

`dtc -I dts -O dtb` 编译验证通过（仅 `unit_address_vs_reg` 一个无害警告，因 overlay
只改状态不重复 reg）；`fdtoverlay` 反编译确认 target-path=`/spi@ff500000` 正确。

### 权限与 udev 建议（可选）

`pico` 用户不在 root 组，`/sys/class/gpio/export`、`/sys/class/pwm/.../export`、
`/dev/spidev0.0` 默认 root 所有。若希望 `hardware_selftest` 以 `pico` 用户运行，
可加 udev 规则（root 执行一次）：

```bash
cat > /etc/udev/rules.d/99-luckfox-hardware.rules <<'EOF'
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
SUBSYSTEM=="pwm",  MODE="0660", GROUP="gpio"
KERNEL=="spidev*", MODE="0666"
SUBSYSTEM=="sound", KERNEL=="controlC*", MODE="0666"
EOF
udevadm control --reload-rules && udevadm trigger
```

> 注：sysfs gpio/pwm 的权限实际由内核 `gpio_sysfs`/`pwm_sysfs` 决定，规则对
> `/sys/class/gpio/export` 的归属控制有限；若仍受限，直接以 root 运行自检或
> 将自检程序接入 systemd 服务（root 执行）即可。

### GPIO 引脚（ILI9341 接线参考）

现有 fbtft 节点给出了板载 LCD FPC 的 GPIO 定义，ILI9341 通用：

| 信号 | GPIO | 说明 |
|---|---|---|
| DC | GPIO2_C1 | 数据/命令选择 |
| RESET | GPIO1_C3 | 复位，**低有效**（`GPIO_ACTIVE_LOW`） |
| 背光 BL | GPIO2_C0 | 高电平点亮 |
| SCLK/MOSI/MISO/CS | SPI0 四线 | 复用引脚已由 pinctrl-0 配置 |

> ⚠️ 若 2.8 寸屏通过排针外接而非板载 FPC，请以《答辩硬件展示方案 v2》的 PCB 原理图
> 为准核对实际引脚，避免与扩展板冲突。

---

## 4. boot.img 重打包与签名风险

### 4.1 现状

`/dev/mmcblk1p4`（32M boot 分区）是 **FIT 镜像**：

```
description = "FIT image with Linux kernel, FDT blob and resource"
images:
  fdt       @ 0x800    size 0x8e20   (36384 B)
  kernel    @ 0x9800   size 0x2e5450
  resource  @ 0x2eee00 size 0x12400
configurations.conf:
  signature algo = "sha256,rsa2048", padding = "pss", key-name-hint = "dev"
  sign-images = "fdt", "kernel", "multi"
```

### 4.2 推荐实施流程（正规路径）

1. 在老电脑拉取 Luckfox 完整 SDK（含内核源码与 u-boot 打包工具链）；
2. 修改内核 dts（`spi@ff500000` status → `"okay"`）；
3. `./build.sh kernel` 重新生成 boot.img（SDK 会用 dev 密钥重新签名，保证启动校验通过）；
4. 用官方 `upgrade_tool`/`dd` 写入 TF 卡 boot 分区。

### 4.3 手动解包-修改-重打包（应急路径，有变砖风险）

```bash
# 板端（需 root）备份与提取
dd if=/dev/mmcblk1p4 of=/userdata/boot_backup.img bs=1M count=32
dd if=/tmp/boot.fit of=/tmp/board.dtb bs=1 skip=$((0x800)) count=$((0x8e20))
dtc -I dtb -O dts /tmp/board.dtb -o /tmp/board.dts
# 修改 /tmp/board.dts 中 spi@ff500000 的 status
dtc -I dts -O dtb /tmp/board.dts -o /tmp/board_new.dtb
# 用 u-boot mkimage 按原 .its 重新打包（需 u-boot-tools），再写回分区
```

> ⚠️ 应急路径**必须**先备份原 boot 分区与整卡镜像；若 u-boot 强制校验签名，直接写回
> 修改后的 FIT 可能无法启动。**在硬件到手、有完整备份前，不要执行本节任何写操作。**

---

## 5. LVGL v8 交叉编译（已实测通过 ✅）

### 5.1 脚本

仓库提供 `scripts/cross_build_lvgl.sh`：

```bash
# 老电脑（Ubuntu，已装 Luckfox 工具链）执行
./scripts/cross_build_lvgl.sh /home/liao/lvgl_cross v8.3.11
```

脚本自动完成：下载 LVGL v8.3.11 → 生成 `lv_conf.h`（16bit 颜色、1MB 堆、关闭
examples/demos）→ 生成 `toolchain-armhf.cmake` → 交叉编译 → 输出冒烟测试程序。

### 5.2 实测结果（2026-10-27）

- 工具链：`gcc-arm-11.2-2022.02-x86_64-arm-none-linux-gnueabihf`（11.2.1）
- 编译：0 错误 0 警告，约 15s（`-j4`，关闭 examples/demos）
- 产物：`build/lvgl_test`（ELF 32-bit ARM，interpreter `/lib/ld-linux-armhf.so.3`）、
  `build/lib/liblvgl.a`（LVGL 静态库）
- **真机验证**：`lvgl_test` 拷贝到开发板 `/tmp` 后直接运行成功：

```
LVGL cross OK v8.3.11, tick=0
heap total=49152 free=47676 used=4%
deinit OK
```

（`lv_init` / 内存监控 / `lv_timer_handler` / `lv_deinit` 均正常，证明交叉编译产物与
板端运行环境完全匹配。）

### 5.3 后续链接方式

`LvglMenuBackend` 接入时，板端程序链接：`liblvgl.a + ili9341.h 驱动（SysfsSpiBus/SysfsLcdPins 板端适配） +
lv_drivers（触摸）`，编译命令以 SDK 的 CMake 工程为准，可复用 `toolchain-armhf.cmake`。

### 5.4 ILI9341 显示驱动（主机侧已实现 ✅）

`src/hardware/ili9341.h` 提供与硬件无关的显示后端（`hw::Ili9341`）：

- **抽象接口**：`hw::SpiBus`（板端实现 `/dev/spidev0.0` 读写）、`hw::LcdPins`（DC/RESET/背光 GPIO）；
- **功能**：复位脉冲 + 标准初始化命令序列（SWRESET→SLPOUT→COLMOD 0x55→MADCTL 0x08→DISPON）、
  窗口设置（CASET/PASET 大端参数）、矩形填充（边界裁剪、分批写入兼容 spidev 长度限制）、
  整帧 flush（320×240 RGB565，150KB）、帧缓冲读写、读版本 ID（0xD3 → `00 93 41 00`，供自检）；
- **测试**：`tests/test_ili9341.cc` 123 项（FakeSpi/FakeLcdPins 内存替身，校验命令序列/窗口
  参数/裁剪/颜色字节序/flush 首尾像素/读 ID/NULL 容错），`make test-ili9341` 一键运行。

真机阶段仅需实现 `SysfsSpiBus`（spidev ioctl）与 `SysfsLcdPins`（sysfs GPIO），即可把驱动
接到 LVGL 的 `flush_cb` 或硬件自检的屏幕检测项。

---

## 6. 后续路线图（待硬件采购）

| 步骤 | 内容 | 依赖 |
|---|---|---|
| 1 | 采购 ILI9341 2.8 寸屏（含 XPT2046 触摸）+ 排线 | 硬件 |
| 2 | **root 执行一次 `scripts/enable_spi0_overlay.sh`**（Luckfox 动态 overlay，无需 SDK/重编 boot.img；需人工提供 root 凭据） | root 权限 |
| 3 | 板端验证 `/dev/spidev0.0`，spidev_test 回读 ID `0x9341` | 步骤 2 |
| 4 | 板端 ILI9341 显示驱动：**驱动主机侧已实现**（`src/hardware/ili9341.h`：复位/初始化命令序列/窗口/矩形填充/帧缓冲/flush/读 ID，`SpiBus`+`LcdPins` 抽象，123 项主机测试）；**板端适配层已实现**（`src/hardware/sysfs_spi.{h,cc}`：`SysfsSpiBus` 打开 /dev/spidev0.0 并配置 mode/bits/speed、全双工 transfer；`SysfsLcdPins` 通过 sysfs GPIO 控制 DC/RESET/背光；40 项 Linux 单元测试，已在开发板 armv7l 真机编译运行通过）。剩余为真机接线验证与屏幕 ID 回读 | 步骤 3 |
| 5 | 实现 `LvglMenuBackend` 真机适配（对接 `lcd_menu.h`，见 `lvgl_menu.md`） | 步骤 4 |
| 6 | XPT2046 触摸适配（中断 + SPI 读坐标）：**驱动主机侧已实现**（`src/hardware/xpt2046.h`：0xD0/0x90 命令协议、12 位数据提取（dataShift 可调）、PENIRQ+压力阈值触摸判定、校准坐标映射（镜像/轴交换）、中值抗噪，`TouchPin` 抽象，98 项主机测试）；**板端适配层已实现**（`SysfsTouchPin` 通过 sysfs GPIO 读 PENIRQ 低有效电平，随 `sysfs_spi.{h,cc}` 一并验证）。剩余为真机接线（独立片选 spidev）与电阻屏校准 | 步骤 4 |
| 7 | 中文字体子集化（`lv_font_conv`，见 `lvgl_menu.md`） | 步骤 5 |
| 8 | 更新 `hardware_selftest` 屏幕检测项为真实 spidev 读 ID | 步骤 3 |
| 9 | 演示模式联调（菜单 + 风险等级页 + 参数页） | 步骤 5-7 |

---

## 附录 A：板端调查命令备忘

```bash
# SPI 控制器状态
ls /sys/class/spi_master/
for d in spi@ff500000 spi@ff510000 spi@ffac0000; do
  echo "$d: $(cat /proc/device-tree/$d/status 2>/dev/null | tr -d '\0')"
done
# fbtft/spidev 驱动是否内置
ls /sys/bus/platform/drivers/ | grep -E 'fb_|fbtft'
ls /sys/bus/spi/drivers/ | grep spidev
# 读取 SPI0 引脚复用 phandle（大端）
od -A n -t x1 /proc/device-tree/pinctrl/spi0/spi0m0-clk/phandle
# 启用 SPI0（Luckfox 动态 overlay，需 root）
sudo ./scripts/enable_spi0_overlay.sh
ls -l /dev/spidev0.0
# 提取实际运行的 DTB
cp /sys/firmware/fdt /tmp/current.dtb && dtc -I dtb -O dts /tmp/current.dtb -o /tmp/current.dts
# boot 分区 FIT 提取（root）
dd if=/dev/mmcblk1p4 of=/tmp/boot.fit bs=1M count=32
dtc -I dtb -O dts /tmp/boot.fit | head -120
```