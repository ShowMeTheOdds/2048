# I Solved 2048* — code and data

Code behind the Show Me The Odds video **"I Solved 2048\*"**.

The rules match the original 2048 by Gabriele Cirulli (4×4 grid, 2 starting tiles, each new tile is a 2 with probability 0.9 or a 4 with probability 0.1).

## Files

| File | What it does |
|---|---|
| `sim2048.c` | Fast bitboard 2048 simulator with several players: `random`, `corner`, `corner_greedy`, `greedy`, `ai` (expectimax, up to 6 levels deep), `ai_no4` (only 2s spawn), `ai_d0/1/2` (fixed look-ahead). Includes a self-test against a slow reference implementation. |
| `analyze.py` | Summarizes the simulations into `data/summary.json`. |
| `replay.py` | Replays a recorded game move by move (independent Python check of the C engine). |
| `data/summary.json` | All the numbers used in the video. |
| `data/ai_100_games.jsonl` | The 100 full games of the expectimax program, one line per game. |

The 1,000,000-game files for the simple strategies are not included (about 250 MB each). Regenerate them with the commands below.

## Build and run

```
clang -O3 -o sim2048 sim2048.c -lm
./sim2048 selftest
./sim2048 random 1 1000000 > data/random.jsonl
./sim2048 corner 1 1000000 > data/corner.jsonl
./sim2048 corner_greedy 1 1000000 > data/corner_greedy.jsonl
./sim2048 greedy 1 1000000 > data/greedy.jsonl
./sim2048 ai_d0 1 100000 > data/depth_d0.jsonl
./sim2048 ai_d1 1 20000 > data/depth_d1.jsonl
./sim2048 ai_d2 1 1000 > data/depth_d2.jsonl
./sim2048 ai 1000 100 > data/ai_0.jsonl        # slow: 10-20 minutes per game
./sim2048 ai 1401 1 record_1401.txt           # records the best game
python3 replay.py record_1401.txt
python3 analyze.py
```

## Credits

- Original game: [gabrielecirulli/2048](https://github.com/gabrielecirulli/2048) (MIT license).
- The board-scoring weights of the expectimax player follow the public 2048 AI by nneonneo (2014). The search, simulator, experiments and analysis are our own code.

Found a mistake? Open an issue or leave a comment under the video, with your numbers.

## License

MIT
