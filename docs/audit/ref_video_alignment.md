# task-6 参考视频对齐三项：ROI 配置色 / Dangerous Person / Clusters（上板验证）

- 执行人：board-perf（teammate）
- 日期：2026-10-30 23:52 ~ 00:20 CST
- 写 scope：src/luckfox_rtsp_yolov5/main.cc（+ 发现回归后按 lead 批准改 luckfox_mpi.cc 一行）、docs/review/20261030_round3/ref_video_alignment.md
- 真机：192.168.0.136（pico）；构建机：192.168.0.63（~/ped_build_bp）

---

## 1. 三项改动（main.cc，只动这一个文件的业务改动）

| # | 需求 | 实现 | 位置 |
|---|---|---|---|
| 1 | ROI 折线颜色读 riskCfg.roi.color | cv::polylines 颜色改为 cv::Scalar(riskCfg.roi.color_b, riskCfg.roi.color_g, riskCfg.roi.color_r)，线宽仍 riskCfg.roi.thickness | main.cc ROI 绘制段（原硬编码 cv::Scalar(255,255,0)） |
| 2 | OSD 增加 Dangerous Person:N | 新增帧级计数 dangerPersons，检测框中心点落入 ROI 内时 +1；OSD 第三行显示 osd_red | main.cc 绘制段 + OSD 段 |
| 3 | Assembly 显示 DBSCAN 簇计数（两者并列） | 帧内对 IPM 地面脚点跑 crowd_dbscan.h（eps=riskCfg.edge_dist_m, min_samples=2，与 Python 默认一致），OSD 第四行显示 Clusters: K  Assembly: xx% | main.cc 决策段 + OSD 段 |

- DBSCAN 只用于 OSD 显示，不参与 FSM/等级决策（不改算法结果）。
- crowd_dbscan.h 的类在全局命名空间（无 namespace），调用处已按此编写。

---

## 2. 交叉编译产物

在 192.168.0.63 ~/ped_build_bp（board_sdk CMake，-O2 -mfpu=neon-vfpv4 -DRV1106_1103）：

    task-6 版（三项改动，RK_FMT_RGB888）: 5,118,456 B  md5 da738bcd0154af5118d240d0c8f94762
    task-6 终版（加 RK_FMT_BGR888 颜色回归修复）: 5,118,456 B  md5 d19f88f2c921d05307c1e62d05178288

---

## 3. 抓帧证据（部署 da738bcd 后，Windows OpenCV 拉 RTSP）

    opened: True；60 帧 / 6.2s；截图：
    tmp/board_snap/r6_frame10.png ~ r6_frame60.png
    tmp/board_snap/r6_last.png

### 3.1 颜色读数（Python 像素分析，非肉眼）

da738bcd 版（修复前）读数：

| 对象 | main.cc 绘制的 BGR | RTSP 流实测 BGR(分桶) | 结论 |
|---|---|---|---|
| ROI 折线（board 配置 color=[0,255,255]） | (0,255,255) | (224,224,0) | 画黄显青 = R/B 互换 |
| OSD Dangerous Person（osd_red） | (0,0,255) | (160~192, 0~64, 0~64) | 画红显蓝 = R/B 互换 |
| OSD Clusters/Assembly（osd_yellow） | (0,255,255) | (192,192,32~64) | 黄→青，同样互换 |
| OSD Num of Person（osd_green） | (0,255,0) | 绿色（对称色不受影响） | 正常 |

### 3.2 由此发现并修复的回归（已报 lead 并获批准）

- 根因：帧缓冲是 OpenCV BGR 字节序 (B,G,R)，VENC 配的是 RK_FMT_RGB888（按 (R,G,B) 解释）→ 整条 RTSP 流 R/B 互换（场景与叠加层都换）。
- 历史未暴露的原因：旧链路 cvtColor(COLOR_YUV420sp2BGR=NV21 别名) 本身就 R/B 互换，与 VENC 的互换互相抵消；task-1 把 NV12/NV21 语义修正确后，VENC 这个错误就单独暴露。
- 修复：src/luckfox_rtsp_yolov5/src/luckfox_mpi.cc venc_init 一行，RK_FMT_RGB888 → RK_FMT_BGR888（board_sdk/include/rk_comm_video.h:156 确认枚举存在）。
- 修复后预期：流内场景颜色正确；OpenCV 绘制的颜色所见即所得（蓝框=蓝、红 ROI=红、红字=红、绿=绿）。

---

## 4. 配置真源（lead 裁决）

- 以仓库 config/default_config.json 为唯一真源：roi_zone.color = [0,0,255]（BGR 语义 → 红色）。
- 板上历史配置是 [0,255,255]（会显示黄色）。部署终版时会把仓库 config 一并 scp 到板上，使其显示红色（与参考视频一致）。

---

## 5. [PERF] 帧率证据（da738bcd 版，无 BGR888 修复）

    [PERF] fps=7.69 frames=8 persons=0 level=SAFE vi=0.5 prep=251.2 infer=731.6 decide=0.2 draw=24.1 enc=32.4 | per-frame ms: vi=0.07 prep=31.41 infer=91.45 decide=0.02 draw=3.02 enc=4.05
    [PERF] fps=7.71 frames=8 persons=0 level=SAFE vi=0.5 prep=248.9 infer=730.6 decide=0.2 draw=24.7 enc=32.4 | per-frame ms: vi=0.07 prep=31.11 infer=91.33 decide=0.02 draw=3.09 enc=4.05
    （随后抓帧期间出现与 T1 A/B 基线同样模式的周期性下探 5.83~6.52，prep 48~66ms，非本轮引入）

- 结论：改动后稳态 fps 7.69~7.71 ≥ 基线 7.52，未回退；draw 由 2.4~3.0 升到 3.0~3.1（新增一行 OSD + DBSCAN，约 +0.1~0.6ms，可接受）。
- DBSCAN 簇计数在 n 为 0~1 时 decide 仍 0.02~0.05ms（无实质开销）。

---

## 6. 终版部署状态与待办（截至 00:20 被板侧 SSH 中断）

- 已部署运行：da738bcd（三项改动，无 BGR888 修复）。
- 已编译就绪（构建机 /home/liao/bp_r6）：d19f88f2c921d05307c1e62d05178288（含 BGR888 修复）。
- 待板恢复 SSH 后需完成的终验清单：
    1. scp d19f88f2 到板上 + scp 仓库 config/default_config.json（红色 ROI）；
    2. 重启 pedestrian-risk，确认 active、NRestarts=0、md5 与 bp_r6 一致；
    3. 重新抓帧 tmp/board_snap/final_*.png，逐项读像素：ROI 红 (0,0,255) 附近、Dangerous Person 红、Clusters/Assembly 黄、检测框蓝；
    4. grep [PERF] 确认 fps ≥ 7.52；
    5. 把以上终验结果回填本文第 7 节并发送给 lead。

---

## 7. 终验回填（2026-10-31 01:38，板恢复后完成）

- 部署：md5 d19f88f2c921d05307c1e62d05178288（与构建机 /home/liao/bp_r6 一致）；仓库 config
  （roi_zone.color [0,0,255]）已同步到板，板上 config md5 5c79a22aea44783c32a63ca5438237c8。
- 服务：active；NRestarts=0；MainPID=1451；观察期无新 core dump（最新 core 仍是 10:39 的历史文件，38 个仍在，未清理）。
- 抓帧：tmp/board_snap/r6_frame10..60.png + tmp/board_snap/final_last.png（60 帧 / 6.3s）。

逐像素读数（OpenCV 读 PNG 后按 32 分桶统计高频色，非肉眼）：

| 对象 | 实测 BGR（分桶） | 判定 |
|---|---|---|
| ROI 边框 | (B=0, G=0, R=224) | 红色 ✓（与仓库 config [0,0,255] 一致） |
| OSD 第三行 Dangerous Person | (B=0~32, G=0~32, R=160~192) | 红色 ✓ |
| OSD 第四行 Clusters/Assembly | (B=0, G=160~192, R=128~160) | 黄色 ✓ |
| OSD 第一/二行 | 绿色 | ✓ |
| 全帧红色像素数 | 3792（修复前为 0） | R/B 互换回归确认消除 |

- 蓝框：本轮抓帧时画面内无 person 检测框，未取得蓝框样本（**未验证**）；颜色链路已由 ROI/OSD 三处独立读数证明通道正确。

[PERF] 原文（稳态，02:41 前采样）：

    [PERF] fps=7.74 frames=8 persons=1 level=SAFE vi=0.5 prep=249.2 infer=714.9 decide=1.2 draw=31.9 enc=36.2 | per-frame ms: vi=0.06 prep=31.15 infer=89.36 decide=0.15 draw=3.99 enc=4.53
    [PERF] fps=7.85 frames=8 persons=0 level=SAFE vi=0.5 prep=245.7 infer=717.1 decide=0.2 draw=24.1 enc=31.6 | per-frame ms: vi=0.06 prep=30.71 infer=89.63 decide=0.02 draw=3.01 enc=3.95
    [PERF] fps=7.78 frames=8 persons=0 level=SAFE vi=0.5 prep=253.8 infer=717.1 decide=0.2 draw=25.1 enc=31.3 | per-frame ms: vi=0.06 prep=31.73 infer=89.64 decide=0.02 draw=3.14 enc=3.91
    （fps 7.74~7.85 ≥ 基线 7.52；周期性下探 5.43~6.94 / prep 40~72ms 与 T1 的 A/B 基线同模式，非本次引入）

- 结论：三项对齐功能（ROI 配置色 / Dangerous Person / Clusters+Assembly）与 R/B 颜色回归修复**均已上板验证**。

## 8. 板侧异常记录（需 lead 处理）

- 00:00 起板子 SSH 握手不出 banner（paramiko 报 No existing session / Error reading SSH protocol banner WinError 10038），Windows OpenSSH 同样失败；但 ping 通、TCP 22/554/8554 全开、RTSP VideoCapture 打开失败。
- 判断：OS 级 sshd/服务卡死，可能与 17MB 可用内存、38 个 core 文件（约 546MB）、服务反复重启有关，也可能是我高频 paramiko 连接触发 sshd 限制。
- 已请 lead 断电重启板子；恢复后由我完成第 7 节终验。
