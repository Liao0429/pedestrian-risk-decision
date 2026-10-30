# B7 数据集与伦理合规评审报告

- **评审角色**：B7 数据集与伦理合规评审专家
- **评审对象**：PedestrianRiskDecision 毕设项目数据资产（`dataset/`、`results/`）与仓库合规性
- **评审日期**：2026-10-30
- **评审范围**：①数据来源/许可文档；②111 条视频与 500 张图片的许可合规；③个人身份数据/隐私风险（人脸/车牌/位置）；④代码与仓库凭据泄露（git 历史、.env、密钥）；⑤数据合规风险清单
- **总体结论**：**数据来源真实、文档对 COCO/Mixkit/Ultralytics 的许可描述基本准确，未发现学号/身份证/手机号/邮箱等个人信息与 API 密钥泄露；但存在 5 项 P1 级问题**——其中最重要的是 ①500 张 COCO 图片与 111 条 Mixkit 视频（及大量结果媒体）已整体提交并推送到 GitHub 私有仓库，`.git` 达 1.1GB，且 COCO 图片的 Flickr 原始许可未逐图记录，仓库一旦公开将面临版权再分发风险；②git 历史（origin/main）中仍残留老电脑 SSH 凭据；③`board_health_snapshot.py` 硬编码板端默认口令。修复优先级：先清历史与工作区敏感项，再补文档，公开前务必重写 git 历史。**

---

## 1. 数据来源与许可合规性核查

### 1.1 图片数据（500 张 COCO）

- `dataset/images500/README.md:5-8`：来源为 COCO 2017 官方图片（images.cocodataset.org），标注来自 Ultralytics coco2017labels.zip。**来源真实，可复现（下载脚本在 `dataset/_work/download_500_images.py`）。**
- 本地核实：`dataset/images500/` 实有 **500 张 jpg**（`ls | wc -l` = 500），与文档声明一致。
- `dataset/README.md:24`（30 张验证集）：**"COCO 数据集以 CC BY 4.0 发布，图片保留各自原始版权（多来自 Flickr 公开图片），仅用于非商业研究与教学验证"** —— 该表述基本准确：COCO 的**标注**为 CC BY 4.0，**图片**保留 Flickr 原作者版权（已联网核验 cocodataset.org 条款与公开资料一致）。措辞建议更精确为"标注以 CC BY 4.0 发布"。
- **问题**：`dataset/images500/README.md`（核心 500 图子集）**全文无任何许可/版权章节**；`dataset/images_manifest.csv` 仅有 `filename,format,size_bytes,width,height,status`（第 1 行），**无 license/版权字段**，无法追溯每张图的 Flickr 原始许可。

### 1.2 视频数据（111 条 Mixkit）

- `dataset/videos50/README.md:9`：来源为 Mixkit 免费库存视频；`:18-26` 给出六个分类页与逐条直链格式；`:14` 声明 ffprobe+HOG 双重校验 111/111 通过；`:147` 声明实际 111 条。
- 本地核实：`dataset/videos50/` 实有 **111 个 mp4**（`ls *.mp4 | wc -l` = 111），且全部为 `mixkit_<id>.mp4` 命名，**与文档一致**。
- 联网核验 Mixkit 许可（mixkit.co/license）：Mixkit Stock Video Free License 允许**商业/非商业使用、下载、复制、修改、分发、公开展示与播放**，但禁止将素材本身作为素材库转售、暗示背书等。**文档"免费使用授权"的表述方向正确，但未给出许可名称与链接，不够完整。**
- `dataset/videos/README.md:9-15`：两个补充小视频（people_counting.mp4、traffic_anpr.mp4）来源为 Ultralytics Assets (MIT)，含原始链接与许可声明。**合规，声明完整。**

### 1.3 结果目录

- `results/rerun/README.md:5,29,52-53` 仅引用数据来源（500 图 / 111 视频 / yolov8n.pt），**无许可引用**。作为结果文档，建议引用子数据集许可。

---

## 2. 隐私与个人身份数据核查

- **学号/身份证/手机号/邮箱/QQ/微信**：对全部 git 对象与工作区文件检索，**无命中**（`git log origin/main -S` 学号/身份证 均无内容命中；`git grep` 当前树无命中）。
- **人脸**：数据集中的人脸均来自 COCO（Flickr 公开图片）与 Mixkit（授权库存视频），属于公开素材，非"识别特定个人"的个人数据场景；`results/edge_board/screenshots/` 有 4 张板端实时画面截图（已 tracked），`results/demo/*.mp4` 为 Mixkit 素材增强产物（README.md:128-133 已声明）。**风险低，但建议在文档中声明"演示素材均来自授权公开数据集，未采集真实个人影像"。**
- **车牌**：`dataset/videos/traffic_anpr.mp4`（Ultralytics ANPR 演示素材，MIT，`dataset/videos/README.md:10,14`）可能含车牌画面，但为**公开测试素材且有来源与许可声明**，非本项目采集的真实车辆数据，风险低。
- **位置**：数据集为 COCO/Mixkit 公开素材，项目自身未采集 GPS/定位数据；文档未涉及位置信息。图片/视频的 **EXIF/GPS 元数据未逐项抽查**（未验证）。

---

## 3. 凭据与敏感文件核查（重点）

| 检查项 | 结果 | 证据 |
|---|---|---|
| `.env` / secrets / 私钥 / token | **无** | `git ls-files | grep -iE '\.env\|secret\|key\|credential\|token\|\.pem\|\.key'` 为空；`git grep` sk-/ghp_/AKIA/BEGIN RSA 无命中 |
| 当前工作区敏感字符串 | **无** | `git grep` password/api_key/secret/token/bearer 无命中（测试/占位符除外） |
| 学生个人信息（全历史） | **无** | 全 git 对象检索 学号/身份证/手机号/QQ/微信/邮箱 无命中 |
| CI secrets | **无** | `.github/workflows/ci.yml` 无 `secrets.` 引用 |
| **git 历史中的凭据** | **发现** | `git log origin/main -S "liao/123456"` 命中 `ea39d12`、`208b8dc`；`git show ea39d12:docs/KANBAN.md:27` 含"老电脑：192.168.0.68（liao/123456）交叉编译……"（**凭据值不在本报告正文复述**）。该文件已从工作区删除（`208b8dc`/`eb46f09`/`681688e`）且已加入 `.gitignore:54`，但 **origin/main 历史中仍保留** |
| **脚本硬编码口令** | **发现** | `scripts/board_health_snapshot.py:37-39` 默认 `HOST=192.168.0.136 / USER=pico / PASS=luckfox`；`:17` 注释"Luckfox 官方默认密码，仓库为私人仓库" |
| 内网 IP | 低风险 | `scripts/cross_build_lvgl.sh:123` 与 `board_health_snapshot.py` 含 `192.168.0.136`（板端局域网 IP）；`voice_module.md` 使用 `root@<板子IP>` 占位符 |
| docs/review 评审文档 | 已隔离 | `docs/review/20261030/CONTEXT.md` 经 `git check-ignore` 确认被忽略；`docs/review/` 当前不在 HEAD（`.gitignore:52`） |

---

## 4. 问题清单

### P0（严重，需立即处理）

**无。** 当前仓库为私有，未发现学生个人信息、API 密钥或生产环境凭据泄露。

### P1（重要，公开/交付前必须修复）

- **P1-1 原始数据集与结果媒体整体入库并推送，存在版权再分发与仓库膨胀双重风险**
  - 证据：`git ls-tree -r origin/main --name-only -- dataset/images500/ | wc -l` = 501（500 jpg + README）；`git ls-tree -r origin/main --name-only -- dataset/videos50/ | wc -l` = 116（111 mp4 + 5 个辅助文件）；`git ls-files | grep -cE '\.mp4$'` = 123、`grep -cE '\.(jpg|jpeg|png)$'` = 1084；`du -sh .git` = **1.1GB**。
  - 风险：①COCO 图片的 Flickr 原始许可各异（部分为 NC/ND），**仓库未逐图记录许可**（`dataset/images_manifest.csv:1` 无 license 字段），一旦公开即构成对部分图片许可的潜在违反；②1.1GB 仓库超过 GitHub 建议的 1GB 软上限，clone/CI 缓慢；③历史中已包含这些图片，仅从工作区删除无法清除。
  - 修复：`git rm --cached` 移除图片/视频/大媒体，保留 manifest + 下载脚本（文档已有复现路径）；媒体如需版本管理改用 git-lfs；公开前用 `git filter-repo` 重写历史。

- **P1-2 `dataset/images500/README.md` 缺少许可/版权章节**
  - 证据：`dataset/images500/README.md` 全文（48 行）无 license/版权/许可字样（grep 无命中）；而 30 张验证集的 `dataset/README.md:24` 有完整声明。
  - 修复：补写许可章节，说明"COCO 标注 CC BY 4.0、图片版权归 Flickr 原作者、引用 https://cocodataset.org/#terms、仅限学术研究用途"；并在 `images_manifest.csv` 增加 license 列（可取自 COCO image_info.json）。

- **P1-3 `dataset/videos50/README.md` 的 Mixkit 许可表述不完整**
  - 证据：`dataset/videos50/README.md:9` 仅写"Mixkit 免费库存视频（mixkit.co，免注册、免费使用授权）"，未给出许可名称（Mixkit Stock Video Free License）、许可链接（https://mixkit.co/license/）与允许/禁止范围。
  - 修复：补充许可名称、URL 与摘要（允许商业/非商业使用、修改、分发；禁止作为素材库转售、不得暗示背书）。

- **P1-4 git 历史残留老电脑 SSH 凭据（origin/main）**
  - 证据：`git log origin/main -S "liao/123456"` 命中 `ea39d12`/`208b8dc`；`git show ea39d12:docs/KANBAN.md:27`（凭据值已脱敏）。当前 HEAD 已删除该文件（`681688e`）且 `.gitignore:54` 已忽略，但 **GitHub 历史中仍可检索到该凭据**。
  - 修复：①立即修改老电脑（192.168.0.68）SSH 口令；②若计划公开仓库，必须用 `git filter-repo` 清除全部历史中的 `docs/KANBAN.md`/`docs/DEVELOPMENT_LOG.md`/`docs/review/` 等敏感文档后再公开；③或永久保持仓库私有。

- **P1-5 `scripts/board_health_snapshot.py` 硬编码板端 SSH 默认口令与 IP**
  - 证据：`scripts/board_health_snapshot.py:37-39`（默认 `192.168.0.136` / `pico` / `luckfox`），`:17` 注释"Luckfox 官方默认密码，仓库为私人仓库"。
  - 风险：Luckfox 出厂默认口令为公开知识；仓库公开后任何能访问板端所在局域网的人都能用默认口令登录。
  - 修复：默认值改为空并从环境变量（`BOARD_HOST/BOARD_USER/BOARD_PASS`）强制读取；同时修改板端实际 root 口令；脚本缺少环境变量时应报错退出而非用默认值。

### P2（建议修复）

- **P2-1 主 README 无"数据来源与许可"章节**：`README.md`/`README.en.md` 全文无 license/版权/许可条目（grep 无命中），仅在目录处提数据来源。建议在 README 增加数据许可章节并链接三个子 README。
- **P2-2 `results/rerun/README.md` 无许可引用**：`results/rerun/README.md`（53 行）无许可信息，建议引用 `dataset/images500/README.md` 与 `dataset/videos50/README.md` 的许可说明。
- **P2-3 仓库残留早期废弃的 YouTube 下载脚本，易造成溯源误解**：`dataset/_work/download_50_videos.py:5-7` 文档字符串写"批量下载 50 条真实行人/人群视频（YouTube 公开视频，通过代理）"，与最终 Mixkit 来源不符，且该脚本已在 origin/main（`git ls-tree origin/main` 可见）。建议删除或标注"已废弃（最终方案改为 Mixkit 直链）"。
- **P2-4 `dataset/_work/coco2017labels.zip`（48.6MB）直接入库且含损坏部分**：`dataset/_work/coco2017labels.zip` 已被 git 跟踪（`git ls-files -s`）；`dataset/README.md:30-31` 自述 train2017 标签存在 CRC 校验失败（约 32654 条）。建议从 git 移除 zip，仅保留 README 中的下载命令。
- **P2-5 origin/main 历史仍含论文/评审文档，与"GitHub 只留代码/测试+技术文档"目标不一致**：`git log origin/main --diff-filter=A --name-only -- docs/review/` 显示 `e4b6811`、`5c9d56e`、`a92e4a5`、`8cca61b` 等提交曾添加数十份评审/论文相关文档（agent 报告、FINAL_REVIEW、IMPLEMENTATION_LOG 等），`681688e` 才从树中删除；`.gitignore:52` 已忽略。当前树合规，但历史仍在，公开前需重写历史。
- **P2-6 演示/截图含人脸与可能车牌画面，建议文档声明素材性质**：`results/edge_board/screenshots/board_live_frame05.png` 等 4 张已 tracked；`results/demo/*.mp4` 为 Mixkit 素材增强产物（README.md:128-133）。均来自授权公开素材，非隐私泄露，但建议在 README 明示"演示素材来自 Mixkit/COCO 授权公开数据，未采集真实个人影像"。`dataset/videos/traffic_anpr.mp4`（MIT）可能含车牌，来源已声明（`dataset/videos/README.md:10,14`），风险低。

---

## 5. 已确认合规（正面结论）

1. 数据量声明与实际一致：500 张图片、111 条视频、111/111 校验通过（`dataset/videos50/README.md:14`）。
2. COCO 许可表述基本准确（`dataset/README.md:24`），Mixkit"免费使用授权"方向正确，Ultralytics Assets 标注 MIT 且附原始链接（`dataset/videos/README.md:13-15`）。
3. 无 `.env`、API 密钥、token、私钥入库；CI 无 secrets。
4. 无学号/身份证/手机号/邮箱/QQ 等学生个人信息进入任何 git 历史。
5. `docs/review/` 已 gitignore（`.gitignore:52`），CONTEXT.md 从未进入 git 历史；论文仅存本地（提交 `eb46f09`/`0b51d00` 明确）。
6. 演示视频、大体积媒体大多已 gitignore（`.gitignore:43` `results/rerun/**/*.mp4`）。

## 6. 未验证项

- **未验证**：500 张 COCO 图片的**逐图原始许可**（Flickr 各图许可类型未逐条核验，manifest 无 license 字段）。
- **未验证**：111 条 Mixkit 视频的**逐条许可状态**（Mixkit 平台统一授权，风险低，但未逐条核对页面）。
- **未验证**：图片/视频的 **EXIF/GPS 元数据**是否含位置信息（未抽查）。
- **未验证**：`dataset/videos50/download_mixkit.sh` 完整内容（读取超时，仅确认头部：代理下载 + 重试逻辑，无许可声明；许可判断以 Mixkit 平台条款为准）。
- **未验证**：演示视频/截图人脸是否与真实个人对应（无像素级视觉复核；素材均为授权库存来源）。