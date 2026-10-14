"""Rigioca una partita registrata (record_*.txt) e ne estrae i momenti chiave."""
import json
import sys

MOVES = {"L": (0, 1), "R": (0, -1), "U": (1, 1), "D": (1, -1)}


def slide(line):
    tiles = [v for v in line if v]
    out, i, gain = [], 0, 0
    while i < len(tiles):
        if i + 1 < len(tiles) and tiles[i] == tiles[i + 1] and tiles[i] < 15:
            out.append(tiles[i] + 1); gain += 1 << (tiles[i] + 1); i += 2
        else:
            out.append(tiles[i]); i += 1
    return out + [0] * (4 - len(out)), gain


def move(b, d):
    b = [row[:] for row in b]; gain = 0
    for k in range(4):
        if d in "LR":
            line = b[k] if d == "L" else b[k][::-1]
            res, g = slide(line); gain += g
            b[k] = res if d == "L" else res[::-1]
        else:
            col = [b[r][k] for r in range(4)]
            line = col if d == "U" else col[::-1]
            res, g = slide(line); gain += g
            res = res if d == "U" else res[::-1]
            for r in range(4):
                b[r][k] = res[r]
    return b, gain


def replay(path):
    b = [[0] * 4 for _ in range(4)]
    states, score = [], 0
    for line in open(path):
        p = line.split()
        if p[0] == "S":
            pos, val = int(p[1]), int(p[2]); b[pos // 4][pos % 4] = val
        else:
            b, g = move(b, p[1]); score += g
            pos, val = int(p[2]), int(p[3]); b[pos // 4][pos % 4] = val
            states.append((p[1], [r[:] for r in b], score))
    return states


if __name__ == "__main__":
    st = replay(sys.argv[1])
    empt = [sum(v == 0 for r in s[1] for v in r) for s in st]
    mx = [max(v for r in s[1] for v in r) for s in st]
    first = {}
    for i, m in enumerate(mx):
        first.setdefault(m, i + 1)
    print("mosse", len(st), "punti", st[-1][2])
    print("prima volta:", {1 << k: v for k, v in sorted(first.items()) if k >= 9})
    # momenti con 0 caselle libere dopo lo spawn (prima della fine), escluse le ultime 30 mosse
    tight = [i for i, e in enumerate(empt[:-30]) if e == 0]
    print("mosse con griglia piena (0 libere) prima della fine:", len(tight), tight[:10])
    ones = [i for i, e in enumerate(empt[:-30]) if e <= 1]
    print("mosse con <=1 libera:", len(ones))
    if tight:
        i = max(tight, key=lambda i: mx[i])
        print("griglia piena con tessera max più alta: mossa", i + 1, "max", 1 << mx[i])
        for r in st[i][1]:
            print([1 << v if v else 0 for v in r])
        print("mossa successiva:", st[i + 1][0], "->")
        for r in st[i + 1][1]:
            print([1 << v if v else 0 for v in r])
    json.dump({"empty": empt, "max": mx}, open(sys.argv[1].replace(".txt", "_curve.json"), "w"))
