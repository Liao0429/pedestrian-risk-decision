# 双舵机云台自动追踪方案（Luckfox Pico Max + 2×SG90）

> 项目：基于 C++ 的边缘端轻量化行人感知与风险决策软件系统（毕设）
> 目标板：Luckfox Pico Max（RV1106G3，单核 Cortex-A7 1.2GHz，256MB DDR3L，NPU 1 TOPS）
> 本文档：调研双舵机云台（Pan-Tilt）实现摄像头自动追踪的硬件选型、接口、接线、控制算法与软件集成方案。
> 舵机型号：用户已有的 **2 个 SG90（9g 微舵机）** 为默认方案。

---

## 0. 方案结论速览

| 项目 | 结论 |
|---|---|
| 舵机型号 | SG90 × 2（9g，4.8~6V，约 1.8kg·cm） |
| 控制接口 | Luckfox Pico Max 硬件 PWM（sysfs），PWM8_M1 / PWM9_M1（或 PWM10_M1 / PWM11_IR_M1） |
| 信号电平 | 3.3V（RV1106 GPIO 域），SG90 可直驱（TTL 高电平阈值约 2.5V）；如求稳可加三极管/电平转换 |
| 舵机供电 | **外部 5V 电源（≥1.5A），绝不接板载 3.3V；必须与板子共地** |
| 控制频率 | 舵机线程 20~50Hz，PWM 50Hz（周期 20ms，0.5~2.5ms 对应 0~180°） |
| 控制算法 | 画面偏差 → P/PID + 死区 + 限幅 + 低通平滑 |
| 追踪目标 | 可配置：最大行人 / 行人质心（高密度风险区域） |
| 新增成本 | 约 ¥60~120（含舵机、云台支架、5V 电源、杜邦线） |

---

## 1. 舵机选型与工作原理

### 1.1 SG90 关键参数（用户已持有）

| 参数 | 值 |
|---|---|
| 重量 | 9g |
| 工作电压 | 4.8V ~ 6.0V |
| 扭矩 | 约 1.8 kg·cm（4.8V）/ 2.2 kg·cm（6V） |
| 速度 | 约 0.1s/60°（无负载） |
| 控制信号 | PWM，频率 50Hz（周期 20ms） |
| 脉冲宽度 | 0.5ms~2.5ms 对应 0°~180° |
| 引线 | 棕色=GND，红色=VCC(5V)，橙色=信号线 |
| 电流 | 空载约 10mA，运行约 100~250mA，堵转峰值约 500~700mA |

> 注：不同品牌 SG90 的中位（90°）脉宽略有差异（1.45~1.55ms），上电前用 1.5ms 作为中位最安全，实际安装时可用 1.0ms/1.5ms/2.0ms 做三点标定。

### 1.2 为什么选 SG90（对比 MG996R 等）

| 型号 | 重量 | 扭矩 | 电压 | 电流 | 适用性 |
|---|---|---|---|---|---|
| **SG90**（推荐） | 9g | 1.8kg·cm | 4.8~6V | ~250mA | 轻负载云台、毕设演示足够；便宜 |
| MG996R | 55g | 9.4~11kg·cm | 4.8~6V | ~500mA~2.5A | 扭矩大但太重、耗电大，云台会明显抖动，不推荐给本方案 |
| MG90S | 13.4g | 1.8~2.2kg·cm | 4.8~6V | ~250mA | 金属齿轮、更耐用，可作为 SG90 的加固替代 |
| DS3218 | 60g+ | 20kg·cm+ | 6~7.4V | 大 | 工业级，毕设不需要 |

**结论**：既然已持有 SG90，直接采用。注意 SG90 扭矩小，云台负载（摄像头/板子）必须尽量轻，且俯仰（tilt）舵机要处理重力，建议：
- 安装时让俯仰舵机尽量靠近负载重心；
- 用塑料云台支架 + 轻量化固定件；
- 可加配重（对面加小块铅/螺母）平衡俯仰力矩；
- 若最终发现俯仰抖动/抬不起来，可把俯仰舵机换成 MG90S（金属齿、同尺寸同电压，扭矩略大，几乎无改造成本）。

### 1.3 舵机控制原理

SG90 是模拟舵机，内部有电位器反馈，外部只需输入 **50Hz PWM**，通过**高电平脉宽**控制角度：

```
周期 T = 20ms（50Hz）
脉宽 duty = 0.5ms → 0°
           1.5ms → 90°（中位）
           2.5ms → 180°
角度与脉宽近似线性： angle = (duty_us - 500) / (2000 / 180)
```

**注意**：舵机角度由**脉宽**决定，不是占空比百分比。所以 PWM 周期必须固定 20ms，只改 `duty_cycle` 的 ns 值（0.5ms~2.5ms = 500000~2500000ns）。

---

## 2. Luckfox Pico Max 的 PWM / GPIO 能力

### 2.1 硬件 PWM（不是软件模拟）

RV1106 内部有 PWM 控制器（32 位定时器/计数器），Linux 下通过 sysfs 导出为 `/sys/class/pwm/pwmchipN`，由内核驱动产生**硬件 PWM**，**不占 CPU**，非常适合舵机控制。

官方文档明确示例：
- 导出 PWM10_M1（即 `GPIO1_C6_d`）：
  ```
  echo 0 > /sys/class/pwm/pwmchip10/export
  ```
- 舵机控制时设置：
  ```
  echo 20000000 > /sys/class/pwm/pwmchip10/pwm0/period       # 20ms = 50Hz
  echo 1500000 > /sys/class/pwm/pwmchip10/pwm0/duty_cycle   # 1.5ms = 90°中位
  echo normal  > /sys/class/pwm/pwmchip10/pwm0/polarity
  echo 1       > /sys/class/pwm/pwmchip10/pwm0/enable
  ```

### 2.2 电压

- RV1106 的 GPIO/PWM 输出为 **3.3V 逻辑**（部分引脚域是 1.8V，如 GPIO4_C1 的 SARADC，见下警告）。
- Luckfox Pico Max 排针提供 **3.3V（VCC_3V3）** 与 **VBUS（USB 5V）**，但**不建议用板载电源直接带舵机**（见第 3 节）。

### 2.3 推荐使用的 PWM 引脚（40Pin 排针）

结合官方原理图（Luckfox-Pico-Pro-Max 原理图 PART C）与 wiki，以下 PWM 通道位于排针上且为 3.3V 域：

| PWM 通道 | SoC GPIO | GPIO sysfs 编号 | 排针标号(Pico 风格) | 排针物理脚 | 与其它功能复用 |
|---|---|---|---|---|---|
| **PWM8_M1** | GPIO1_C4_d | 52 | GP3 | **第 5 脚** | UART4_RX_M1 / VI_CIF_D6_M1 / VO_LCDC_D3 |
| **PWM9_M1** | GPIO1_C5_d | 53 | GP2 | **第 4 脚** | UART4_TX_M1 / VI_CIF_D7_M1 / VO_LCDC_D2 |
| PWM10_M1 | GPIO1_C6_d | 54 | GP1 | 第 2 脚 | UART4_RTS_M1 / VI_CIF_D8_M1 / VO_LCDC_D1 |
| PWM11_IR_M1 | GPIO1_C7_d | 55 | GP0 | 第 1 脚 | UART4_CTS_M1 / VI_CIF_D9_M1 / VO_LCDC_D0 |

> - **推荐默认用 PWM8_M1（脚5）和 PWM9_M1（脚4）**：两脚相邻，接线方便，且为普通 PWM 通道（非 IR 变体）。
> - 若这两个通道在 `/sys/class/pwm` 下不存在（未使能），可改用 **PWM10_M1（脚2）+ PWM9_M1（脚4）** 或 **PWM10_M1 + PWM11_IR_M1**，以 `ls /sys/class/pwm` 实际存在为准。
> - GPIO1_C4/C5/C6/C7 在 Max 板上默认未被 RGB/其它外设占用（这是 Pico Ultra 才有 RGB 冲突的问题），可放心用于 PWM。

### 2.4 使能 PWM（关键步骤）

**方式 A：Buildroot 系统**（板子如果刷的是官方 buildroot 镜像）：
```
sudo luckfox-config
  → Advanced Options → PWM
  → 勾选 PWM8_M1、PWM9_M1（按需）
  → 保存并 reboot
```

**方式 B：Ubuntu 系统**（本项目的板子刷的是 Ubuntu 22.04，无 luckfox-config 工具）：
需要修改设备树源文件并重编译内核 DTB，在 SDK 中 `rv1106g-luckfox-pico-pro-max.dts` 添加：
```c
&pwm8 {
    status = "okay";
    pinctrl-names = "active";
    pinctrl-0 = <&pwm8m1_pins>;
};
&pwm9 {
    status = "okay";
    pinctrl-names = "active";
    pinctrl-0 = <&pwm9m1_pins>;
};
```
然后在老电脑上交叉编译内核/DTB，重新烧录或替换 `/boot` 下的 dtb。

> **项目实操建议**：先运行 `ls -l /sys/class/pwm` 看板上是否已存在 `pwmchip8`/`pwmchip9`。若默认镜像已使能（官方镜像通常带 pwmchip1、pwmchip7、pwmchip10 等），直接使用即可；若没有，优先选**已存在**的通道（例如 pwmchip10 对应 PWM10_M1=GPIO1_C6=第2脚）。**不要为了两个舵机去重编内核**，除非实在没有可用通道。

---

## 3. 接线方案

### 3.1 引脚连接总表

```
舵机1（Pan 水平）：
  橙色 信号线  → GPIO1_C4_d（PWM8_M1）＝ 排针第 5 脚
  红色 电源线  → 外部 5V 电源正极
  棕色 地线    → 外部电源负极 ＋ 板子 GND（共地）

舵机2（Tilt 垂直）：
  橙色 信号线  → GPIO1_C5_d（PWM9_M1）＝ 排针第 4 脚
  红色 电源线  → 外部 5V 电源正极
  棕色 地线    → 外部电源负极 ＋ 板子 GND（共地）

板子 GND：排针第 3 / 8 / 13 / 18 / 23 脚任选一个
```

### 3.2 电源接线（重点）

```
                ┌─────────────────────────────┐
                │  外部 5V 电源（USB充电器/DC-DC/电池组，≥1.5A）│
                └─────────────────────────────┘
                        │ +5V              │ GND
            ┌───────────┴─────┐            ├──────────────┐
            │ 舵机1 红线       │ 舵机2 红线 │              │
            │ 舵机1 棕线       │ 舵机2 棕线│              │
            ▼                 ▼            ▼              ▼
        SG90_1             SG90_2        板子 GND      板子 Type-C/电源 共地
```

**三条铁律：**
1. **舵机电源绝不要接板载 3.3V（VCC_3V3）**。SG90 堵转电流可达 500~700mA，会拉垮板载 3.3V LDO 导致板子重启/烧毁。
2. **必须共地**：外部电源 GND 与 Luckfox 板 GND 连在一起，否则 PWM 信号没有参考电平，舵机会乱抖。
3. **不建议直接取排针的 VBUS（第20脚，USB 5V）**：那是板子 USB 输入的 5V 总线，同时给板载 DCDC 供电，舵机电流会导致 USB 电源跌落、板子重启。**舵机一律外部独立供电。**

### 3.3 信号电平兼容性

- RV1106 GPIO 输出高电平 = 3.3V。
- SG90 的输入信号高电平阈值（TTL）一般 ≥2.5V 即可识别，**3.3V 直接驱动绝大多数 SG90 可正常工作**。
- 若发现舵机乱抖/不响应（个别舵机对高电平要求 ≥4.5V），用**一个简单的 NPN 三极管（如 2N2222/S8050）做电平抬升**到舵机 5V 电源：

```
GPIO 信号 → 1kΩ → 三极管基极
三极管发射极 → GND
三极管集电极 → 舵机信号线
舵机 5V 通过 4.7kΩ 上拉电阻到集电极（集电极开路输出，输出摆幅到 5V）
```

或用现成模块：`3.3V→5V 舵机逻辑电平转换模块`（约 ¥5）。

### 3.4 云台机械安装（SG90 扭矩小，务必轻量化）

- 购买**塑料双轴云台支架**（约 ¥5~10），两个舵机一横一竖组成 Pan-Tilt。
- 负载建议：**只把摄像头模组（SC3336，约 5g）+ 短 FPC 排线装在云台上**，板子固定在底座不动。这样 SG90 非常轻松。
  - 但 SC3336 的 MIPI FPC 线较短且硬，若板子必须跟着转，可换长 FPC 或把整块板（约 40×40mm、含排针约 30~40g）装在云台上。
- 若整板随动：Type-C 供电线和网线要留足余量，且**重心尽量贴近俯仰舵机转轴**；可在云台对面加小配重。
- 俯仰舵机承受重力，是薄弱点：把摄像头放俯仰臂近轴处；若抖动，把俯仰换 MG90S。

---

## 4. 控制算法

追踪目标：检测给出的人框（显示坐标 720×480）中心。两种追踪模式可配置：
- **模式 A：最大行人** —— 选面积最大的人框中心作为目标。
- **模式 B：高密度风险区域** —— 用所有行人脚点/中心的加权质心（权重=密度/风险），或直接用 `RiskEngine` 判定 DANGER 时跟踪人群质心。

### 4.1 偏差计算

```
图像尺寸：W=720, H=480，画面中心 (360, 240)
目标中心：(tx, ty)
偏差：    err_x = tx - 360
          err_y = ty - 240
```
死区（deadzone）：`|err| < 15px` 时不更新角度，避免抖动。

### 4.2 角度映射（P 控制）

```
目标角度 = 90° + Kp * err
```
取 `Kp`：全幅偏差 ±360px 对应 ±45°，即 `Kp ≈ 45/360 = 0.125°/px`。
- 俯仰同理，±240px 对应 ±30°，`Kp_y ≈ 30/240 = 0.125°/px`（可微调）。

### 4.3 平滑（限幅 + 低通）

1. **限幅（饱和）**：舵机角度限制在 `10°~170°`，防止机械卡死。
2. **角速度限幅**：每次更新最大变化 `maxStep`（如 2°，20Hz 下即 40°/s），防止舵机猛甩。
3. **一阶低通**：`angle_smooth = 0.7 * angle_target + 0.3 * angle_smooth_prev`，进一步抑制抖动。

（PID 的 I 项可消除静差，但舵机控制对 D 项敏感容易振荡，**先用 P + 低通 + 限幅**，效果不佳再加 I，D 一般不用。）

### 4.4 角度 → 脉宽

```
duty_ns = 500000 + (angle / 180.0) * 2000000   // 0.5ms ~ 2.5ms
```

---

## 5. C++ 实现与集成

### 5.1 Servo 类（PWM sysfs 封装）

```cpp
// servo.h
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

class Servo {
public:
    Servo(int chip, int channel = 0)
        : m_chip(chip), m_chan(channel) {
        snprintf(m_base, sizeof(m_base),
                 "/sys/class/pwm/pwmchip%d/pwm%d", chip, channel);
    }

    // 导出并配置为 50Hz、normal、中位 1.5ms，然后使能
    bool init() {
        char p[64];
        snprintf(p, sizeof(p), "/sys/class/pwm/pwmchip%d/export", m_chip);
        FILE *f = fopen(p, "w");
        if (f) { fprintf(f, "%d", m_chan); fclose(f); usleep(10000); }

        if (!writeFile("period", 20000000)) return false;      // 20ms = 50Hz
        if (!writeFile("polarity", "normal")) return false;
        if (!writeFile("duty_cycle", 1500000)) return false;  // 中位 90°
        if (!writeFile("enable", 1)) return false;
        return true;
    }

    // angle: 0~180
    void setAngle(float angle) {
        if (angle < 0)   angle = 0;
        if (angle > 180) angle = 180;
        long ns = 500000L + (long)((angle / 180.0f) * 2000000.0f);
        writeFile("duty_cycle", ns);
    }

private:
    bool writeFile(const char *attr, long val) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE *f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%ld", val);
        fclose(f);
        return true;
    }
    bool writeFile(const char *attr, const char *val) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE *f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%s", val);
        fclose(f);
        return true;
    }

    int  m_chip, m_chan;
    char m_base[128];
};
```

### 5.2 云台控制器（P + 死区 + 限幅 + 低通）

```cpp
// pan_tilt.h
#pragma once
#include "servo.h"
#include <cmath>

class PanTilt {
public:
    PanTilt(Servo &pan, Servo &tilt)
        : m_pan(pan), m_tilt(tilt),
          m_panAngle(90), m_tiltAngle(90),
          m_smoothPan(90), m_smoothTilt(90) {}

    void init() {
        m_pan.init();  m_tilt.init();
        m_pan.setAngle(90);  m_tilt.setAngle(90);
    }

    // imgW/imgH: 图像宽高; tx,ty: 目标中心(原图像素坐标)
    // mode: 0 追踪最大行人, 1 追踪人群质心
    void update(int imgW, int imgH, float tx, float ty, bool hasTarget) {
        if (!hasTarget) return;   // 无目标保持不动

        const float cx = imgW / 2.0f, cy = imgH / 2.0f;
        float errX = tx - cx;
        float errY = ty - cy;

        // 死区
        const float DEAD = 15.0f;
        if (fabs(errX) < DEAD && fabs(errY) < DEAD) return;

        // P 增益
        const float KP_X = 0.125f;   // °/px
        const float KP_Y = 0.125f;

        float targetPan  = 90.0f + KP_X * errX;
        float targetTilt = 90.0f + KP_Y * errY;

        // 机械限幅
        if (targetPan  < 10) targetPan  = 10;
        if (targetPan  > 170) targetPan = 170;
        if (targetTilt < 10) targetTilt = 10;
        if (targetTilt > 170) targetTilt = 170;

        // 角速度限幅（每次最大 2°，约 40°/s @ 20Hz）
        const float MAX_STEP = 2.0f;
        float dPan  = clamp(targetPan  - m_panAngle,  -MAX_STEP,  MAX_STEP);
        float dTilt = clamp(targetTilt - m_tiltAngle, -MAX_STEP,  MAX_STEP);
        m_panAngle  += dPan;
        m_tiltAngle += dTilt;

        // 一阶低通平滑
        m_smoothPan  = 0.7f * m_panAngle  + 0.3f * m_smoothPan;
        m_smoothTilt = 0.7f * m_tiltAngle + 0.3f * m_smoothTilt;

        m_pan.setAngle(m_smoothPan);
        m_tilt.setAngle(m_smoothTilt);
    }

    // 查询当前目标中心（供其它线程使用）：由调用方在多线程时加锁
    void currentAngles(float &pan, float &tilt) const {
        pan = m_smoothPan; tilt = m_smoothTilt;
    }

private:
    static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    Servo &m_pan, &m_tilt;
    float m_panAngle, m_tiltAngle;
    float m_smoothPan, m_smoothTilt;
};
```

### 5.3 追踪目标计算（对接现有检测结果）

现有 `main.cc` 中，检测框已经 `mapCoordinates` 到 720×480 显示坐标。在该处提取目标：

```cpp
// 在 for(i<od_results.count) 循环里同时做：
// 1) 计算目标中心并喂给云台
bool hasTarget = false;
float tgtX = 0, tgtY = 0;
float maxArea = 0;

for (int i = 0; i < od_results.count; i++) {
    object_detect_result *dr = &(od_results.results[i]);
    if (dr->cls_id != 0) continue;              // 只跟踪 person
    int l = dr->box.left, t = dr->box.top;
    int r = dr->box.right, b = dr->box.bottom;
    int sx = l, sy = t, ex = r, ey = b;
    mapCoordinates(&sx, &sy); mapCoordinates(&ex, &ey);   // 转显示坐标

    float area = (ex - sx) * (ey - sy);
    float boxCx = (sx + ex) / 2.0f;
    float boxCy = (sy + ey) / 2.0f;

    if (trackMode == 0) {                       // 最大行人
        if (area > maxArea) { maxArea = area; tgtX = boxCx; tgtY = boxCy; hasTarget = true; }
    } else {                                    // 模式1: 人群质心(高密度风险区域)
        tgtX += boxCx; tgtY += boxCy; hasTarget = true;
    }
}
if (trackMode == 1 && hasTarget) {                // 质心平均
    tgtX /= pedestrianCount; tgtY /= pedestrianCount;
}

// 将 tgtX/tgtY 发布给舵机线程（原子变量，见下）
g_target.store({tgtX, tgtY, hasTarget});
```

> 更简单的模式 B 变体：只在 `crowd == CROWD_DANGER` 时跟踪人群质心，否则跟踪最大行人。

### 5.4 集成方式（推荐：独立轻量线程）

现有程序是单循环（取帧→推理→风险→画框→RTSP）。为避免舵机 sysfs 写入拖慢主循环，**开一个独立 std::thread**，频率 20Hz：

```cpp
// 共享目标（原子结构，避免锁）
struct Target { float x=0, y=0; bool valid=false; };
std::atomic<Target> g_target;

void servoThread() {
    Servo  pan(8, 0);   // PWM8_M1  → 排针第5脚
    Servo  tilt(9, 0);  // PWM9_M1  → 排针第4脚
    PanTilt pt(pan, tilt);
    pt.init();

    const int W = 720, H = 480;
    while (running) {
        Target t = g_target.load();
        pt.update(W, H, t.x, t.y, t.valid);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 20Hz
    }
}
```

- **控制频率**：20~50Hz 足够（舵机自身 PWM 是 50Hz，角度更新不必更快）。20Hz 下 CPU 占用几乎可忽略（主要是一次 fopen/fprintf/fclose + sleep）。
- **低 CPU 技巧**：`setAngle` 只在角度变化 >0.1° 时才写 duty_cycle（可在 PanTilt 里加判断），减少文件 IO。
- **与主循环解耦**：主循环检测完只需 `g_target.store(...)`，不阻塞。

### 5.5 编译

老电脑交叉编译，把 `servo.cc`/新文件加入 CMake 目标，链接 pthread：
```cmake
find_package(Threads REQUIRED)
target_link_libraries(${TARGET} ${CMAKE_THREAD_LIBS_INIT})
```

---

## 6. 成本估算

| 物料 | 单价 | 数量 | 小计 |
|---|---|---|---|
| SG90 舵机（已有） | ~¥12 | 2 | ¥0（已有） |
| 塑料双轴云台支架 | ~¥8 | 1 | ¥8 |
| 5V/2A USB 电源适配器 | ~¥15 | 1 | ¥15 |
| 面包板/杜邦线（公对母） | ~¥8/条若干 | 1 套 | ¥8 |
| 100~470µF 电解电容（舵机电源滤波） | ~¥1 | 2 | ¥2 |
| （可选）3.3V→5V 电平转换模块 | ~¥5 | 1 | ¥5 |
| （可选）MG90S 替代俯仰舵机 | ~¥18 | 1 | ¥18 |
| **合计** | | | **约 ¥40~60（不含已有舵机）** |

---

## 7. 注意事项与避坑

1. **PWM 必须先使能再使用**：`/sys/class/pwm/pwmchipN/export` 写通道号后，目录里才会出现 `pwm0/`。若 `ls /sys/class/pwm` 没有对应 `pwmchip8`/`pwmchip9`，说明 DTS 未使能，需 luckfox-config（buildroot）或改设备树（Ubuntu）。
2. **必须 root/sudo 运行**：sysfs 写入需要 root 权限。本程序运行本来就 `sudo`（访问 `/dev/rknpu`），所以同一进程内写 PWM 没问题。
3. **舵机电源绝对独立**：不要用板子 3.3V，不要直接挂排针 VBUS 带大电流；堵转是最危险情况（可能烧舵机/电源）。
4. **必须共地**：不共地 = 舵机乱抖甚至烧信号线。
5. **信号电平**：3.3V 一般直驱 SG90 没问题；若抖，加三极管电平转换。
6. **开机抖动**：程序启动先让舵机回中位（1.5ms）再进入追踪；避免上电瞬间舵机猛甩。
7. **机械限位**：角度限在 10°~170°，防止云台支架卡死导致舵机堵转烧毁。
8. **死区与限幅**：不加死区，目标稍微移动舵机就嗡嗡抖动；不加限幅，目标跳变会甩舵机。
9. **俯仰重心**：SG90 扭矩小，俯仰舵机承重是薄弱点，务必轻量化、近轴、可加配重；不行换 MG90S。
10. **电源滤波**：舵机工作电流脉动大，在舵机电源端并联 100~470µF 电解电容（尽量靠近舵机），可明显减少抖动和干扰。
11. **FPC/线缆余量**：若整板随动，Type-C 与网线留足余量，否则云台转不到位；测试时先用手转确认活动范围无阻碍。
12. **线程安全**：主循环与舵机线程共享目标坐标，用 `std::atomic` 或加 mutex；舵机线程只读最新目标，不阻塞主流程。
13. **追踪目标判定**：画面中心与检测框中心直接相关，注意 `mapCoordinates` 后的坐标系是 720×480；`tgtY` 建议用框的几何中心或脚点，避免框高变化导致俯仰抖动。

---

## 附录 A：Shell 手动测试舵机（上电前验证）

```bash
# 假设 PWM8_M1 已使能（pwmchip8 存在）
echo 0 > /sys/class/pwm/pwmchip8/export
echo 20000000 > /sys/class/pwm/pwmchip8/pwm0/period      # 20ms
echo normal   > /sys/class/pwm/pwmchip8/pwm0/polarity
echo 1500000  > /sys/class/pwm/pwmchip8/pwm0/duty_cycle   # 90°中位
echo 1        > /sys/class/pwm/pwmchip8/pwm0/enable

# 慢慢摆动测试（0.5ms→90°，2.5ms→180°）
echo 500000  > /sys/class/pwm/pwmchip8/pwm0/duty_cycle
sleep 1
echo 2500000 > /sys/class/pwm/pwmchip8/pwm0/duty_cycle
```

## 附录 B：参考来源

- Luckfox 官方 Wiki PWM：`https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/PWM`
- Luckfox 官方 Wiki GPIO：`https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/GPIO`
- Luckfox Pico Pro/Max 原理图（引脚复用表与排针网络）：`https://files.luckfox.com/wiki/Luckfox-Pico/PDF/Luckfox-Pico-SCH/Luckfox%20Pico%20Pro_Max.pdf`
- Luckfox 官方 SDK 设备树：`github.com/LuckfoxTECH/luckfox-pico`