# -*- coding: utf-8 -*-
"""Parse a `nc3000_headless.exe --dsp-trace` log into the exact 6502->061 byte
stream of one playback, split into (setup cmds, TTS records, 0x33/0x22 stream
payload).  Useful to check what the firmware really sends for one word.

usage: parse_uart_log.py <log> [--payload-out file.bin]
"""
import re
import struct
import sys


def main():
    args = sys.argv[1:]
    out = None
    if "--payload-out" in args:
        i = args.index("--payload-out")
        out = args[i + 1]
        del args[i:i + 2]
    wire_out = None
    if "--wire-out" in args:
        i = args.index("--wire-out")
        wire_out = args[i + 1]
        del args[i:i + 2]
    log = args[0]

    sent = []
    for line in open(log, encoding="utf-8", errors="ignore"):
        m = re.search(r"\[6502>061\] ([0-9A-F]{2})\s+@(\d+)ms", line)
        if m:
            sent.append((int(m.group(2)), int(m.group(1), 16)))
    # keep only the burst around the playback (drop the boot handshakes)
    if not sent:
        print("no [6502>061] lines")
        return 1
    tmax = max(t for t, _ in sent)
    stream = [b for t, b in sent if t > tmax - 3000]
    print("burst bytes: %d" % len(stream))

    p = 0
    cmds = []
    while p + 1 < len(stream):
        op = stream[p]
        if op == 0xBB:
            cmds.append(("link", stream[p:p + 2]))
            p += 2
        elif op in (0x44, 0x55, 0x99):
            cmds.append(("cmd %02X" % op, stream[p:p + 2]))
            p += 2
        elif op == 0xAA:
            cmds.append(("aa %02X" % stream[p + 1], stream[p:p + 2]))
            p += 2
        elif op == 0x11:
            sub = stream[p + 1]
            if sub == 0x01:
                cmds.append(("records=%d" % stream[p + 2], stream[p:p + 3]))
                p += 3
            elif sub == 0x03:
                cmds.append(("record", stream[p:p + 5]))
                p += 5
            else:
                cmds.append(("11 %02X ?" % sub, stream[p:p + 2]))
                p += 2
        else:
            break
    for name, b in cmds:
        print("  %-12s %s" % (name, " ".join("%02X" % x for x in b)))

    payload = bytearray()
    blocks = 0
    while p < len(stream):
        op = stream[p]
        if op == 0x33:
            payload += bytes(stream[p + 1:p + 16])
            blocks += 1
            p += 16
        elif op == 0x22:
            n = stream[p + 1]
            payload += bytes(stream[p + 2:p + 2 + n])
            blocks += 1
            p += 2 + n
        else:
            print("  (stop at %02X, %d bytes left)" % (op, len(stream) - p))
            break
    print("stream: %d blocks, %d payload bytes (%.2f frames of 18)"
          % (blocks, len(payload), len(payload) / 18.0))
    if wire_out:
        open(wire_out, "wb").write(bytes(stream))
        print("wire ->", wire_out)
    if out:
        open(out, "wb").write(bytes(payload))
        print("payload ->", out)


if __name__ == "__main__":
    sys.exit(main())
