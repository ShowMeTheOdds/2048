/*
 * Show Me The Odds — "I Solved 2048*"
 * Motore di simulazione: regole identiche al 2048 originale di Gabriele Cirulli
 * (griglia 4x4, 2 tessere iniziali, nuova tessera 2 con p=0.9 o 4 con p=0.1).
 *
 * Strategie:
 *   random        mossa valida scelta a caso
 *   corner        priorità fissa giù > sinistra > destra > su
 *   corner_greedy fra giù/sinistra/destra sceglie la mossa che fonde di più; su solo se obbligato
 *   greedy        fra tutte e 4 sceglie la mossa che fonde di più (nessun angolo)
 *   ai            expectimax (media sulle tessere casuali, massimo sulle nostre mosse)
 *   ai_no4        come ai, ma nel gioco escono solo tessere da 2
 *   ai_d0/1/2     come ai, ma con anticipo fisso: 0 = giudica solo la griglia dopo la mossa
 *
 * Uso: sim2048 <strategia> <seed_iniziale> <n_partite> [file_partita_registrata]
 * Output: una riga JSON per partita su stdout.
 *
 * Bitboard: 16 caselle da 4 bit (esponente: 1=2, 2=4, ... 15=32768).
 * Casella (r,c) al bit 4*(4r+c); r=0 è la riga in alto, c=0 la colonna a sinistra.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef uint64_t board_t;
typedef uint16_t row_t;

enum { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3 };
static const char *DIR_NAME[4] = {"up", "down", "left", "right"};

/* ---------- generatore casuale riproducibile (xorshift64*) ---------- */
static uint64_t rng_state;
static uint64_t rng_next(void) {
    rng_state ^= rng_state >> 12; rng_state ^= rng_state << 25; rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ULL;
}
static double rng_unit(void) { return (rng_next() >> 11) * (1.0 / 9007199254740992.0); }
static int rng_int(int n) { return (int)(rng_unit() * n); }

/* ---------- tabelle delle righe ---------- */
static row_t row_left[65536], row_right[65536];
static uint32_t row_score[65536];          /* punti guadagnati muovendo la riga a sinistra */
static float heur_table[65536];

static row_t reverse_row(row_t r) {
    return (row_t)((r >> 12) | ((r >> 4) & 0x00F0) | ((r << 4) & 0x0F00) | (r << 12));
}

static void init_tables(void) {
    for (unsigned r = 0; r < 65536; r++) {
        unsigned line[4] = { r & 0xF, (r >> 4) & 0xF, (r >> 8) & 0xF, (r >> 12) & 0xF };

        /* euristica (pesi dell'IA expectimax pubblica di nneonneo, 2014) */
        double sum = 0; int empty = 0, merges = 0, prev = 0, counter = 0;
        for (int i = 0; i < 4; i++) {
            int rank = line[i];
            sum += pow(rank, 3.5);
            if (rank == 0) empty++;
            else {
                if (prev == rank) counter++;
                else if (counter > 0) { merges += 1 + counter; counter = 0; }
                prev = rank;
            }
        }
        if (counter > 0) merges += 1 + counter;
        double mono_l = 0, mono_r = 0;
        for (int i = 1; i < 4; i++) {
            if (line[i - 1] > line[i]) mono_l += pow(line[i - 1], 4) - pow(line[i], 4);
            else mono_r += pow(line[i], 4) - pow(line[i - 1], 4);
        }
        heur_table[r] = (float)(200000.0 + 270.0 * empty + 700.0 * merges
                                - 47.0 * (mono_l < mono_r ? mono_l : mono_r) - 11.0 * sum);

        /* mossa a sinistra: ogni tessera si fonde al massimo una volta */
        unsigned out[4] = {0, 0, 0, 0}; int k = 0; uint32_t sc = 0; int can_merge = 0;
        for (int i = 0; i < 4; i++) {
            if (!line[i]) continue;
            if (can_merge && out[k - 1] == line[i] && line[i] < 15) {
                out[k - 1]++; sc += 1u << out[k - 1]; can_merge = 0;
            } else { out[k++] = line[i]; can_merge = 1; }
        }
        row_t res = (row_t)(out[0] | (out[1] << 4) | (out[2] << 8) | (out[3] << 12));
        row_left[r] = res;
        row_score[r] = sc;
        row_t rr = reverse_row((row_t)r);
        row_right[rr] = reverse_row(res);
    }
}

static board_t transpose(board_t x) {
    board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    board_t a2 = x & 0x0000F0F00000F0F0ULL;
    board_t a3 = x & 0x0F0F00000F0F0000ULL;
    board_t a = a1 | (a2 << 12) | (a3 >> 12);
    board_t b1 = a & 0xFF00FF0000FF00FFULL;
    board_t b2 = a & 0x00FF00FF00000000ULL;
    board_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

static board_t apply_rows(board_t b, const row_t *t) {
    return (board_t)t[b & 0xFFFF] | ((board_t)t[(b >> 16) & 0xFFFF] << 16)
         | ((board_t)t[(b >> 32) & 0xFFFF] << 32) | ((board_t)t[(b >> 48) & 0xFFFF] << 48);
}
static uint32_t rows_score(board_t b) {
    return row_score[b & 0xFFFF] + row_score[(b >> 16) & 0xFFFF]
         + row_score[(b >> 32) & 0xFFFF] + row_score[(b >> 48) & 0xFFFF];
}

/* esegue la mossa; restituisce la nuova griglia e (se gain != NULL) i punti guadagnati */
static board_t do_move(board_t b, int dir, uint32_t *gain) {
    board_t t, r;
    switch (dir) {
    case LEFT:  r = apply_rows(b, row_left); break;
    case RIGHT: r = apply_rows(b, row_right); break;
    case UP:    t = transpose(b); r = transpose(apply_rows(t, row_left)); break;
    default:    t = transpose(b); r = transpose(apply_rows(t, row_right)); break;
    }
    if (gain) {
        /* punti: per sinistra/su la riga così com'è, per destra/giù la riga capovolta */
        board_t src = (dir == LEFT || dir == RIGHT) ? b : transpose(b);
        uint32_t g = 0;
        for (int i = 0; i < 4; i++) {
            row_t row = (row_t)((src >> (16 * i)) & 0xFFFF);
            g += row_score[(dir == LEFT || dir == UP) ? row : reverse_row(row)];
        }
        *gain = g;
    }
    return r;
}

static int count_empty(board_t b) {
    int n = 0;
    for (int i = 0; i < 16; i++) if (((b >> (4 * i)) & 0xF) == 0) n++;
    return n;
}
static int max_rank(board_t b) {
    int m = 0;
    for (int i = 0; i < 16; i++) { int v = (b >> (4 * i)) & 0xF; if (v > m) m = v; }
    return m;
}
static int count_distinct(board_t b) {
    uint16_t seen = 0;
    for (int i = 0; i < 16; i++) seen |= 1u << ((b >> (4 * i)) & 0xF);
    seen >>= 1; int n = 0;
    while (seen) { n += seen & 1; seen >>= 1; }
    return n;
}

static int only_twos = 0;
static board_t spawn(board_t b, int *pos, int *val) {
    int empty = count_empty(b);
    int pick = rng_int(empty);
    int v = (only_twos || rng_unit() < 0.9) ? 1 : 2;
    for (int i = 0; i < 16; i++) {
        if (((b >> (4 * i)) & 0xF) == 0) {
            if (pick == 0) { if (pos) *pos = i; if (val) *val = v; return b | ((board_t)v << (4 * i)); }
            pick--;
        }
    }
    return b;
}

/* ---------- expectimax ---------- */
static float heuristic(board_t b) {
    board_t t = transpose(b);
    return heur_table[b & 0xFFFF] + heur_table[(b >> 16) & 0xFFFF] + heur_table[(b >> 32) & 0xFFFF]
         + heur_table[(b >> 48) & 0xFFFF] + heur_table[t & 0xFFFF] + heur_table[(t >> 16) & 0xFFFF]
         + heur_table[(t >> 32) & 0xFFFF] + heur_table[(t >> 48) & 0xFFFF];
}

#define CACHE_BITS 22
#define CACHE_SIZE (1u << CACHE_BITS)
typedef struct { board_t b; float v; uint16_t depth; uint16_t gen; } entry_t;
static entry_t *cache;
static uint16_t cache_gen = 1;
static int depth_limit;
static long long nodes;

static float tile_node(board_t b, float cprob, int depth);

static float move_node(board_t b, float cprob, int depth) {
    float best = 0.0f;
    for (int d = 0; d < 4; d++) {
        board_t nb = do_move(b, d, NULL);
        if (nb != b) { float v = tile_node(nb, cprob, depth + 1); if (v > best) best = v; }
    }
    return best;
}

static float tile_node(board_t b, float cprob, int depth) {
    nodes++;
    if (cprob < 0.0001f || depth >= depth_limit) return heuristic(b);
    uint32_t h = (uint32_t)((b * 0x9E3779B97F4A7C15ULL) >> (64 - CACHE_BITS));
    entry_t *e = &cache[h];
    if (e->gen == cache_gen && e->b == b && e->depth <= depth) return e->v;
    int empty = count_empty(b);
    float p2 = only_twos ? 1.0f : 0.9f, p4 = 1.0f - p2;
    cprob /= empty;
    float res = 0.0f;
    for (int i = 0; i < 16; i++) {
        if (((b >> (4 * i)) & 0xF) == 0) {
            res += move_node(b | (1ULL << (4 * i)), cprob * p2, depth) * p2;
            if (p4 > 0) res += move_node(b | (2ULL << (4 * i)), cprob * p4, depth) * p4;
        }
    }
    res /= empty;
    e->b = b; e->v = res; e->depth = (uint16_t)depth; e->gen = cache_gen;
    return res;
}

static int fixed_depth = -1;   /* >= 0: profondità fissa (ai_d0, ai_d1, ai_d2) */
static int ai_move(board_t b) {
    if (fixed_depth >= 0) depth_limit = fixed_depth;
    else {
        depth_limit = count_distinct(b) - 2;
        if (depth_limit < 3) depth_limit = 3;
        if (depth_limit > 6) depth_limit = 6;
    }
    cache_gen++;
    if (cache_gen == 0) { memset(cache, 0, sizeof(entry_t) * CACHE_SIZE); cache_gen = 1; }
    float best = -1; int bestd = -1;
    for (int d = 0; d < 4; d++) {
        board_t nb = do_move(b, d, NULL);
        if (nb == b) continue;
        float v = tile_node(nb, 1.0f, 0) + 1e-6f;
        if (v > best) { best = v; bestd = d; }
    }
    return bestd;
}

/* ---------- strategie semplici ---------- */
static int pick_random(board_t b) {
    int legal[4], n = 0;
    for (int d = 0; d < 4; d++) if (do_move(b, d, NULL) != b) legal[n++] = d;
    return n ? legal[rng_int(n)] : -1;
}
static int pick_corner(board_t b) {
    static const int order[4] = {DOWN, LEFT, RIGHT, UP};
    for (int i = 0; i < 4; i++) if (do_move(b, order[i], NULL) != b) return order[i];
    return -1;
}
static int pick_greedy_among(board_t b, const int *dirs, int nd) {
    int best = -1; long bestv = -1;
    for (int i = 0; i < nd; i++) {
        uint32_t g; board_t nb = do_move(b, dirs[i], &g);
        if (nb == b) continue;
        long v = (long)g * 32 + count_empty(nb);
        if (v > bestv || (v == bestv && rng_int(2))) { bestv = v; best = dirs[i]; }
    }
    return best;
}
static int pick_corner_greedy(board_t b) {
    static const int three[3] = {DOWN, LEFT, RIGHT};
    int d = pick_greedy_among(b, three, 3);
    if (d >= 0) return d;
    return do_move(b, UP, NULL) != b ? UP : -1;
}
static int pick_greedy(board_t b) {
    static const int four[4] = {UP, DOWN, LEFT, RIGHT};
    return pick_greedy_among(b, four, 4);
}

/* ---------- autotest: confronta le mosse veloci con una versione lenta ---------- */
static int get_cell(board_t b, int r, int c) { return (b >> (4 * (4 * r + c))) & 0xF; }
static board_t set_cell(board_t b, int r, int c, int v) {
    int s = 4 * (4 * r + c); return (b & ~(0xFULL << s)) | ((board_t)v << s);
}
static board_t slow_move(board_t b, int dir, uint32_t *gain) {
    board_t out = 0; uint32_t g = 0;
    for (int k = 0; k < 4; k++) {
        int line[4], res[4] = {0, 0, 0, 0}, n = 0, can = 0;
        for (int i = 0; i < 4; i++) {
            int r, c;
            if (dir == LEFT) { r = k; c = i; } else if (dir == RIGHT) { r = k; c = 3 - i; }
            else if (dir == UP) { r = i; c = k; } else { r = 3 - i; c = k; }
            line[i] = get_cell(b, r, c);
        }
        for (int i = 0; i < 4; i++) {
            if (!line[i]) continue;
            if (can && res[n - 1] == line[i] && line[i] < 15) { res[n - 1]++; g += 1u << res[n - 1]; can = 0; }
            else { res[n++] = line[i]; can = 1; }
        }
        for (int i = 0; i < 4; i++) {
            int r, c;
            if (dir == LEFT) { r = k; c = i; } else if (dir == RIGHT) { r = k; c = 3 - i; }
            else if (dir == UP) { r = i; c = k; } else { r = 3 - i; c = k; }
            out = set_cell(out, r, c, res[i]);
        }
    }
    if (gain) *gain = g;
    return out;
}
static int selftest(void) {
    for (int t = 0; t < 2000000; t++) {
        board_t b = 0;
        for (int i = 0; i < 16; i++) { int v = rng_int(3) ? rng_int(6) : 0; b |= (board_t)v << (4 * i); }
        for (int d = 0; d < 4; d++) {
            uint32_t g1, g2;
            board_t f = do_move(b, d, &g1), s = slow_move(b, d, &g2);
            if (f != s || g1 != g2) {
                fprintf(stderr, "ERRORE dir=%s board=%016llx fast=%016llx slow=%016llx g=%u/%u\n",
                        DIR_NAME[d], (unsigned long long)b, (unsigned long long)f, (unsigned long long)s, g1, g2);
                return 1;
            }
        }
    }
    fprintf(stderr, "autotest OK: 8 milioni di mosse identiche alla versione lenta\n");
    return 0;
}

/* ---------- partita ---------- */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: sim2048 <strategia|selftest> <seed> <n> [registrazione]\n"); return 2; }
    init_tables();
    rng_state = 88172645463325252ULL;
    if (!strcmp(argv[1], "selftest")) return selftest();
    if (argc < 4) return 2;
    const char *strat = argv[1];
    uint64_t seed0 = strtoull(argv[2], NULL, 10);
    long n = atol(argv[3]);
    FILE *rec = argc > 4 ? fopen(argv[4], "w") : NULL;
    int is_ai = !strncmp(strat, "ai", 2);
    if (!strcmp(strat, "ai_no4")) only_twos = 1;
    if (!strncmp(strat, "ai_d", 4)) fixed_depth = atoi(strat + 4);
    if (is_ai) cache = calloc(CACHE_SIZE, sizeof(entry_t));

    for (long g = 0; g < n; g++) {
        uint64_t seed = seed0 + (uint64_t)g;
        rng_state = seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL; if (!rng_state) rng_state = 1;
        for (int w = 0; w < 10; w++) rng_next();
        board_t b = 0; int pos, val;
        b = spawn(b, &pos, &val); if (rec) fprintf(rec, "S %d %d\n", pos, val);
        b = spawn(b, &pos, &val); if (rec) fprintf(rec, "S %d %d\n", pos, val);
        long moves = 0, score = 0, dircount[4] = {0, 0, 0, 0}, maxpos[16];
        long reach[16]; for (int i = 0; i < 16; i++) { reach[i] = -1; maxpos[i] = 0; }
        nodes = 0;
        for (;;) {
            int d;
            if (is_ai) d = ai_move(b);
            else if (!strcmp(strat, "random")) d = pick_random(b);
            else if (!strcmp(strat, "corner")) d = pick_corner(b);
            else if (!strcmp(strat, "corner_greedy")) d = pick_corner_greedy(b);
            else d = pick_greedy(b);
            if (d < 0) break;
            uint32_t gain; b = do_move(b, d, &gain);
            score += gain; moves++; dircount[d]++;
            b = spawn(b, &pos, &val);
            if (rec) fprintf(rec, "M %c %d %d\n", "UDLR"[d], pos, val);
            int m = max_rank(b);
            if (reach[m] < 0) reach[m] = moves;
            for (int i = 0; i < 16; i++) if (((b >> (4 * i)) & 0xF) == m) maxpos[i]++;
        }
        int m = max_rank(b);
        printf("{\"strategy\":\"%s\",\"seed\":%llu,\"max_tile\":%d,\"score\":%ld,\"moves\":%ld,"
               "\"up\":%ld,\"down\":%ld,\"left\":%ld,\"right\":%ld,\"nodes\":%lld,\"reach\":{",
               strat, (unsigned long long)seed, 1 << m, score, moves,
               dircount[UP], dircount[DOWN], dircount[LEFT], dircount[RIGHT], nodes);
        int first = 1;
        for (int i = 1; i < 16; i++) if (reach[i] >= 0) { printf("%s\"%d\":%ld", first ? "" : ",", 1 << i, reach[i]); first = 0; }
        printf("},\"maxpos\":[");
        for (int i = 0; i < 16; i++) printf("%s%ld", i ? "," : "", maxpos[i]);
        printf("],\"final\":\"%016llx\"}\n", (unsigned long long)b);
        fflush(stdout);
        if (rec) { fclose(rec); rec = NULL; }
    }
    return 0;
}
