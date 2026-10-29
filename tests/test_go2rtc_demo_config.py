# -*- coding: utf-8 -*-
"""
go2rtc demo 流配置工具单元测试（P2-1）
========================================
验证 scripts/go2rtc_demo_stream.py 的 YAML 行级编辑逻辑：
1. 在已有 cam 流配置上新增 demo 流，cam 保留；
2. 幂等：同一视频重复执行不改变文件；
3. 更新：demo 流指向不同视频时原位替换；
4. 损坏配置（无 streams 块）自动重建为最小可运行配置；
5. 写回前自动备份（.bak.<时间戳>）；
6. --dry-run 不写文件；
7. 空文件创建最小配置；
8. CLI 入口返回码与输出。

可两种方式运行：
  python tests/test_go2rtc_demo_config.py          # 独立运行（无需 pytest）
  python -m pytest tests/test_go2rtc_demo_config.py  # 若安装了 pytest
"""

import os
import shutil
import subprocess
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPTS = os.path.join(PROJECT_ROOT, "scripts")
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)

from go2rtc_demo_stream import (  # noqa: E402
    add_demo_stream,
    load_lines,
    minimal_config,
    now_stamp,
    write_with_backup,
)

# ---------------------------------------------------------------------------
# 工具
# ---------------------------------------------------------------------------
CAM_CFG = [
    "streams:\n",
    "  cam:\n",
    "  - rtsp://127.0.0.1:554/live/0\n",
    "api:\n",
    '  listen: ":1984"\n',
]


def write(path, lines):
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(lines)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
class TestAddDemoStream:
    def test_add_demo_keeps_cam(self):
        lines = list(CAM_CFG)
        new, changed = add_demo_stream(lines, "/userdata/demo/mixkit_13192_enhanced.mp4")
        assert changed == "created"
        text = "".join(new)
        assert "  cam:\n" in text
        assert "  - rtsp://127.0.0.1:554/live/0\n" in text
        assert "  demo:\n" in text
        assert "  - file:///userdata/demo/mixkit_13192_enhanced.mp4\n" in text
        assert '  listen: ":1984"\n' in text
        # demo 应插入在 streams 块内、api 之前
        assert text.index("  demo:") < text.index("api:")
        assert text.index("api:") < text.index('  listen:')

    def test_idempotent_same_video(self):
        lines = list(CAM_CFG)
        new, changed = add_demo_stream(lines, "/userdata/demo/mixkit_13192_enhanced.mp4")
        new2, changed2 = add_demo_stream(new, "/userdata/demo/mixkit_13192_enhanced.mp4")
        assert changed2 == "unchanged"
        assert new2 == new

    def test_update_existing_demo(self):
        lines = list(CAM_CFG)
        new, _ = add_demo_stream(lines, "/userdata/demo/mixkit_13192_enhanced.mp4")
        new2, changed = add_demo_stream(new, "/userdata/demo/mixkit_13265_enhanced.mp4")
        assert changed == "updated"
        text = "".join(new2)
        assert "file:///userdata/demo/mixkit_13265_enhanced.mp4" in text
        assert "file:///userdata/demo/mixkit_13192_enhanced.mp4" not in text
        assert "  - rtsp://127.0.0.1:554/live/0\n" in text  # cam 仍在

    def test_update_keeps_other_streams(self):
        lines = [
            "streams:\n",
            "  cam:\n",
            "  - rtsp://127.0.0.1:554/live/0\n",
            "  demo:\n",
            "  - file:///old.mp4\n",
            "api:\n",
            '  listen: ":1984"\n',
        ]
        new, changed = add_demo_stream(lines, "/userdata/demo/new.mp4")
        assert changed == "updated"
        text = "".join(new)
        assert "file:///userdata/demo/new.mp4" in text
        assert "file:///old.mp4" not in text
        assert "  - rtsp://127.0.0.1:554/live/0\n" in text

    def test_corrupt_config_rebuilt(self):
        lines = ["api:\n", '  listen: ":1984"\n']  # 无 streams 块
        new, changed = add_demo_stream(lines, "/userdata/demo/mixkit_13192_enhanced.mp4")
        assert changed == "created"
        text = "".join(new)
        assert "streams:\n" in text
        assert "  demo:\n" in text
        assert "file:///userdata/demo/mixkit_13192_enhanced.mp4" in text
        assert '  listen: ":1984"\n' in text  # api 保留

    def test_empty_lines_create_minimal(self):
        new, changed = add_demo_stream([], "/userdata/demo/mixkit_13192_enhanced.mp4")
        assert changed == "created"
        text = "".join(new)
        assert "streams:\n" in text
        assert "  demo:\n" in text
        assert "file:///userdata/demo/mixkit_13192_enhanced.mp4" in text
        assert "api:\n" in text

    def test_custom_stream_name(self):
        new, changed = add_demo_stream(
            list(CAM_CFG), "/userdata/demo/other.mp4", stream_name="fallback")
        assert changed == "created"
        assert "  fallback:\n" in "".join(new)
        assert "  demo:\n" not in "".join(new)

    def test_minimal_config_contains_api(self):
        cfg = "".join(minimal_config("/userdata/demo/x.mp4"))
        assert "streams:\n" in cfg
        assert "  demo:\n" in cfg
        assert "api:\n" in cfg
        assert '  listen: ":1984"\n' in cfg


class TestFileIO:
    def test_write_with_backup(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        write(str(cfg), CAM_CFG)
        new, _ = add_demo_stream(load_lines(str(cfg)), "/userdata/demo/mixkit_13192_enhanced.mp4")
        write_with_backup(str(cfg), new)
        text = read(str(cfg))
        assert "  demo:\n" in text
        backups = list(tmp_path.glob("go2rtc.yaml.bak.*"))
        assert len(backups) == 1
        assert "  cam:\n" in read(str(backups[0]))  # 备份是原内容

    def test_backup_not_created_on_first_write(self, tmp_path):
        cfg = tmp_path / "new.yaml"
        new, _ = add_demo_stream([], "/userdata/demo/mixkit_13192_enhanced.mp4")
        write_with_backup(str(cfg), new)
        assert read(str(cfg)).startswith("streams:\n")
        assert list(tmp_path.glob("new.yaml.bak.*")) == []  # 原文件不存在，无需备份

    def test_load_lines_missing_file(self, tmp_path):
        assert load_lines(str(tmp_path / "nope.yaml")) == []

    def test_dry_run_no_write(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        write(str(cfg), CAM_CFG)
        rc = subprocess.call([
            sys.executable, os.path.join(SCRIPTS, "go2rtc_demo_stream.py"),
            "--config", str(cfg),
            "--video", "/userdata/demo/mixkit_13192_enhanced.mp4",
            "--dry-run",
        ])
        assert rc == 0
        assert "  demo:\n" not in read(str(cfg))  # 未写入
        assert list(tmp_path.glob("go2rtc.yaml.bak.*")) == []

    def test_cli_add_and_idempotent(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        write(str(cfg), CAM_CFG)
        py = os.path.join(SCRIPTS, "go2rtc_demo_stream.py")
        r1 = subprocess.run(
            [sys.executable, py, "--config", str(cfg),
             "--video", "/userdata/demo/mixkit_13192_enhanced.mp4"],
            capture_output=True, text=True)
        assert r1.returncode == 0
        assert "done:" in r1.stdout
        assert "  demo:\n" in read(str(cfg))
        r2 = subprocess.run(
            [sys.executable, py, "--config", str(cfg),
             "--video", "/userdata/demo/mixkit_13192_enhanced.mp4"],
            capture_output=True, text=True)
        assert r2.returncode == 0
        assert "unchanged:" in r2.stdout

    def test_cli_missing_args(self):
        py = os.path.join(SCRIPTS, "go2rtc_demo_stream.py")
        r = subprocess.run([sys.executable, py], capture_output=True, text=True)
        assert r.returncode == 2  # argparse 缺参报错


class TestShellScript:
    """通过 bash 脚本 --dry-run 验证编辑逻辑（与 Python 工具等价）。

    注：目标运行环境是开发板 Linux；CI（GitHub Actions Ubuntu）会真实执行。
    Windows 本机存在 MSYS 参数路径转换差异（反斜杠/转义），故跳过。
    """

    BASH_SCRIPT = os.path.join(SCRIPTS, "setup_go2rtc_demo_stream.sh")

    @staticmethod
    def _bash():
        if os.name == "nt":
            import pytest
            pytest.skip("Windows MSYS 路径转换与 Linux 不同，shell 测试由 CI 覆盖")
        bash = shutil.which("bash")
        if not bash:
            import pytest
            pytest.skip("bash 不在 PATH，跳过 shell 脚本测试")
        return bash

    def test_shell_dry_run_adds_demo_keeps_cam(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        video = tmp_path / "demo.mp4"
        write(str(cfg), CAM_CFG)
        video.write_text("fake", encoding="utf-8")
        bash = self._bash()
        env = dict(os.environ, GO2RTC_CFG=str(cfg), GO2RTC_BIN="/bin/true")
        r = subprocess.run(
            [bash, self.BASH_SCRIPT,
             "--video", str(video), "--dry-run"],
            capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stderr
        out = r.stdout
        assert "  demo:\n" in out
        assert "file://%s" % video in out
        assert "  cam:\n" in out
        assert "rtsp://127.0.0.1:554/live/0" in out

    def test_shell_dry_run_idempotent(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        video = tmp_path / "demo.mp4"
        write(str(cfg), CAM_CFG + [
            "  demo:\n", "  - file://%s\n" % video])
        video.write_text("fake", encoding="utf-8")
        bash = self._bash()
        env = dict(os.environ, GO2RTC_CFG=str(cfg), GO2RTC_BIN="/bin/true")
        r = subprocess.run(
            [bash, self.BASH_SCRIPT,
             "--video", str(video), "--dry-run"],
            capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stderr
        # 幂等：输出仍是同一指向，不出现第二个 demo
        assert r.stdout.count("  demo:\n") == 1

    def test_shell_dry_run_update_old_video(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        video = tmp_path / "demo.mp4"
        write(str(cfg), CAM_CFG + [
            "  demo:\n", "  - file:///old.mp4\n"])
        video.write_text("fake", encoding="utf-8")
        bash = self._bash()
        env = dict(os.environ, GO2RTC_CFG=str(cfg), GO2RTC_BIN="/bin/true")
        r = subprocess.run(
            [bash, self.BASH_SCRIPT,
             "--video", str(video), "--dry-run"],
            capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stderr
        assert "file:///old.mp4" not in r.stdout
        assert "file://%s" % video in r.stdout

    def test_shell_missing_video_exits_1(self, tmp_path):
        cfg = tmp_path / "go2rtc.yaml"
        write(str(cfg), CAM_CFG)
        bash = self._bash()
        env = dict(os.environ, GO2RTC_CFG=str(cfg), GO2RTC_BIN="/bin/true")
        r = subprocess.run(
            [bash, self.BASH_SCRIPT,
             "--video", str(tmp_path / "nope.mp4"), "--dry-run"],
            capture_output=True, text=True, env=env)
        assert r.returncode == 1
        assert "视频文件不存在" in r.stdout


# ---------------------------------------------------------------------------
# 独立运行入口
# ---------------------------------------------------------------------------
def _run_all():
    import tempfile
    from pathlib import Path

    passed = 0
    failed = 0
    skipped = 0
    test_classes = [TestAddDemoStream, TestFileIO, TestShellScript]
    for cls in test_classes:
        obj = cls()
        for name in sorted(dir(obj)):
            if not name.startswith("test_"):
                continue
            method = getattr(obj, name)
            try:
                if cls is TestShellScript and os.name == "nt":
                    skipped += 1
                    print("SKIP %s.%s（Windows MSYS 路径转换，shell 测试由 CI 覆盖）"
                          % (cls.__name__, name))
                    continue
                if name in (
                    "test_write_with_backup",
                    "test_backup_not_created_on_first_write",
                    "test_load_lines_missing_file",
                    "test_dry_run_no_write",
                    "test_cli_add_and_idempotent",
                    "test_shell_dry_run_adds_demo_keeps_cam",
                    "test_shell_dry_run_idempotent",
                    "test_shell_dry_run_update_old_video",
                    "test_shell_missing_video_exits_1",
                ):
                    with tempfile.TemporaryDirectory() as td:
                        method(Path(td))
                else:
                    method()
                passed += 1
                print("PASS %s.%s" % (cls.__name__, name))
            except Exception as e:  # noqa: BLE001
                failed += 1
                print("FAIL %s.%s: %r" % (cls.__name__, name, e))
    print("\n%d passed, %d failed, %d skipped" % (passed, failed, skipped))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_run_all())