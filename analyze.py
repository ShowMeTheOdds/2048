"""Riassume i risultati delle simulazioni 2048 (data/*.jsonl) in data/summary.json."""
import glob
import json
import os
import statistics
from collections import Counter

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
TILES = [64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]


def load(pattern):
    games = []
    for f in sorted(glob.glob(os.path.join(DATA, pattern))):
        with open(f) as fh:
            games += [json.loads(line) for line in fh if line.strip()]
    return games


def summarize(games):
    n = len(games)
    if not n:
        return None
    maxes = Counter(g["max_tile"] for g in games)
    reach = {t: sum(1 for g in games if g["max_tile"] >= t) / n for t in TILES}
    moves = [g["moves"] for g in games]
    scores = [g["score"] for g in games]
    dirs = {d: sum(g[d] for g in games) for d in ("up", "down", "left", "right")}
    tot = sum(dirs.values())
    heat = [sum(g["maxpos"][i] for g in games) for i in range(16)]
    hsum = sum(heat)
    best = max(games, key=lambda g: (g["max_tile"], g["score"]))
    longest = max(games, key=lambda g: g["moves"])
    return {
        "games": n,
        "max_tile_counts": dict(sorted(maxes.items())),
        "reach_at_least": {t: round(p, 6) for t, p in reach.items() if p > 0},
        "moves_median": statistics.median(moves),
        "moves_mean": round(statistics.mean(moves), 1),
        "score_median": statistics.median(scores),
        "score_mean": round(statistics.mean(scores), 1),
        "score_max": max(scores),
        "direction_share": {d: round(v / tot, 4) for d, v in dirs.items()},
        "maxtile_heatmap_share": [round(h / hsum, 4) for h in heat],
        "best_game": {k: best[k] for k in ("seed", "max_tile", "score", "moves")},
        "longest_game": {k: longest[k] for k in ("seed", "max_tile", "score", "moves")},
        "median_moves_to": {
            str(t): statistics.median(g["reach"][str(t)] for g in games if str(t) in g["reach"])
            for t in TILES if any(str(t) in g["reach"] for g in games)
        },
    }


if __name__ == "__main__":
    out = {}
    for name, pat in [("random", "random.jsonl"), ("corner", "corner.jsonl"),
                      ("corner_greedy", "corner_greedy.jsonl"), ("greedy", "greedy.jsonl"),
                      ("depth_0", "depth_d0.jsonl"), ("depth_1", "depth_d1.jsonl"),
                      ("depth_2", "depth_d2.jsonl"),
                      ("ai", "ai_[0-9]*.jsonl"), ("ai_no4", "ai_no4.jsonl")]:
        s = summarize(load(pat))
        if s:
            out[name] = s
    with open(os.path.join(DATA, "summary.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    for name, s in out.items():
        print(f"\n== {name} ({s['games']} partite)")
        print("  arriva almeno a:", s["reach_at_least"])
        print("  mosse mediana:", s["moves_median"], "| punti mediana:", s["score_median"], "max:", s["score_max"])
        print("  direzioni:", s["direction_share"])
        print("  migliore:", s["best_game"], "| più lunga:", s["longest_game"])
