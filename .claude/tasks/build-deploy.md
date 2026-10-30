## 任务：排查并修复毕设项目 PedestrianRiskDecision 的编译部署问题

### 当前状态
- 本地仓库：G:\PedestrianRiskDecision，分支 358a36f（基准版本）
- 已修改的文件（待部署）：
  - `src/luckfox_rtsp_yolov5/src/main.cc`（横幅 30px + target_classes 过滤 + 中文叠加）
  - `src/luckfox_rtsp_yolov5/include/chinese_font.h`（24x24 点阵字库，10 个汉字）
- 开发板：192.168.0.136（pico/luckfox），当前运行 fa7f2384（旧版，不含中文叠加）
- 交叉编译机：192.168.0.63（liao/123456，Ubuntu arm-linux-gnueabihf-g++ 11.4.0）

### 问题
1. 老电脑 (192.168.0.63) 上的交叉编译 cmake 缓存指向 /home/liao/board_sdk，不认 ped_build 下的源文件
2. 多次尝试重编，产物 md5 始终不变（fa7f2384）
3. 正确源码在 /home/liao/ped_build_284/src/ 和 /home/liao/ped_build_284/include/

### 需要你做
1. SSH 登录老电脑 (192.168.0.63) 检查编译环境
2. 找到正确的编译方式（283/284/ped_build_old 哪个能用）
3. 确保 main.cc 和 chinese_font.h 被编译进新的二进制
4. 把新二进制部署到开发板 192.168.0.136
5. 验证：拉取 RTSP 帧，横幅应该有中文（""安全""/""警告""/""危险""显示在横幅右端）、检测框只画 person

### 可用凭据
- SSH 老电脑：liao/123456
- SSH 开发板：pico/luckfox
- 交叉编译器路径：/usr/bin/arm-linux-gnueabihf-g++
- SDK 路径：/home/liao/board_sdk
- 服务管理：systemctl stop/start pedestrian-risk