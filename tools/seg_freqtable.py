"""Greedy longest-match segmentation of the dictionary's frequency table
(the Huffman code-book).  Units are: English words, one GBK char, one other
single character."""
import sys

import numpy as np

COMMON = """the of and to a in that is was he for it with as his on be at by i this had not are but from or have an they which one you were her all she there would their we him been has when who will more no if out so said what up its about into than them can only other new some could time these two may then do first any my now such like our over man me even most made after also did many before must through back years where much your way well down should because each just those people mr how too little state good very make world still own see men work long get here between both life being under never day same another know while last might us great old year off come since against go came right used take three""".split()


def load_words():
    words = set(COMMON)
    for line in open(r'out\ahd_headwords_clean.txt', encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        if len(p) == 3:
            w = p[1]
            if w.isalpha():
                words.add(w.lower())
                words.add(w)
    return {w for w in words if 1 <= len(w) <= 20}


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    start = int(args[0], 16) if args else None
    dump_json = '--json' in sys.argv
    a = np.fromfile(r'out\file_ahddata.bin', dtype=np.uint8)
    # the shared code table lives in inode 8 (dict_cam) at file offset 0x41050
    img = np.fromfile(r'out\nc3000_full.bin', dtype=np.uint8)
    tbl_start = 99 * 0x4000 + 0x41050
    a = img[tbl_start:tbl_start + 0x80000]
    words = load_words()
    # index words by first letter for greedy matching
    by1 = {}
    for w in words:
        by1.setdefault(w[0], []).append(w)
    for k in by1:
        by1[k].sort(key=len, reverse=True)
    p = 0 if start is None else start
    end = len(a)
    units = []
    # T1 is already verified by decoding: "the 的 。 ， to of and that" (20 bytes).
    # Anchor it instead of letting greedy matching guess.
    if p == 0 and a[0:20].tobytes() == 'the'.encode() + b'\xb5\xc4\xa1\xa3\xa3\xac' + b'toofandthat':
        units = ['the', '的', '。', '，', 'to', 'of', 'and', 'that']
        p = 20
    limit = 10 ** 9 if dump_json else 400
    while p < end and len(units) < limit:
        b = int(a[p])
        if b >= 0x81:                      # GBK lead -> one Chinese char
            units.append(bytes(a[p:p + 2]).decode('gbk', 'replace'))
            p += 2
            continue
        ch = chr(b)
        cand = by1.get(ch, [])
        got = None
        for w in cand:
            if a[p:p + len(w)].tobytes().decode('latin1').lower() == w.lower():
                got = w
                break
        if got:
            units.append(got)
            p += len(got)
        else:
            s = ''
            while p < end and 32 <= a[p] < 127:
                s += chr(a[p]); p += 1
            units.append(s if s else chr(b))
            if not s:
                p += 1
    if dump_json:
        import json
        t1 = units[0:8]
        t2 = units[8:8 + 1024]
        t3 = units[8 + 1024:8 + 1024 + 8192]
        t4 = units[8 + 1024 + 8192:8 + 1024 + 8192 + 65536]
        json.dump({'t1': t1, 't2': t2, 't3': t3, 't4': t4},
                  open(r'out\tables_guess.json', 'w', encoding='utf-8'), ensure_ascii=False)
        print('units', len(units), '-> out/tables_guess.json')
        print('t1', t1)
        print('t2[:24]', t2[:24])
        return
    for i, u in enumerate(units):
        print('%4d %s' % (i + 1, u))


if __name__ == '__main__':
    main()
