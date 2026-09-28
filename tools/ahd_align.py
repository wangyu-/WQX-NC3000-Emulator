"""Check the extracted AHD headword list for alphabetical order and try to
align it with the `word_ID` table (7413 entries with pronunciations).
"""
import numpy as np


def load_list(path=r'out\ahd_headwords.txt'):
    offs, words = [], []
    for line in open(path, encoding='utf-8'):
        a, b = line.rstrip('\n').split('\t')
        offs.append(int(a, 16))
        words.append(b)
    return offs, words


def main():
    offs, words = load_list()
    key = [w.lower() for w in words]
    bad = [i for i in range(len(key) - 1) if key[i + 1] < key[i]]
    print('records:', len(words), 'order violations:', len(bad))
    print('first violations:', [(hex(offs[i]), words[i], words[i + 1]) for i in bad[:10]])

    # duplicate headwords (consecutive)
    dup = sum(1 for i in range(len(key) - 1) if key[i] == key[i + 1])
    print('consecutive duplicate headwords:', dup)

    img = np.fromfile(r'out\nc3000_full.bin', dtype=np.uint8)
    wd = img[2501 * 0x4000: 2501 * 0x4000 + 0x10000]
    rec = wd[:7413 * 3].reshape(-1, 3)
    wid = (rec[:, 0].astype(np.int64) << 16) | rec[:, 1] | (rec[:, 2].astype(np.int64) << 8)
    print('word_ID entries:', len(wid), 'first', hex(int(wid[0])), 'last', hex(int(wid[-1])))
    d = np.diff(wid)
    print('word_ID delta histogram:', {int(k): int(v) for k, v in zip(*np.unique(d, return_counts=True))})
    print('sum of deltas (=span):', int(d.sum()))


if __name__ == '__main__':
    main()
