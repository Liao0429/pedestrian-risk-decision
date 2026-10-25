# 网页观看监控（免安装软件）

> 手机/电脑浏览器直接打开网页看监控画面，**无需下载任何软件**。
> 通过 go2rtc 把开发板 RTSP 流转成 WebRTC/HLS，浏览器原生播放。

## 访问地址

```
http://192.168.0.136:1984
```

- 打开即进入 go2rtc Web 界面，点击 `cam` 流即可实时观看（WebRTC 低延迟）。
- 手机、电脑、平板浏览器均支持（Chrome/Edge/Safari/Firefox）。

## go2rtc 部署（已在开发板完成）

### 1. 下载（ARM 32 位，v1.9.15）

```bash
# 老电脑（Ubuntu，走代理）下载 arm 版
export https_proxy=http://127.0.0.1:7890 http_proxy=http://127.0.0.1:7890
curl -sL -o /tmp/go2rtc https://github.com/AlexxIT/go2rtc/releases/latest/download/go2rtc_linux_arm
```

### 2. 配置 `/home/pico/go2rtc.yaml`

```yaml
streams:
  cam:
    - rtsp://127.0.0.1:554/live/0   # 开发板本机 RTSP 源
api:
  listen: ":1984"
```

### 3. 启动（开发板）

```bash
chmod +x /home/pico/go2rtc
cd /home/pico
sudo setsid ./go2rtc -config go2rtc.yaml > /tmp/go2rtc.log 2>&1 < /dev/null &
```

### 4. 验证

```bash
ps aux | grep go2rtc                 # 进程存在
ss -tlnp | grep -E '1984|8554|8555' # Web/HLS/WebRTC 端口监听
curl -s http://127.0.0.1:1984/api/streams  # 返回 cam 流
```

## 端口说明

| 端口 | 用途 |
|---|---|
| 1984 | Web 界面 / API |
| 8554 | HLS / RTSP 输出 |
| 8555 | WebRTC（TCP+UDP） |

## 特性

- 浏览器原生播放，无需插件/软件
- WebRTC 低延迟（<1s），HLS 兼容性更好
- go2rtc 按需拉流（有人观看才从开发板 RTSP 拉流，省资源）

## 复现

开发板 RTSP 源（`luckfox_pico_rtsp_yolov5`）需先运行：
```bash
cd /home/pico/daima/luckfox_pico_rtsp_yolov5_demo
sudo LD_LIBRARY_PATH=./lib ./luckfox_pico_rtsp_yolov5
```

## 注

- go2rtc 开源：https://github.com/AlexxIT/go2rtc （MIT）
- 开发板增强版程序需持续运行以提供 RTSP 源（含检测/风险决策叠加画面）。