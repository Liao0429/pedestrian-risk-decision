# 三色 LED 与按键驱动（led / button / gpio）

> 位置：`src/hardware/` —— `gpio.h`、`led.h`、`button.h`
> 状态：✅ 代码完成 + 主机逻辑测试通过；⏳ 真机验证待硬件打样后执行

## 1. 模块职责

| 文件 | 职责 |
|---|---|
| `gpio.h` | GPIO 抽象层：`SysfsGpio`（Linux `/sys/class/gpio` 实现，供板端）+ `FakeGpio`（内存替身，供 PC/CI 测试） |
| `led.h` | 三色 LED 驱动：红/绿/蓝三路 GPIO 输出，风险等级 → 颜色映射 |
| `button.h` | 按键驱动：GPIO 输入 + 连续采样软件防抖 + 上升/下降沿检测 |

设计要点：

- **可测试性**：LED/按键逻辑全部依赖 `hw::Gpio` 抽象接口，不直接碰 sysfs。
  主机测试注入 `FakeGpio`，无需真实硬件即可验证颜色映射与防抖状态机。
- **容错**：GPIO 节点不存在/被占用时 `init()` 返回 `false`，模块标记不可用但不会崩溃，
  主程序可据此选择禁用硬件功能继续运行（不阻塞主检测链路）。
- **无第三方依赖**：仅 C++11 标准库，主机与交叉编译均无需额外库。

## 2. 引脚映射（Luckfox Pico Max，以实测为准）

| 信号 | 排针 | GPIO 名 | sysfs 编号（实测） | 说明 |
|---|---|---|---|---|
| 按键（告警使能/功能键） | **Pin 27** | **GPIO2_A3_d** | **`67`** | PCB J8(SW_ALARM)+R1 10kΩ 下拉到 GND，按下为高 |
| LED 红 | 待定版 | 待定 | 构造参数传入 | 高电平点亮；**56/57/58 不可用**（见下） |
| LED 绿 | 待定 | 待定 | 构造参数传入 | 高电平点亮 |
| LED 蓝 | 待定 | 待定 | 构造参数传入 | 高电平点亮 |

> ❌ 旧版本表写「按键 Pin 92 / GPIO1_C3_d / sysfs 51」是错的（2026-10-30 板上实测）：
> - sysfs **92 = GPIO2_D4**（`/sys/class/gpio` 里 gpiochip2 base=64 可算），与按键无关；
> - **GPIO1_C3 = sysfs 51 实测是 SPI0_MISO**（`pinmux-pins`: pin51 = spi0m0-miso），
>   用作按键会与 ILI9341 读版本 ID 冲突；
> - **56/57/58 也不是空闲脚**：GPIO1_D0/D1(56/57) 属 UART3_TX/RX，GPIO1_D2(58) 实测是 I2C3_SCL。
> LED 三路请先用 `cat /sys/kernel/debug/pinctrl/*/pinmux-pins` 挑未复用脚再定版。

> ⚠️ sysfs GPIO 编号算法：`bank*32 + group*8 + pin`（GPIO0=0~31，GPIO1=32~63）。
> 实际以板面丝印与 `/sys/class/gpio` 实测为准；若节点缺失需先在设备树使能。

## 3. 使用示例

```cpp
#include "hardware/gpio.h"
#include "hardware/led.h"
#include "hardware/button.h"

// 板端实例化（引脚按打样定版后填写）
// ⚠ 引脚待实测定版：56/57/58 已被 UART3/I2C3 复用，这里只是占位
hw::SysfsGpio ledR(0), ledG(0), ledB(0);
hw::SysfsGpio btnGpio(67);      // GPIO2_A3_d（排针 Pin27）
hw::RgbLed    statusLed(&ledR, &ledG, &ledB);
hw::Button    alarmSwitch(&btnGpio, /*按下为高*/ true, /*防抖*/ 5);

bool alarmEnabled = true;

// 初始化（失败不致命）
statusLed.init();
alarmSwitch.init();

// —— 主循环每帧 ——
alarmSwitch.update();
if (alarmSwitch.risingEdge()) {
    alarmEnabled = !alarmEnabled;      // 按键切换告警使能
}

int level = (int)riskEngine.evaluate();  // 0=SAFE 1=WARNING 2=DANGER
if (alarmEnabled) {
    statusLed.setLevel(level);           // 三色灯随风险等级
} else {
    statusLed.setColor(hw::LedColor::OFF);
}
```

## 4. 颜色映射

| 风险等级 | 颜色 | 红 | 绿 | 蓝 |
|---|---|---|---|---|
| SAFE (0) | 绿 | 0 | 1 | 0 |
| WARNING (1) | 黄 | 1 | 1 | 0 |
| DANGER (2) | 红 | 1 | 0 | 0 |
| 未知/其他 | 灭 | 0 | 0 | 0 |

## 5. 测试

```bash
# 主机单元测试（无硬件依赖）
g++ -std=c++11 -O2 -Isrc/hardware \
    src/luckfox_rtsp_yolov5/tests/test_hardware_modules.cc -o /tmp/hw_test
/tmp/hw_test
# 期望输出：passed=NN failed=0

# 或通过 CMake/ctest（CI 同路径）
cmake -S src/luckfox_rtsp_yolov5 -B build
cmake --build build
ctest --test-dir build -R hardware
```

覆盖场景：FakeGpio 行为、LED 等级映射/直接颜色/不可用容错、
按键防抖（<5 次抖动不触发）、按下/释放边沿、低电平有效模式。

## 6. 待办

- [ ] PCB 打样定版后实测 GPIO 编号，更新默认值
- [ ] 真机验证：LED 颜色随风险等级切换、按键切换告警使能
- [ ] 接入 `main.cc` 主循环（与 voice_alert、servo 云台一起做系统集成）