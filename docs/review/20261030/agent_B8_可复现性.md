# B8 可复现性与工程化独立评审报告

> 评审人：B8（可复现性与工程化评审专家）
> 日期：2026-10-30
> 范围：README/docs 从零复现完整性、依赖锁定、一键复现脚本、CI 一致性、文档路径/命令时效、新可复现性问题
> 验证方式：真实运行 `scripts/reproduce_consistency.py` 与 `pytest tests -q`；静态核对 CI/CMake/Makefile/requirements/gitignore/下载脚本；本机无 g++/cmake，C++ 侧以 `cpptest.log` 为旁证并标注未验证。

---

## 一、总体结论

项目在**本机一键复现与测试**层面是真实可用的：`scripts/reproduce_consistency.py` 实测输出 111 条时间线 / 45021 帧，M0_ms33=89.6737%、PROD_ms33=91.2974%、PROD_fps=91.6350%，与 README L80 口径一致；`pytest tests -q` 实测 **190 passed / 4 skipped（6.74s）**，CI 配置（`.github/workflows/ci.yml`）与测试实际依赖一致。依赖锁定、数据集下载脚本、一键复现脚本的健壮性存在**中等程度可复现性缺口**：没有 lockfile、requirements 只有下限、README 未给出环境安装步骤、数据集下载/校验脚本硬编码本机路径与代理、`reproduce_consistency.py` 在全新 clone（无 gitignore 排除的逐视频时间线）场景会除零崩溃。**无 P0**；P1×4、P2×5。C++ 主机测试与 CMake/ctest 本次未能在本机复跑（无工具链），以仓库内 `cpptest.log` 旁证并标注「未验证」。

## 二、验证动作与真实输出

### 2.1 一键复现脚本（真实运行）

命令：`cd /g/PedestrianRiskDecision && env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" scripts/reproduce_consistency.py`

```
时间线：111 条，总帧数 45021
  M0_ms33: 40372/45021 = 89.6737%
  PROD_ms33: 41103/45021 = 91.2974%
  PROD_frame: 40785/45021 = 90.5911%
  PROD_fps: 41255/45021 = 91.6350%
已存档：G:\PedestrianRiskDecision\results\rerun\_consistency.json
```

结论：README L80 引用的 M0=89.67%、生产配置回放 91.3~91.6% 完全可复现；98.21% 不可复现的标注属实。

### 2.2 Python 全量测试（真实运行）

命令：`cd /g/PedestrianRiskDecision && env -u PYTHONPATH "G:/py311_gpu/Scripts/python.exe" -m pytest tests -q -rs`

```
190 passed, 4 skipped in 6.74s
SKIPPED [4] tests\test_go2rtc_demo_config.py:235: Windows MSYS 路径转换与 Linux 不同，shell 测试由 CI 覆盖
```

结论：4 个 skip 均为 Windows 平台 shell 测试，CI（ubuntu）会执行；与 CI 设计一致。

### 2.3 CI 配置与实际测试一致性（静态核对）

- `ci.yml:20-24` python-tests 安装 `numpy pytest opencv-python-headless` 后跑 `pytest tests/ -q`。实测 tests/ 仅导入 numpy/cv2/pytest（脚本扫描 17 个测试文件），无 PIL/matplotlib/paramiko/trimesh/ultralytics 导入，CI 依赖集足够。✅
- `ci.yml:33-38` cpp-tests `apt install g++ make` 后 `make test`；Makefile `test` 目标在 Linux 下自动纳入 `test-sysfs-spi`（Makefile:36-41,133-139）。✅
- `ci.yml:46-54` cmake-tests 用 `cmake -S src/luckfox_rtsp_yolov5 -B build` + `ctest`；与 CMakeLists 注释命令一致。⚠️ 但 ctest 仅注册 10 个目标，不含 `test_sysfs_spi`（见 P2-4）。
- CI 实际运行状态本次无法远程查看（无 gh 访问），标注「未验证」。

### 2.4 C++ 主机测试

本机 `which g++ cmake` 无结果、`G:/DevTools/mingw/bin/g++.exe` 不存在，**本次未复跑 `make test` 与 ctest**。仓库根 `cpptest.log`（2026-10-29，x86_64-w64-mingw32-g++ 产物）记录各目标全过：enhanced 26/26、minijson 32/32、risk_config 58/58（OpenCV 不可用跳 2 项）、golden 104/104、preprocess 174/174、hardware 118/118、selftest 80/80、lcd_menu 99/99、ili9341 123/123、xpt2046 98/98。CONTEXT 记载 CMake 10/10、make test 26/26，与日志吻合。**本项标注「未验证」（本机无工具链）**。

## 三、问题清单

### P0（阻断级）

无。

### P1（重要，影响第三方从零复现）

#### P1-1：`scripts/reproduce_consistency.py` 在全新 clone 上除零崩溃，并非真正「一键复现」

- **证据**：
  - `.gitignore:43-46` 忽略 `results/rerun/video_enhanced/*/`、`results/rerun/video_baseline/*/`、`results/rerun/**/*.mp4`；
  - `git ls-files results/rerun` 仅 14 个汇总 JSON（`_aggregate.json`、`_summary.json` 等），逐视频 `*_timeline.json` 不在版本库；
  - `scripts/reproduce_consistency.py:44-51` 的 `timelines()` 只扫描 `results/rerun/video_enhanced/*/*_timeline.json`；`main()` 在 `total_frames=0` 时仍调用 `replay_consistency`，`replay_consistency` 内 `agree/total*100.0`（:88）在 total=0 时 **ZeroDivisionError**；
  - 实测模拟（空 `video_enhanced` 目录）输出 `时间线：0 条，总帧数 0` 后崩溃 `ZeroDivisionError: division by zero`；
  - 同项目 pytest 用例已知此问题并做了防护：`tests/test_spectral_fsm_experiment.py:30-32,96-97` 明确写「timeline 是 GPU 全量重跑的本地产物（.gitignore 排除），CI 不收集」，并用 `@pytest.mark.skipif(len(TIMELINES)==0)` 跳过；而 `reproduce_consistency.py` 没有同等防护。
- **修复建议**：在 `main()` 开头检测 `len(tls)==0`，打印明确指引「请先运行 `python scripts/rerun_all_videos.py` 生成逐视频时间线」并以非零码退出；或直接读取已入库的 `_aggregate.json`/`_summary.json` 给出提示。同时在 CI 增加一个用合成时间线（或 tests/golden 数据）驱动该脚本的冒烟 job，防止回归到崩溃态。

#### P1-2：README 缺少「从零复现」的环境安装与数据获取步骤

- **证据**：
  - `README.md` 全文无 `pip`、`install`、`requirements`、`pytest`、`环境` 字样（脚本 grep 证实）；「部署方法 → PC 端测试」直接给 `python scripts/crowd_test.py ...`（README.md:140-145），假设依赖已装好；
  - `requirements.txt` 存在但 README 未引用，第三方 clone 后不知道要 `pip install -r requirements.txt`；
  - README 未给出 Python 单元测试命令（`pytest tests/ -q`），只给出 C++ `make test`（README.md:154-160）；测试命令仅在 `docs/导读.md:141` 出现；
  - 数据获取仅一句「下载脚本与校验脚本在 `dataset/_work/` 和 `dataset/videos50/`」（README.md:64），无主 README 级别的安装→取数→跑实验→复现一致率的完整链路。
- **修复建议**：在 README「部署方法」前增加「环境安装」小节：`pip install -r requirements.txt`、Python 3.10~3.14 说明、`pytest tests/ -q`、以及数据集已在仓库内（111 视频 + 500 图已入库）或如何用脚本重下；把 `docs/导读.md` 第 6 节「常用命令入口」链接到 README。

#### P1-3：数据集下载/校验脚本硬编码本机路径与代理，第三方无法运行

- **证据**：
  - `dataset/videos50/download_mixkit.sh:4`：`IDFILE="/h/PedestrianRiskDecision/mixkit_good_ids_lf.txt"` —— 绝对路径，且该文件不在仓库中（`git ls-files` 无此文件）；
  - `dataset/videos50/download_mixkit.sh:21`：`curl -s -x http://127.0.0.1:7890 ...` —— 硬编码本机代理；
  - `dataset/_work/download_50_videos.py:11,13`：`OUT = r"H:/PedestrianRiskDecision/dataset/videos50"`、`PROXY = "http://127.0.0.1:7890"`；
  - `dataset/videos50/check_people.py:6`：`vdir = r"H:/PedestrianRiskDecision/dataset/videos50"` —— 「校验脚本」同样不可移植；
  - README 声称下载/校验脚本在仓库内（README.md:64），但脚本离开作者机器无法运行。
- **修复建议**：下载脚本改为相对路径（`$(dirname "$0")` 已用于 cd，但 IDFILE 应改为仓库内路径并入库 id 清单），代理改为环境变量（`${PROXY:-}`）且默认直连；校验脚本 `check_people.py` 的 `vdir` 改为基于 `__file__` 推导。若 111 条视频已入库，则把下载脚本标注为「历史采集脚本，数据已入库，仅供溯源」。

#### P1-4：依赖未锁定，`requirements.txt` 注释声称「主版本锁上限」但实际只有下限

- **证据**：
  - `requirements.txt:1-8`：`ultralytics>=8.2.0`、`opencv-python>=4.8.0`、`numpy>=1.24.0`、`pytest>=7.0.0`，全部只有 `>=`；
  - `requirements.txt:4` 注释「版本策略：主版本锁上限，兼容 3.10~3.14」—— 与文件内容不符，实际没有任何上限；
  - 无 `pyproject.toml`/`Pipfile`/`poetry.lock`/`uv.lock`（仓库根目录实测仅 requirements.txt）；
  - `ultralytics` 迭代快（v8→v9+），`>=8.2.0` 未来会装到破坏性大版本，实验数字（82.5%、89.67% 等）可能漂移。
- **修复建议**：要么把注释改掉并给出版本上限（如 `ultralytics>=8.2.0,<9`、`numpy>=1.24,<3`），要么生成 lockfile（`pip freeze > requirements-lock.txt` 或 uv/poetry）。至少保证 CI 与本地用同一组版本。

### P2（次要）

#### P2-1：脚本 docstring 残留本机 Python 路径

- **证据**：`scripts/generate_golden.py:22` 与 `tests/test_golden_regression.py:11` 的运行命令均为 `H:/py311_gpu/Scripts/python.exe ...`。
- **修复建议**：改为 `python scripts/generate_golden.py` / `python -m pytest tests/test_golden_regression.py -q`。

#### P2-2：README 仍残留本机盘符引用（隐私说明场景，修复后仍存在）

- **证据**：`README.md:193`、`README.en.md:159` 写「仅保存在本地（`H:\毕设论文\`）」；`docs/导读.md:182`、`docs/工作日志.md:7,22,51` 同。
- **说明**：这是隐私声明而非命令，不阻断复现；但公开仓库读者看到的是作者本机盘符，与「已清除 H: 盘引用」的修复口径不完全一致。
- **修复建议**：改为「仅保存在本地私有目录（不入库）」等无盘符表述，彻底消除本机路径。

#### P2-3：CMake 测试目标与 Makefile 不一致，`test_sysfs_spi` 未注册到 ctest

- **证据**：`CMakeLists.txt:34-134` 仅注册 10 个 `add_test`；`Makefile:133-139` 有 `test-sysfs-spi`（Linux 板端适配层，40 项）；CI `cmake-tests` job（ci.yml:39-54）因此不覆盖 sysfs_spi，而 `cpp-tests` job（make test）覆盖，两条 C++ 流水线覆盖不一致。
- **修复建议**：CMakeLists 增加 `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` 注册 `test_sysfs_spi`（源文件已存在），使 ctest 与 make test 对齐。

#### P2-4：CI 未覆盖一键复现脚本，且无数据时无法运行

- **证据**：`.github/workflows/ci.yml` 三个 job 均不运行 `scripts/reproduce_consistency.py`；该脚本又依赖被 gitignore 的时间线（见 P1-1），CI 即使想跑也会崩。
- **修复建议**：随 P1-1 一起，给脚本加空数据防护后用合成时间线在 CI 冒烟运行，至少保证「脚本不崩溃、能输出正确格式」。

#### P2-5：文档中代理/内网 IP 未标注「示例，需按环境替换」

- **证据**：`docs/WEB_VIEWER.md:21` 下载 go2rtc 用 `export https_proxy=http://127.0.0.1:7890`；`dataset/videos50/README.md:147` 注明下载脚本「通过代理 127.0.0.1:7890」；`docs/ARCHITECTURE.md:95`、`docs/WEB_VIEWER.md:9` 出现 `192.168.0.136` 等内网地址。
- **修复建议**：在命令前加一行「以下代理/地址为本机示例，请替换为你自己的环境」。

## 四、未验证项

- **`make test` / `cmake` + `ctest` 本次未复跑**：本机 PATH 无 g++/cmake，`G:/DevTools/mingw` 不存在；仅以仓库内 `cpptest.log`（2026-10-29 全过）与 CONTEXT 记载（CMake 10/10、make test 26/26）为旁证。建议在有 g++ 的 Linux/WSL 环境复跑确认。
- **CI 远程实际执行状态未验证**：本环境无法访问 GitHub Actions 运行记录；仅静态核对 ci.yml 与本地测试一致。
- **板端真机部署链路未验证**：`make luckfox` 交叉编译、`start_demo.sh`、go2rtc 等依赖 Luckfox SDK 与真机，本次未执行；仅核对文档命令。

## 五、对 README 的总体评价

README 的**实验数字口径**做得非常好（89.67% 可复现 / 98.21% 不可复现已标注、生产公式与 M1/M2 区分、7807 vs 11748 口径说明），这是可复现性评审里少见的高质量做法。短板在**工程入口**：环境安装、数据重下、Python 测试命令、一键复现脚本的前置条件都没有在 README 主文档给出，导致「第三方从零复现」链条断在第一步。补齐 P1-1~P1-4 后，项目可以达到「README 拉下来即复现」的标准。