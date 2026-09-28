#!/usr/bin/env python3
"""对比两个二进制文件的差异范围（只读分析工具）。

用法: python bindiff.py <fileA> <fileB> [--gap N] [--max N]
"""
import sys


def ranges(a: bytes, b: bytes):
    out = []
    n = min(len(a), len(b))
    i = 0
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    gap = 64
    show = 100
    for a in sys.argv[1:]:
        if a.startswith("--gap="):
            gap = int(a.split("=", 1)[1])
        if a.startswith("--max="):
            show = int(a.split("=", 1)[1])
    fa, fb = args[0], args[1]
    a = open(fa, "rb").read()
    b = open(fb, "rb").read()
    print(f"A = {fa}  ({len(a)} bytes)")
    print(f"B = {fb}  ({len(b)} bytes)")
    if len(a) != len(b):
        print("!! sizes differ; comparing common prefix only")
    rs = ranges(a, b)
    tot = sum(j - i for i, j in rs)
    print(f"differing runs: {len(rs)}   bytes: {tot}")
    merged = []
    for i, j in rs:
        if merged and i - merged[-1][1] <= gap:
            merged[-1] = (merged[-1][0], j)
        else:
            merged.append((i, j))
    print(f"merged blocks (gap<={gap}): {len(merged)}")
    for i, j in merged[:show]:
        print(f"  {i:#010x}..{j:#010x}  len={j-i:6d}  A[0:8]={a[i:i+8].hex(' ')}  B[0:8]={b[i:i+8].hex(' ')}")
    if len(merged) > show:
        print(f"  ... {len(merged)-show} more")


if __name__ == "__main__":
    main()
