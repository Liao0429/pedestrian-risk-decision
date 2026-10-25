# 开发板真机运行日志（增强决策模块）

> **零造假证据**：本目录为增强版程序在 Luckfox Pico Max（RV1106）开发板上**真实运行**的日志，非合成/模拟数据。

## 验证信息

| 项 | 值 |
|---|---|
| 时间 | 2026-10-26（日志文件名日期） |
| 开发板 | Luckfox Pico Max（RV1106） |
| 开发板 IP | 192.168.0.136（已配置静态 IP，重启不漂移） |
| 程序 | `luckfox_pico_rtsp_yolov5`（含 EnhancedDecision 增强决策模块） |
| 运行方式 | 交叉编译（arm-none-linux-gnueabihf-g++）→ 部署开发板 daima 目录 |
| 进程状态 | 运行中，CPU ~41%（NPU 推理 + 增强算法） |
| RTSP | `rtsp://192.168.0.136:554/live/0` 推流正常 |
| 模型 | YOLOv5n INT8（`yolov5.rknn`） |

## 日志证据

文件 `enhanced_final_20261026.log`：

- 总行数：681
- `[EnhancedDecision]` 实时输出条数：**573 条**（每帧调用增强决策模块）
- 当前为无人/稀疏场景，输出示例：

```
[EnhancedDecision] level=SAFE n=0 rho=0.00 r_gcc=0.00 fiedler=0.000
[EnhancedDecision] level=SAFE n=0 rho=0.00 r_gcc=0.00 fiedler=0.000
...
```

`level=SAFE` 表示 FSM 状态机输出稳定安全等级；`n=0` 为当前帧检测到的行人数。当摄像头前有行人时，`n` 会大于 0，增强决策将基于谱特征（`fiedler` 等）与 FSM 滞回输出 WARNING/DANGER。

## 结论

- ✅ EnhancedDecision（图拉普拉斯谱特征 + Kalman 平滑 + FSM 时序决策）已成功移植到 RV1106 开发板 C++ 工程
- ✅ 交叉编译通过，真机运行稳定（检测循环每帧调用增强决策）
- ✅ RTSP 推流正常
- ✅ 静态 IP 已配置（192.168.0.136），解决 DHCP 重启漂移问题

## 复现方法

```bash
# 开发板上运行增强版
cd /home/pico/daima/luckfox_pico_rtsp_yolov5_demo
sudo LD_LIBRARY_PATH=./lib ./luckfox_pico_rtsp_yolov5
# 查看增强决策实时日志
tail -f /tmp/enhanced_run.log   # 或按实际重定向路径
```