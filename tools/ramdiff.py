# -*- coding: utf-8 -*-
"""Find RAM bytes that track a monotonically changing value across snapshots.

usage: ramdiff.py <file0> <file1> ... [--max 40]

Prints addresses whose byte value increases/decreases by a constant step
across the whole series (useful for locating menu cursors / counters).
"""
import sys


def main():
    args = sys.argv[1:]
    limit = 40
    if "--max" in args:
        i = args.index("--max")
        limit = int(args[i + 1])
        del args[i:i + 2]
    data = [open(p, "rb").read() for p in args]
    n = len(data)
    size = min(len(d) for d in data)
    hits = []
    for a in range(size):
        vals = [d[a] for d in data]
        if len(set(vals)) < 3:
            continue
        diffs = [(vals[i + 1] - vals[i]) & 0xFF for i in range(n - 1)]
        step = diffs[0]
        if step == 0 or not all(x == step for x in diffs):
            continue
        signed = step if step < 128 else step - 256
        hits.append((a, vals, signed))
    for a, vals, step in hits[:limit]:
        print("$%04X step=%+d %s" % (a, step, " ".join("%02X" % v for v in vals)))
    print("total %d" % len(hits))


if __name__ == "__main__":
    main()
