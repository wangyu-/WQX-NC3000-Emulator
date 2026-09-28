#!/usr/bin/env python3
"""生成给 nc3000_headless.exe --cmd-file 用的命令文件（原始字节，支持 GBK 文件名）。

用法:
  python mk_cmdfile.py <out.bin> "put host.bin /midi/音乐1.mid"
  python mk_cmdfile.py <out.bin> --put host.bin /midi/音乐1.mid

字符串按 GBK 编码写入；路径分隔符保持原样（模拟器里 '\\' 和 '/' 都能用）。
"""
import sys


def main():
    out = sys.argv[1]
    args = sys.argv[2:]
    if args and args[0] == "--put":
        cmd = "put " + args[1] + " " + args[2]
    else:
        cmd = " ".join(args)
    data = cmd.encode("gbk")
    with open(out, "wb") as f:
        f.write(data)
    print("wrote %d bytes to %s: %s" % (len(data), out, cmd))


if __name__ == "__main__":
    main()
