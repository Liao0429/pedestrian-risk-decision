# 行人拥挤度识别验证数据集（真实图片子集）

用于毕业设计《边缘端行人感知与风险决策系统》的**真实图片验证集**，覆盖不同行人数量与聚集程度：
`分散(SAFE)` / `局部聚集(WARNING)` / `拥挤(DANGER)` 三档。

- **图片总数**：30 张
- **全部为真实公开数据**，非生成、非合成。
- **格式**：全部 JPEG（已验证可打开，尺寸见清单）。
- **下载时间**：2026-10-25 21:18 (中国标准时间, UTC+8)

---

## 1. 数据来源

图片全部来自 **COCO 2017 目标检测数据集**（Common Objects in Context）官方 CDN：

- 数据主页：https://cocodataset.org/#download
- 图片直链：
  - val2017：`http://images.cocodataset.org/val2017/<image_id>.jpg`
  - train2017：`http://images.cocodataset.org/train2017/<image_id>.jpg`
- 版权/许可：COCO 数据集以 CC BY 4.0 发布，图片保留各自原始版权（多来自 Flickr 公开图片），仅用于非商业研究与教学验证。

**人数标注来源**：COCO 官方 ground-truth 标注经 Ultralytics 转换为 YOLO 格式标签（class 0 = person），
下载自 GitHub release：`https://github.com/ultralytics/assets/releases/download/v0.0.0/coco2017labels.zip`
（内含 `coco/labels/val2017/*.txt` 与 `coco/labels/train2017/*.txt`）。每张图的人数即该图 `class 0` 标注框数量。

## 2. 下载命令（可复现）

```bash
# 1) 下载标签（用于确定每图人数，48.6MB）
mkdir -p _work images
curl -L -o _work/coco2017labels.zip \
  "https://github.com/ultralytics/assets/releases/download/v0.0.0/coco2017labels.zip"

# 2) 下载图片（示例：val2017 一张 1 人图）
curl -L -o images/000000122672_1.jpg \
  "http://images.cocodataset.org/val2017/000000122672.jpg"

# 示例：train2017 一张 19 人密集图
curl -L -o images/000000497261_19.jpg \
  "http://images.cocodataset.org/train2017/000000497261.jpg"
```

完整挑选逻辑见 `_work/` 下脚本（可复现本次 30 张的挑选过程）：

- `_work/scan_train2017_dense.py`：扫描 train2017 标签，找出每图 >=10 人的密集图片。
- `_work/select_person_images_stream.py`：扫描 val2017 标签，按人数分桶。
- `_work/download_coco_images.py`：按桶均衡挑选 30 张并从官方 CDN 并发下载。
- `_work/verify_images.py`：校验每张图为有效 JPEG 并提取尺寸，输出 `images_manifest.csv`。

## 3. 图片清单

`<image_id>_<person_count>.jpg`，其中 `person_count` 为该图 COCO 标注的行人数。

| # | 文件名 | 人数 | 子集 | 尺寸 (W×H) | 密度档 |
|---|--------|------|------|------------|--------|
| 1 | 000000122672_1.jpg | 1 | val2017 | 640×480 | SAFE（分散） |
| 2 | 000000099054_3.jpg | 3 | val2017 | 427×640 | SAFE（分散） |
| 3 | 000000360097_1.jpg | 1 | val2017 | 500×333 | SAFE（分散） |
| 4 | 000000527750_1.jpg | 1 | val2017 | 427×640 | SAFE（分散） |
| 5 | 000000422886_1.jpg | 1 | val2017 | 640×640 | SAFE（分散） |
| 6 | 000000358195_1.jpg | 1 | val2017 | 483×640 | SAFE（分散） |
| 7 | 000000394510_1.jpg | 1 | val2017 | 640×480 | SAFE（分散） |
| 8 | 000000291791_3.jpg | 3 | val2017 | 640×499 | SAFE（分散） |
| 9 | 000000168883_4.jpg | 4 | val2017 | 479×640 | WARNING（局部聚集） |
| 10 | 000000275198_5.jpg | 5 | val2017 | 640×480 | WARNING（局部聚集） |
| 11 | 000000055167_6.jpg | 6 | val2017 | 640×360 | WARNING（局部聚集） |
| 12 | 000000160864_6.jpg | 6 | val2017 | 640×389 | WARNING（局部聚集） |
| 13 | 000000579070_6.jpg | 6 | val2017 | 640×427 | WARNING（局部聚集） |
| 14 | 000000221754_7.jpg | 7 | val2017 | 640×427 | WARNING（局部聚集） |
| 15 | 000000306893_9.jpg | 9 | val2017 | 500×368 | WARNING（局部聚集） |
| 16 | 000000057672_11.jpg | 11 | val2017 | 640×480 | DANGER（拥挤） |
| 17 | 000000209222_13.jpg | 13 | val2017 | 640×427 | DANGER（拥挤） |
| 18 | 000000254814_13.jpg | 13 | val2017 | 640×480 | DANGER（拥挤） |
| 19 | 000000374545_13.jpg | 13 | val2017 | 549×640 | DANGER（拥挤） |
| 20 | 000000570688_13.jpg | 13 | val2017 | 640×480 | DANGER（拥挤） |
| 21 | 000000044729_14.jpg | 14 | train2017 | 403×640 | DANGER（拥挤） |
| 22 | 000000061507_14.jpg | 14 | train2017 | 640×480 | DANGER（拥挤） |
| 23 | 000000127279_14.jpg | 14 | train2017 | 640×360 | DANGER（拥挤） |
| 24 | 000000170754_14.jpg | 14 | train2017 | 640×480 | DANGER（拥挤） |
| 25 | 000000480133_14.jpg | 14 | train2017 | 640×318 | DANGER（拥挤） |
| 26 | 000000524595_14.jpg | 14 | train2017 | 640×427 | DANGER（拥挤） |
| 27 | 000000553396_14.jpg | 14 | train2017 | 640×424 | DANGER（拥挤） |
| 28 | 000000193479_15.jpg | 15 | train2017 | 640×480 | DANGER（拥挤） |
| 29 | 000000460361_15.jpg | 15 | train2017 | 640×359 | DANGER（拥挤） |
| 30 | 000000497261_19.jpg | 19 | train2017 | 640×480 | DANGER（拥挤） |

密度档为按人数划分的**建议标签**（供验证分类器参考，阈值可按系统设计调整）：
`1-3 人 → SAFE`，`4-9 人 → WARNING`，`10-19 人 → DANGER`。

完整校验信息（文件大小、格式、尺寸、状态）见同目录 `images_manifest.csv`。

## 4. 备注与阻塞项

- **图片有效性**：30 张均用 Python 校验为有效 JPEG（完整文件头+SOF 解析，全部 `OK`），尺寸见上表。
- **人数可信度**：人数来自 COCO 官方标注（经 Ultralytics YOLO 标签），是公开数据集的 ground truth。
- **视觉复核**：本环境视觉模型（u2-flash）不支持图像输入，未能做像素级人工复核；如需可在后续迭代用支持视觉的模型抽查。
- **未选用的候选源及原因**：
  - CrowdHuman：官方下载需填表注册；HuggingFace 镜像（sshao0516/CrowdHuman）在本网络不可达。
  - WiderPerson：官方需申请/百度网盘，无直链。
  - MOT17：官网 motchallenge.net 返回 410 Gone。
  - UCF-QNRF：整包 4.24GB 过大，仅需少量图片不值得。
  - Wikimedia Commons / Google Drive：本网络不可达（连接超时）。
- **密度上限**：COCO val2017 每图人数上限 13、train2017 标签上限 19，因此本集 "拥挤/DANGER" 档为 10-19 人的真实密集场景；若需更高密度（50+ 人）验证，后续可接入 UCF-QNRF / CrowdHuman / NWPU-Crowd。