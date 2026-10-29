#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
go2rtc demo 流配置工具（P2-1：本地视频流兜底演示）
====================================================
向 go2rtc.yaml 安全地添加/更新一个本地视频文件流（如预录增强视频），
保留已有流（cam 等），自动备份，幂等，损坏配置自动重建。

适用场景：答辩/演示时现场镜头前无人走动，仅实时画面一直显示 SAFE，
用预录的密集人群增强视频（如 mixkit_13192_enhanced.mp4）作为兜底流，
手机浏览器打开 go2rtc 网页点击 demo 流即可播放 DANGER 切换过程。

用法：
  python3 go2rtc_demo_stream.py --config /home/pico/go2rtc.yaml \\
      --video /userdata/demo/mixkit_13192_enhanced.mp4 [--stream demo] [--dry-run]

不依赖 PyYAML（板端可能未安装），用行级解析处理 go2rtc 的简单 YAML 结构：
  streams:
    cam:
    - rtsp://127.0.0.1:554/live/0
    demo:
    - file:///userdata/demo/mixkit_13192_enhanced.mp4
  api:
    listen: ":1984"
"""

import argparse
import os
import re
import shutil
import sys
from datetime import datetime

# 顶层键：无缩进的行（如 streams: / api:）
TOP_KEY_RE = re.compile(r"^\S")
# 流键：恰好两个空格缩进，后跟冒号（如 "  cam:" / "  demo:"）
STREAM_KEY_RE = re.compile(r"^  ([\w.\-]+):\s*(?:#.*)?$")
# 流值行：两个或四个空格缩进的列表项（go2rtc 常见 "  - value"）
VALUE_LINE_RE = re.compile(r"^  - |^    - ")


def now_stamp():
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def load_lines(path):
    """读取文件为行列表；文件不存在返回空列表。"""
    if not os.path.exists(path):
        return []
    with open(path, "r", encoding="utf-8") as f:
        return f.readlines()


def find_streams_block(lines):
    """定位顶层 streams 块。

    返回 (streams_idx, block_end)：
      streams_idx —— "streams:" 所在行索引；不存在则为 None
      block_end   —— streams 块结束后下一行索引（不含，即下一个顶层键或 EOF）
    """
    streams_idx = None
    for i, ln in enumerate(lines):
        if TOP_KEY_RE.match(ln) and ln.strip() == "streams:":
            streams_idx = i
            break
    if streams_idx is None:
        return None, None
    end = len(lines)
    for j in range(streams_idx + 1, len(lines)):
        if TOP_KEY_RE.match(lines[j]) and lines[j].strip():
            end = j
            break
    return streams_idx, end


def find_stream_key(lines, start, end, name):
    """在 streams 块 [start, end) 内查找名为 name 的流键行，返回行索引；不存在返回 None。"""
    for i in range(start, end):
        m = STREAM_KEY_RE.match(lines[i])
        if m and m.group(1) == name:
            return i
    return None


def collect_entry_range(lines, key_idx, end):
    """返回该流条目占用的 (first, after_last) 行范围（键行 + 其全部值行）。"""
    i = key_idx + 1
    while i < end:
        if STREAM_KEY_RE.match(lines[i]):
            break
        if lines[i].strip() and not VALUE_LINE_RE.match(lines[i]):
            # 非列表项的非空行（如内联注释/错误缩进）保守视为条目一部分，继续
            i += 1
            continue
        i += 1
    return key_idx, i


def demo_entry_lines(name, video_path):
    return ["  %s:\n" % name, "  - file://%s\n" % video_path]


def minimal_config(video_path, stream_name="demo", api_listen='":1984"'):
    """文件不存在/无法解析时重建的最小可运行配置。"""
    return (
        ["streams:\n"] + demo_entry_lines(stream_name, video_path) +
        ["api:\n", "  listen: %s\n" % api_listen]
    )


def add_demo_stream(lines, video_path, stream_name="demo"):
    """向配置行列表添加/更新 demo 流。

    返回 (new_lines, changed)：
      changed ∈ {'created', 'updated', 'unchanged'}
    """
    if not lines:
        return minimal_config(video_path, stream_name), "created"

    streams_idx, block_end = find_streams_block(lines)
    if streams_idx is None:
        # 配置损坏（无 streams 块）：由调用方先备份，再重建
        return minimal_config(video_path, stream_name), "created"

    key_idx = find_stream_key(lines, streams_idx + 1, block_end, stream_name)
    entry = demo_entry_lines(stream_name, video_path)
    if key_idx is None:
        # 不存在：追加到 streams 块末尾（block_end 之前）
        new_lines = lines[:block_end] + entry + lines[block_end:]
        return new_lines, "created"

    # 已存在：检查是否指向同一视频（幂等）
    first, after = collect_entry_range(lines, key_idx, block_end)
    existing = "".join(lines[first:after])
    if "file://%s" % video_path in existing:
        return lines, "unchanged"

    new_lines = lines[:first] + entry + lines[after:]
    return new_lines, "updated"


def write_with_backup(path, lines):
    """写回前自动备份为 <path>.bak.<时间戳>。"""
    if os.path.exists(path):
        backup = "%s.bak.%s" % (path, now_stamp())
        shutil.copy2(path, backup)
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="向 go2rtc.yaml 添加/更新本地视频 demo 流（P2-1 兜底演示）")
    parser.add_argument("--config", required=True, help="go2rtc.yaml 路径")
    parser.add_argument("--video", required=True, help="本地视频文件绝对路径（file:// 源）")
    parser.add_argument("--stream", default="demo", help="流名称（默认 demo）")
    parser.add_argument("--dry-run", action="store_true", help="只打印结果不写文件")
    args = parser.parse_args(argv)

    lines = load_lines(args.config)
    new_lines, changed = add_demo_stream(lines, args.video, args.stream)

    if changed == "unchanged":
        print("unchanged: %s 已指向 %s" % (args.stream, args.video))
        return 0

    if args.dry_run:
        print("[dry-run] 将 %s demo 流：%s" % ("更新" if changed == "updated" else "新增", args.video))
        print("".join(new_lines))
        return 0

    write_with_backup(args.config, new_lines)
    action = "更新" if changed == "updated" else "新增"
    print("done: 已%s %s 流 -> file://%s（原配置已备份为 %s.bak.*）" %
          (action, args.stream, args.video, args.config))
    return 0


if __name__ == "__main__":
    sys.exit(main())