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
 - rtsp://127.0.0.1:554/live/0 # 开发板本机 RTSP 源
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
ps aux | grep go2rtc # 进程存在
ss -tlnp | grep -E '1984|8554|8555' # Web/HLS/WebRTC 端口监听
curl -s http://127.0.0.1:1984/api/streams # 返回 cam 流
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

## 本地视频流兜底演示（P2-1）

现场镜头前无人走动时，实时画面会一直显示 SAFE，不利于展示 DANGER 切换。
用预录的密集人群增强视频作为 `demo` 流兜底：手机浏览器打开 go2rtc 网页，点击 `demo` 即可循环播放。

### 一键配置（开发板）

```bash
# 1. 把增强视频放到开发板（PC 上执行，目录不存在先 sudo mkdir -p /userdata/demo）
scp results/video_enhanced_fsm/mixkit_13192_enhanced.mp4 \
    pico@<开发板IP>:/userdata/demo/

# 2. 开发板上执行（自动备份 go2rtc.yaml、添加 demo 流、重启 go2rtc、验证 API）
sudo bash /home/pico/daima/luckfox_pico_rtsp_yolov5_demo/scripts/setup_go2rtc_demo_stream.sh \
    --video /userdata/demo/mixkit_13192_enhanced.mp4
```

- 脚本 `scripts/setup_go2rtc_demo_stream.sh` 仅依赖 bash + awk（板端无需 python3）；
  支持 `--dry-run` 预览、`--stream <名>` 自定义流名、`GO2RTC_CFG`/`GO2RTC_BIN` 环境变量覆盖路径。
- 配置编辑逻辑（新增保留 cam / 幂等 / 更新替换 / 损坏重建 / 自动备份）由
  `tests/test_go2rtc_demo_config.py` 覆盖（14 项主机测试 + 4 项 shell 测试在 Linux/CI 执行）。

### 配置效果

```yaml
streams:
  cam:
  - rtsp://127.0.0.1:554/live/0                          # 实时摄像头
  demo:
  - file:///userdata/demo/mixkit_13192_enhanced.mp4     # 预录增强视频（循环播放）
api:
  listen: ":1984"
```

### 演示方法

- 浏览器打开 `http://<开发板IP>:1984`，点击 `demo` 流播放预录视频（DANGER 切换）；
  点击 `cam` 流切回实时画面。
- 也可用 VLC 直接打开 `http://<开发板IP>:1984/api/stream.mjpeg?src=demo`。

### 板端验证状态

⬜ **待开发板上线后执行**：上传 `mixkit_13192_enhanced.mp4` → 运行一键脚本 →
手机浏览器真机验证 `demo` 流可播放（P2-1 收尾）。

## 注

- go2rtc 开源：https://github.com/AlexxIT/go2rtc （MIT）
- 开发板增强版程序需持续运行以提供 RTSP 源（含检测/风险决策叠加画面）。