# 行人/人群视频数据集（videos50）

## 概述

- **视频总数**：111 条
- **内容**：真实拍摄的行人走动、人群聚集、市场/商场人流、街道行人等场景
- **来源**：Mixkit 免费库存视频（mixkit.co，免注册、免费使用授权）
- **格式**：MP4（H.264），分辨率 640×360（360p）
- **单条大小**：0.33 ~ 3.50 MB（全部 < 20 MB）
- **单条时长**：3.9 ~ 50.0 秒
- **切割/转码**：均为 Mixkit 原始片段直链下载，未做二次切割；已由 Mixkit 提供 360p 转码版本
- **校验方式**：`ffprobe` 逐条验证视频头可打开（111/111 通过）；OpenCV HOG 行人检测抽样帧验证（111/111 均检出人形）

## 来源说明

所有视频来自 Mixkit 的免费库存视频分类页（无需注册/登录，直链下载）：
- **crowd**（人群）：`https://mixkit.co/free-stock-video/crowd/` → 本数据集 23 条，人群聚集
- **people**（人物）：`https://mixkit.co/free-stock-video/people/` → 本数据集 19 条，人物特写/多人
- **walking**（行走）：`https://mixkit.co/free-stock-video/walking/` → 本数据集 19 条，行人走动
- **market**（市场）：`https://mixkit.co/free-stock-video/market/` → 本数据集 23 条，市场人群
- **shopping**（购物）：`https://mixkit.co/free-stock-video/shopping/` → 本数据集 10 条，商场/购物人群
- **street**（街道）：`https://mixkit.co/free-stock-video/street/` → 本数据集 17 条，街道行人

每一条的原始直链格式为 `https://assets.mixkit.co/videos/<id>/<id>-360.mp4`（360p 版本，H.264）。

## 文件明细

| 文件名 | 视频ID | 分类 | 来源URL | 分辨率 | 时长(s) | 大小(MB) | 是否片段化 |
|---|---|---|---|---|---|---|---|
| mixkit_13192.mp4 | 13192 | crowd | https://assets.mixkit.co/videos/13192/13192-360.mp4 | 640x360 | 12.72 | 0.79 | 否（原始片段） |
| mixkit_13265.mp4 | 13265 | crowd | https://assets.mixkit.co/videos/13265/13265-360.mp4 | 640x360 | 11.24 | 0.86 | 否（原始片段） |
| mixkit_134.mp4 | 134 | walking | https://assets.mixkit.co/videos/134/134-360.mp4 | 640x360 | 25.02 | 1.47 | 否（原始片段） |
| mixkit_14084.mp4 | 14084 | crowd | https://assets.mixkit.co/videos/14084/14084-360.mp4 | 640x360 | 10.2 | 0.76 | 否（原始片段） |
| mixkit_151.mp4 | 151 | people | https://assets.mixkit.co/videos/151/151-360.mp4 | 640x360 | 14.01 | 0.79 | 否（原始片段） |
| mixkit_15914.mp4 | 15914 | shopping | https://assets.mixkit.co/videos/15914/15914-360.mp4 | 640x360 | 11.76 | 0.71 | 否（原始片段） |
| mixkit_15997.mp4 | 15997 | crowd | https://assets.mixkit.co/videos/15997/15997-360.mp4 | 640x360 | 23.76 | 1.8 | 否（原始片段） |
| mixkit_17630.mp4 | 17630 | crowd | https://assets.mixkit.co/videos/17630/17630-360.mp4 | 640x360 | 11.28 | 0.84 | 否（原始片段） |
| mixkit_18264.mp4 | 18264 | market | https://assets.mixkit.co/videos/18264/18264-360.mp4 | 640x360 | 10.0 | 0.7 | 否（原始片段） |
| mixkit_18272.mp4 | 18272 | walking | https://assets.mixkit.co/videos/18272/18272-360.mp4 | 640x360 | 19.44 | 1.5 | 否（原始片段） |
| mixkit_18305.mp4 | 18305 | market | https://assets.mixkit.co/videos/18305/18305-360.mp4 | 640x360 | 15.97 | 1.14 | 否（原始片段） |
| mixkit_21326.mp4 | 21326 | shopping | https://assets.mixkit.co/videos/21326/21326-360.mp4 | 640x360 | 12.04 | 0.84 | 否（原始片段） |
| mixkit_221.mp4 | 221 | people | https://assets.mixkit.co/videos/221/221-360.mp4 | 640x360 | 20.1 | 1.22 | 否（原始片段） |
| mixkit_22427.mp4 | 22427 | crowd | https://assets.mixkit.co/videos/22427/22427-360.mp4 | 640x360 | 9.04 | 0.54 | 否（原始片段） |
| mixkit_23168.mp4 | 23168 | market | https://assets.mixkit.co/videos/23168/23168-360.mp4 | 640x360 | 7.2 | 0.53 | 否（原始片段） |
| mixkit_23327.mp4 | 23327 | shopping | https://assets.mixkit.co/videos/23327/23327-360.mp4 | 640x360 | 9.88 | 0.71 | 否（原始片段） |
| mixkit_23410.mp4 | 23410 | walking | https://assets.mixkit.co/videos/23410/23410-360.mp4 | 640x360 | 28.12 | 2.11 | 否（原始片段） |
| mixkit_23818.mp4 | 23818 | walking | https://assets.mixkit.co/videos/23818/23818-360.mp4 | 640x360 | 8.68 | 0.64 | 否（原始片段） |
| mixkit_241.mp4 | 241 | people | https://assets.mixkit.co/videos/241/241-360.mp4 | 640x360 | 15.81 | 0.85 | 否（原始片段） |
| mixkit_25516.mp4 | 25516 | shopping | https://assets.mixkit.co/videos/25516/25516-360.mp4 | 640x360 | 7.41 | 0.58 | 否（原始片段） |
| mixkit_25890.mp4 | 25890 | walking | https://assets.mixkit.co/videos/25890/25890-360.mp4 | 640x360 | 10.31 | 0.81 | 否（原始片段） |
| mixkit_28616.mp4 | 28616 | crowd | https://assets.mixkit.co/videos/28616/28616-360.mp4 | 640x360 | 6.08 | 0.54 | 否（原始片段） |
| mixkit_31086.mp4 | 31086 | shopping | https://assets.mixkit.co/videos/31086/31086-360.mp4 | 640x360 | 8.08 | 0.65 | 否（原始片段） |
| mixkit_32622.mp4 | 32622 | walking | https://assets.mixkit.co/videos/32622/32622-360.mp4 | 640x360 | 11.71 | 0.73 | 否（原始片段） |
| mixkit_3428.mp4 | 3428 | people | https://assets.mixkit.co/videos/3428/3428-360.mp4 | 640x360 | 19.02 | 1.44 | 否（原始片段） |
| mixkit_3429.mp4 | 3429 | street | https://assets.mixkit.co/videos/3429/3429-360.mp4 | 640x360 | 10.26 | 0.79 | 否（原始片段） |
| mixkit_3469.mp4 | 3469 | people | https://assets.mixkit.co/videos/3469/3469-360.mp4 | 640x360 | 12.29 | 0.93 | 否（原始片段） |
| mixkit_39767.mp4 | 39767 | walking | https://assets.mixkit.co/videos/39767/39767-360.mp4 | 360x640 | 10.39 | 0.73 | 否（原始片段） |
| mixkit_4000.mp4 | 4000 | people | https://assets.mixkit.co/videos/4000/4000-360.mp4 | 640x360 | 13.37 | 1.01 | 否（原始片段） |
| mixkit_4026.mp4 | 4026 | crowd | https://assets.mixkit.co/videos/4026/4026-360.mp4 | 640x360 | 9.48 | 0.81 | 否（原始片段） |
| mixkit_40656.mp4 | 40656 | walking | https://assets.mixkit.co/videos/40656/40656-360.mp4 | 360x640 | 12.01 | 0.91 | 否（原始片段） |
| mixkit_4067.mp4 | 4067 | street | https://assets.mixkit.co/videos/4067/4067-360.mp4 | 640x360 | 6.21 | 0.54 | 否（原始片段） |
| mixkit_4108.mp4 | 4108 | street | https://assets.mixkit.co/videos/4108/4108-360.mp4 | 640x360 | 17.56 | 1.11 | 否（原始片段） |
| mixkit_4147.mp4 | 4147 | market | https://assets.mixkit.co/videos/4147/4147-360.mp4 | 640x360 | 19.62 | 1.49 | 否（原始片段） |
| mixkit_41537.mp4 | 41537 | street | https://assets.mixkit.co/videos/41537/41537-360.mp4 | 640x360 | 28.99 | 2.05 | 否（原始片段） |
| mixkit_4154.mp4 | 4154 | crowd | https://assets.mixkit.co/videos/4154/4154-360.mp4 | 640x360 | 6.25 | 0.55 | 否（原始片段） |
| mixkit_4157.mp4 | 4157 | people | https://assets.mixkit.co/videos/4157/4157-360.mp4 | 640x360 | 19.72 | 1.59 | 否（原始片段） |
| mixkit_41574.mp4 | 41574 | street | https://assets.mixkit.co/videos/41574/41574-360.mp4 | 640x360 | 15.02 | 1.13 | 否（原始片段） |
| mixkit_41576.mp4 | 41576 | street | https://assets.mixkit.co/videos/41576/41576-360.mp4 | 640x360 | 50.05 | 3.5 | 否（原始片段） |
| mixkit_41581.mp4 | 41581 | walking | https://assets.mixkit.co/videos/41581/41581-360.mp4 | 640x360 | 13.1 | 1.0 | 否（原始片段） |
| mixkit_4178.mp4 | 4178 | shopping | https://assets.mixkit.co/videos/4178/4178-360.mp4 | 640x360 | 9.93 | 0.86 | 否（原始片段） |
| mixkit_4180.mp4 | 4180 | crowd | https://assets.mixkit.co/videos/4180/4180-360.mp4 | 640x360 | 9.27 | 0.78 | 否（原始片段） |
| mixkit_4185.mp4 | 4185 | crowd | https://assets.mixkit.co/videos/4185/4185-360.mp4 | 640x360 | 5.2 | 0.51 | 否（原始片段） |
| mixkit_4186.mp4 | 4186 | crowd | https://assets.mixkit.co/videos/4186/4186-360.mp4 | 640x360 | 3.9 | 0.34 | 否（原始片段） |
| mixkit_4188.mp4 | 4188 | crowd | https://assets.mixkit.co/videos/4188/4188-360.mp4 | 640x360 | 19.53 | 1.47 | 否（原始片段） |
| mixkit_42121.mp4 | 42121 | market | https://assets.mixkit.co/videos/42121/42121-360.mp4 | 640x360 | 9.01 | 0.64 | 否（原始片段） |
| mixkit_42123.mp4 | 42123 | market | https://assets.mixkit.co/videos/42123/42123-360.mp4 | 640x360 | 14.97 | 1.02 | 否（原始片段） |
| mixkit_42130.mp4 | 42130 | market | https://assets.mixkit.co/videos/42130/42130-360.mp4 | 640x360 | 10.93 | 0.83 | 否（原始片段） |
| mixkit_42133.mp4 | 42133 | market | https://assets.mixkit.co/videos/42133/42133-360.mp4 | 360x640 | 10.55 | 0.84 | 否（原始片段） |
| mixkit_42135.mp4 | 42135 | market | https://assets.mixkit.co/videos/42135/42135-360.mp4 | 360x640 | 8.05 | 0.62 | 否（原始片段） |
| mixkit_42136.mp4 | 42136 | market | https://assets.mixkit.co/videos/42136/42136-360.mp4 | 360x640 | 9.68 | 0.75 | 否（原始片段） |
| mixkit_42137.mp4 | 42137 | market | https://assets.mixkit.co/videos/42137/42137-360.mp4 | 360x640 | 13.56 | 0.95 | 否（原始片段） |
| mixkit_4231.mp4 | 4231 | street | https://assets.mixkit.co/videos/4231/4231-360.mp4 | 640x360 | 18.96 | 1.42 | 否（原始片段） |
| mixkit_4269.mp4 | 4269 | crowd | https://assets.mixkit.co/videos/4269/4269-360.mp4 | 640x360 | 7.0 | 0.56 | 否（原始片段） |
| mixkit_43150.mp4 | 43150 | walking | https://assets.mixkit.co/videos/43150/43150-360.mp4 | 640x360 | 10.26 | 0.79 | 否（原始片段） |
| mixkit_4331.mp4 | 4331 | people | https://assets.mixkit.co/videos/4331/4331-360.mp4 | 640x360 | 14.9 | 1.13 | 否（原始片段） |
| mixkit_4332.mp4 | 4332 | street | https://assets.mixkit.co/videos/4332/4332-360.mp4 | 640x360 | 19.87 | 1.5 | 否（原始片段） |
| mixkit_4344.mp4 | 4344 | crowd | https://assets.mixkit.co/videos/4344/4344-360.mp4 | 640x360 | 12.93 | 1.07 | 否（原始片段） |
| mixkit_4348.mp4 | 4348 | street | https://assets.mixkit.co/videos/4348/4348-360.mp4 | 640x360 | 22.12 | 1.75 | 否（原始片段） |
| mixkit_4384.mp4 | 4384 | crowd | https://assets.mixkit.co/videos/4384/4384-360.mp4 | 640x360 | 6.17 | 0.55 | 否（原始片段） |
| mixkit_4390.mp4 | 4390 | crowd | https://assets.mixkit.co/videos/4390/4390-360.mp4 | 640x360 | 5.0 | 0.49 | 否（原始片段） |
| mixkit_4401.mp4 | 4401 | crowd | https://assets.mixkit.co/videos/4401/4401-360.mp4 | 640x360 | 19.8 | 1.58 | 否（原始片段） |
| mixkit_4437.mp4 | 4437 | crowd | https://assets.mixkit.co/videos/4437/4437-360.mp4 | 640x360 | 37.48 | 2.71 | 否（原始片段） |
| mixkit_4450.mp4 | 4450 | crowd | https://assets.mixkit.co/videos/4450/4450-360.mp4 | 640x360 | 20.3 | 1.52 | 否（原始片段） |
| mixkit_4451.mp4 | 4451 | people | https://assets.mixkit.co/videos/4451/4451-360.mp4 | 640x360 | 22.36 | 1.76 | 否（原始片段） |
| mixkit_4519.mp4 | 4519 | people | https://assets.mixkit.co/videos/4519/4519-360.mp4 | 640x360 | 20.1 | 1.38 | 否（原始片段） |
| mixkit_4530.mp4 | 4530 | people | https://assets.mixkit.co/videos/4530/4530-360.mp4 | 640x360 | 17.97 | 1.19 | 否（原始片段） |
| mixkit_4547.mp4 | 4547 | people | https://assets.mixkit.co/videos/4547/4547-360.mp4 | 640x360 | 28.27 | 1.92 | 否（原始片段） |
| mixkit_4600.mp4 | 4600 | street | https://assets.mixkit.co/videos/4600/4600-360.mp4 | 640x360 | 11.36 | 0.7 | 否（原始片段） |
| mixkit_4629.mp4 | 4629 | people | https://assets.mixkit.co/videos/4629/4629-360.mp4 | 640x360 | 7.08 | 0.5 | 否（原始片段） |
| mixkit_4633.mp4 | 4633 | street | https://assets.mixkit.co/videos/4633/4633-360.mp4 | 640x360 | 22.08 | 1.6 | 否（原始片段） |
| mixkit_4642.mp4 | 4642 | walking | https://assets.mixkit.co/videos/4642/4642-360.mp4 | 640x360 | 18.4 | 1.21 | 否（原始片段） |
| mixkit_4653.mp4 | 4653 | market | https://assets.mixkit.co/videos/4653/4653-360.mp4 | 640x360 | 6.04 | 0.33 | 否（原始片段） |
| mixkit_4661.mp4 | 4661 | walking | https://assets.mixkit.co/videos/4661/4661-360.mp4 | 640x360 | 9.2 | 0.66 | 否（原始片段） |
| mixkit_46788.mp4 | 46788 | walking | https://assets.mixkit.co/videos/46788/46788-360.mp4 | 640x360 | 13.76 | 1.08 | 否（原始片段） |
| mixkit_47012.mp4 | 47012 | market | https://assets.mixkit.co/videos/47012/47012-360.mp4 | 640x360 | 18.24 | 1.35 | 否（原始片段） |
| mixkit_47213.mp4 | 47213 | market | https://assets.mixkit.co/videos/47213/47213-360.mp4 | 640x360 | 30.3 | 2.29 | 否（原始片段） |
| mixkit_4742.mp4 | 4742 | market | https://assets.mixkit.co/videos/4742/4742-360.mp4 | 640x360 | 9.98 | 0.66 | 否（原始片段） |
| mixkit_47673.mp4 | 47673 | walking | https://assets.mixkit.co/videos/47673/47673-360.mp4 | 640x360 | 17.4 | 1.3 | 否（原始片段） |
| mixkit_47792.mp4 | 47792 | market | https://assets.mixkit.co/videos/47792/47792-360.mp4 | 640x360 | 17.0 | 1.17 | 否（原始片段） |
| mixkit_4800.mp4 | 4800 | people | https://assets.mixkit.co/videos/4800/4800-360.mp4 | 640x360 | 18.31 | 1.17 | 否（原始片段） |
| mixkit_4823.mp4 | 4823 | walking | https://assets.mixkit.co/videos/4823/4823-360.mp4 | 640x360 | 10.31 | 0.71 | 否（原始片段） |
| mixkit_4832.mp4 | 4832 | walking | https://assets.mixkit.co/videos/4832/4832-360.mp4 | 640x360 | 10.39 | 0.63 | 否（原始片段） |
| mixkit_4840.mp4 | 4840 | people | https://assets.mixkit.co/videos/4840/4840-360.mp4 | 640x360 | 15.35 | 0.95 | 否（原始片段） |
| mixkit_48507.mp4 | 48507 | crowd | https://assets.mixkit.co/videos/48507/48507-360.mp4 | 640x360 | 25.84 | 2.08 | 否（原始片段） |
| mixkit_48509.mp4 | 48509 | crowd | https://assets.mixkit.co/videos/48509/48509-360.mp4 | 640x360 | 13.32 | 1.05 | 否（原始片段） |
| mixkit_48510.mp4 | 48510 | crowd | https://assets.mixkit.co/videos/48510/48510-360.mp4 | 640x360 | 10.96 | 0.93 | 否（原始片段） |
| mixkit_4855.mp4 | 4855 | walking | https://assets.mixkit.co/videos/4855/4855-360.mp4 | 640x360 | 11.88 | 0.87 | 否（原始片段） |
| mixkit_4856.mp4 | 4856 | walking | https://assets.mixkit.co/videos/4856/4856-360.mp4 | 640x360 | 13.75 | 1.03 | 否（原始片段） |
| mixkit_4893.mp4 | 4893 | walking | https://assets.mixkit.co/videos/4893/4893-360.mp4 | 640x360 | 5.37 | 0.4 | 否（原始片段） |
| mixkit_49137.mp4 | 49137 | market | https://assets.mixkit.co/videos/49137/49137-360.mp4 | 640x360 | 23.8 | 1.62 | 否（原始片段） |
| mixkit_4915.mp4 | 4915 | people | https://assets.mixkit.co/videos/4915/4915-360.mp4 | 640x360 | 15.0 | 0.97 | 否（原始片段） |
| mixkit_4948.mp4 | 4948 | people | https://assets.mixkit.co/videos/4948/4948-360.mp4 | 640x360 | 10.3 | 0.62 | 否（原始片段） |
| mixkit_49845.mp4 | 49845 | street | https://assets.mixkit.co/videos/49845/49845-360.mp4 | 640x360 | 20.9 | 1.59 | 否（原始片段） |
| mixkit_49846.mp4 | 49846 | street | https://assets.mixkit.co/videos/49846/49846-360.mp4 | 640x360 | 43.75 | 3.24 | 否（原始片段） |
| mixkit_49878.mp4 | 49878 | street | https://assets.mixkit.co/videos/49878/49878-360.mp4 | 360x640 | 14.29 | 1.03 | 否（原始片段） |
| mixkit_50134.mp4 | 50134 | street | https://assets.mixkit.co/videos/50134/50134-360.mp4 | 640x360 | 16.18 | 1.1 | 否（原始片段） |
| mixkit_513.mp4 | 513 | people | https://assets.mixkit.co/videos/513/513-360.mp4 | 640x360 | 20.47 | 1.27 | 否（原始片段） |
| mixkit_52427.mp4 | 52427 | street | https://assets.mixkit.co/videos/52427/52427-360.mp4 | 640x360 | 7.38 | 0.57 | 否（原始片段） |
| mixkit_52454.mp4 | 52454 | street | https://assets.mixkit.co/videos/52454/52454-360.mp4 | 640x360 | 31.03 | 2.46 | 否（原始片段） |
| mixkit_5396.mp4 | 5396 | market | https://assets.mixkit.co/videos/5396/5396-360.mp4 | 640x360 | 15.02 | 1.1 | 否（原始片段） |
| mixkit_5944.mp4 | 5944 | shopping | https://assets.mixkit.co/videos/5944/5944-360.mp4 | 640x360 | 23.49 | 1.59 | 否（原始片段） |
| mixkit_6302.mp4 | 6302 | shopping | https://assets.mixkit.co/videos/6302/6302-360.mp4 | 640x360 | 16.25 | 1.19 | 否（原始片段） |
| mixkit_805.mp4 | 805 | market | https://assets.mixkit.co/videos/805/805-360.mp4 | 640x360 | 16.83 | 1.01 | 否（原始片段） |
| mixkit_8739.mp4 | 8739 | people | https://assets.mixkit.co/videos/8739/8739-360.mp4 | 640x360 | 15.0 | 0.92 | 否（原始片段） |
| mixkit_8869.mp4 | 8869 | shopping | https://assets.mixkit.co/videos/8869/8869-360.mp4 | 640x360 | 10.28 | 0.7 | 否（原始片段） |
| mixkit_9060.mp4 | 9060 | shopping | https://assets.mixkit.co/videos/9060/9060-360.mp4 | 640x360 | 7.0 | 0.51 | 否（原始片段） |
| mixkit_9607.mp4 | 9607 | market | https://assets.mixkit.co/videos/9607/9607-360.mp4 | 640x360 | 13.98 | 1.06 | 否（原始片段） |
| mixkit_991.mp4 | 991 | market | https://assets.mixkit.co/videos/991/991-360.mp4 | 640x360 | 24.75 | 1.49 | 否（原始片段） |
| mixkit_992.mp4 | 992 | market | https://assets.mixkit.co/videos/992/992-360.mp4 | 640x360 | 11.71 | 0.86 | 否（原始片段） |
| mixkit_993.mp4 | 993 | market | https://assets.mixkit.co/videos/993/993-360.mp4 | 640x360 | 40.62 | 2.5 | 否（原始片段） |

## 校验与复现

- **校验脚本**：`check_people.py`（ffprobe 头部校验 + OpenCV HOG 行人检测）
- **下载脚本**：`download_mixkit.sh`（通过代理 127.0.0.1:7890，带重试）
- **数据量**：本目录满足实验要求的 ≥50 条真实行人视频，实际共 111 条。
