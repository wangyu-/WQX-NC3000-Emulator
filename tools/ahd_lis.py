"""Filter the extracted AHD headwords down to the longest alphabetical run.

The real dictionary is in alphabetical order, so words that came from inside
definitions and break the order can be removed with a longest
non-decreasing-subsequence pass (duplicates allowed).
"""
import bisect


def load(path=r'out\ahd_headwords.txt'):
    rows = []
    for line in open(path, encoding='utf-8'):
        parts = line.rstrip('\n').split('\t')
        if len(parts) == 3:
            rows.append((int(parts[0], 16), parts[1], parts[2]))
    return rows


def lis(rows):
    """Longest non-decreasing subsequence over the lowercase key."""
    keys = [w.lower() for _, w, _ in rows]
    tails = []          # tails[j] = index of the smallest tail of a run of length j+1
    tail_keys = []      # keys[tails[j]] kept in sync
    prev = [-1] * len(keys)
    for i, k in enumerate(keys):
        j = bisect.bisect_right(tail_keys, k)
        if j == len(tails):
            tails.append(i)
            tail_keys.append(k)
        else:
            tails[j] = i
            tail_keys[j] = k
        prev[i] = tails[j - 1] if j else -1
    out = []
    i = tails[-1]
    while i >= 0:
        out.append(i)
        i = prev[i]
    return out[::-1]


def main():
    rows = load()
    print('extracted:', len(rows))
    keep = lis(rows)
    print('longest alphabetical run:', len(keep))
    kept = [rows[i] for i in keep]
    print('first 15:', [w for _, w, _ in kept[:15]])
    print('sample mid:', [w for _, w, _ in kept[len(kept) // 2: len(kept) // 2 + 12]])
    print('last 15:', [w for _, w, _ in kept[-15:]])
    with open(r'out\ahd_headwords_clean.txt', 'w', encoding='utf-8') as f:
        for off, w, m in kept:
            f.write('%07X\t%s\t%s\n' % (off, w, m))
    print('wrote out/ahd_headwords_clean.txt')


if __name__ == '__main__':
    main()
