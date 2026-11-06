/*
 * Preflop solver for the 8-handed table: vector-form Discounted CFR over the
 * public preflop tree (src/preflop_tree.py) with 169 hand classes per player.
 *
 * usage: preflop <work_dir> <iterations> <out_prefix> [rfile] [threads]
 *
 * Inputs (in work_dir): tree.txt, eq169.bin (exact HU equities and combo-pair counts),
 * rfile (realisation factors and expected rake fraction per flop terminal, written by
 * src/rmodel.py; default <work_dir>/rfile.bin).
 * Outputs: <out_prefix>.strat (float32 average strategy, per decision node nact x 169),
 * <out_prefix>.ev (float32 EV in bb of the acting player per class, per decision node),
 * <out_prefix>.reach (float64 probability each decision node is reached),
 * <out_prefix>.log (iterations, exploitability per seat in bb/100).
 *
 * Model (REPORT.md, "Preflop payoff model"):
 *  - Card removal between the hero and each opponent uses the exact combo-pair counts
 *    N[h][v]: P(v | h) = N[h][v] / (combos(h) * 1225). Opponents are treated as
 *    independent of each other (card removal among opponents is ignored).
 *  - Fold terminal: the last player wins the pot, no rake (no flop, no drop).
 *  - All-in terminal (heads-up): pot share = exact all-in equity, rake min(10% pot, cap).
 *  - Flop terminal, heads-up: share(h, v) = R_i(h) e of the pot net of the expected rake
 *    fraction rho, e = exact equity of h vs v, R measured from postflop solves (fit_r.py).
 *  - Flop terminal, 3-way: hero's share ~ kappa R_i(h) E_j(h) E_k(h) where E_j(h) is h's
 *    equity against j's range; kappa normalises the three shares to 1 for the current
 *    ranges (the 3-way model is approximate and flagged in REPORT.md).
 */
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NC 169
#define NP 8
#define MAXA 5

typedef struct {
    char kind;             /* D decision, F fold, P flop, A all-in */
    int player;            /* D: actor, F: winner */
    int nact;
    char act[MAXA];
    int child[MAXA];
    double inv[NP];
    double pot;
    int ptype, aggr, np, players[3];
    /* decision storage */
    float *reg, *ssum, *cur;
    /* terminal data */
    float *R[3];           /* 3-way: R per player */
    double rho;
    int tidx;              /* index among flop terminals in the rfile */
} Node;

static Node *T;
static int nnodes;
static double E[NC][NC], Pc[NC][NC], PE[NC][NC], prior[NC];
/* card_mode 1 (default, item 5 of the improvement plan): card removal only between the players who
   meet at a terminal (combo-pair counts there), folded players enter as plain probabilities, and
   3-way terminals use one consensus reach for all three players - every player then sees the same
   probability for every line and the payoffs are conserved. card_mode 0 (PF_CARD_MODE=0): the
   original pairwise conditioning on the traverser's hand everywhere (not money-conserving). */
static int card_mode = 1;
static double rake_pct = 0.10, rake_cap = 5.0;
static float *ev_out;      /* per decision node 169 */
static float *eva_out;     /* per decision node MAXA x 169: EV of each action */
static double *reach_out;

static void die(const char *m) { fprintf(stderr, "preflop: %s\n", m); exit(1); }

static void load_tree(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open tree.txt");
    if (fscanf(f, "%d", &nnodes) != 1) die("bad tree");
    T = calloc(nnodes, sizeof(Node));
    for (int n = 0; n < nnodes; n++) {
        int id;
        char k;
        if (fscanf(f, "%d %c", &id, &k) != 2 || id != n) die("bad tree line");
        Node *x = &T[n];
        x->kind = k;
        if (k == 'D') {
            if (fscanf(f, "%d %d", &x->player, &x->nact) != 2 || x->nact > MAXA) die("bad D");
            for (int a = 0; a < x->nact; a++)
                if (fscanf(f, " %c %d", &x->act[a], &x->child[a]) != 2) die("bad act");
        } else if (k == 'F') {
            if (fscanf(f, "%d", &x->player) != 1) die("bad F");
        } else {
            if (fscanf(f, "%d %d %d", &x->ptype, &x->aggr, &x->np) != 3 || x->np > 3) die("bad P/A");
            for (int i = 0; i < x->np; i++)
                if (fscanf(f, "%d", &x->players[i]) != 1) die("bad players");
        }
        x->pot = 0;
        for (int p = 0; p < NP; p++) {
            if (fscanf(f, "%lf", &x->inv[p]) != 1) die("bad inv");
            x->pot += x->inv[p];
        }
    }
    fclose(f);
}

static void load_eq(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open eq169.bin");
    static int32_t N[NC][NC];
    if (fread(E, sizeof(double), NC * NC, f) != NC * NC) die("short eq");
    if (fread(N, sizeof(int32_t), NC * NC, f) != NC * NC) die("short eq counts");
    fclose(f);
    for (int h = 0; h < NC; h++) {
        double s = 0;
        for (int v = 0; v < NC; v++) s += N[h][v];
        prior[h] = s / 1225.0 / 1326.0;          /* combos(h) / 1326 */
        for (int v = 0; v < NC; v++) Pc[h][v] = N[h][v] / s;
    }
    if (getenv("PF_CARD_MODE") && atoi(getenv("PF_CARD_MODE")) == 2)   /* test mode: no card removal */
        for (int h = 0; h < NC; h++)
            for (int v = 0; v < NC; v++) Pc[h][v] = prior[v];
    for (int h = 0; h < NC; h++)
        for (int v = 0; v < NC; v++) PE[h][v] = Pc[h][v] * E[h][v];
}

/* rfile: int32 count, then per flop terminal (tree order): int32 node id, float64 rho,
   then np x 169 float32 R. */
static void load_rfile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open rfile");
    int32_t cnt;
    if (fread(&cnt, 4, 1, f) != 1) die("bad rfile");
    for (int i = 0; i < cnt; i++) {
        int32_t id;
        double rho;
        if (fread(&id, 4, 1, f) != 1 || fread(&rho, 8, 1, f) != 1) die("bad rfile entry");
        if (id < 0 || id >= nnodes || T[id].kind != 'P') die("rfile node is not a flop terminal");
        Node *x = &T[id];
        x->rho = rho;
        for (int k = 0; k < x->np; k++) {
            x->R[k] = malloc(sizeof(float) * NC);
            if (fread(x->R[k], sizeof(float), NC, f) != NC) die("short R");
        }
    }
    fclose(f);
    for (int n = 0; n < nnodes; n++)
        if (T[n].kind == 'P' && !T[n].R[0]) die("flop terminal missing from rfile");
}

static double rake_of(double pot) { double r = rake_pct * pot; return r < rake_cap ? r : rake_cap; }

/* Terminal values are computed from the shared matrices Pc and PE at each visit (item 6: no
   per-terminal 169 x 169 matrices, so the un-pruned tree fits in memory). */
static void prepare_terminals(void) {}

static void alloc_store(void)
{
    for (int n = 0; n < nnodes; n++) {
        Node *x = &T[n];
        if (x->kind != 'D') continue;
        x->reg = calloc((size_t)x->nact * NC, sizeof(float));
        x->ssum = calloc((size_t)x->nact * NC, sizeof(float));
        x->cur = malloc(sizeof(float) * x->nact * NC);
    }
}

/* matvec: out[h] = sum_v A[h][v] r[v] */
static void mv_pc(const double *r, double *out)
{
    for (int h = 0; h < NC; h++) {
        double s = 0;
        const double *row = Pc[h];
        for (int v = 0; v < NC; v++) s += row[v] * r[v];
        out[h] = s;
    }
}

static void mv_pce(const double *r, double *out)
{
    for (int h = 0; h < NC; h++) {
        double acc = 0;
        const double *row = PE[h];
        for (int v = 0; v < NC; v++) acc += row[v] * r[v];
        out[h] = acc;
    }
}

static double mass(const double *r)
{
    double m = 0;
    for (int v = 0; v < NC; v++) m += prior[v] * r[v];
    return m;
}

enum { W_CFR, W_BR, W_EV };

typedef struct {
    double r[NP][NC];      /* reach of every player per class */
    double w[NC];          /* traverser: product over folded opponents of P(their path | h) */
    int folded[NP];
} State;


/* weight of the active opponents' current reach for traverser i */
static void opp_mass(const State *s, int i, double *out)
{
    double tmp[NC];
    for (int h = 0; h < NC; h++) out[h] = s->w[h];
    for (int j = 0; j < NP; j++) {
        if (j == i || s->folded[j]) continue;
        if (card_mode) {
            double m = mass(s->r[j]);
            for (int h = 0; h < NC; h++) out[h] *= m;
            continue;
        }
        mv_pc(s->r[j], tmp);
        for (int h = 0; h < NC; h++) out[h] *= tmp[h];
    }
}

static void terminal_value(const Node *x, const State *s, int i, double *v)
{
    if (x->kind == 'F') {
        double g = x->pot - x->inv[i];
        for (int h = 0; h < NC; h++) v[h] = s->w[h] * g;
        return;
    }
    if (x->np == 2) {
        int side = x->players[0] == i ? 0 : 1, j = x->players[1 - side];
        double A[NC], F[NC];
        mv_pce(s->r[j], A);          /* sum_v P(v|h) r_j(v) e(h, v) */
        mv_pc(s->r[j], F);           /* sum_v P(v|h) r_j(v) */
        if (x->kind == 'A') {
            double net = x->pot - rake_of(x->pot);
            for (int h = 0; h < NC; h++) v[h] = s->w[h] * (net * A[h] - x->inv[i] * F[h]);
            return;
        }
        /* flop: value = net R_i e / Z - inv, Z = expected R_i e + R_j (1 - e) under the current
           ranges, so the two realised shares add up to the pot net of the expected rake */
        double rr[NC], Bo[NC], Bp[NC], num = 0, den = 0;
        for (int u = 0; u < NC; u++) rr[u] = x->R[1 - side][u] * s->r[j][u];
        mv_pc(rr, Bp);
        mv_pce(rr, Bo);
        for (int h = 0; h < NC; h++) {
            double wgt = prior[h] * s->r[i][h];
            num += wgt * (x->R[side][h] * A[h] + (Bp[h] - Bo[h]));
            den += wgt * F[h];
        }
        double Z = den > 0 && num > 0 ? num / den : 1.0;
        double net = x->pot * (1.0 - x->rho);
        for (int h = 0; h < NC; h++) v[h] = s->w[h] * (net * x->R[side][h] * A[h] / Z - x->inv[i] * F[h]);
        return;
    }
    /* 3-way flop */
    double B[3][2][NC], F[3][2][NC];
    for (int a = 0; a < 3; a++) {
        int pa = x->players[a], o = 0;
        for (int b = 0; b < 3; b++) {
            if (b == a) continue;
            mv_pce(s->r[x->players[b]], B[a][o]);
            mv_pc(s->r[x->players[b]], F[a][o]);
            o++;
        }
        (void)pa;
    }
    /* additive 3-way model (fitted on 20,000 exact 3-way all-ins, REPORT.md): the share of a
       hand is q = E_j E_k plus an equal third of the probability the pairwise product misses,
       (1 - sum of the three players' average q) / 3; then times R and normalised. */
    double tq = 0, T[3], U[3], D[3];
    for (int a = 0; a < 3; a++) {
        double q = 0, t = 0, u = 0, d = 0;
        const double *ra = s->r[x->players[a]];
        for (int h = 0; h < NC; h++) {
            double ff = F[a][0][h] * F[a][1][h], bb = B[a][0][h] * B[a][1][h], wgt = prior[h] * ra[h];
            q += wgt * bb;
            t += wgt * x->R[a][h] * bb;
            u += wgt * x->R[a][h] * ff;
            d += wgt * ff;
        }
        T[a] = t; U[a] = u; D[a] = d;
        if (d > 0) tq += q / d;
    }
    double add = (1.0 - tq) / 3.0, Z = 0;
    for (int a = 0; a < 3; a++) if (D[a] > 0) Z += (T[a] + add * U[a]) / D[a];
    if (!(Z > 0)) Z = 1.0;
    int a = x->players[0] == i ? 0 : x->players[1] == i ? 1 : 2;
    double net = x->pot * (1.0 - x->rho);
    /* consensus reach: each player's estimate of the probability of this terminal, D_a, differs
       under pairwise card removal; card_mode 1 rescales all three to their mean */
    double cons = 1.0;
    if (card_mode && D[a] > 0) cons = (D[0] + D[1] + D[2]) / 3.0 / D[a];
    for (int h = 0; h < NC; h++) {
        double ff = F[a][0][h] * F[a][1][h];
        v[h] = cons * s->w[h] * (net * x->R[a][h] * (B[a][0][h] * B[a][1][h] + add * ff) / Z - x->inv[i] * ff);
    }
}

static double dcfr_pos, dcfr_neg, dcfr_avg;

static void walk(int n, State *s, int i, int mode, double *v)
{
    Node *x = &T[n];
    if (x->kind != 'D') { terminal_value(x, s, i, v); return; }
    int p = x->player, na = x->nact;
    const float *sig = mode == W_CFR ? x->cur : NULL;
    float sigavg[MAXA * NC];
    if (mode != W_CFR) {
        for (int h = 0; h < NC; h++) {
            double t = 0;
            for (int a = 0; a < na; a++) t += x->ssum[a * NC + h];
            for (int a = 0; a < na; a++) sigavg[a * NC + h] = t > 0 ? (float)(x->ssum[a * NC + h] / t) : 1.0f / na;
        }
        sig = sigavg;
    }
    if (p == i) {
        double va[MAXA][NC];
        double save[NC];
        memcpy(save, s->r[i], sizeof save);
        for (int a = 0; a < na; a++) {
            if (x->act[a] == 'f') {
                double om[NC];
                opp_mass(s, i, om);
                for (int h = 0; h < NC; h++) va[a][h] = -x->inv[i] * om[h];
                continue;
            }
            for (int h = 0; h < NC; h++) s->r[i][h] = save[h] * sig[a * NC + h];
            walk(x->child[a], s, i, mode, va[a]);
        }
        memcpy(s->r[i], save, sizeof save);
        for (int h = 0; h < NC; h++) {
            double val = 0;
            if (mode == W_BR) {
                val = va[0][h];
                for (int a = 1; a < na; a++) if (va[a][h] > val) val = va[a][h];
            } else
                for (int a = 0; a < na; a++) val += sig[a * NC + h] * va[a][h];
            v[h] = val;
        }
        if (mode == W_CFR) {
            for (int a = 0; a < na; a++)
                for (int h = 0; h < NC; h++) {
                    float *rg = &x->reg[a * NC + h];
                    double r = *rg + (va[a][h] - v[h]);
                    *rg = (float)r;
                    x->ssum[a * NC + h] += (float)(dcfr_avg * save[h] * sig[a * NC + h]);
                }
        }
        if (mode == W_EV) {
            double om[NC], rch = 0;
            opp_mass(s, i, om);
            if (getenv("PF_DEBUG_NODE") && atoi(getenv("PF_DEBUG_NODE")) == n)
                for (int a = 0; a < na; a++)
                    fprintf(stderr, "node %d act %c AA va %.6g om %.6g -> %.4f\n", n, x->act[a], va[a][0], om[0], va[a][0] / om[0]);
            for (int a = 0; a < na; a++)
                for (int h = 0; h < NC; h++)
                    eva_out[((size_t)n * MAXA + a) * NC + h] = om[h] > 1e-12 ? (float)(va[a][h] / om[h]) : 0.0f;
            for (int h = 0; h < NC; h++) {
                ev_out[(size_t)n * NC + h] = om[h] > 1e-12 ? (float)(v[h] / om[h]) : 0.0f;
                rch += prior[h] * save[h] * om[h];
            }
            reach_out[n] = rch;
        }
        return;
    }
    /* opponent (or the traverser already out): sum over actions */
    if (s->folded[i]) { for (int h = 0; h < NC; h++) v[h] = 0; return; }
    double save[NC], wsave[NC], tmp[NC];
    memcpy(save, s->r[p], sizeof save);
    memcpy(wsave, s->w, sizeof wsave);
    for (int h = 0; h < NC; h++) v[h] = 0;
    for (int a = 0; a < na; a++) {
        for (int h = 0; h < NC; h++) s->r[p][h] = save[h] * sig[a * NC + h];
        if (x->act[a] == 'f') {
            if (card_mode) {
                double m = mass(s->r[p]);
                for (int h = 0; h < NC; h++) s->w[h] = wsave[h] * m;
            } else {
                mv_pc(s->r[p], tmp);
                for (int h = 0; h < NC; h++) s->w[h] = wsave[h] * tmp[h];
            }
            s->folded[p] = 1;
            walk(x->child[a], s, i, mode, tmp);
            s->folded[p] = 0;
            memcpy(s->w, wsave, sizeof wsave);
        } else
            walk(x->child[a], s, i, mode, tmp);
        for (int h = 0; h < NC; h++) v[h] += tmp[h];
        if (mode == W_EV && getenv("PF_DEBUG_SUB") && n >= atoi(getenv("PF_DEBUG_SUB")) && n < atoi(getenv("PF_DEBUG_SUB")) + 4)
            fprintf(stderr, "  opp node %d (P%d) act %c child %d AA %.6g\n", n, p, x->act[a], x->child[a], tmp[0]);
    }
    memcpy(s->r[p], save, sizeof save);
}

static void init_state(State *s)
{
    for (int p = 0; p < NP; p++) { for (int h = 0; h < NC; h++) s->r[p][h] = 1.0; s->folded[p] = 0; }
    for (int h = 0; h < NC; h++) s->w[h] = 1.0;
}

static void current_strategies(void)
{
    for (int n = 0; n < nnodes; n++) {
        Node *x = &T[n];
        if (x->kind != 'D') continue;
        for (int h = 0; h < NC; h++) {
            double t = 0;
            for (int a = 0; a < x->nact; a++) { float r = x->reg[a * NC + h]; t += r > 0 ? r : 0; }
            for (int a = 0; a < x->nact; a++) {
                float r = x->reg[a * NC + h];
                x->cur[a * NC + h] = t > 0 ? (float)((r > 0 ? r : 0) / t) : 1.0f / x->nact;
            }
        }
    }
}

static void discount(int t)
{
    double ta = pow(t, 1.5);
    dcfr_pos = ta / (ta + 1);
    dcfr_neg = 0.5;
    for (int n = 0; n < nnodes; n++) {
        Node *x = &T[n];
        if (x->kind != 'D') continue;
        for (int k = 0; k < x->nact * NC; k++)
            x->reg[k] *= (float)(x->reg[k] > 0 ? dcfr_pos : dcfr_neg);
    }
}

static double root_value(int i, int mode)
{
    State *s = malloc(sizeof(State));
    double v[NC], tot = 0;
    init_state(s);
    walk(0, s, i, mode, v);
    for (int h = 0; h < NC; h++) tot += prior[h] * v[h];
    free(s);
    return tot;
}

static const char *SEATS[NP] = { "UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB" };

static double exploitability(FILE *log, int it, double *worst_out)
{
    double br[NP], ev[NP];
#pragma omp parallel for schedule(dynamic, 1)
    for (int k = 0; k < 2 * NP; k++) {
        int i = k % NP;
        if (k < NP) br[i] = root_value(i, W_BR);
        else ev[i] = root_value(i, W_EV);
    }
    double worst = 0, sum = 0;
    fprintf(log, "%d", it);
    for (int i = 0; i < NP; i++) {
        double g = (br[i] - ev[i]) * 100.0;
        if (g > worst) worst = g;
        sum += ev[i];
        fprintf(log, " %s %.3f", SEATS[i], g);
    }
    fprintf(log, " worst %.3f bb/100, sum EV %.4f bb (= -rake)\n", worst, sum);
    fflush(log);
    *worst_out = worst;
    return worst;
}

static void write_out(const char *prefix)
{
    char path[512];
    snprintf(path, sizeof path, "%s.strat", prefix);
    FILE *f = fopen(path, "wb");
    for (int n = 0; n < nnodes; n++) {
        Node *x = &T[n];
        if (x->kind != 'D') continue;
        for (int a = 0; a < x->nact; a++)
            for (int h = 0; h < NC; h++) {
                double t = 0;
                for (int b = 0; b < x->nact; b++) t += x->ssum[b * NC + h];
                float s = t > 0 ? (float)(x->ssum[a * NC + h] / t) : 1.0f / x->nact;
                fwrite(&s, 4, 1, f);
            }
    }
    fclose(f);
    /* EV and reach under the average strategy */
    for (int i = 0; i < NP; i++) root_value(i, W_EV);
    snprintf(path, sizeof path, "%s.ev", prefix);
    f = fopen(path, "wb");
    fwrite(ev_out, sizeof(float), (size_t)nnodes * NC, f);
    fclose(f);
    snprintf(path, sizeof path, "%s.evact", prefix);
    f = fopen(path, "wb");
    for (int n = 0; n < nnodes; n++)
        if (T[n].kind == 'D') fwrite(eva_out + (size_t)n * MAXA * NC, sizeof(float), (size_t)T[n].nact * NC, f);
    fclose(f);
    snprintf(path, sizeof path, "%s.reach", prefix);
    f = fopen(path, "wb");
    fwrite(reach_out, sizeof(double), nnodes, f);
    fclose(f);
}

/* state save/load for resuming: regrets and strategy sums of every decision node */
static int save_state(const char *path, int it)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(&it, 4, 1, f);
    for (int n = 0; n < nnodes; n++)
        if (T[n].kind == 'D') {
            fwrite(T[n].reg, 4, (size_t)T[n].nact * NC, f);
            fwrite(T[n].ssum, 4, (size_t)T[n].nact * NC, f);
        }
    fclose(f);
    return 0;
}

static int load_state(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int it = 0;
    if (fread(&it, 4, 1, f) != 1) { fclose(f); return 0; }
    for (int n = 0; n < nnodes; n++)
        if (T[n].kind == 'D') {
            if (fread(T[n].reg, 4, (size_t)T[n].nact * NC, f) != (size_t)T[n].nact * NC ||
                fread(T[n].ssum, 4, (size_t)T[n].nact * NC, f) != (size_t)T[n].nact * NC) { fclose(f); return 0; }
        }
    fclose(f);
    return it;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: preflop <work_dir> <iterations> <out_prefix> [rfile] [threads] [rake_pct rake_cap]\n"); return 2; }
    char path[512];
    const char *wd = argv[1];
    int iters = atoi(argv[2]);
    const char *prefix = argv[3];
    if (argc > 5) omp_set_num_threads(atoi(argv[5]));
    if (argc > 7) { rake_pct = atof(argv[6]); rake_cap = atof(argv[7]); }
    if (getenv("PF_CARD_MODE")) card_mode = atoi(getenv("PF_CARD_MODE"));
    snprintf(path, sizeof path, "%s/tree.txt", wd);
    load_tree(path);
    snprintf(path, sizeof path, "%s/eq169.bin", wd);
    load_eq(path);
    if (argc > 4) snprintf(path, sizeof path, "%s", argv[4]);
    else snprintf(path, sizeof path, "%s/rfile.bin", wd);
    load_rfile(path);
    prepare_terminals();
    alloc_store();
    ev_out = calloc((size_t)nnodes * NC, sizeof(float));
    eva_out = calloc((size_t)nnodes * MAXA * NC, sizeof(float));
    reach_out = calloc(nnodes, sizeof(double));
    snprintf(path, sizeof path, "%s.log", prefix);
    FILE *log = fopen(path, "a");
    snprintf(path, sizeof path, "%s.state", prefix);
    int start = load_state(path);
    if (start) fprintf(log, "resumed at iteration %d\n", start);
    double worst = 1e9;
    for (int t = start + 1; t <= iters; t++) {
        current_strategies();
        dcfr_avg = pow((double)t / (t + 1), 2.0);   /* gamma = 2: weights of this iteration relative... */
        /* DCFR: strategy sums are discounted by (t/(t+1))^gamma before adding */
        for (int n = 0; n < nnodes; n++)
            if (T[n].kind == 'D')
                for (int k = 0; k < T[n].nact * NC; k++) T[n].ssum[k] *= (float)dcfr_avg;
        dcfr_avg = 1.0;
#pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < NP; i++) {
            State *s = malloc(sizeof(State));
            double v[NC];
            init_state(s);
            walk(0, s, i, W_CFR, v);
            free(s);
        }
        discount(t);
        if (t % 50 == 0 || t == iters) {
            exploitability(log, t, &worst);
            snprintf(path, sizeof path, "%s.state", prefix);
            save_state(path, t);
        }
    }
    write_out(prefix);
    fprintf(log, "done, worst %.3f bb/100\n", worst);
    fclose(log);
    return 0;
}
