# 答辩现场无线连接方案（开发板自动连手机热点）

> **目标**：答辩现场无网线、无法进入后台命令行时，开发板上电后**自动连接手机热点**，浏览器直接看监控。

## 核心思路

1. **开发板无板载 WiFi** → 需 **USB WiFi 模块**（推荐 RTL8723BU，Linux 内核免驱，USB 即插即用）。
2. **提前一次性配置好手机热点连接**（NetworkManager 保存连接）→ 之后每次上电**自动连接**，现场零操作。
3. **屏幕菜单显示当前 IP**（硬件扩展方案已包含）→ 现场从屏幕看到地址，手机/电脑直接访问。
4. 备用：手机热点管理界面查看已连接设备 IP。

## 硬件要求

- USB WiFi 模块（RTL8723BU，约 20-30 元）
- 开发板 USB 口配成 **HOST 模式**（`luckfox-config`），插上模块
- 手机开启热点（固定 SSID/密码）

## 配置步骤（只需做一次）

### 1. 确认 WiFi 模块被识别

```bash
lsusb                      # 应看到 RTL8723BU
ip link                    # 应出现 wlan0
```

### 2. 用 NetworkManager 添加热点连接（开发板上执行一次）

```bash
# 开发板用 nmcli 添加 WiFi 连接（假设热点 SSID=MyHotspot，密码=12345678）
sudo nmcli dev wifi connect "MyHotspot" password "12345678"
```

连接成功后，NetworkManager 会把该连接**保存为系统连接**，之后开机自动连接。

### 3. 验证自动连接

```bash
sudo reboot                # 重启后应自动连上热点
ip -br addr show wlan0     # 确认 wlan0 拿到 IP
```

### 4. （可选）设置开机自启网络

NetworkManager 默认自动连接已保存的 WiFi；确认连接是 `autoconnect yes`：

```bash
nmcli -g connection.autoconnect con show "MyHotspot"
# 若为 no，执行：
sudo nmcli con mod "MyHotspot" connection.autoconnect yes
```

## 现场使用流程（零命令行）

1. 手机开热点（SSID/密码与配置时一致）。
2. 开发板上电 → 数秒后自动连上热点。
3. **从屏幕菜单读取开发板 IP**（如 192.168.43.xxx）。
4. 手机/电脑连同一热点，浏览器打开 `http://<开发板IP>:1984` 直接看监控。
5. 如需 RTSP：`rtsp://<开发板IP>:554/live/0`。

## 如何知道开发板 IP（现场不敲命令）

- **屏幕菜单参数栏显示 IP**（硬件方案 docs/hardware/HARDWARE_EXPANSION_PLAN.md 已含）。
- 手机热点管理界面（设置 → 个人热点 → 已连接设备）也能看到开发板 IP。

## 注意事项

- 手机热点与开发板必须同一网段；热点 IP 一般 192.168.43.x（Android）或 192.168.137.x（Windows）。
- 若现场换热点 SSID/密码：**无法现场改**，必须在答辩前用新热点重新配一次并保存。
- 备选：开发板保留网线接口，若现场恰好有网线可临时插入（IP 192.168.0.136）。

## 相关

- 硬件方案：`docs/hardware/HARDWARE_EXPANSION_PLAN.md`
- 网页观看：`docs/WEB_VIEWER.md`（go2rtc，http://IP:1984）