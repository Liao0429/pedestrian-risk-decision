#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 dataset/images/ 下所有图片: 文件大小、JPEG 完整性、宽高。输出 images_manifest.csv"""
import os, csv, struct, sys

IMGDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images")

def jpeg_dimensions(data: bytes):
    """从 JPEG 头解析宽高 (SOF markers). 返回 (width, height) 或 None."""
    if data[:2] != b'\xff\xd8':
        return None
    i = 2
    n = len(data)
    while i + 9 < n:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i+1]
        if marker in (0xD8, 0xD9) or (marker >= 0xD0 and marker <= 0xD7):
            i += 2
            continue
        if i + 4 > n:
            return None
        length = struct.unpack('>H', data[i+2:i+4])[0]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            if length >= 5 and i + 9 <= n:
                h = struct.unpack('>H', data[i+5:i+7])[0]
                w = struct.unpack('>H', data[i+7:i+9])[0]
                return (w, h)
        i += 2 + length
    return None

def main():
    rows = []
    for fn in sorted(os.listdir(IMGDIR)):
        if not fn.lower().endswith(('.jpg', '.jpeg', '.png')):
            continue
        path = os.path.join(IMGDIR, fn)
        size = os.path.getsize(path)
        with open(path, 'rb') as f:
            data = f.read(min(size, 4 * 1024 * 1024))  # 读全文件(小图)以捕获偏移靠后的 SOF
        if data[:2] == b'\xff\xd8':
            dim = jpeg_dimensions(data)
            fmt = 'JPEG'
        elif data[:8] == b'\x89PNG\r\n\x1a\n':
            dim = (struct.unpack('>I', data[16:20])[0], struct.unpack('>I', data[20:24])[0])
            fmt = 'PNG'
        else:
            fmt, dim = 'UNKNOWN', None
        ok = dim is not None
        rows.append((fn, fmt, size, dim[0] if ok else 0, dim[1] if ok else 0, "OK" if ok else "BAD"))
        print(f"{fn:32s} {fmt:6s} {size:>9d} B  {dim if ok else 'INVALID'}")

    manifest = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images_manifest.csv")
    with open(manifest, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["filename", "format", "size_bytes", "width", "height", "status"])
        w.writerows(rows)
    bad = [r for r in rows if r[5] != "OK"]
    print("\ntotal:", len(rows), " valid:", len(rows)-len(bad), " invalid:", len(bad))
    print("manifest written:", manifest)

if __name__ == "__main__":
    main()