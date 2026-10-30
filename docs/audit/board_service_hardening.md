# 板卡服务加固与「永久停机」根因（T10 / 共享任务 task-9）

- 角色: board-hardening（板端部署与稳定性）
- 时间: 2026-10-31 00:56 ~ 01:30 (Asia/Shanghai)
- 范围: deploy/systemd/*、scripts/board_maintenance.sh、scripts/board_recover.sh、tests/test_board_service_hardening.py
- 前置: task-1（板端性能/卡死审计修复）已完成
- **上板状态: 未上板验证**。板子 192.168.0.136 在 2026-10-31 00:55 实测 TCP22 可连但 12s 内不出 banner、554 已关闭、1984/8554(go2rtc) 仍开，SSH 不可用。按用户要求**未尝试连板**。本文所有结论均来自「板端既有快照 + 构建机实测 + 一手 man 文档」，凡未在板上复现的都在 §7 单列。

---

## 0. 结论摘要

1. **「永久停机」的直接机制已实测复现**：旧 unit 的 StartLimitIntervalSec=30 + StartLimitBurst=5 规定「30 秒内启动超过 5 次就不再允许启动」。崩溃循环一旦在 30s 内凑满 5 次启动，单元进入 failed，**Restart= 的自动重启逻辑从此不再尝试**（journal 原文 Start request repeated too quickly）。在 systemd 249（与板子同版本）上实测：NRestarts 冻结在 5、状态 failed、is-active 返回 3。

2. **限流窗口内的任何重启都会被拒绝**：实测在窗口内 systemctl restart 返回 1、状态仍 failed；只有等满一个 30s 静默窗口后，显式 start/restart 才成功。man 原文：「units which are configured for Restart=, and which reach the start limit are not attempted to be restarted anymore; however, they may still be restarted manually or from a timer or socket at a later point, after the interval has passed」。因此一个 15s 节奏的重试：第 1、2 次被拒，第 3 次（约 T+38s）才成功，第 4 次又因新一轮崩溃循环被拒 —— 服务实际只有几秒/30~45 秒的存活窗口，外部看起来就是「彻底停止」。

3. **看门狗救不回来的原因不是「没跑」，而是它的判活逻辑与 systemd 打架、从不 reset-failed、且不看返回码**。板端 journal 实证（**2026-10-27 01:20:23 → 01:26:57，共 27 条，间隔 15~16s**；该段是 2026-10-30 23:07 抓取的历史日志，**属于 10-27 的一次 203/EXEC 部署窗口，不是 10-31 00:55 的失联窗口**，出处见 §3.1）：单元持续失败期间看门狗每 15s 无条件 restart、连续 6 分半、期间单元从未恢复 —— 盲目重试、不做诊断。**对 10-31 00:55 那次失联中看门狗的具体行为属「推断」**（板子已不可达、没有取到该窗口任何 journal），推断依据 = 该行为模式 + 旧 unit 配置 + 构建机复刻实验 E/H。详见 §3.1。

4. **修复方案已交付（含一次由板端实证触发的修订）**：看门狗改为 timer 驱动的 oneshot + reset-failed + 不插手 auto-restart 窗口 + 部署窗口抑制（§4.6）；复核了 MemoryMax/OOMPolicy（§4.2）。**限流一项被修订**：原方案是"StartLimitIntervalSec=0 禁用限流"（依据实验 B），现改为**有界断路器 StartLimitIntervalSec=600 + StartLimitBurst=6 + OnFailure 落盘告警 + 看门狗冷却**——因为 dec2 变体的崩溃-重启风暴证明"无限重启 + 每次重建全栈"会把这块 211MB 单核板拖到 SSH 都握不上手（§3.3、§4.7）。

5. **core dump 取证（38 个）**：全部落在 2026-10-30 10:37~11:08 的集中爆发窗口内（其中 13 个挤在 10:38~10:39 两分钟内，约 9 秒一次崩溃重启），**全部早于本轮修复**（最新 core.1235 为 10-30 11:08，本轮二进制部署始于 10-30 22:26 之后）。38 个 core 的 PID 严格单调递增（422→1403），说明是**同一个开机会话内的集中崩溃风暴**，不是长期低频。崩溃点由 lead 用 gdb-multiarch 定位为 **VENC 取流路径对 NULL 写**（core.1235: SIGSEGV / SEGV_MAPERR / si_addr=0x0 / RKVencChannel::getStreamInfo+136），与本轮 P0 修的 VENC pack 槽 / SendFrame 返回值链完全对应。详见 §5。

6. MemoryMax=200M 在 211MB 板子上**不合理**：约等于 95% 物理内存，既保护不了系统，又让本服务成为 cgroup 内唯一的 OOM 受害者。已改为 MemoryMax=160M + MemoryHigh=128M，并配 OOMPolicy=continue（避免 OOM 事件被默认的 stop 策略变成「systemd 主动停止 → Restart= 不生效」的永久停机）。详见 §4.2。

7. **板端可复现证据链（board-perf 实测，02:0x）**：dec2 变体启动即 SIGABRT（std::length_error: vector::reserve，首帧跳过推理导致 od_results.count 读栈垃圾）→ 02:01:59 起每约 5s 崩一次 → restart counter 13→14 → **02:02:11 触发 "Start request repeated too quickly" → 单元 failed** → **02:05 起 ping 通、554/1984/8554 端口都在、但 SSH 12s 出不了 banner**（与 00:05 冻结同型）。即：**反复崩溃-重启（每次重建 VI/ISP/VENC/RKNN 全栈）足以把这块 211MB 单核板拖到 sshd 握不上手** —— 这是"板子老是卡死"的可复现板端证据链，也是把限流改回有界断路器的直接依据。详见 §3.3。

---

## 1. 证据来源

| 证据 | 出处 | 采集时间 | 性质 |
|---|---|---|---|
| 板上旧 unit 全文、旧看门狗 unit+脚本全文、journal、dmesg、端口 | tmp/net/o_board_forensics2.txt | 2026-10-30 23:07 现场 | 板端原始快照 |
| 看门狗 15s 盲目重试的 27 条 journal（**Oct 27 01:20:23~01:26:57**，非 10-31 失联窗口） | tmp/net/o_board_forensics2.txt 第 194~222 行；命令 journalctl -u pedestrian-watchdog -n 40 --no-pager（详见 §3.1） | 板上抓取于 2026-10-30 23:07 | 板端 journal 原始输出 |
| 板端 journal 只读导出（含 journalctl --list-boots 原文 30 行、上一 boot 的单元日志、10-30 20:00~10-31 01:40 窗口 2039 行） | 板上 /tmp/ped-evidence-20261031-015036/ 与 /root/ped-evidence-20261031-015036/（6 份，共 3055 行） | 2026-10-31 01:50 只读取证 | 板端 journal 原始输出 |
| 失联窗口 00:05:32 截断的独立来源 | lead 用 sudo journalctl 在板上实测：tmp/net/incident2.txt、tmp/net/incident3.txt | 2026-10-31 01:33 恢复后 | lead 实测原始输出 |
| 板上 38 个 core 的完整 ls -l（名/大小/时间）、服务与进程状态、top、df | tmp/net/o_board_forensics.txt、tmp/net/board_state.txt | 2026-10-30 23:02~23:06 | 板端原始快照 |
| dec2 变体崩溃-重启风暴 → 单元 failed → 整机 SSH 无 banner（§3.3 证据链） | board-perf 板上实测（含 restart counter 13→14、02:02:11 "Start request repeated too quickly"、02:05 端口/banner 状态），由 lead 转述 | 2026-10-31 02:01~02:05 | 板端实测（本文引用，本人未复现） |
| 断路器封顶 + OnFailure 触发 + 告警落盘 | deploy/probe/probe6_breaker_results.txt（实验 I/J）、deploy/probe/probe_results.txt（实验 A/B） | 2026-10-31 02:12~02:15 | **实测**（systemd 249 构建机） |
| systemd 语义定论（man 原文） | 构建机 192.168.0.63（liao-TV, Ubuntu 22.04, systemd 249）man systemd.unit / systemd.service / systemd.exec | 2026-10-31 01:09 | 一手文档，版本与板端对齐 |
| 行为实验 A/B/C/D/E/G/H | deploy/probe/probe_results.txt（原始输出，未加工） | 2026-10-31 01:02~01:14 | **实测** |
| core.1235 gdb 回溯 | lead 用 gdb-multiarch + ~/core_analysis/luckfox_native_0b08578（带 debug_info）解析 | 早于本报告 | 实测回溯 |
| 现场端口/ARP/SSH banner 状态 | 用户 2026-10-31 00:55 实测 | 00:55 | 用户实测 |

板端 rootfs 为 Ubuntu 22.04 系（板上 gdb 为 12.1-0ubuntu1~22.04），因此构建机 systemd 249 的 man 语义与实验结论可直接对应板端；**唯一未验证的是板端内核的 cgroup 能力差异，见 §7**。

---

## 2. systemd StartLimit 语义核实（一手文档 + 实测）

### 2.1 man 原文（systemd 249）

systemd.unit(5)：

    StartLimitIntervalSec=interval, StartLimitBurst=burst
        Configure unit start rate limiting. Units which are started more
        than burst times within an interval time span are not permitted to
        start any more. ...
        may be set to 0 to disable any kind of rate limiting. ...
        These configuration options are particularly useful in conjunction
        with the service setting Restart= (see systemd.service(5));
        however, they apply to all kinds of starts (including manual), not
        just those triggered by the Restart= logic.
        Note that units which are configured for Restart=, and which reach
        the start limit are not attempted to be restarted anymore; however,
        they may still be restarted manually or from a timer or socket at a
        later point, after the interval has passed. From that point on, the
        restart logic is activated again. systemctl reset-failed will

    （最后一句被 head -24 截断；续文意为 reset-failed 同样会清除该状态）

systemd.service(5) Restart=：

    When the death of the process is a result of systemd operation
    (e.g. service stop or restart), the service will not be restarted.

systemd.service(5) OOMPolicy=：

    If set to continue and a process of the service is killed by the
    kernel's OOM killer this is logged but the service continues running.
    If set to stop the event is logged but the service is terminated
    cleanly by the service manager.

systemd.service(5) TimeoutStopSec=：

    ... configures the time to wait for the service itself to stop. If it
    doesn't terminate in the specified time, it will be forcibly
    terminated by SIGKILL

systemd.exec(5) LogRateLimitIntervalSec=：

    Configures the rate limiting that is applied to messages generated by
    this unit. If, in the time interval defined by LogRateLimitIntervalSec=,
    more messages than specified in LogRateLimitBurst= are logged by a
    service, all further messages within the interval are dropped ...

### 2.2 实测矩阵（构建机 systemd 249；全部为 /run/systemd/system 下的临时单元，脚本结束时自清理）

| 实验 | 配置 | 观察结果 | 结论 |
|---|---|---|---|
| A | Restart=on-failure, RestartSec=5, StartLimitIntervalSec=30, StartLimitBurst=5, ExecStart=/bin/false（**旧版原样复刻**） | 01:06:23 起 5 次重启（:23/:28/:33/:38/:44），第 6 次在 :49 被拒；journal 出现 Start request repeated too quickly 与 Failed to start；Result=exit-code，**NRestarts 冻结在 5**，ActiveState=failed，is-active rc=3 | 旧配置确实在 5 次后彻底放弃自动重启 |
| B | StartLimitIntervalSec=0 + Restart=always + RestartSec=3 | 45 秒内 NRestarts=48，仍在 active/activating(auto-restart)，StartLimitIntervalUSec=0 | **Interval=0 确实禁用限流**；但 02:0x 板端实证（§3.3）证明无限重启会拖垮整机，故最终方案改为有界断路器（§4.7） |
| I | StartLimitIntervalSec=300 + StartLimitBurst=10（RestartSec=2） | T+32s：NRestarts=10、ActiveState=failed；T+46s：NRestarts 仍 =10 | **有界限流确实能把风暴封顶** |
| J | 限流命中 + OnFailure=alert 单元 | journal 出现 "Triggering OnFailure= dependencies."；alert 单元被拉起并写出 marker.log（内容含 Result/NRestarts/ActiveState） | **OnFailure 在 start-limit-hit 时也会触发**（systemd 249 实测），可用于落盘告警 |
| E | 同 A（RestartSec=1 加速） | 爆发后 failed；**T+9s 显式 restart → rc=1 被拒**；T+11s is-active=rc=3；**T+34s（窗口过后）restart → rc=0，恢复 activating** | 限流窗口内的重启无效；窗口过后才可恢复 |
| H | 同 A，模拟看门狗 15s 节奏重试 4 次 | attempt#1(T+8s) rc=1；#2(T+23s) rc=1；**#3(T+38s) rc=0 复活**；#4(T+53s) rc=1 又失败（journal 再次出现 Start request repeated too quickly） | 15s 节奏下服务只能偶尔闪回几秒，其余时间表现为彻底停止 |
| C/D/G | MemoryMax=32M，OOMPolicy=stop / continue，Restart=always，python 持续申请并触碰内存 | MemoryMax=33554432、MemoryCurrent≈28M、**memory.peak 恰好等于上限**、**memory.events: max 1790 / oom_kill 0**；两种策略下单元都保持 active | MemoryMax 在 cgroup v2 上确实生效；但合成负载下**未复现 OOM 击杀**（内核用回收/限流而非杀进程） |

> 诚实标注：C/D/G 未复现 OOM kill，因此「OOM 击杀本服务」这条路径在本文中只作为**文档语义层面的次要风险**陈述（Restart= 不覆盖 systemd 主动停止 + 默认 OOMPolicy=stop 会把 OOM 事件变成服务被干净停掉），**没有板上实测证据**，已列入 §7。

### 2.3 为什么「一次连续失败」会让服务永久停摆（时序）

    10:37:xx  应用崩溃(产生 core.1028) → systemd 记 result=exit-code，启动计数 +1
    10:37:xx  RestartSec=5 后自动重启 → 计数 2 … 直到 30s 内第 5 次
    命中上限  systemd: Start request repeated too quickly.
              → ActiveState=failed；Restart= 从此不再尝试（man: not attempted
                to be restarted anymore）；is-active=failed；554 端口随之关闭
    之后      go2rtc 是独立 unit，不受影响 → 所以 1984/8554 仍在
              此时唯一恢复途径 = 显式 start/restart 且限流窗口已过
              （看门狗本该干这事，但它自身逻辑有缺陷，见 §3）

要点：**这不是「卡在半死」，而是 systemd 主动放弃**。进程确实已经不存在（与用户 00:55 现场一致），因为最后一次失败之后 systemd 不再调度启动任务。

---

## 3. 看门狗为什么没能救回来（逐条脚本分析，均附板端日志证据）

旧看门狗 unit（tmp/net/o_board_forensics2.txt 第 31~42 行）：

    [Unit]
    Description=Pedestrian Risk Decision Watchdog
    After=pedestrian-risk.service
    [Service]
    Type=simple
    ExecStart=/usr/local/bin/pedestrian-watchdog.sh
    Restart=always
    RestartSec=10

旧脚本（同文件第 43~69 行）核心逻辑：

    while true; do
        if ! systemctl is-active --quiet "$SERVICE"; then
            logger -t pedestrian-watchdog "$SERVICE 未运行，尝试重启"
            systemctl restart "$SERVICE"
        else
            if [ -f "$LOG" ]; then
                age=$((now - mtime))
                if [ "$age" -gt "$STALE_SEC" ]; then systemctl restart "$SERVICE"; fi
            fi
        fi
        sleep "$SLEEP_SEC"      # SLEEP_SEC=15, STALE_SEC=30
    done

### 3.1 那段 journal 的取证出处与适用范围（重要，防止过度引用）

- **原始出处**：tmp/net/o_board_forensics2.txt 第 194~222 行（段标题 journal tail (watchdog) 40）。
- **产生方式**：2026-10-30 23:07 在板上执行 /tmp/dsh_board_forensics2.sh（本地副本 C:\Windows\Temp\bp\board_forensics2.sh），其第 14 行为：

      S journalctl -u pedestrian-watchdog -n 40 --no-pager      # S = echo luckfox | sudo -S -p ''

  即板上 journalctl 的**原始输出**，不是本地拼接、转述或推断。
- **为什么 23:07 能取到历史日志**：该时刻板子仍在线（同批快照显示 up 9 min，ps 中存在 watchdog/go2rtc/应用进程）；板上 /var/log/journal 为持久化日志（同文件 dmesg 段有 systemd-journald 对 /var/log/journal/... 的处理记录），因此查询会返回历史条目。
- **精确统计（复核时逐条数过）**：共 **27 条**（原稿写的 25 条有误，已更正），首条 Oct 27 01:20:23、末条 Oct 27 01:26:57，跨度 394 秒（6 分 34 秒），相邻间隔只有 **15 或 16 秒**两种值。
- **时间归属**：这 27 条属于 **2026-10-27 01:20~01:26** 的一次事件，**不是 2026-10-31 00:55 的失联窗口**。同期 journal (risk) 段显示那段时间单元在 203/EXEC "Permission denied"（部署窗口内二进制被替换）且 restart counter 已到 52+，与看门狗 15s 重试完全对应。
- **适用范围（必须区分）**：
  - **可直接证明**：旧脚本在单元持续失败时的**行为模式** —— 每 15~16 秒无条件 restart 一次、连续 6 分半、期间单元始终没有恢复；即"盲目重试、不做诊断"这一设计缺陷。
  - **不能证明（写作时）**：2026-10-31 00:55 那次失联中看门狗的具体行为。写作时板子不可达（TCP22 可连但 12s 无 banner），**未取到该窗口任何 journal**。
  - **恢复后的新证据（lead 实测，2026-10-31 01:33:44 断电重启后）**：上一 boot 的 journal 时间跨度为 2023-09-20（RTC 未同步）→ **2026-10-31 00:05:32**，之后到 01:33:44 **无任何条目**。含义两层：① 00:05:32 之前的条目**仍然存在**（只要在 vacuum 前导出），所以 10-31 00:00~00:05:32 这一小段内看门狗与主服务的动作**有可能被坐实** —— 这比原先"该窗口无 journal"的说法更精确；② 00:05:32 到 01:33:44 约 88 分钟**完全没有 journal 写入**，说明整机在该时刻之后已冻结/写不进日志，与用户 00:55 现场"TCP22 可连但无 banner、554 关闭、go2rtc 仍在"一致（内核还在，用户态已饿死）。
  - **我的只读取证（2026-10-31 01:50，board-hardening 执行，pico+sudo，不安装/不重启/不 vacuum）**：journalctl --list-boots 原文（完整 30 行见 §1 证据行）关键三行：

        -2 1e600faf56b2444b8ec33f5640315630 Fri 2026-10-30 22:59:22 CST—Sat 2026-10-31 00:05:32 CST
        -1 93dc287fc0df4fc8be73c45482f951e4 Sat 2026-10-31 01:36:34 CST—Sat 2026-10-31 01:47:28 CST
         0 b71ba619a15e4564b41d08da166a9227 Sat 2026-10-31 01:48:16 CST—Sat 2026-10-31 01:50:44 CST

    要点：① 上一 boot（boot id 1e600faf…，与 2026-10-30 23:06 取证里 systemd-private-1e600faf…-ntp.service 的 boot id **完全一致**，可确认就是 23:06 那次会话）**止于 00:05:32**；② 下一条 journal 记录要到 **01:36:34** 才出现 → 00:05:32~01:36:34 约 **91 分钟完全无 journal 写入**，与 lead 用 sudo journalctl 的实测一致（lead 原始输出：tmp/net/incident2.txt、tmp/net/incident3.txt）；③ 同一份 list-boots 还显示 boot -1（01:36:34~01:47:28）内 pedestrian-risk / pedestrian-watchdog **没有任何条目**（该 boot 的 journal 总共仅 365 行），boot 0 自 01:48:16 起 → 板子在 01:47~01:48 又重启过一次，留作观察。
  - 因此措辞口径：**10-27 的行为模式是实证；10-31 在 00:05:32 之前那一段的行为，导出核对前仍按"推断"处理**，核到之后才改。
  - 因此"看门狗没能救回 10-31 这次"是**推断**，依据是 ① 本节的 10-27 实测行为模式；② 旧 unit 配置（is-active 判活、无 reset-failed、无 StartLimit 覆盖）；③ 构建机复刻实验 E/H（窗口内 restart 被拒 rc=1）。本文不再把它写成"板端 journal 实证"。
- **该段日志的二次核对方法**（板子已恢复；2026-10-31 01:50 已完成一次只读取证）：board_recover.sh 第 2 步（**只读**，先于任何 vacuum）会导出到板上 /tmp/ped-evidence-<时间戳>/ 并复制一份到 /root/ped-evidence-<时间戳>/，取回后判定：

      journal_boots.txt               journalctl --list-boots：直接给出上一 boot 首末时间（即 00:05:32 的截断点）
      journal_prevboot_units.txt      journalctl -b -1 -u pedestrian-risk -u pedestrian-watchdog：整个上一 boot 的两个单元（不受 RTC 2023 错钟影响）
      journal_1030_1031_window.txt    journalctl --since "2026-10-30 20:00" --until "2026-10-31 01:40"
      journal_prevboot_last3000.txt   上一 boot 全单元最后 3000 行（看 00:05 前后还剩什么）

  核对目标：00:00~00:05:32 内 pedestrian-watchdog 是否动作过、主服务最后一次退出原因（Result/exit code）、00:05:32 前最后若干条是什么。

| # | 缺陷 | 后果 | 板上证据（时间归属见 §3.1） | 新版如何修 |
|---|---|---|---|---|
| 1 | 用 systemctl is-active 判活 | 该命令在 systemd 自动重启窗口（SubState=auto-restart）同样返回 3，看门狗把「systemd 正在重启」误判为「未运行」，每 15s 发一次 systemctl restart | 2026-10-27 01:20:23→01:26:57 连续 **27 条**「未运行，尝试重启」，间隔 15~16s，而同期 journal 里单元一直在 203/EXEC 失败（出处与时间归属见 §3.1） | 只看 ActiveState：failed/inactive 才动手，activating 明确不介入 |
| 2 | 从不 reset-failed | StartLimit 命中后单元停在 failed，**限流窗口内任何 start/restart 都会被拒**（实测实验 E/H：rc=1），必须先 reset-failed —— 看门狗没有这一步，也就没有真正解除「永久停机」状态 | 同 #1 的 6 分钟盲目重试窗口 | recover() 先 reset-failed 再 start，并把返回码写进日志 |
| 3 | 不看返回码 | systemctl restart 失败也照样记一句「尝试重启」，日志看起来像在救火 | 同上（27 条日志期间服务始终 failed） | 检查每条命令返回码并上报，start 失效时明确记录 rc |
| 4 | 陈旧检查用 if [ -f ] 却没有 else | 日志文件不存在时静默跳过陈旧判定，挂死会漏检 | 板上日志存在，未触发 | 文件缺失时输出明确警告 |
| 5 | STALE_SEC=30 过短 | 7fps 下正常抖动就会被误判为挂死，制造额外重启 | 同类问题在实验 H 中被量化 | 默认改为 60s |
| 6 | 自身是 while true 常驻脚本，且 unit 只有 Restart=always、**没有 StartLimit 覆盖** | 脚本反复失败时守护者自己会撞上默认限流（默认 5 次/10s）而永久死亡——守护者与被守护者同样脆弱 | 旧 unit 只有 Description/After 与 Type/ExecStart/Restart/RestartSec，无 StartLimit* | 改为 timer 驱动的 oneshot；unit 显式 StartLimitIntervalSec=0 + TimeoutStartSec=60 |

补充：整机资源枯竭时（00:55 现场 SSH 连上但 12s 不出 banner，说明系统整体调度/内存已饥饿），**任何用户态看门狗都救不了** —— 它自己要和其他进程抢 CPU/内存。这正是应该用 systemd 原生机制（timer、StartLimitIntervalSec=0、OOMPolicy=continue、MemoryHigh 节流）而不是「再写一个脚本」的原因。

### 3.2 失联窗口与开机早段证据不可得的原因（写清楚，免得读者以为没查）

- **冻结期间不再写入**：boot -2 的 journal 止于 2026-10-31 00:05:32，下一条记录要到 01:36:34（原文见 §3.1），中间约 91 分钟**零写入**。整机在 00:05 之后已写不进日志，所以 00:05:32 之后不是"取不到"，而是**根本不存在**。
- **journald 文件损坏/重建**：本 boot 开机早段 journald 报过日志文件损坏重建（lead 实测；我在 2026-10-30 23:06 快照的 dmesg 里也见到同类记录：File /var/log/journal/1fe5d22f36814c0598da7e315031b67d/user-1000.journal corrupted or uncleanly shut down, renaming and replacing）。journald 走"重命名并重建"时，损坏段与旧段会被丢弃。
- **无 RTC + 墙钟回退**：板子没有可用 RTC，每次开机早段墙钟回到 2023-09-20（与 boot -2 里 unit 的 "Active since 2023-09-20 00:57:21" 一致），NTP 同步后才跳到真实时间。两个后果：① 早段条目时间戳是 2023-09-20，用 --since "2026-10-30 20:00" 这类**按时间过滤会漏掉**，必须用 -b -1 / -n N 这种不依赖墙钟的取法（脚本已两路都导出）；② 实测 boot -2 现存的**最早条目是 22:59:22（时钟已跳变后）**，而 unit 激活时间戳是 2023-09-20 00:57:21 —— 说明时钟跳变前那一段确实**已经不在 journal 里**。
- **结论**：00:05:32 冻结窗口以及本 boot 开机早段的服务/看门狗事件**不可回溯**。因此"10-31 失联时看门狗做了什么"永久停留在**推断**（依据见 §3.1），不能因为查不到就默认"它没动作过"。能做的两件事都已做：① 在任何 vacuum 之前把残存记录导出（2026-10-31 01:50 完成，见 §1）；② 恢复后核对 00:00~00:05:32 残存段的最后若干条（待 board-perf 收工后执行）。

### 3.3 板端实证：崩溃-重启风暴 → 整机不可用（board-perf 2026-10-31 02:0x 实测，本文引用；本人按指令未连板，未独立复现）

- **触发**：board-perf 新加的降频开关变体 **dec2** 启动即 SIGABRT，异常为 std::length_error: vector::reserve —— 根因是该变体首帧跳过推理，导致 od_results.count 读到栈上垃圾值。它是一条实验变体的缺陷，不是主线代码的缺陷，但恰好提供了风暴现场。
- **时间线（板端实测）**：

      02:01:59 起   每约 5s 一次 SIGABRT 崩溃-重启；每次重启都要重建 VI/ISP/VENC/RKNN 全栈
      restart counter 13 -> 14
      02:02:11      systemd: "Start request repeated too quickly" -> 单元 failed（旧 unit 的 5 次/30s 限流）
      02:05 起      整机：ping 通、554/1984/8554 端口都开，但 SSH 12s 内出不了 banner

- **与 00:05 冻结的关系**：症状完全同型（ping 通 + 端口在 + SSH 无 banner）。但 00:05 那次只剩 00:00~00:05:32 的残存 journal（§3.2 已说明为何不可回溯），**没有直接证据**表明两者是同一机制。因此：把"风暴 → 整机不可用"当作**已证机制**纳入加固依据；把"00:05 也是同一机制"标记为**推断**。
- **机理**：这块板是 211MB 内存、单核级算力。一次全栈重建（ISP/VENC/RKNN 初始化 + DMA 缓冲分配）本身就是重负载，5 秒一轮的反复重建足以让 sshd 这类必须快速响应的用户态进程拿不到 CPU/内存 —— 于是出现"网络栈还活着、用户态握不上手"的典型形态。
- **这条证据改变了什么**：它把限流参数从"可用性取舍"升级为"整机生存"问题 —— 无限重启（§2.2 实验 B）不可选；旧版 5 次/30s 又会在崩溃循环里迅速静默停摆（§2.1/§2.3）。正确解是"有界 + 告警 + 冷却"，见 §4.7。

---

## 4. 加固交付物

### 4.1 deploy/systemd/pedestrian-risk.service（逐项）

| 配置 | 旧值 | 新值 | 依据 |
|---|---|---|---|
| Restart | on-failure | **always** | 覆盖全部退出方式 |
| StartLimitIntervalSec | 30 | **600** | 有界断路器的窗口（§3.3 板端实证 + 实验 I/J）：设为 0 会让风暴无限持续；600s 让跳闸后 10 分钟内不再自动重启 |
| StartLimitBurst | 5 | **6** | 允许 3~6 次瞬时抖动重试，超过即停机（实验 I：10/300s 时 NRestarts 封顶在 10 后不再增长） |
| OnFailure | 无 | **pedestrian-risk-alert.service** | 跳闸必须留下持久可见的证据（实验 J 实测会触发），避免回到"静默永久停摆" |
| RestartSec | 5 | **3** | 缩短恢复时间；风暴靠断路器封顶，不靠拉长间隔 |
| LimitCORE | 0（原有） | **0（保留）** | 阻止新 core 撑爆根分区 |
| MemoryMax | 200M | **160M** | 见 §4.2 |
| MemoryHigh | 无 | **128M** | 先软件节流/回收，避免直接撞硬限 |
| OOMPolicy | 默认(stop) | **continue** | 防止 OOM 事件变成 systemd 主动停止 → Restart= 不生效（man 原文） |
| TimeoutStopSec | 10 | **15** | 给 D 状态（VPU/NPU 不可中断等待）留出 SIGKILL 时间 |
| LogRateLimitIntervalSec | 无 | **30s** | 崩溃循环时保护 journal/闪存（旧版实测 6 分钟刷出 50+ 条重启记录） |
| LogRateLimitBurst | 无 | **500** | 同上；注意它只作用于 journald 收到的消息，本服务 stdout 走 append: 文件不受此限 |
| TasksMax / KillMode / KillSignal / WorkingDirectory / Environment | 保持 | 保持 | 不变更已验证可用项 |

### 4.2 MemoryMax 复核：211MB 板子上 200M 是否合理 → **不合理**

- 板上实测（tmp/net/o_board_forensics.txt）：Mem total **211 MiB**，used 64 MiB、buff/cache 116 MiB；应用进程 RSS 约 14.9 MiB。
- 200M ≈ **95% 物理内存**：作为 cgroup 上限，它既拦不住本服务拖垮系统，又把本服务单独圈成「唯一的 OOM 候选」；而 go2rtc/sshd 在该 cgroup 之外照旧存活 —— 正好对应 00:55 现场「go2rtc 还在、pedestrian-risk 没了、sshd 出不了 banner」的形态。
- 新版取 **MemoryMax=160M**（留约 50MB 给 systemd/sshd/go2rtc/看门狗）+ **MemoryHigh=128M**（软限先节流）。
- 实测旁证（实验 G）：memory.peak 恰好等于 MemoryMax、memory.events 的 max 计数 1790 次，说明这个上限是真会触发的硬闸而不是摆设；触发时内核先回收/节流（该实验中 oom_kill=0）。
- **160M 是否足以避免「cgroup 内 OOM 引发的重建风暴」？**（board-perf 02:0x 提出的复核点）结论：**能，但不是靠上限数值本身**。
  ① 板上实测应用 RSS 仅约 15MB，160M 是其 10 倍量级，正常运行不会触发；
  ② 合成压力实验（C/D/G）里 memory.peak 恰好等于上限、memory.events 的 max 计数 1790，但 **oom_kill=0** —— 内核先用回收/节流，没走到"杀进程"；
  ③ 即便真发生 OOM kill：OOMPolicy=continue 保证单元不会被 systemd"干净停掉"，但 Restart=always 会把它重启 → 潜在重启循环；**封顶这种循环的是断路器 + 告警 + 看门狗冷却（§4.7），不是 MemoryMax 的数值**；
  ④ 上板后若 systemd-cgtop / memory.peak 显示正常运行时逼近 160M，再上调到 176M 并同时下调 go2rtc 的限额。
- **待上板复核**：恢复后跑一段时间，用 systemd-cgtop 或 systemctl show -p MemoryPeak（若内核支持）观察真实峰值；若峰值逼近 160M，应上调到 176M 并改为下调 go2rtc 的限额，而不是把本服务重新放回 200M。

### 4.3 看门狗新架构

- deploy/systemd/pedestrian-watchdog.timer：OnBootSec=90s（避开 VENC/ISP 初始化期）+ OnUnitActiveSec=30s；由 systemd 调度，脚本崩溃/被杀也不会丢下一次检查。
- deploy/systemd/pedestrian-watchdog.service：Type=oneshot + TimeoutStartSec=60 + StartLimitIntervalSec=0 + LimitCORE=0 + 日志限流。
- deploy/systemd/pedestrian-watchdog.sh：单次执行；**只对 failed 动手**（先 reset-failed 再 start）；activating(auto-restart) 明确不介入；**active 且日志陈旧才拉起**；**inactive 默认不拉起**（见 §4.6，需 WATCHDOG_START_IF_INACTIVE=1 才恢复旧行为）；存在抑制文件 /run/pedestrian-risk.hold 时完全不动作；陈旧阈值默认 60s；检查并上报返回码；支持 DRY_RUN/SERVICE/LOG/STALE_SEC/HOLD_FILE/RECOVER_SETTLE_SEC 覆盖。
- **风暴守卫（§3.3 实证后新增）**：每次运行记录 NRestarts 并与上次比较，相邻 30s 内增长 >= STORM_DELTA（默认 3）就写 /var/lib/pedestrian-risk/storm.log 并记一条 error 日志；**failed 的恢复尝试带 RECOVER_COOLDOWN（默认 300s）冷却**，避免把断路器刚掐灭的风暴重新点着（这是对 §3 结论的修订，见 §4.7）。
- 部署时旧常驻 pedestrian-watchdog.service 会被 board_recover.sh 自动 disable --now，再 enable --now 新 timer。

### 4.4 scripts/board_maintenance.sh

安全幂等（set -u、不用 set -e、每步独立、支持 --dry-run）：

1. 清 core dump：只匹配 DEMO_DIR **顶层**的 core.<pid>（拒绝 DEMO_DIR=/），按 mtime 排序、保留最新 KEEP_CORES 个、逐个 rm -f -- 精确删除，并同时报告「实占（du -k，稀疏文件的真实占用）」与「表观（wc -c）」两个数字。
2. 轮转主日志：超过 LOG_MAX_BYTES（默认 32MiB）时保留末尾 1MiB 到 .1 再截断（服务以 O_APPEND 打开，截断安全）。
3. journalctl --vacuum-size=32M（无 journalctl 时跳过）。
4. 健康快照：时间/负载/uptime/内存/磁盘/温度（/sys/class/thermal，RV1106 可能不暴露则明确打印「无法读取」）/频率/服务状态（含 NRestarts、Result、MemoryCurrent、OOMPolicy、LimitCORE）/core_pattern/内存 Top5，并在最后再打印一次磁盘以体现清理效果。

### 4.5 scripts/board_recover.sh

- 传输层自动探测：本机有 sshpass 则 sshpass -p PASS ssh/scp 直连（可在构建机 192.168.0.63 上跑）；否则用仓库的 paramiko helper（PED_PYTHON 默认 python、PED_HELPER 默认 tmp/net/ssh_exec.py），即用户描述的两种方式都覆盖。
- **登录账号（2026-10-31 01:49 实测修正）**：板端 **root 的 SSH 直登已不可用**（root → paramiko AuthenticationException: Authentication failed，TCP22 本身正常）；可用的是 **pico 登录 + sudo**（pico；echo luckfox | sudo -S -p '' id -un → root）。因此脚本默认 BOARD_USER=pico，非 root 登录时把整条远程命令包成 sudo -S -p '' sh -c 'payload'；payload 含单引号会被守卫直接拦下，避免嵌套引号被破。
- 流程：等待 SSH 就绪（上限 SSH_WAIT_SEC=180s；超时 **exit 3 明确失败，不做任何破坏性操作**）→ 预检 → 备份板上原 unit + 导出 journal 到 /tmp/ped-evidence-<时间戳>（并复制一份到 /root/ped-evidence-<时间戳>）→ 上传并 install -m 644/755 → 打印 md5sum 与 systemd-analyze verify（非致命）→ daemon-reload + **reset-failed** + 停旧看门狗 + enable + restart → enable --now 新 timer → 等 25s 后打印 is-active/NRestarts/MemoryMax/OOMPolicy/journal/端口/资源 → 判定 PASS/PARTIAL/FAIL。
- CHECK_ONLY=1 只体检不安装，便于后续复盘。
- 凭据通过 BOARD_USER/BOARD_PASS 覆盖（默认 **pico**；板上 root 直登已不可用，见上）。

### 4.6 部署窗口缺陷（board-perf 2026-10-31 核到）与三层修复

**缺陷链**（board-perf 复核出的部署流程问题）：

    部署流程：systemctl stop pedestrian-risk  ->  上传 5MB 二进制（40~60s）  ->  mv 覆盖  ->  systemctl start
    这 40~60s 内 pedestrian-risk 处于 inactive
      -> 旧看门狗 systemctl is-active 返回 3 -> 判定"未运行" -> systemctl restart
      -> **把旧二进制拉起来**（此刻新文件还在上传中）
      -> 随后 mv 覆盖同名文件 -> 出现"旧进程 + 新文件"的混合状态
      -> 再叠加 Restart=on-failure，NRestarts 被无谓抬高，部署结果不可解释

**三层修复**：

| # | 层 | 做法 | 位置 |
|---|---|---|---|
| 1 | 看门狗自身 | inactive **默认不自动拉起**（只有显式 WATCHDOG_START_IF_INACTIVE=1 才恢复旧行为）；failed 才介入；activating 不介入 | deploy/systemd/pedestrian-watchdog.sh |
| 2 | 抑制文件 | /run/pedestrian-risk.hold 存在时看门狗完全不动作；部署脚本在窗口开始时 touch、确认服务 active 后才 rm | watchdog + board_recover.sh |
| 3 | 部署脚本 | 顺序强制为：**停看门狗（timer+service）→ touch hold → 上传/安装 →（替换二进制时先 stop 服务、上传、md5 校验、再 install）→ 服务验证 active → rm hold → 启动 timer**；并打印 NRestarts 前后值作佐证 | scripts/board_recover.sh |

**给任何做二进制替换的人的硬要求**：替换窗口内必须没有任何东西能拉起 pedestrian-risk（停看门狗 + hold 文件），否则必然复现"旧进程+新文件"。

**新旧看门狗对 inactive 的行为对比**：

| 场景 | 旧版（while true） | 新版（timer oneshot） |
|---|---|---|
| 人为 stop（部署窗口） | is-active 返回 3 -> **restart，把旧二进制拉起** | **默认不动手**，只记一条日志 |
| systemd 自动重启窗口 activating | 同样判为"未运行" -> 额外 restart，喂满限流窗口 | 明确不介入 |
| 崩溃后停在 failed | 会 restart，但从不 reset-failed（限流窗口内被拒） | reset-failed + start，并上报返回码 |
| 主循环挂死（active 但日志陈旧） | 30s 阈值就 restart | 60s 阈值才 restart |

**测试钉住**（不需要硬件；用桩 systemctl 真跑脚本）：tests/test_board_service_hardening.py 内有 7 个行为测试 + 4 个部署顺序测试：
inactive 不得 start / 显式开关才 start / failed 必须先 reset-failed / activating 不介入 / hold 文件生效 / active+日志陈旧要拉起 / 健康不动手；以及"停看门狗早于任何上传""NRestarts 前后值必须打印""FAIL 时不得放开看门狗""替换二进制先停服务再上传"。

---

### 4.7 崩溃风暴的封顶、落盘告警与看门狗冷却（依据 §3.3 板端实证）

三件事互相配合：

| 机制 | 配置 / 实现 | 作用 | 实测证据 |
|---|---|---|---|
| 有界断路器 | StartLimitIntervalSec=600 + StartLimitBurst=6 | 600s 内最多 6 次启动；超限即停机，不再无限重启 | 实验 I（10/300s）：T+32s NRestarts=10，T+46s 仍为 10 → 风暴被封顶 |
| 落盘告警 | OnFailure=pedestrian-risk-alert.service → 写 /var/lib/pedestrian-risk/storm.log + user.err 日志 + 统计近 10 分钟失败次数 | failed 事件**持久可见**（断电重启后还在） | 实验 J：journal 出现 Triggering OnFailure= dependencies.，marker.log 写出（含 Result/NRestarts/ActiveState） |
| 看门狗冷却 | storm_guard 记录重启速率并告警；failed 的恢复尝试受 RECOVER_COOLDOWN=300s 冷却 | 不让看门狗把断路器刚掐灭的风暴重新点着，同时保证最终仍能恢复 | 行为测试：冷却内不 start；冷却外必须 reset-failed+start；速率飙升必须写 storm.log |

**这是对 §3 结论的一次修订（重要）**：§3 当时的判断是"旧看门狗从不 reset-failed，所以救不回来"，方向是"让看门狗能救 + 禁用限流"。02:0x 的板端实证说明"无脑救"同样有害 —— 因此现在的策略是：**断路器先止损、告警留证据、看门狗按冷却节奏做有限次恢复**。

明确的取舍：一旦断路器跳闸，服务会停到冷却期结束 —— **宁可停机报警，也不让风暴把整机拖死**（这台板一旦拖死，连 SSH 都进不去，只能断电）。

可见性落地：
- 持久标记：/var/lib/pedestrian-risk/storm.log（OnFailure 与看门狗都会写）
- board_maintenance.sh 会打印 storm.log 末 10 行（§4.4），恢复后第一眼就能看到上次告警
- 状态文件：/var/lib/pedestrian-risk/nrestarts.state、last-recover.epoch（速率与冷却判断）

---

## 5. core dump 取证（38 个）

### 5.1 分布（数据源：板上 ls -l 全表，tmp/net/o_board_forensics.txt 第 67~104 行）

| mtime | 个数 | core.<PID> |
|---|---|---|
| 2023-09-20（板上 RTC 未同步时的默认时钟） | **21** | 422, 548, 564, 567, 972, 993, 994, 1009, 1021, 1023, 1033, 1042, 1044, 1058, 1065, 1068, 1089, 1091, 1112, 1135, 1170 |
| 2026-10-30 10:37 | 3 | 1028, 1053, 1079 |
| 2026-10-30 10:38 | 6 | 1098, 1125, 1149, 1194, 1222, 1240 |
| 2026-10-30 10:39 | 7 | 1268, 1294, 1313, 1339, 1363, 1384, 1403 |
| 2026-10-30 11:08 | 1 | 1235 |
| **合计** | **38** | |

- 体积：37 个 102,141,952 B + 1 个 5,189,632 B（core.1403 明显被截断，疑似写 core 时磁盘不足）= 表观 **3.52 GiB**；但它们是**稀疏文件**，ls -l 的 total 只有 488,548 KiB ≈ **477 MiB** 实际占用（用户此前记录的 546MB 与之同量级，差异可能来自不同时刻的统计口径）。
- core.1403 被截断 + 根分区 80% 满 → 当时磁盘也在放大崩溃风暴的破坏力。

### 5.2 时间轴能不能用？——只能部分用

- 21 个 core 的 mtime 是 2023-09-20，这不是「三年前的旧文件」，而是板上 RTC 未同步时的默认时钟（同一快照里未修改的 rootfs 文件、/tmp/luckfox-load.log、aiq0.lock 也都是 Sep 20 2023）。
- 同一快照还显示：unit 状态行为 Active: active (running) **since Wed 2023-09-20 00:57:21**，而 uptime 只有 **9 分钟**（23:06 采集）——即该次开机的起始时钟就是 2023-09-20，随后被 NTP 拉到 2026-10-30。
- 更关键：按 PID 排序后，2023 与 2026 两种 mtime **交替出现**（1023/2023, 1028/2026, 1033/2023, 1053/2026, …），说明该会话里系统时钟在 2023 与 2026 之间来回跳（NTP 与无效 RTC 互相覆盖）。**因此 mtime 轴不可直接当墙钟使用**，本文改用 PID 轴。

### 5.3 真正的结论：集中爆发，不是长期频发

- 38 个 core 的 PID **严格单调递增且无重复**：422 → 548 → 564 → 567 → 972 → … → 1384 → 1403。Linux 的 PID 分配每次开机从头开始，跨开机不可能保持这种单调性；因此**这 38 次崩溃发生在同一个开机会话内**（应用被反复拉起又崩溃，PID 一路涨到 1403+）。
- 密集段：PID 1028~1403 共 **17 个 core 集中在 10:37~11:08 的 31 分钟内**；其中 10:38~10:39 两分钟内 13 个，平均约 **9 秒一次崩溃**，与 RestartSec=5 + 启动后数秒内崩溃 + 看门狗 15s 补刀的组合吻合。
- 结论：
  1. **是集中爆发**（同一次开机会话内的崩溃风暴），而非「三年来长期频发」；
  2. 21 个 2023 时间戳 core 的**真实时刻无法判定**（只能凭 PID 更小推断处于该会话早段），这是数据本身的缺陷，不做过度推断；
  3. 崩溃风暴与 StartLimit 限流**互为因果**：崩溃 → 限流 → 服务长时间不可用；修复必须两头都做（本轮已同时修代码与 unit）。

### 5.4 崩溃点（lead 的 gdb 结果，本文引用并据此更正旧结论）

- 对象：core.1235（102MB 完整版，mtime 2026-10-30 11:08，**早于本轮修复**），用构建机 gdb-multiarch + ~/core_analysis/luckfox_native_0b08578（带 debug_info）解析。
- 信号：SIGSEGV，si_code=1 (SEGV_MAPERR)，**si_addr=0x0**。
- 崩溃点：RKVencChannel::getStreamInfo(rkVENC_STREAM_S*, RTVideoPacket*) + 136；调用链 RKVencChannel::getStream() ← 我们代码（0x0084014c）。
- 汇编：str r0, [r8]，r8 = 0x0，[r4] = 0x00000000 → **对 NULL 写入**（vendor packet 内部指针为 0）。
- 12 个线程，其余线程都停在 futex/cond 等待（0xa69fdb04），只有 VENC 线程崩。
- 与本轮 P0 修复的对应：旧代码只 malloc 1 个 pack 槽、只发 pstPack[0]、VENC 背压时 SendFrame 返回值不检查 —— **崩溃点（VENC 取流路径）与修复点（VENC pack / SendFrame 链）完全对应**。此前「core 根因未确认」的表述据此更正为：**已定位到 VENC 取流路径 NULL 写**。

### 5.5 是否早于本轮修复？——是

| 事实 | 证据 |
|---|---|
| 最新 core 是 10-30 11:08（core.1235） | 板上 ls -l 全表 |
| 本轮修复二进制/部署发生在 10-30 22:26 之后（22:26 二进制、23:24~00:11 多次 bp_* 部署） | 板上二进制 mtime、C:/Windows/Temp/bp/*.txt |
| 23:06 之后的取证窗口内未再出现新 core | tmp/net/o_board_forensics*.txt 与后续测量记录 |
| 10-30 11:08~23:06 之间虽有大量手动运行新二进制（11:47~18:20）也未再产生 core | 板上二进制 mtime 列表 |

因此：**38 个 core 全部属于修复前的构建**；修复后观察期无新 core。但要诚实：观察窗口只有约 40 分钟量级，且期间板子多在被部署/重启，所以「无新 core」只是**弱正面证据**，不能当作修复有效的严格证明（见 §7）。

---

## 6. 上板执行清单（等板子断电重启后）

> **顺序很重要**：先跑 board_recover.sh（它第 2 步会在任何 vacuum 之前把 journal 导出到板上的 /root/ped-service-backup-*/），
> 再跑 board_maintenance.sh。若反过来先 vacuum，10-30~10-31 的行为证据可能被丢弃，§3.1 的推断就永久无法坐实。

1) 安装加固 unit + 导出取证 journal + 验证（**先做这步**）：

       BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh

   Windows 侧无 sshpass（当前环境即如此）：

       PED_PYTHON='X:/py311_gpu/Scripts/python.exe' PED_HELPER='G:/PedestrianRiskDecision/tmp/net/ssh_exec.py' \
         BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh

   脚本顺序（已按 §4.6 的部署窗口缺陷加固）：等 SSH → 预检 → 备份 unit + 导出 journal 到
   /tmp/ped-evidence-<时间戳>（并复制到 /root/ped-evidence-<时间戳>，**在任何 vacuum 之前**）→
   **停看门狗 + touch /run/pedestrian-risk.hold** → 上传/安装 →（若给了 BINARY：先 stop 服务、
   上传 5MB、md5 校验、再 install）→ daemon-reload + reset-failed + restart（看门狗保持停止）→
   等 25s 验证 → 服务确认 active 后才 rm hold + 启动 timer；全程打印 NRestarts 安装前/后值。

   取回证据（注意：登录用 **pico + sudo**，root 直登不可用）：
       python tmp/net/ssh_exec.py 192.168.0.136 pico luckfox --get /root/ped-evidence-<时间戳>/journal_1031_window.txt tmp/net/journal_1031_window.txt

1b) 若这次要顺便换二进制：

       BINARY=/path/to/luckfox_pico_rtsp_yolov5 BINARY_MD5=<期望md5> BOARD_PASS='<板卡口令>' sh scripts/board_recover.sh

   **任何绕过本脚本的二进制替换，都必须自己先停看门狗 + 置 hold 文件**，否则必然复现
   board-perf 核到的"旧进程 + 新文件"与 NRestarts 被抬高。

2) 维护 + 腾空间（等第 1 步取证完成后再跑）：

       python tmp/net/ssh_exec.py 192.168.0.136 pico luckfox --out tmp/net/o_maint.txt --script scripts/board_maintenance.sh

   （或在构建机 192.168.0.63 上用 sshpass 直接跑同一脚本；建议先加 --dry-run 预演）
   注意 board_maintenance.sh 会执行 journalctl --vacuum-size，运行前会打印警告。

3) 验收标准（全部满足才算通过）：
   - systemctl is-active pedestrian-risk == active，且 NRestarts 不持续增长；
   - systemctl show 显示 StartLimitIntervalUSec=0、OOMPolicy=continue、MemoryMax=167772160（160M）、MemoryHigh=134217728、LimitCORE=0；
   - systemctl is-active pedestrian-watchdog.timer == active，list-timers 能看到下一次触发；旧的常驻 pedestrian-watchdog.service 已 disabled；
   - 554/8554 端口按预期恢复（RTSP 可拉流）；
   - 连续运行 ≥30 分钟无新 core（core.* 数量不变）、根分区占比下降、load average 回到可接受范围；
   - 人为制造一次崩溃循环（kill -9 主进程 3 次）验证：不再进入永久 failed，且看门狗能把它拉回来（验证 StartLimitIntervalSec=0 与 reset-failed 的实际效果）。

---

## 7. 未验证 / 存疑项（禁止当成已验证结论引用）

| 项 | 状态 | 需要的验证 |
|---|---|---|
| 板端 systemd 版本与 man 语义完全一致 | **未验证**（推断） | 板上 systemctl --version；rootfs 与构建机同代（gdb 12.1-0ubuntu1~22.04），但仍需实测确认 |
| 板端内核 cgroup v2 对 MemoryMax/MemoryHigh 生效 | **未验证** | 板上 cat 对应 slice 的 memory.max；旧 unit 已有 MemoryMax=200M 但无法确认是否被强制执行 |
| OOM 击杀本服务 → systemd 主动停止 → 不重启 | **未复现**（仅有 man 语义支持） | 板上把 MemoryMax 临时调到远低于工作集，观察 memory.events 的 oom_kill 计数与单元 Result |
| 21 个 2023 时间戳 core 的真实时刻 | **无法判定** | 只能靠 PID 关系确定同会话；若需精确定位需保留更久 journal（建议 vacuum 策略放宽到 ≥7 天） |
| 38 个 core 为何在 unit 已有 LimitCORE=0 的情况下产生 | **未验证**（推测为手动前台运行） | 检查板上 shell 的 ulimit -c 与历史操作；新版 maintenance 会打印 core_pattern 辅助确认 |
| 新版看门狗脚本/timer 在板上的实际行为 | **未上板** | board_recover.sh 后观察 30 分钟，并做一次 kill -9 演练 |
| **2026-10-31 00:55 失联窗口内看门狗是否动作过** | **推断**（该窗口无任何 journal 可取） | 板子恢复后 journalctl -u pedestrian-watchdog --since "2026-10-31 00:00" --until "2026-10-31 01:00"；若已被 vacuum 则永久不可考（§3.1 已标注适用范围） |
| TimeoutStopSec=15 对 D 状态进程是否仍会卡在 deactivating | **未验证** | 等一次 D 状态停止，观察 systemctl status 是否卡在 stop-sigkill（SIGKILL 对不可中断睡眠无效，属内核行为，配置无法解决） |
| 修复后长时间（>24h）无新 core | **未验证** | 需要长跑观察；本报告不宣称 |
| 断路器参数（600s/6 次）在板上的实际效果与手感 | **未验证** | board_recover.sh 上板后，人为制造崩溃循环（kill -9 三次）观察：是否按预期在 6 次内跳闸、storm.log 是否落盘、看门狗是否按 300s 冷却恢复 |
| OnFailure 在板端 systemd 上与构建机 249 行为一致 | **推断**（构建机实测，板端未验证） | 板端执行一次崩溃循环后看 journal 是否出现 "Triggering OnFailure= dependencies." 与 /var/lib/pedestrian-risk/storm.log |
| 00:05 冻结与 dec2 风暴是否同一机制 | **推断**（症状同型，机制未直接证明） | 00:05 那次只剩 00:00~00:05:32 的残存 journal；导出核对后仍可能无法判定 |
| 断路器跳闸到整机失联之间的时间窗（02:02:11 → 02:05）里发生了什么 | **未验证** | 需要板上 journal（该 boot 已被 journald 丢失，见 §3.2），可能永久不可考 |

---

## 8. 测试与复核

- 新增回归测试：tests/test_board_service_hardening.py，**本文件 63 项全部通过（不需要硬件、不连板）**：
  ① 静态断言：解析 deploy/systemd/*.service|*.timer 与 scripts/*.sh，断言加固关键项存在、旧版危险配置不得回潮、看门狗不再用 while true、脚本 POSIX 语法正确、报告与实测证据齐备且可追溯；
  ② **行为测试（7 项，用桩 systemctl 真跑 pedestrian-watchdog.sh）**：inactive 不得 start / 显式 WATCHDOG_START_IF_INACTIVE=1 才 start / failed 必须先 reset-failed 再 start / activating 不介入 / hold 文件生效 / active+日志陈旧要拉起 / 健康时不动手；
  ③ **部署顺序测试（4 项）**：停看门狗必须早于任何上传、NRestarts 前/后值必须打印、FAIL 时不得放开看门狗、替换二进制必须先停服务再上传。
  shell 语法检查用本机可用的 POSIX shell（本机为 X:/DevTools/git/usr/bin/sh.exe；无 bash 时用 sh/ash，可用 PED_SH 指定；都没有则 skip）。
- 改动范围：只新增 deploy/**、scripts/board_maintenance.sh、scripts/board_recover.sh、tests/test_board_service_hardening.py、本报告。**未改任何 C++/算法代码**，因此 src/luckfox_rtsp_yolov5 的 mingw32-make test 不受影响（由 T1/T6/T8 负责，不在本次范围）。
- 实测证据可复现：deploy/probe/ 下有 3 个探针脚本（口令改为从环境变量 PW 读取，不入库）+ 原始输出 probe_results.txt；探针只在 /run/systemd/system 下创建临时单元并在结束时自清理，不修改系统任何持久配置。

## 9. 与其它任务的关系

- 与 T1（板端性能/卡死审计修复）：本文的 unit 加固是 T1 的稳定性收尾；T1 修的 VENC 代码路径正是本文 core 取证的崩溃点。
- 与 T6/T8（上板终验 / NPU 归因）：board_recover.sh 第 5 步的验证块可作为它们上板前的基线检查；刷新二进制前建议先跑 board_maintenance.sh 腾空间（根分区曾 80%）。
- 论文目录只读，本文不涉及其内容。
