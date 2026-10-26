# 行人感知与风险决策系统 —— 硬件系统集成方案

- 项目：基于 C++ 的边缘端轻量化行人感知与风险决策软件系统（毕设成品）
- 平台：Luckfox Pico Max（RV1106G3，Cortex-A7 1.2GHz 单核，NPU 1 TOPS，256MB DDR3L）
- 摄像头：SC3336 300 万像素 MIPI CSI 模组（F2.0，98.3° FOV）
- 现有能力：YOLOv5(RKNN) 行人检测 → 复杂网络风险决策（SAFE / WARNING / DANGER）→ H264 编码 + RTSP 推流
- 本次扩展：**双舵机云台追踪 + 语音告警 + OLED(SSD1306) 显示 + 物理开关**
- 文档日期：2026-10-25
- 关联文档：`docs/ALGORITHM_OPTIMIZATION.md`（算法审查与优化建议，本方案的跟踪/云台策略需与之对齐）

---

## 0. 总体架构与系统框图

扩展后的系统由 5 个功能子系统组成，全部在单块 Luckfox Pico Max 上运行：

```
                         ┌───────────────────────────────────────────────┐
                         │             Luckfox Pico Max (RV1106)           │
                         │  ┌─────────────┐   ┌─────────────────────────┐  │
   SC3336 摄像头 ───────▶│  │ 视觉主线程    │   │  RiskEngine             │  │
   (MIPI CSI)            │  │ YOLOv5+编码   │   │ 复杂网络风险决策          │  │
                         │  │ +RTSP 推流    │──▶│ SAFE/WARNING/DANGER     │  │
                         │  └──────┬──────┘   └───────────┬─────────────┘  │
                         │         │ 共享状态(线程安全)                 │  │
                         │  ┌──────▼──────┐  ┌─────────┴─────────┐      │
                         │  │ 云台控制线程  │  │ 语音告警线程         │      │
                         │  │ PWM → 双舵机 │  │ UART → JQ8900 → 喇叭 │      │
                         │  └─────────────┘  └───────────────────┘      │
                         │  ┌─────────────┐  ┌───────────────┐       │
                         │  │ OLED 显示线程 │  │ 物理开关轮询线程  │      │
                         │  │ I2C→SSD1306 │  │ GPIO → 使能标志 │       │
                         │  └─────────────┘  └───────────────┘       │
                         └──────┬──────────────────────────────────────┘
                                │
                         5V 电源 / 共地配电（见 §2）
```

各子系统职责与交互对象：

| 子系统 | 输入 | 输出 | 对外接口 | 数据来源/去向 |
|---|---|---|---|---|
| 视觉主线程（已有） | SC3336 图像 | 检测框、风险等级、行人脚点/中心点 | 每帧更新共享状态 | `SystemState.risk / count / features / target` |
| 云台控制 | 目标行人图像坐标 | 双舵机 PWM 脉冲 | 读共享状态 → sysfs PWM | 保持目标居中 |
| 语音告警 | 风险等级跳变 | JQ8900 播放预录音频 | 读共享状态 → UART | 播报三段告警音 |
| OLED 显示 | 风险/统计/角度/使能 | SSD1306 显示 | 读共享状态 → I2C | 实时状态看板 |
| 物理开关 | GPIO 电平 | 告警/云台使能标志 | 写共享状态 | 断电式总开关、功能开关 |

---

## 1. 完整硬件 BOM（清单）

> 价格均为 2026 年淘宝/京东参考价（人民币），实际以店铺为准。**标记"已有"的项无需购买。**

### 1.1 已有资产（无需购买）

| # | 器件 | 型号/规格 | 数量 | 状态 | 备注 |
|---|---|---|---|---|---|
| 1 | 开发板 | Luckfox Pico Max（RV1106G3，256MB） | 1 | 已有 | 核心板，带 USB-C、10/100M 以太网、26 个 GPIO 排针 |
| 2 | 摄像头 | SC3336 3MP Camera (B)（微雪/Luckfox 原厂） | 1 | 已有 | MIPI CSI 2-lane，FFC 排线，98.3° 广角 |
| 3 | 舵机 | **SG90 9g 微舵机 ×2** | 2 | **已有** | 4.8~6V，扭矩约 1.8kg·cm，工作电流 100~300mA，堵转 ~650mA |

> **设计约束（重要）**：SG90 是 9g 微舵机，扭矩仅 1.8kg·cm，云台上的摄像头负载必须**轻量化**（SC3336 模组 + FFC 排线约 10~15g，完全可行）。云台支架需选用专为 SG90 设计的轻质塑料套件，**不要**在云台上堆叠金属支架、散热片或过重外壳；摄像头用双面胶/扎带固定到倾斜臂上，减少转动惯量，避免堵转烧舵机。

### 1.2 需新购硬件

| # | 器件 | 推荐型号/规格 | 数量 | 参考价 | 购买渠道建议 | 备注 |
|---|---|---|---|---|---|---|
| 4 | SG90 双轴云台支架套件 | 铝合金/塑料二自由度云台（配 SG90 ×2） | 1 套 | ¥12~25 | 淘宝搜"SG90 云台支架套件" | 含舵盘、U 型架、底板、螺丝，直接兼容已有 SG90 |
| 5 | 语音播报模块 | **JQ8900-16P** 串口 MP3 模块 | 1 | ¥6~12 | 淘宝搜"JQ8900-16P 语音模块" | UART 串口控制（9600bps 8N1），3.3V TTL 电平，板载功放，支持 TF 卡；比 DFPlayer Mini 控制协议更简单、输出功率更大，是首选 |
| 6 | 功放喇叭 | 8Ω 3W 微型扬声器（直径 40~50mm） | 1 | ¥3~8 | 淘宝/拼多多 | 直接插 JQ8900 的 SP+ / SP- 引脚 |
| 7 | TF 卡 | 8~16GB Class 10 | 1 | ¥15~25（手头有可用） | 已有卡则无需买 | 存放 3 段 WAV/MP3 告警音频，JQ8900 可 USB 直拷 |
| 8 | OLED 显示屏 | **SSD1306 0.96 寸 128×64 I2C 四针**（白色或蓝黄双色） | 1 | ¥6~12 | 淘宝搜"SSD1306 0.96寸 OLED I2C" | 板上 I2C 地址默认 0x3C；供电 3.3~5V（本方案接 3.3V） |
| 9 | 物理开关（总开关） | 自锁式拨动/船型开关，带 LED 灯可选 | 1 | ¥2~5 | 淘宝/电子商城 | 串接在电源适配器与开发板 USB-C 之间，作为系统总电源开关 |
| 10 | 物理开关（功能开关） | 小型自锁按钮/拨动开关 | 1~2 | ¥1~4/个 | 同上 | 一个控制"告警使能"，一个控制"云台使能"（可只做一个） |
| 11 | 电源适配器 | USB-A 口 **5V/3A**（QC 兼容更佳） | 1 | ¥10~25 | 淘宝/京东"5V3A 电源适配器" | 双路供电方案的总能源（见 §2） |
| 12 | USB 一分二 / 电源线 | USB 一分二线（一个公头分两个母头）或两根 USB-A 电源线 | 1~2 | ¥5~10 | 淘宝"USB 一分二" | 把 5V 同时送到开发板与舵机轨 |
| 13 | 降压模块（可选，推荐） | **MP1584EN 3A DC-DC 可调降压模块**（7~28V→5V） | 1 | ¥3~8 | 淘宝"MP1584 降压模块" | 若改用 12V 适配器方案 B，用它把 12V 降到 5V 舵机轨，彻底隔离舵机瞬态 |
| 14 | 电容组 | 1000µF/16V 电解电容 + 0.1µF 陶瓷电容 | 各 2 | ¥2~3 | 电子元件店/淘宝 | 并联在舵机 5V 电源轨上吸收启动浪涌（关键！） |
| 15 | 杜邦线 | 母对母 40P ×2 套、公对母 20P ×1 套 | 3 套 | ¥5~15 | 淘宝"杜邦线 母对母" | 舵机、OLED、语音、开关、电源分配接线 |
| 16 | 面包板 | 400 孔 / 800 孔面包板 | 1 | ¥5~15 | 淘宝/实体店 | 用于电源分配（+5V / GND 汇流排）与信号中转，便于快速接线 |
| 17 | 外壳/固定件（可选） | 亚克力板/3D 打印外壳、铜柱、扎带、双面胶、热缩管 | 1 套 | ¥20~60 | 淘宝"亚克力外壳 定制" | 毕设成品展示用，提升观感与安全性 |
| 18 | 以太网线（可选） | CAT5e，1~2m | 1 | ¥3~8 | 已有可用 | RTSP 推流/调试，比 WiFi 稳 |

---

## 2. 电源设计

### 2.1 核心风险：舵机启动大电流拉低电压导致开发板复位

- SG90 工作电流 100~300mA，但**堵转电流可达 650mA/个**；两个舵机同时动作/堵转时峰值可到 **1.2~1.5A**。
- RV1106 开发板由 USB-C 5V 供电，板上经 DCDC/LDO 产生 3.3V / 1.8V / 1.1V 核心供电。若舵机直接挂在开发板 5V 引脚上，动作瞬间会把 5V 拉低，导致 3.3V/1.8V 跌落 → 出现**重启、花屏、NPU 推理错误**。
- **结论：舵机必须走独立的 5V 电源轨，与开发板供电隔离，只共地。** OLED、语音模块建议也由舵机轨（或独立）供电，尽量别吃开发板 3.3V/5V 电流预算。

### 2.2 电流估算

| 模块 | 电压 | 典型电流 | 峰值/堵转 | 说明 |
|---|---|---|---|---|
| Luckfox Pico Max + SC3336 | 5V | 0.3~0.5A | 0.8A | 含 NPU 推理、编码、RTSP、OLED I2C 总线 |
| SG90 舵机 ×2 | 5V（独立轨） | 0.2~0.4A | 1.2~1.5A | 两舵机同时堵转的最坏情况 |
| OLED SSD1306 | 3.3V（经板上稳压） | 0.02~0.03A | 0.03A | 全屏点亮约 24mA@3.3V |
| JQ8900 语音模块 + 3W 喇叭 | 5V（独立轨） | 0.1~0.3A | 0.6A | 音量最大时 |
| **合计** | — | **0.8~1.4A@5V** | **约 2.5A@5V** | 推荐电源 ≥ 5V/3A |

### 2.3 推荐配电方案（双路 5V，共地）

```
       5V/3A 电源适配器
     ┌─────────────────┐
     │  USB-A 口 1  ────┼──────────▶ 开发板 USB-C（板上自带 5V→3.3V，供电给 SoC/OLED 逻辑）
     │                │
     │  USB-A 口 2  ────┼────┐
     └─────────────────┘    │   舵机/语音独立 5V 轨（"舵机轨"）
                              │
                         ┌────▼────┐
                         │ 面包板  │  +5V 汇流排
                         │  +5V GND│  ──┬─▶ SG90 Yaw  红/VCC
                         └────┬────┘  ├─▶ SG90 Pitch 红/VCC
                              │       ├─▶ JQ8900 VCC
                              │       └─▶ (可选) 预留
                       1000µF/16V
                       + 0.1µF ──── 并联在 +5V 与 GND 之间（吸收浪涌）
                              │
                         GND 汇流排 ──▶ 所有模块 GND 均接到此处
                              │
                              └──────────▶ 开发板 GND 排针（共地）
```

- **方案 A（推荐，最简单）**：一台 **5V/3A 适配器**，用 **USB 一分二线**分出两路 5V。一路给开发板 USB-C，另一路进面包板做"舵机轨"。共地。
- **方案 B（更稳，成本 +~10 元）**：**12V/2A 适配器 → MP1584 降压为 5V/3A** 专门驱动舵机+语音；开发板用独立 5V/1A USB 充电器。两个电源只在 GND 端相连。彻底隔离舵机瞬态，供电最干净，适合毕设展示演示"专业电源设计"。
- **必须共地**：所有模块 GND 与开发板 GND 用导线连通，否则 UART / I2C / PWM 信号的参考电平不一致，会出现乱码、舵机抖动、OLED 花屏。
- **舵机轨上加电容**：在面包板上给 +5V/GND 并联 **1000µF 电解电容 + 0.1µF 陶瓷电容**，吸收舵机启动浪涌，抑制电压跌落（关键措施）。
- **舵机信号线**：SG90 三根线为 红(+5V) / 棕(GND) / 橙(信号)。信号线接开发板 PWM 引脚，电源线接舵机轨，**不要**把舵机 VCC 接到开发板 5V 排针。

---

## 3. 接线总图（ASCII）

### 3.1 引脚分配总表

| 功能 | 外设引脚 | 开发板排针 | GPIO 名 | 说明 |
|---|---|---|---|---|
| 舵机 Yaw（水平）信号 | 橙色信号线 | **Pin 54** | GPIO1_C6_d | PWM10_1（sysfs 默认使能） |
| 舵机 Pitch（俯仰）信号 | 橙色信号线 | **Pin 55** | GPIO1_C7_d | PWM11_1（sysfs 默认使能） |
| 舵机电源 | 红色 VCC | 面包板 5V 轨 | — | 独立 5V，勿接开发板 5V |
| 舵机地 | 棕色 GND | 面包板 GND 轨 | — | 共地 |
| OLED SCL | 模块 SCL | **Pin 72** | GPIO1_A0_d | I2C2_SCL_M0 |
| OLED SDA | 模块 SDA | **Pin 73** | GPIO1_A1_d | I2C2_SDA_M0 |
| OLED VCC | 模块 VCC | 3.3V 排针（Pin 1） | — | SSD1306 3.3V 供电 |
| OLED GND | 模块 GND | GND 排针 | — | 共地 |
| 语音模块 TX | JQ8900 TX → 板 RX | **Pin 77** | GPIO1_B0_d | UART4_RX_M0 |
| 语音模块 RX | JQ8900 RX ← 板 TX | **Pin 78** | GPIO1_B1_d | UART4_TX_M0 |
| 语音模块 VCC/GND | JQ8900 VCC/GND | 5V 轨 / GND 轨 | — | 5V 供电 |
| 喇叭 | SP+ / SP- | — | — | 接 JQ8900 功放输出 |
| 功能开关（告警使能） | 一端 3.3V，一端 GPIO | **Pin 92** | GPIO1_C3_d | 外接 10kΩ 下拉到 GND，按下为高 |
| 总电源开关 | 串接适配器与开发板 | — | — | 自锁开关，断总电 |

> 注：以上引脚为 Luckfox Pico Max 排针推荐映射。若在 `/sys/class/pwm`、`/sys/class/gpio`、`/dev/i2c-*`、`/dev/ttyS*` 中未找到对应节点，请按 §4.4 在设备树中使能对应功能（参考 Luckfox 官方 wiki：PWM / I2C2 / UART4）。实际以板面丝印为准。

### 3.2 完整接线图

```
                            ┌────────────────────────────────────────────┐
                            │            Luckfox Pico Max                │
                            │                                            │
   ┌─────────────┐          │  3.3V ●Pin1────▶ OLED VCC                │
   │ 5V/3A 适配器 │          │  GND  ●───────▶ OLED GND / 共地          │
   │  USB-A 口1   ├────USB──▶│  USB-C (板上供电)                         │
   │  USB-A 口2   ├───5V────▶│                                            │
   └──────┬──────┘          │                                            │
          │   +5V 轨         │  舵机信号：                                │
          │                 │  Pin54(PWM10_1)──▶ SG90 Yaw 橙           │
    ┌─────▼─────┐          │  Pin55(PWM11_1)──▶ SG90 Pitch 橙           │
    │  面包板    │          │                                            │
    │ +5V 汇流排 │──▶ 舵机红 / 语音VCC                                    │
    │ GND 汇流排 │──▶ 所有GND / 开发板GND                                │
    │ 1000µF 电容│          │  OLED I2C：                                │
    └─────┬─────┘          │  Pin72(I2C2_SCL)──▶ OLED SCL               │
          │                 │  Pin73(I2C2_SDA)──▶ OLED SDA               │
          │                 │                                            │
          │                 │  语音 UART：                                │
          │                 │  Pin77(UART4_RX)──◀── JQ8900 TX            │
          │                 │  Pin78(UART4_TX)──▶── JQ8900 RX                 │
          │                 │                                            │
          │                 │  功能开关：                                 │
          │                 │  Pin92(GPIO1_C3)──┬── 自锁按钮 ── 3.3V       │
          │                 │       └────10kΩ──── GND（下拉）             │
          │                 │                                            │
          │                 │  SC3336 摄像头 ── FFC(MIPI CSI) ──▶ 板载接口  │
          └──────────────────┴────────────────────────────────────────────┘

  JQ8900 语音模块            SG90 双轴云台（装于摄像头下方/后方）
   ┌─────────────┐            ┌────────────────────────────┐
   │ VCC(5V)  GND│            │   Yaw 舵机(Pin54) 固定在底座  │
   │ RX ←Pin78   │            │   Pitch 舵机(Pin55) 带动云台臂 │
   │ TX →Pin77   │            │   SC3336 摄像头贴于云台臂前端    │
   │ SP+ / SP- ──┴── 3W喇叭    │   FFC 排线随动(留足够余量/走线) │
   └─────────────┘            └────────────────────────────┘
```

接线步骤建议：

1. **电源**：适配器 USB 一分二 → 开发板 USB-C + 面包板 5V/GND。面包板 +5V/GND 并联电容。
2. **舵机**：SG90 三线——红接面包板 +5V 轨，棕接 GND 轨，橙（Yaw）接 Pin54，橙（Pitch）接 Pin55。
3. **OLED**：VCC→3.3V，GND→GND，SCL→Pin72，SDA→Pin73。
4. **语音**：JQ8900 的 VCC→面包板 5V，GND→GND；TX→Pin77（板接收），RX→Pin78（板发送）；喇叭接 SP+/SP-。TF 卡预录 3 段音频（见 §4.3）。
5. **开关**：自锁按钮一端接 3.3V，另一端接 Pin92；Pin92 经 10kΩ 电阻下拉到 GND。
6. **云台**：Yaw 舵机固定于底座，Pitch 舵机驱动云台臂，摄像头轻质固定于臂端；FFC 排线用扎带在臂上留松弛量，避免随动时拉扯损坏。

---

## 4. 软件集成架构

### 4.1 总体线程模型

现有 `src/luckfox_rtsp_yolov5/src/main.cc` 是**单线程**（取帧→推理→风险→画框→编码→RTSP 串行）。新增外设后，采用 **1 个主线程 + 4 个扩展线程**，通过一个**互斥锁保护的共享状态结构**通信。

```
线程优先级（建议）：
  [主线程: SCHED_FIFO 80]  视觉流水线（最高，保证帧率与风险决策实时性）
  [云台线程: SCHED_FIFO 60]  30~50Hz 控制环（中等，保证跟踪平滑）
  [语音线程: SCHED_OTHER ]  事件驱动（风险跳变才动作，平时阻塞等待）
  [OLED线程: SCHED_OTHER ]  5~10Hz 刷新（低，显示而已）
  [开关线程: SCHED_OTHER ]  20ms 轮询去抖（最低）
```

### 4.2 共享状态结构（新增 `include/system_state.h`）

```cpp
// system_state.h —— 所有线程间共享的数据，统一用一把互斥锁保护
#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "risk_engine.h"   // 复用 CrowdLevel 枚举

typedef struct {
    // ---- 视觉主线程写 ----
    CrowdLevel risk;            // 当前风险等级
    int        ped_count;       // 行人数 n
    float      density;         // 网络密度 rho
    float      r_gcc;           // 最大连通分量占比
    bool       target_valid;    // 本帧是否有可跟踪目标
    int        target_x, target_y; // 目标行人中心(原图 720x480 像素坐标)

    // ---- 云台线程写 ----
    float      pan_angle;       // 当前水平角(度, -45~+45)
    float      tilt_angle;     // 当前俯仰角(度, -30~+30)

    // ---- 开关线程写 ----
    bool       alarm_enabled;    // 语音告警使能
    bool       gimbal_enabled;   // 云台使能

    // ---- 语音线程内部状态 ----
    CrowdLevel last_alarm_level;  // 上次已播报的等级(防重复播报)
    uint64_t   last_alarm_ms;     // 上次播报时间(ms, 冷却用)

    bool       running;         // 系统运行标志(退出通知)
    pthread_mutex_t lock;      // 互斥锁
} SystemState;

void     system_state_init(SystemState *s);
void     system_state_destroy(SystemState *s);
// 便捷封装：读/写某字段前 lock/unlock，所有模块只通过这些函数访问
void     ss_set_risk(SystemState *s, CrowdLevel r, int n, float rho, float gcc);
void     ss_set_target(SystemState *s, bool valid, int x, int y);
void     ss_get_status(SystemState *s, CrowdLevel *r, int *n, float *rho, float *gcc,
                       float *pan, float *tilt, bool *alarm_on, bool *gimbal_on);
bool     ss_get_target(SystemState *s, int *x, int *y);
void     ss_set_angles(SystemState *s, float pan, float tilt);
bool     ss_is_alarm_enabled(SystemState *s);
bool     ss_is_gimbal_enabled(SystemState *s);
bool     ss_set_alarm_level(SystemState *s, CrowdLevel lvl, uint64_t now_ms); // 返回是否允许播报(去抖)
void     ss_set_running(SystemState *s, bool running);
#endif
```

> 锁粒度要小：主线程每帧只短暂加锁写入几个字段，不长时间持有；云台/OLED/语音线程读快照后立即解锁再处理。

### 4.3 各扩展模块设计

#### 4.3.1 云台控制（`pantilt.h/.cc`）

- **接口**：直接操作 Linux sysfs PWM，无需第三方库。
  - 舵机周期 **20ms**（`period = 20000000ns`）。
  - 脉宽映射：`0°→500000ns`，`90°→1500000ns`，`180°→2500000ns`。
    `duty_ns = 500000 + angle/180.0 * 2000000`
  - Yaw 用 PWM10_1（`/sys/class/pwm/pwmchip10/pwm0`），Pitch 用 PWM11_1（`/sys/class/pwm/pwmchip11/pwm0`）。
- **控制环**（云台线程，30~50Hz）：
  1. `ss_get_target` 取目标中心 `(tx,ty)`（原图 720×480 坐标）。
  2. 计算偏差：`err_x = tx - 360`（图像中心 x），`err_y = ty - 240`。
  3. 角度增量：`delta_pan = -Kp * err_x`，`delta_tilt = -Kp * err_y`（Kp≈0.15°/像素，初始角度设为 0°/中位）。
  4. 死区：`|err_x| < 4px` 不动作，防止微抖。
  5. 限幅：`pan ∈ [-45,45]`，`tilt ∈ [-30,30]`；超过限幅停在该边界（保护舵机）。
  6. **EMA 平滑**：`angle = alpha*new + (1-alpha)*old`，`alpha≈0.25`，抑制逐帧抖动。
  7. 写入 `duty_cycle`，并把当前角度回写 `ss_set_angles`（供 OLED 显示）。
- **目标选择策略**（与视觉主线程配合）：没有现成多目标跟踪器，采用启发式"最近/最危险目标"——每帧从检测结果中选一个作为跟踪目标：
  - 优先选**画面底部最靠近相机**的行人（`box.bottom` 最大者，即最近者）；
  - 或选**离图像中心最近的已检行人**；
  - 若 `risk == DANGER`，优先跟踪危险聚类中心对应的行人（视觉线程计算）。
  - 代码在 main.cc 检测循环里选出后调用 `ss_set_target`。
- **后续增强（可选）**：按 `ALGORITHM_OPTIMIZATION.md` 第 5 节加入 IoU 匹配的轻量跟踪器 + Kalman 平滑，云台跟踪会更稳（毕设可写进"下一步工作"）。

#### 4.3.2 语音告警（`voice_alarm.h/.cc`）

- **接口**：JQ8900-16P 用 UART（9600 8N1）控制，3.3V TTL，直接连 UART4。
- **初始化**：`open("/dev/ttyS4", O_RDWR|O_NOCTTY)`，设置 termios 9600 8N1。
- **JQ8900 串口协议**（每帧 8 字节，校验=反码累加）：
  ```
  7E FF 06 [命令] [参数1] [参数2] [校验] FE
  命令 0x0F = 指定曲目播放；参数1=0x00，参数2=曲目序号(1~3)
  例：播放第 1 段： 7E FF 06 0F 00 01 0F FE
      播放第 2 段： 7E FF 06 0F 00 02 0E FE
      播放第 3 段： 7E FF 06 0F 00 03 0D FE
  ```
- **音频素材**（预录 3 段，拷入 TF 卡根目录，命名如 `0001.mp3`~`0003.mp3`）：
  - Track1（SAFE/常态）："系统运行正常，请保持通行安全。"
  - Track2（WARNING）："注意！前方人员密集，请保持安全距离。"
  - Track3（DANGER）："危险！人群过度聚集，请立即疏散，注意安全！"
- **告警逻辑**（语音线程）：
  - 线程循环中 `ss_set_alarm_level()` 判断：仅当**风险等级上升**（SAFE→WARNING、WARNING→DANGER）且距上次播报 **≥10s** 冷却期，才播放对应曲目；同级持续不重复播报。
  - `alarm_enabled == false`（物理开关断开）时静默。
  - 也可监听 JQ8900 `BUSY` 引脚（可选）避免播报叠加；简单起见用软件冷却即可。

#### 4.3.3 OLED 显示（`oled_display.h/.cc`）

- **接口**：SSD1306 接 I2C2，地址 **0x3C**，通过 `/dev/i2c-2` 访问。
- **实现**：自写轻量驱动（约 200 行，避免引入 lib 依赖）：
  - 打开 `/dev/i2c-2`，`ioctl(I2C_SLAVE, 0x3C)`。
  - 发送 SSD1306 初始化序列（0xAE→0x20 设 0x00→0x21 设列→0xC8 镜像→0xA8 多路→0xD3 偏移→0x40→0xA1→0xA6→0xA8 0x3F→0xD5→0x80→0x8D 电荷泵 0x14→0xAF 开显示 等，可直接采用 SSD1306 标准 init 表）。
  - 维护 **1024 字节帧缓冲（128×64/8）**，内置 6×8 ASCII 字体。
  - 写入：命令 `0x00`，数据 `0x40`，一次性写 1024 字节。
- **显示内容**（5~10Hz 刷新）：
  ```
  [第1行] RISK: DANGER        ← 风险等级（红字语义由颜色区分，单色屏用反白/闪烁）
  [第2行] N=12  RHO=0.62        ← 行人数、网络密度
  [第3行] PAN=+12 TILT=-3       ← 云台角度
  [第4行] ALARM:ON  SYS:OK      ← 告警使能、系统状态
  ```
- **线程**：OLED 线程循环 `ss_get_status` → 渲染帧缓冲 → I2C 写入 → `usleep(100000)`（10Hz）。

#### 4.3.4 物理开关（`switch_input.h/.cc`）

- **总电源开关**：自锁开关串接在适配器与开发板 USB-C 之间，断电即系统完全关闭，无需软件处理。
- **功能开关（告警/云台使能）**：
  - GPIO 使用 sysfs：`echo 92 > /sys/class/gpio/export`，`echo in > /sys/class/gpio/gpio92/direction`，循环 `read(value)`。
  - 开关接法：一端 3.3V，一端 GPIO92，GPIO 外接 10kΩ 下拉到 GND → **按下为高电平**。
  - 轮询线程 20ms 读一次，做**软件去抖**（连续 3 次采样一致才确认），检测到上升/下降沿后切换 `alarm_enabled` / `gimbal_enabled` 标志并回写共享状态。
  - 可扩展：第二个开关接另一 GPIO（如 Pin 91），控制云台使能。

### 4.4 系统配置与设备树使能

编译烧录前需确认以下设备树/内核配置（参考 Luckfox wiki：PWM / I2C / UART）：

1. **PWM**：PWM10_1、PWM11_1 在 Pro/Max 上默认可用，开机后检查：
   ```bash
   ls /sys/class/pwm/          # 应看到 pwmchip10、pwmchip11
   ```
2. **I2C2**：确认 `/dev/i2c-2` 存在；可用 `i2cdetect -y 2` 检测 OLED 是否在 0x3C。
3. **UART4**：确认 `/dev/ttyS4`（或对应 tty）存在且未被控制台占用（console 通常在 UART2/ttyS2）。若占用，改用 UART3/UART5，需在 `rv1103g-luckfox-pico.dts` 中使能对应 `&uartN { status = "okay"; ... }`。
4. **GPIO**：开关所用 GPIO92 默认即可作为普通 GPIO 输入，无需改 dts（需在 shell 中 export）。

> 若 `i2cdetect` / `devmem` 工具不在镜像里，可先用 Luckfox 官方 buildroot 镜像自带工具，或交叉编译静态版本放入 `/usr/bin`。

### 4.5 代码集成到现有工程

新增文件（放入 `src/luckfox_rtsp_yolov5/`）：

```
include/system_state.h
include/pantilt.h        src/pantilt.cc
include/voice_alarm.h    src/voice_alarm.cc
include/oled_display.h   src/oled_display.cc
include/switch_input.h   src/switch_input.cc
```

`main.cc` 改动要点：

```cpp
#include "system_state.h"
#include "pantilt.h"
#include "voice_alarm.h"
#include "oled_display.h"
#include "switch_input.h"

int main(...) {
    // …… 现有初始化（模型、ISP、MPI、RTSP）保持不变 ……

    SystemState st;
    system_state_init(&st);

    // 启动扩展线程
    pthread_t t_pan, t_voice, t_oled, t_sw;
    pthread_create(&t_sw,   NULL, switch_thread,   &st);   // 先启开关，尽早建立使能
    pthread_create(&t_pan,  NULL, pantilt_thread,  &st);
    pthread_create(&t_voice,NULL, voice_thread,    &st);
    pthread_create(&t_oled, NULL, oled_thread,     &st);

    while (1) {
        // …… 现有取帧/推理/风险计算逻辑 ……
        // 在算出 crowd 和检测框后，补充两处写入：
        // 1) 风险状态
        ss_set_risk(&st, crowd, riskEngine.features().n,
                    riskEngine.features().density, riskEngine.features().r_gcc);
        // 2) 目标中心（启发式选最近/最危险行人；无目标则 valid=false）
        bool has = false; int tx=0, ty=0;
        // …… 从 od_results 中选出目标，赋值 has/tx/ty ……
        ss_set_target(&st, has, tx, ty);
        // …… 其余画框/编码/RTSP 不变 ……
    }

    // 退出：置 running=false，join 所有线程，销毁状态
    ss_set_running(&st, false);
    pthread_join(t_sw, NULL); pthread_join(t_pan, NULL);
    pthread_join(t_voice, NULL); pthread_join(t_oled, NULL);
    system_state_destroy(&st);
    // …… 现有资源释放保持不变 ……
}
```

- **优先级实现**：可用 `pthread_setschedparam` 设 `SCHED_FIFO`（需要 root，Luckfox 默认 root 可用）。若设失败降级 `SCHED_OTHER` 不影响功能。
- **性能**：PWM / GPIO 用 sysfs 写文件，单次操作 <1ms；OLED 10Hz 写 1KB，I2C 400kHz 下约 20ms/次，对单核影响很小。避免在控制循环里做动态内存分配、避免 printf 高频打印。
- **错误降级**：某模块初始化失败（如 OLED 未接）只打日志，**不阻塞主流水线**；云台/语音初始化失败同理，保证 RTSP 推流始终运行。

---

## 5. 开发路线图（分阶段实现）

> 每个阶段都"可运行、可演示"，避免一次性集成后难以定位问题。建议每阶段同步更新 `docs/` 记录。

| 阶段 | 目标 | 主要工作 | 验证标准 | 预计工时 |
|---|---|---|---|---|
| **阶段 1：舵机云台跟踪** | 让云台跟随行人 | ① 组装云台支架，摄像头固定；② 接线（PWM10/11 + 独立电源）；③ 设备树确认 PWM；④ shell 手动写 duty_cycle 测试舵机角度；⑤ 编写 `pantilt.cc`，先在独立小程序验证"输入像素→舵机角度→转动"；⑥ 集成 `system_state` + 主线程目标选择，云台线程跟踪 | 摄像头对准目标时舵机平滑居中，无抖动；RTSP 画面中目标保持居中；断电重启舵机回中位 | 2~3 天 |
| **阶段 2：OLED 显示** | 实时状态看板 | ① 接线 I2C2；② 编写 SSD1306 驱动并在 shell 用 `i2c-tools` 点亮测试；③ 实现 6×8 字体渲染；④ 集成 `oled_display.cc`，显示风险/人数/角度/使能 | 屏幕每 100ms 更新，显示内容与 RTSP 叠加一致；无花屏 | 1~2 天 |
| **阶段 3：语音告警** | 风险等级语音播报 | ① TF 卡预录 3 段音频；② 接线 UART4；③ shell 用 `echo`/`python` 发 `7E FF 06 0F ... FE` 验证 JQ8900 播放；④ 实现 `voice_alarm.cc` 串口驱动 + 冷却去抖逻辑；⑤ 集成 | 风险跳变时 1s 内播报对应音频，同级不重复、有 10s 冷却；断开使能开关则静音 | 1~2 天 |
| **阶段 4：物理开关** | 硬件总开关 + 功能开关 | ① 总电源开关串接；② 功能开关接线 GPIO；③ 实现 `switch_input.cc` 去抖轮询；④ 集成使能标志 | 总开关断电系统完全关闭；功能开关即时切换告警/云台使能，OLED 同步显示 ON/OFF | 0.5~1 天 |
| **阶段 5：联调与成品化** | 稳定、美观、可答辩 | ① 全系统 8 小时长稳测试（看门狗/内存/日志）；② 外壳装配、走线整理、热缩管保护；③ 录制演示视频 + 拍摄实物照片；④ 撰写论文硬件章节 + 系统框图 | 连续运行不崩溃、不掉流；演示一次"人群聚集→告警+云台跟踪+OLED+RTSP"完整闭环 | 2~3 天 |

**阶段依赖说明**：
- 阶段 1 最先做，因为云台涉及机械装配与电源，风险最高、最能提前暴露电源共地问题。
- 阶段 2、3 相互独立，可与阶段 1 并行准备材料。
- 阶段 4 依赖共享状态结构（`alarm_enabled`/`gimbal_enabled`），建议在阶段 1 就把 `system_state` 建好。
- 每阶段完成后及时把 `SystemState` 字段扩充到位，避免后期重构。

---

## 6. 成本合计、成品装配与预期效果

### 6.1 成本合计

| 类别 | 明细 | 小计 |
|---|---|---|
| 已有资产 | 开发板 + 摄像头 + 2×SG90 | **¥0（已有）** |
| 云台 | SG90 双轴支架套件 | ¥12~25 |
| 语音 | JQ8900 模块 + 3W 喇叭 | ¥9~20 |
| TF 卡 | 8GB（如有可用） | ¥0~25 |
| 显示 | SSD1306 OLED | ¥6~12 |
| 开关电源 | 5V/3A 适配器 + USB 一分二 + 自锁/拨动开关 | ¥13~40 |
| 电源辅料 | MP1584（可选）+ 电容组 + 面包板 + 杜邦线 | ¥10~30 |
| 外壳/固定 | 亚克力/3D 打印 + 铜柱扎带双面胶（可选） | ¥20~60 |
| **合计（不含外壳/TF）** | | **约 ¥50~120** |
| **合计（含外壳 + 新购 TF）** | | **约 ¥90~180** |

> 若方案 B（12V 适配器 + MP1584 隔离舵机轨），额外 +¥10~25。

### 6.2 成品装配与固定建议

1. **云台与摄像头**：
   - Yaw 舵机固定在底板（云台底座）上，底板用双面胶/螺丝固定到外壳底部或支架。
   - Pitch 舵机装在 U 型臂上，臂前端是摄像头安装位。SC3336 模组用 **3M 双面胶 + 扎带**轻质固定，避免金属螺丝过重。
   - FFC 排线沿云台臂走向，**预留松弛量并扎带固定**，防止随动时拉扯；避免排线在舵机转动范围内缠绕。
2. **电路板与电源**：
   - 开发板用铜柱悬空固定于外壳内，下方留散热空间；面包板贴在外壳底部。
   - 所有电源接头处套热缩管；舵机/语音线用扎带分束（电源线一束、信号线一束）。
   - 舵机 5V 轨的 **1000µF 电容焊/插在面包板最靠近舵机接口的位置**，越近效果越好。
3. **外壳开孔**：OLED 露于外壳正面；喇叭开孔朝前；功能开关装侧面便于操作；USB-C 与网口开孔便于供电/调试。
4. **散热**：RV1106 满载时芯片发热，建议在 SoC 上贴小型散热片（如 15×15×5mm 铝散热片 + 导热硅胶），尤其长时间演示时。
5. **线缆标识**：用标签纸给每条线标注（5V/GND/PWM/I2C/UART），便于答辩现场讲解与排查。

### 6.3 预期效果

集成完成后，系统具备完整闭环：

1. **检测与决策**：SC3336 采集画面 → YOLOv5 检出行人 → 复杂网络计算密度/连通分量 → 输出 SAFE / WARNING / DANGER。
2. **云台自动跟踪**：锁定最近/最危险行人，双舵机平滑转动让目标始终位于画面中央；角度实时显示在 OLED 上。
3. **分级语音告警**：风险升级时，JQ8900 立即播报对应等级的语音提示（注意安全/立即疏散）；同级不重复、带冷却。
4. **OLED 实时看板**：显示风险等级、行人数、网络密度、云台角度、告警使能状态，与 RTSP 画面叠加信息一致。
5. **物理开关控制**：总电源开关控制整机上下电；功能开关可一键启停告警/云台，直观演示"硬件可控"。

**演示脚本（答辩推荐）**：
- 单人缓慢走动 → 系统 SAFE，云台跟随，OLED 显示 SAFE；
- 多人靠近聚集 → 等级升至 WARNING → 语音播报"注意安全"；
- 人群密集拥挤 → 等级升至 DANGER → 语音播报"立即疏散" + OLED 红色反白 + 云台锁定人群中心；
- 拨动功能开关 → 语音静音/云台停动，OLED 同步显示 ALARM:OFF，体现硬件控制的实时性。

---

## 附录 A：常用调试命令速查

```bash
# PWM（舵机）手动测试：PWM10 通道 0
echo 0 > /sys/class/pwm/pwmchip10/export
echo 20000000 > /sys/class/pwm/pwmchip10/pwm0/period     # 20ms
echo 1500000  > /sys/class/pwm/pwmchip10/pwm0/duty_cycle  # 90° 中位
echo 1        > /sys/class/pwm/pwmchip10/pwm0/enable

# I2C 检测 OLED（应在 0x3C 出现）
i2cdetect -y 2

# UART 测试 JQ8900（播放第 1 段，9600 8N1）
stty -F /dev/ttyS4 9600 cs8 -cstopb -parenb
printf '\x7e\xff\x06\x0f\x00\x01\x0f\xfe' > /dev/ttyS4

# GPIO 开关测试
echo 92 > /sys/class/gpio/export
echo in  > /sys/class/gpio/gpio92/direction
cat /sys/class/gpio/gpio92/value          # 按下应显示 1

# 查看已使能的串口 / 确保 UART4 未被占用
cat /proc/tty/driver/serial
```

## 附录 B：参考链接

- Luckfox 官方 Wiki（GPIO / PWM / I2C / UART / 设备树）：https://wiki.luckfox.com/
- Luckfox Pico Pro/Max 引脚图（PDF 原理图）：https://files.luckfox.com/wiki/Luckfox-Pico/PDF/Luckfox-Pico-SCH/Luckfox%20Pico%20Pro_Max.pdf
- SSD1306 数据手册与标准初始化序列：https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
- JQ8900-16P 串口控制协议说明：淘宝商品页/厂商文档（串口帧格式见 §4.3.2）
- 微雪 SC3336 摄像头模块产品页：https://www.waveshare.net/shop/SC3336-3MP-Camera-B.htm