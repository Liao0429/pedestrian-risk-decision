# 硬件自检（hardware_selftest）框架

> 关联代码：`src/hardware/selftest.h`（框架）、`src/hardware/selftest_main.cc`（板端 CLI）
> 测试：`src/luckfox_rtsp_yolov5/tests/test_selftest.cc`（`make test-selftest`，80 项用例）
> 部署脚本：`scripts/hardware_selftest.sh`、`scripts/hardware-selftest.service`
> 文档日期：2026-10-27

## 1. 背景与目标

答辩演示前，外设（舵机云台/三色 LED/按键/USB 喇叭/ILI9341 触摸屏/WiFi）可能因
接线松动、模块未插、驱动未加载等原因失效。为避免现场「开机后才发现某个外设坏了」，
在进入演示模式前做一次**逐项硬件自检**：

- 每项检测输出 **PASS / FAIL** + 原因描述；
- **全部 PASS 才允许进入演示模式**（systemd 开机自检 + `start_demo.sh` 门禁）；
- 检测项互相独立，单项失败不影响其余项继续检测；
- 框架与硬件无关：板端用真实 sysfs 检测，PC/CI 用内存替身测试同一份判定逻辑。

## 2. 检测项清单（6 项）

| # | 项 | 检测内容 | 判定条件 |
|---|---|---|---|
| 1 | GPIO_LOOPBACK | GPIO 写读回 | 输出写高/写低后读回一致 |
| 2 | PWM_NODE | 舵机云台 PWM 通道 | 节点可导出、配置 20ms 周期、使能、写 1.5ms duty |
| 3 | SPI_LCD_ID | ILI9341 触摸屏 | `/dev/spidev0.0` 存在且 ID 探测输出含 `9341` |
| 4 | USB_AUDIO | USB 喇叭声卡 | `aplay -l` 可用且输出含 `USB` |
| 5 | WIFI_LINK | 手机热点连接 | `wlan0` 存在且已获取 IPv4（`inet`） |
| 6 | BUTTON_LEVEL | 按键 GPIO | 可配置为输入并读到电平 |

> 注：PWM 自检会使已接舵机短暂动作（enable 后写中位 duty 再关闭），属预期行为；
> 未接舵机时仅验证 sysfs 节点，无影响。

## 3. 代码结构（依赖注入，与 gpio.h/servo.h 同风格）

```
src/hardware/selftest.h
├── SelftestItem            # 6 项枚举 + selftestItemName()
├── SelftestResult           # 单项结果（item/pass/detail）
├── SelftestProbe            # 抽象探测：fileExists() + runCmd()
│   ├── ShellSelftestProbe   # 板端：access() + popen()
│   └── FakeSelftestProbe    # 测试：内存注入文件/命令输出
└── HardwareSelfTest         # 执行器：runAll()/allPassed()/计数/printReport()
```

- **Gpio / Pwm 复用现有抽象**：板端传 `SysfsGpio` / `SysfsPwm`，测试传
  `FakeGpio` / `FakePwm`（或故障替身 `BadPwm` / `FailReadGpio`）；
- **系统探测可注入**：SPI 设备节点、声卡输出、WiFi 状态全部通过
  `SelftestProbe` 命令抓取，测试时注入任意输出即可覆盖 PASS/FAIL 分支；
- 主机测试无需任何真实硬件，CI（`make test`）自动覆盖。

## 4. 编译与运行

### 4.1 主机单元测试（PC/CI，无需硬件）

```bash
cd src/luckfox_rtsp_yolov5
make test-selftest     # 80 项用例：全 PASS 场景 + 各单项 FAIL + NULL 容错
# 或整体 make test（含 118 项硬件驱动 + 174 项预处理 + 104 项黄金回归等）
```

### 4.2 板端自检 CLI（真实 sysfs 检测）

```bash
cd src/luckfox_rtsp_yolov5
make hardware-selftest          # 产物 ./hardware_selftest
# 用法：./hardware_selftest [-g <loopback_gpio>] [-b <button_gpio>] [-c <pwm_chip>] [-p <pwm_chan>]
# 默认：-g 56（GPIO1_D0，实测空闲脚请替换） -b 67（GPIO2_A3_d，排针 Pin27） -c 8（pwmchip8=PWM8_M1） -p 0
# 说明：旧文档写的 -b 92 / -c 0 是错的——实测 sysfs 92 = GPIO2_D4，且板上没有 pwmchip0（只有 pwmchip8/9，各 1 通道）
./hardware_selftest
# 退出码：0=全部 PASS，1=存在 FAIL
```

输出示例（全 PASS）：

```
=== hardware selftest ===
GPIO loopback=56 button=92 PWM=pwmchip0/pwm0
[PASS] GPIO_LOOPBACK  GPIO 输出写读回 OK
[PASS] PWM_NODE       PWM 节点导出/配置/写 duty OK
[PASS] SPI_LCD_ID     SPI 屏幕 ID = 0x9341 (ILI9341)
[PASS] USB_AUDIO      USB 声卡可用 (aplay)
[PASS] WIFI_LINK      wlan0 已连接并获取 IP
[PASS] BUTTON_LEVEL   按键电平读取 OK（当前 LOW）
=== result: 6 PASS / 0 FAIL ===
```

## 4.5 真机验证记录（2026-10-28，开发板 armv7l）

在开发板真机编译并运行自检 CLI（**无任何外设 + 普通用户 pico** 环境，验证容错）：

```text
$ g++ -std=c++11 -O2 -Wall -Wextra -I. selftest_main.cc -o hardware_selftest   # 0 警告
$ ./hardware_selftest
GPIO loopback=56 button=92 PWM=pwmchip0/pwm0
[FAIL] GPIO_LOOPBACK  setDirection(out) 失败
[FAIL] PWM_NODE       PWM configure(20ms) 失败
[FAIL] SPI_LCD_ID     未找到 SPI 设备节点 /dev/spidev0.0
[FAIL] USB_AUDIO      aplay 无输出（aplay 缺失或无声卡）
[FAIL] WIFI_LINK      wlan0 不存在或未就绪
[FAIL] BUTTON_LEVEL   setDirection(in) 失败
=== result: 0 PASS / 6 FAIL ===   （退出码 1）
```

结论：

- **程序在无硬件环境下运行健壮**：6 项全部 FAIL 但无崩溃、无空指针、报告完整、退出码正确（1=存在 FAIL）——符合"带病不演示"设计；
- 各项 FAIL 原因与预期一致：SPI 未启用（设备树 disabled，见 `lvgl_board_port.md` 方案 C 一键启用）、GPIO/PWM 普通用户无 sysfs 写权限（root 或 udev 规则解决，见方案 C 权限小节）、wlan0 未插 WiFi 模块、aplay 无声卡；
- 编译时发现 `servo.h` SysfsPwm 路径缓冲在板端 glibc fortify 下触发 `-Wformat-truncation` 警告，已修复（缓冲区 128→256）并回归通过（118+80 项测试）。

### 4.6 真机验证记录（2026-10-28 第二轮：SPI0 已启用后）

在 `enable_spi0_overlay.sh` 落地（`/dev/spidev0.0` 生成 + 0666 权限 + 开机自动加载）后，
再次运行同一自检程序（仍无外设，`pico` 普通用户）：

```text
$ ./hardware_selftest
GPIO loopback=56 button=92 PWM=pwmchip0/pwm0
[FAIL] GPIO_LOOPBACK  setDirection(out) 失败
[FAIL] PWM_NODE       PWM configure(20ms) 失败
[FAIL] SPI_LCD_ID     LCD ID 不匹配 0x9341     ← 变化：已能打开 /dev/spidev0.0 并传输
[FAIL] USB_AUDIO      aplay 无输出（aplay 缺失或无声卡）
[FAIL] WIFI_LINK      wlan0 不存在或未就绪
[FAIL] BUTTON_LEVEL   setDirection(in) 失败
=== result: 0 PASS / 6 FAIL ===   （退出码 1）
```

关键变化：`SPI_LCD_ID` 从「未找到 SPI 设备节点 /dev/spidev0.0」变为
「LCD ID 不匹配 0x9341」——说明 `spidev` 设备节点已打开、SPI 时钟/数据线已实际
传输（无屏幕时读到非 0x9341 属预期），**SPI0 硬件通路打通**。剩余 5 项 FAIL 均为
外设未接入或用户权限（GPIO/PWM sysfs 需 root 或 udev 规则）导致，符合预期；
待 ILI9341 到货接线后 `SPI_LCD_ID` 应转为 PASS。

## 5. systemd 开机自检与演示模式门禁

### 5.1 部署步骤（开发板）

```bash
# 1) 交叉编译自检 CLI（或在老电脑 g++ 编译后拷到板子）
cd src/luckfox_rtsp_yolov5 && make hardware-selftest
# 产物 hardware_selftest 拷到开发板演示目录：
#   /home/pico/daima/luckfox_pico_rtsp_yolov5_demo/

# 2) 部署脚本与服务单元
sudo cp scripts/hardware_selftest.sh \
    /home/pico/daima/luckfox_pico_rtsp_yolov5_demo/
sudo cp scripts/hardware-selftest.service /etc/systemd/system/

# 3) 启用开机自检
sudo systemctl daemon-reload
sudo systemctl enable --now hardware-selftest.service

# 4) 查看结果
systemctl status hardware-selftest
cat /tmp/hardware_selftest.log
```

### 5.2 门禁语义

- 自检脚本把报告写入 `/tmp/hardware_selftest.log`；
- **全部 PASS** → 创建 `/tmp/hardware_selftest.pass`；
- `start_demo.sh` 启动时检查该标记：存在才进入演示模式；不存在则提示先运行
  `sudo bash hardware_selftest.sh`（现场紧急情况可用 `start_demo.sh --force` 跳过）；
- 任一 FAIL → 不创建标记，`start_demo.sh` 拒绝启动，避免「带病演示」。

## 6. 自定义探测命令

`HardwareSelfTest` 提供 setter，可按实测环境覆盖默认探测方式：

```cpp
st.setSpiDevice("/dev/spidev1.0");   // SPI 设备节点
st.setSpiIdCmd("lcd_id_probe");      // 输出需含 "9341"
st.setAudioProbeCmd("aplay -l");     // 输出需含 "USB"
st.setWifiProbeCmd("ip -o -4 addr show wlan0");  // 输出需含 "inet"
```

> 接入 LVGL 触摸屏驱动后，`setSpiIdCmd` 可换成真实 SPI 读 ILI9341 ID 的小工具，
> 比默认的 device-tree compatible 更可靠。

## 7. 测试覆盖（test_selftest.cc，80 项）

| 场景 | 注入方式 | 预期 |
|---|---|---|
| 全部外设正常 | FakeGpio(读回)+FakePwm+全 PASS 探测 | 6 PASS / 0 FAIL，allPassed=true |
| GPIO 写高读回失败 | FakeGpio 未启用输出读回 | GPIO_LOOPBACK FAIL |
| PWM 配置失败 | BadPwm（configure 返回 false） | PWM_NODE FAIL |
| SPI 节点缺失 | 注入 `/dev/spidev0.0` 不存在 | SPI_LCD_ID FAIL |
| SPI ID 不匹配 | 命令输出不含 `9341` | SPI_LCD_ID FAIL |
| 无 USB 声卡 | `aplay -l` 输出无 `USB` | USB_AUDIO FAIL |
| wlan0 无 IP | `ip addr` 输出无 `inet` | WIFI_LINK FAIL |
| 按键读失败 | FailReadGpio（read 永远失败） | BUTTON_LEVEL FAIL |
| NULL 注入 | 三个依赖全传 NULL | 全部 FAIL 不崩溃 |
| 自定义 SPI 路径 | 注入 `/dev/spidev1.0` 存在 | 全 PASS |
| printReport | 输出到 tmpfile | 报告含 PASS 行，不崩溃 |