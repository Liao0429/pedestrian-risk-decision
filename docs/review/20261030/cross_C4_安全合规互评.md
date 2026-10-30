# C4 交叉互评报告（凭据与数据合规）

- **评审角色**：C4 交叉互评专家（凭据与数据合规）
- **评审对象**：B7（数据伦理）、B1（算法统计）、B6（代码安全）三份独立评审报告
- **评审日期**：2026-10-30
- **验证方式**：只读抽查（git log/show/ls-tree/ls-files/grep/status/diff/check-ignore，未执行任何 filter-repo/历史改写/文件修改）；因 G 盘间歇性 I/O 超时，文件先复制到 TEMP 再读取；对 B1 的核心统计用 111 条时间线 JSON 做了独立重算
- **总体结论**：三份报告的事实基础总体扎实，**安全/合规域无 P0**；B7 与 B6 在凭据问题上高度一致，B6 额外发现 go2rtc 无认证与 CONTEXT.md 明文凭据（B7 漏报）；B1 的 P0 统计问题经独立重算完全成立。但发现 **B6 的 5 项 C++ P1 中有 4 项已在工作区被修复（未提交），报告引用的是 HEAD 旧状态**；且 **git 历史中除 liao/123456 外还残留板端默认口令 pico/luckfox，B7/B6 均未独立报告**。

---

## 一、核心证据逐条核查

| # | 报告声称 | 本次验证结果 | 判定 |
|---|---|---|---|
| 1 | B6 P1-8：`docs/review/20261030/CONTEXT.md:13` 明文记录老电脑 SSH 凭据 | 实测 L13 原文 `远程环境：老电脑 Ubuntu 192.168.0.63（liao/123456），SSH 可用；开发板 192.168.0.136` | **成立（行号精确）** |
| 2 | B7 表格：`当前工作区敏感字符串 无`（git grep password/api_key/secret/token/bearer 无命中） | 对 tracked 文件确实无命中；但 CONTEXT.md 位于 `.gitignore:52` 忽略的 `docs/review/`，git grep 默认不扫未跟踪文件 → B7 结论存在方法盲区 | **B6 的批评成立，B7 该行结论不完整** |
| 3 | B7 合规项 5：`docs/review/ 已 gitignore（.gitignore:52），CONTEXT.md 从未进入 git 历史` | `git check-ignore -v` 命中 `.gitignore:52:docs/review/`；`git log --all -- docs/review/20261030/CONTEXT.md` 为空 | **成立** |
| 4 | B7 P1-5：`scripts/board_health_snapshot.py:37-39` 默认 `HOST=192.168.0.136/USER=pico/PASS=luckfox`，`:17` 注释承认默认密码 | 实测 L37-39 与 L14-17 docstring 完全一致；L50 `echo '{PASS}' | sudo -S` 还会把密码经 sudo 管道发给远端 | **成立（B7 行号精确；B6 引 38-41 略偏但实质一致）** |
| 5 | B6 P1-7：同上脚本 `:176 AutoAddPolicy`、`:179` 明文密码直连 | 实测 L176 `paramiko.AutoAddPolicy()`、L177 `ssh.connect(..., password=PASS)` | **成立** |
| 6 | B6 P1-6：`docs/WEB_VIEWER.md:31-33` go2rtc `api: listen: ":1984"` 绑定所有接口；访问地址 `:20`；`:47-49` curl 验证 API 无鉴权；file:// 本地文件读取面 | 实测 L31-32 `api: listen: ":1984"`、L9 `http://192.168.0.136:1984`、L48 `curl -s http://127.0.0.1:1984/api/streams`、L102 `file:///userdata/demo/mixkit_13192_enhanced.mp4`（确已使用 file:// 协议） | **成立（无认证、全接口绑定、file:// 面均属实）** |
| 7 | B7 P1-2：`dataset/images500/README.md`（48 行）全文无许可/版权章节 | 逐行检查无 license/版权/许可字样 | **成立** |
| 8 | B7 P1-3：`dataset/videos50/README.md:9` 仅“Mixkit 免费库存视频（mixkit.co，免注册、免费使用授权）”，无许可名称/链接 | 实测 L9 原文一致，全文无许可名称与 mixkit.co/license 链接 | **成立** |
| 9 | B7 P2-1：根 `README.md`/`README.en.md` 无数据许可章节 | 关键词扫描：两份 README 均无“许可/版权/license/CC BY”字样，仅出现 COCO/Mixkit 数据源名称 | **成立** |
| 10 | B7 正面结论：`dataset/README.md:24` COCO 许可表述基本准确；`dataset/videos/README.md:9-15` Ultralytics Assets MIT 声明完整 | 实测 L24 原文、L9-15 原文均与 B7 引述一致 | **成立** |
| 11 | B7 P1-4：`git log origin/main -S "liao/123456"` 命中 `ea39d12`/`208b8dc`；`git show ea39d12:docs/KANBAN.md:27` 含老电脑凭据 | 实测两提交均命中；KANBAN.md L27 原文 `老电脑：192.168.0.68（liao/123456）交叉编译…` | **成立（行号精确）** |
| 12 | **B7/B6 均未报告**：git 历史还含板端口令 | `git log origin/main -S "pico/luckfox"` 同样命中 `ea39d12`/`208b8dc`；KANBAN.md L26 含 `开发板：192.168.0.136（pico/luckfox，daima目录…）` | **遗漏（本次补充发现）** |
| 13 | B7 P1-1：images500 501 项（500 jpg+README）、videos50 116 项（111 mp4+5 辅助）、`git ls-files` 123 个 mp4 / 1084 个 jpg/png、`.git` 1.1GB | 实测：501/116、123 mp4、1084 图片、`du -sh .git` = 1.1G | **成立（数字逐一吻合）** |
| 14 | B7 P2-4：`dataset/_work/coco2017labels.zip`（48.6MB）已入库 | `git ls-tree origin/main` 命中该 zip | **成立** |
| 15 | B7 P2-5：`docs/review/` 历史曾入库（e4b6811/5c9d56e/a92e4a5/8cca61b），`681688e` 移除 | `git log origin/main --diff-filter=A -- docs/review/` 命中上述提交 | **成立** |
| 16 | B7 P2-3：`dataset/_work/download_50_videos.py:5-7` docstring 写“YouTube 公开视频，通过代理”且已 tracked | 实测 docstring 原文一致，`git ls-tree origin/main` 可见 | **成立** |
| 17 | B7 P2-6：`results/edge_board/screenshots/` 4 张 PNG 已 tracked | `git ls-files` 命中 4 张 | **成立** |
| 18 | B1 P0-1：111 条时间线中 34.4% DANGER 帧（4041 帧、48 条视频）n<3，n=0:173/n=1:1075/n=2:2793；README 头部统计基于过期时间线 | **C4 独立重算**：45021 帧、SAFE 26885/WARNING 6388/DANGER 11748、n<3 DANGER=4041（34.40%）、48 条视频、173/1075/2793 —— 与 B1 完全一致；`results/rerun/_aggregate.json` 亦载同一批数字；`mixkit_134_timeline.json` mtime=2026-10-28 15:16，早于含 P0-4 门控修复的 `284de8d`（2026-10-29 21:32） | **成立（B1 核心 P0 完全复现）** |
| 19 | B1 P0-2：README 把“7807→11748”表述为谱特征提升 | 实测 README L80 第一句确有“111 条批量 DANGER 帧 7807 → 11748（谱特征提升高风险识别）”，**但紧随其后已有 ①②③ 口径说明**（7807 为历史快照 a92e4a5、M0=7906、M1/M2 为实验公式不可直接对比） | **部分成立（引用语境不完整；核心问题仍成立，见冲突点 2）** |
| 20 | B6 P1-1~P1-5：postprocess.cc 类别过滤下标错、main.cc invalid free、VENC 未初始化、rknn_create_mem 未判空、Handle2VirAddr 未判空 | `git diff HEAD` 证实：HEAD 版本确实存在 B6 描述的全部缺陷（如 `-if (order[i]==-1 || classIds[i]!=filterId)`、`+malloc`、无 memset、无 NULL 检查）；**但工作区 postprocess.cc（mtime 04:22）/main.cc（mtime 04:43）已应用修复且未提交**：classIds[order[i]]/classIds[m]、删除 malloc/free、补 memset、3 处 Handle2VirAddr NULL 检查均已就位；**仅 P1-4（yolov5.cc:119/130 rknn_create_mem 未判空）仍存在** | **B6 对 HEAD 的描述成立；当前树 4/5 已修复未提交；P1-4 仍未修复** |
| 21 | B6 P2-11：`README.md:104` 残留“显著提升高风险判定灵敏度” | 实测 L104 原文含“谱特征在中等/密集聚集场景显著提升高风险判定灵敏度（实质为…” | **成立** |
| 22 | B6 P2-12：`_aggregate.json` fiedler_by_level SAFE positive_mean=1.3176/356 帧 | 实测一致 | **成立** |

---

## 二、三份报告的一致点

1. **无安全/合规域 P0**：B7 与 B6 均判无 P0；B1 无安全域结论（其 P0 属算法/统计域）。
2. **board_health_snapshot.py 硬编码默认口令**：B7 P1-5 与 B6 P1-7 独立指向同一文件同一缺陷（luckfox 默认口令 + AutoAddPolicy），行号仅差 1-2 行，实质完全一致。
3. **git 历史残留凭据**：B7 P1-4 与 B6 P1-8 均确认第一轮清理后历史中仍有凭据（B7 指向 KANBAN.md，B6 指向新生成的 CONTEXT.md），方向一致。
4. **数据真实性正面结论**：B1 与 B6 独立复跑 `reproduce_consistency.py` 得到完全相同的四组一致率（89.6737%/91.2974%/90.5911%/91.6350%）；B7 独立核验 500 图/111 视频数量与文档一致。三份报告的数据真实性口径互相印证。
5. **B1 的 P0 统计问题与 B6 无冲突**：B6 未涉及统计域，但其复现数字间接支撑 B1 的“README 头部统计来自旧时间线”判断。

## 三、冲突点

1. **B7“当前工作区敏感字符串 无” vs B6 P1-8“CONTEXT.md 明文凭据（修复后仍存在）”**：B7 的方法只扫 git tracked 文件，未覆盖 `.gitignore` 忽略的 `docs/review/`；B6 正确指出这一盲区。**冲突裁决：B6 对**。B7 的“无”应限定为“tracked 文件无”，且 B7 自身 P1-4 只报历史、未把当前 CONTEXT.md 明文列为问题，属于 B7 的漏报。
2. **B1 P0-2 对 README L80 的引用语境不完整**：B1 称“README 把 7807→11748 的 +3941 帧全部表述为谱特征提升”，但 README L80 已带 ①②③ 口径说明（历史快照、M0=7906、M1/M2 公式差异），这部分是第一轮已完成的修复。**B1 的核心新问题仍然成立**：README 的 ①②③ 口径只解释了“前值 7807 的出处”，没有解释“现值 11748 本身生成于门控修复之前”这一事实，L79/L83 仍以 11748 为生产口径。因此 P0-2 应保留，但证据表述需修正为“README 已注明 7807 口径，但未标注 11748 时间线版本早于门控修复”。
3. **B6 C++ P1 与当前工作区不一致**：B6 报告（04:08 保存）引用的是 HEAD（05cf1b1，01:45）状态；工作区在 04:22-04:43 已应用 P1-1/P1-2/P1-3/P1-5 的修复但未提交。评审者若直接看当前工作区会认为 B6 报错。**这不是 B6 的事实错误，而是评审与修复并行导致的版本漂移**；正确处置是提交这些未提交修改并复测。
4. **历史 IP 与当前 IP 不一致**：git 历史 KANBAN.md 中老电脑为 `192.168.0.68`，CONTEXT.md 当前为 `192.168.0.63`，口令相同。修复时应按当前实际 IP 处理，不能只改文档里的 IP。

## 四、遗漏

1. **git 历史还残留板端默认口令 `pico/luckfox`**（KANBAN.md L26，`git log origin/main -S "pico/luckfox"` 命中 ea39d12/208b8dc）。B7 P1-4 只报了 liao/123456，B6 只报 CONTEXT.md。**公开前 filter-repo 必须覆盖所有含口令的文档对象，且板端真实口令必须修改**（Luckfox 出厂默认口令为公开知识）。
2. B7 未把 CONTEXT.md 明文凭据列入自身问题清单（其 P1-4 仅覆盖 git 历史）。B6 补上了这一点。
3. B7 P1-1 建议给 manifest 加 license 列，但 `dataset/images_manifest.csv` 只覆盖 30 张验证集，images500 连 manifest 都没有；补 license 字段应同时为 images500 新建清单。
4. B7 的“未验证项”提到 `download_mixkit.sh` 无许可声明，但问题清单未要求该脚本补许可——与 P1-3 属同一合规缺口。
5. B6 未涉及数据许可/媒体入库合规（超出代码安全范围，可理解）；B7 未涉及 go2rtc 监控暴露面（超出数据伦理范围，可理解）——两报告合并后才完整。

## 五、合并安全与合规问题清单（含修复命令建议，未执行）

> 严重度沿用各报告；交叉确认状态：`交叉确认`=多报告独立证实或 C4 本次独立核实；`存疑`=仅单报告且依赖真机/运行时验证。

### P0

- **无**（三份报告一致：安全与合规域无 P0；B1 的 P0 属算法/统计数据可信度域，见下）

### P1

- **C-P1-1 git 历史残留 SSH 凭据（liao/123456 与 pico/luckfox）** — `交叉确认`（B7 P1-4 + C4 本次 `git log -S` 复现 + 补充发现板端口令）
  - 证据：`git log origin/main -S "liao/123456"` 命中 ea39d12/208b8dc；`git show ea39d12:docs/KANBAN.md:26-27` 含两组口令
  - 修复命令（**不执行**）：
    ```bash
    # 1) 立即修改老电脑与开发板真实口令（不可用 git 命令替代）
    # 2) 若公开仓库，备份后重写历史（危险，需完整备份 .git）：
    git clone --mirror <repo> /tmp/repo-backup.git
    git filter-repo --invert-paths --path docs/KANBAN.md --path docs/DEVELOPMENT_LOG.md --path docs/review/
    git push --force --all && git push --force --tags
    # 3) 或永久保持仓库私有（最简单，零风险）
    ```
- **C-P1-2 CONTEXT.md 明文凭据复发** — `交叉确认`（B6 P1-8 + C4 实测 L13）
  - 修复命令（**不执行**）：
    ```bash
    sed -i 's#liao/123456#<REDACTED>#g' docs/review/20261030/CONTEXT.md
    # 并约定 docs/review/ 内禁止写真实凭据；CI/本地加 grep 门禁：
    grep -rn "123456\|luckfox" docs/review/ || true   # 应无命中
    ```
- **C-P1-3 board_health_snapshot.py 硬编码默认口令 + AutoAddPolicy** — `交叉确认`（B7 P1-5 = B6 P1-7 + C4 实测 L37-39/L176）
  - 修复命令（**不执行**）：改 L37-39 为必填环境变量（缺省 `sys.exit`），`AutoAddPolicy` 改 `WarningPolicy` 或显式 known_hosts；同时修改板端真实 root 口令
- **C-P1-4 go2rtc 监控 Web 服务无认证** — `交叉确认`（B6 P1-6 + C4 实测 WEB_VIEWER.md）
  - 修复命令（**不执行**）：`/home/pico/go2rtc.yaml` 中 `api: listen: ":1984"` 改为 `api: { listen: "127.0.0.1:1984", auth: "<用户>:<强口令>" }`，外网访问走 SSH 隧道；WEB_VIEWER.md 加安全警告段
- **C-P1-5 数据集/结果媒体整体入库（.git 1.1GB）且许可表述不完整** — `交叉确认`（B7 P1-1/P1-2/P1-3 + C4 实测）
  - 修复命令（**不执行**）：
    ```bash
    git rm --cached dataset/images500/*.jpg dataset/videos50/*.mp4 dataset/_work/coco2017labels.zip
    # 保留 manifest/下载脚本/README；媒体如需版本管理改 git-lfs
    # images500/README.md 补许可章节；videos50/README.md 补 Mixkit Stock Video Free License 名称+链接
    ```
- **C-P1-6 B1 P0-1/P0-2：核心统计时间线早于门控修复，README 生产口径失真** — `交叉确认`（B1 独立重算 + C4 本次完全复现 4041 帧/48 条 + B6 复现一致率）
  - 修复命令（**不执行**）：用当前 `scripts/crowd_test_enhanced.py` 重跑 111 条时间线，重算 `_aggregate.json`/`_spectral_fsm_experiment.json`/`_consistency.json`，README 生产口径改为重算值；README L80 补“11748 时间线版本早于门控修复”的说明
- **C-P1-7 板端 C++ 健壮性（B6 P1-1~P1-5）** — `存疑→已部分修复`：C4 核对了 HEAD 代码证据（B6 描述属实）；**当前工作区已修复 P1-1/P1-2/P1-3/P1-5（未提交），P1-4（rknn_create_mem 判空）未修复**；实际触发影响需真机验证
  - 修复命令（**不执行**）：先提交工作区已完成的修复并跑 `make test`+CI；再给 `yolov5.cc:119/130` 的 `rknn_create_mem` 补 NULL 检查

### P2

- **C-P2-1 根 README/README.en.md 无数据许可章节** — `交叉确认`（B7 P2-1 + C4 扫描）
- **C-P2-2 results/rerun/README.md 无许可引用** — `交叉确认`（B7 P2-2 + C4 扫描）
- **C-P2-3 废弃 YouTube 下载脚本入库易误导溯源** — `交叉确认`（B7 P2-3 + C4 实测 docstring）：删除或标注废弃
- **C-P2-4 coco2017labels.zip 48.6MB 入库且 train2017 标签 CRC 损坏** — `交叉确认`（B7 P2-4 + C4 ls-tree）
- **C-P2-5 docs/review 评审文档历史残留** — `交叉确认`（B7 P2-5 + C4 git log）：公开前 filter-repo 一并清除
- **C-P2-6 演示截图/视频含人脸与可能车牌，建议文档声明素材性质** — `存疑`（B7 P2-6；素材为授权公开数据，风险低，无视觉复核）
- **C-P2-7 README.md:104 仍残留“显著提升”表述** — `交叉确认`（B6 P2-11 + C4 实测 L104）：改为“提升（描述性，未做统计检验）”
- **C-P2-8 README Fiedler 单调表述未披露条件均值/防抖帧** — `交叉确认`（B1 P2-1 + B6 P2-12，二者独立指向同一数据）

## 六、未验证项与局限性

- B6 的 C++ P1 实际触发影响（跨类 NMS 误抑制率、invalid free 是否 ABRT、VENC 未初始化字段、RKNN OOM）均需板端真机验证，本环境无板端。
- B7 的逐图许可（Flickr 原始 license）、Mixkit 逐条许可状态、EXIF/GPS 元数据均未核验（B7 已如实标注）。
- B1 的“用当前代码重跑 111 条时间线”需要 GPU/YOLO，本交叉评审未重跑，仅用存档特征独立重算了 FSM 统计。
- G 盘间歇性 I/O 超时导致部分读取重试，但所有关键证据均成功读取且可复现。
- 本报告未对三份报告的全部 P2 逐条复核，聚焦凭据/数据合规与安全相关 P0/P1。

*报告结束。*