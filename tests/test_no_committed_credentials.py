#!/usr/bin/env python3
"""凭据防泄漏守卫（2026-10-31 安全整改）。

背景：本仓库是**公开仓库**，而 2026-10-30/31 两轮工作一度把以下凭据写死进被跟踪的文件并推到 GitHub：
  - scripts/board_recover.sh  的 BOARD_PASS 默认值
  - scripts/board_health_snapshot.py 的 PASS 默认值
  - docs/ 里的 用户/口令 简写（如 pico/<口令>）
现已全部改为环境变量注入（BOARD_PASS / BOARD_PASS_FILE），但 **git 历史里仍在**，
所以**必须由设备所有者轮换板卡与构建机口令** —— 这是人工动作，本仓库无法代劳。
本测试只负责防止同类问题再次进入版本库。

设计要点：
1. 不把口令明文写进测试（否则测试自己就成了泄漏源），只比对 token 的 SHA-256。
2. **只做上下文匹配**：`luckfox` 同时是产品/厂商名（全仓库到处都是），
   裸 token 黑名单会误报；只有出现在"口令位置"（PASS=、sshpass -p、
   用户/口令简写、sudo -S 喂入）才判定泄漏。
3. 只检查 git 跟踪的文件（只有它们会进入公开仓库）；无 git 时 skip。
4. 自带回归自测：对历史版本（引入了泄漏的那次提交）必须能报出问题，
   否则本守卫是空的。
"""

import hashlib
import re
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent

# 已知被泄漏过的口令（只存哈希）
BAD_SHA256 = {hashlib.sha256(b"luckfox").hexdigest(), hashlib.sha256(b"123456").hexdigest()}

# 引入泄漏的历史提交（用于自测守卫是否真的有效）
HISTORICAL_LEAK_COMMIT = "e084f4c"

# "口令位置"捕获组：只有这些位置出现的 token 才做黑名单比对
CREDENTIAL_CONTEXT = [
    (re.compile(r"(?:PASS|PASSWORD|PW|PWD|PASSWD)\s*[:=]\s*['\"]?([A-Za-z0-9_.@-]{4,})"), "口令赋值"),
    (re.compile(r"sshpass\s+-p\s*['\"]?([A-Za-z0-9_.@-]{4,})"), "sshpass -p 字面量"),
    (re.compile(r"echo\s+['\"]?([A-Za-z0-9_.@-]{4,})['\"]?\s*\|\s*sudo\s+-S"), "口令管道喂给 sudo -S"),
    (re.compile(r"\b(?:pico|root|liao|luckfox)/([A-Za-z0-9_.@-]{4,})(?![A-Za-z0-9_./-])"), "用户/口令简写"),
    (re.compile(r"environ\.get\(\s*['\"][A-Z_]*PASS[A-Z_]*['\"]\s*,\s*['\"]([^'\"]+)['\"]"), "环境变量默认口令"),
]


def _sha(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def scan_text(text: str):
    """返回 [(lineno, why, snippet)]；只报上下文匹配且命中黑名单的 token。"""
    hits = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for rx, why in CREDENTIAL_CONTEXT:
            for m in rx.finditer(line):
                token = m.group(1)
                if _sha(token) in BAD_SHA256:
                    hits.append((lineno, why, line.strip()[:120]))
    return hits


def _git(*args, cwd=REPO, timeout=60):
    try:
        return subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
    except Exception:
        return None


def tracked_files():
    out = _git("ls-files")
    if out is None or out.returncode != 0:
        pytest.skip("git 不可用或 git ls-files 失败")
    return [x for x in out.stdout.splitlines() if x.strip()]


def _read(rel):
    p = REPO / rel
    if not p.is_file():
        return None
    try:
        return p.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None


def test_no_credentials_in_tracked_files():
    offenders = []
    for rel in tracked_files():
        text = _read(rel)
        if not text:
            continue
        for lineno, why, snippet in scan_text(text):
            offenders.append("%s:%d [%s] %s" % (rel, lineno, why, snippet))
    assert not offenders, "跟踪文件里出现了已知口令（改为环境变量注入，并轮换口令）:\n" + "\n".join(offenders)


def test_guard_actually_catches_the_historical_leak():
    """自测：守卫必须能报出历史版本里的泄漏，否则等于没装。"""
    out = _git("show", "%s:scripts/board_recover.sh" % HISTORICAL_LEAK_COMMIT)
    if out is None or out.returncode != 0:
        pytest.skip("拿不到历史版本 %s:scripts/board_recover.sh" % HISTORICAL_LEAK_COMMIT)
    hits = scan_text(out.stdout)
    assert hits, "守卫没能在历史版本里发现已知泄漏 —— 上下文规则失效了"


def test_product_name_alone_is_not_flagged():
    """误报防护：`luckfox` 是产品名，裸 token 不应被判为泄漏。"""
    assert not scan_text("我们从 luckfox 官方 SDK 里复制了 luckfox_pico_rtsp_yolov5")
    assert not scan_text("# 板端 root 直登不可用，现用 pico 登录 + sudo")
