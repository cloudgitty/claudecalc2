#ifndef ICMCORE_STREETSOLVER_H
#define ICMCORE_STREETSOLVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Heads-up postflop solver (flop, turn or river start) using Discounted CFR over
 * combo-level ranges, with terminal payoffs in chips or in ICM prize money.
 *
 * Player 0 is out of position (acts first on every street), player 1 is in position.
 * All functions may be called from any thread; one solver object must not be used
 * by two threads at once, except ss_cancel(), which may be called at any time.
 */

enum { SS_MODE_CHIPS = 0, SS_MODE_ICM = 1 };

enum {
    SS_OK = 0,
    SS_CANCELLED = 1,
    SS_ERROR = -1
};

/* Node kinds and the actions that lead into a node. */
enum { SS_NODE_DECISION = 0, SS_NODE_CHANCE, SS_NODE_FOLD, SS_NODE_SHOWDOWN };
enum { SS_ACT_NONE = 0, SS_ACT_FOLD, SS_ACT_CHECK, SS_ACT_CALL, SS_ACT_BET, SS_ACT_RAISE, SS_ACT_ALLIN, SS_ACT_DEAL };

#define SS_MAX_BET_SIZES 4
#define SS_MAX_PLAYERS 9

typedef struct {
    /* Board at the start of the solve: 3, 4 or 5 cards (see cards.h). */
    uint8_t board[5];
    int board_len;

    /* Range weights (0..1) indexed by combo (cards.h combo_index); 0 = OOP, 1 = IP.
       The arrays are copied by ss_create. */
    const float *range[2];

    /* Chips in the pot at the start (including dead money) and the effective
       stack each player still has behind. */
    double pot;
    double stack;

    /* Tree: bets as fractions of the pot; facing a bet: fold, call, raise to
       raise_multiple x the bet, all-in. After max_raises raises on a street only
       fold, call or all-in remain. A bet or raise that would leave less than
       allin_threshold x the pot (after a call) behind becomes all-in. */
    int num_bet_sizes;
    double bet_sizes[SS_MAX_BET_SIZES];
    int allow_allin;
    double raise_multiple;
    int max_raises;
    double allin_threshold;

    /* Payoffs. ICM: icm_stacks are every player's chips behind at the start of the
       solve (not counting the pot), seat[0] and seat[1] are the seats of OOP and IP;
       the pot goes to the winner of the hand and the other stacks stay fixed. */
    int mode;
    int icm_players;
    double icm_stacks[SS_MAX_PLAYERS];
    int icm_num_payouts;
    double icm_payouts[SS_MAX_PLAYERS];
    int seat[2];

    /* Stopping: stop when both players' exploitability is below
       target_exploitability_pct (percent of the pot), or after max_iterations. */
    int max_iterations;
    double target_exploitability_pct;
    int check_every;            /* iterations between exploitability checks */

    /* Discounted CFR parameters. */
    double dcfr_alpha, dcfr_beta, dcfr_gamma;

    int threads;                /* 1 = single-threaded */
    size_t memory_limit;        /* bytes, 0 = no limit; ss_create fails above it */
    int use_isomorphism;        /* suit isomorphism at chance nodes (default 1) */
    /* Storage of regrets and strategy sums: 0 = float32 (default); 1 = 16-bit with
       one float32 scale per node and array (about half the memory; all arithmetic
       stays float32); 2 = float32 unless that exceeds memory_limit, then 16-bit. */
    int compact_storage;

    /* Optional progress callback, called from the solving thread after every
       exploitability check: iteration count and max exploitability in % of pot. */
    void (*progress)(void *user, int iteration, double exploitability_pct);
    void *progress_user;
} ss_config;

typedef struct ss_solver ss_solver;

typedef struct {
    int iterations;
    double exploitability[2];       /* best-response gain, in payoff units */
    double exploitability_pct[2];   /* the same in % of each player's pot value */
    double value[2];                /* each player's EV under the average strategies, payoff units */
    double pot_value[2];            /* payoff units of the starting pot for each player */
    double seconds;
    size_t peak_memory;             /* bytes */
    size_t tree_nodes;
    int compact_storage;            /* 1 if 16-bit storage was used */
    int status;                     /* SS_OK, SS_CANCELLED */
} ss_result;

/* Fills the spec's defaults: 33% and 75% pot, all-in, raise 3x, one raise, 0.67,
   chips mode, 1000 iterations, 0.5% target, check every 10, DCFR 1.5 / 0 / 2. */
void ss_config_default(ss_config *cfg);

/* Builds the tree and allocates all memory. Returns NULL and writes a message to
   err on failure (bad input, memory over limit). */
ss_solver *ss_create(const ss_config *cfg, char *err, size_t err_len);
void ss_destroy(ss_solver *s);

/* Bytes the solve needs (tree, storage, tables, scratch), known after ss_create. */
size_t ss_memory_required(const ss_solver *s);

/* Runs iterations until the stop rule or a cancel. May be called again to continue. */
int ss_solve(ss_solver *s, ss_result *result);

/* Recomputes exploitability and values for the current average strategies. */
int ss_evaluate(ss_solver *s, ss_result *result);

/* Copies the solver state (regrets and average strategies) from src into dst.
   Both must have been created with the same board, ranges and tree settings
   (payoff mode and table may differ). Returns 0, or -1 if the trees differ. */
int ss_copy_strategy(ss_solver *dst, const ss_solver *src);

/* Warm start: dst continues from src's regrets (multiplied by regret_scale) with a
   fresh average strategy and iteration count. Same tree requirements as
   ss_copy_strategy. Useful when solving the same spot with different payoffs
   (for example ICM tables after a chip-EV solve). Returns 0 or -1. */
int ss_warm_start(ss_solver *dst, const ss_solver *src, double regret_scale);

/* Saves the solver state (regrets, strategy sums, iteration count) to a file, or
   loads it into a solver built with the same board, ranges, tree and storage mode
   (payoffs may differ). Returns 0, or -1 on I/O error or mismatch. */
int ss_save_state(const ss_solver *s, const char *path);
int ss_load_state(ss_solver *s, const char *path);

/* Keeps the regrets (multiplied by regret_scale), clears the average strategy and
   restarts the iteration count: ss_warm_start = ss_copy_strategy + this. Use after
   ss_load_state to warm-start from a saved solve. */
int ss_restart_average(ss_solver *s, double regret_scale);

/* Thread-safe request to stop ss_solve at the next check point. */
void ss_cancel(ss_solver *s);

/* Live progress (thread-safe): iterations done so far and last exploitability %. */
int ss_progress_iterations(const ss_solver *s);
double ss_progress_exploitability(const ss_solver *s);

/* --- Inspecting the solution ------------------------------------------- */

int ss_root(const ss_solver *s);
int ss_node_kind(const ss_solver *s, int node);
int ss_node_player(const ss_solver *s, int node);       /* acting player (decision), folder (fold) */
int ss_node_num_children(const ss_solver *s, int node);
int ss_node_child(const ss_solver *s, int node, int i);
int ss_node_action(const ss_solver *s, int node);       /* action leading into this node */
double ss_node_amount(const ss_solver *s, int node);    /* chips put in by that action on this street (total, "to") */
double ss_node_pot(const ss_solver *s, int node);       /* pot when this node is reached */
int ss_node_card(const ss_solver *s, int node);         /* card dealt into this node (child of a chance node) */

/* Child of a chance node for a dealt card. With suit isomorphism several cards
   share one child; *perm (may be NULL) receives the suit permutation (4 bytes,
   perm[old_suit] = new_suit) that maps the dealt card to the stored child. */
int ss_chance_child(const ss_solver *s, int node, int card, uint8_t perm[4]);

/* Average strategy at a decision node: out[a * 1326 + combo] for each child a.
   Combos outside the acting player's range are 0. Returns the action count. */
int ss_node_strategy(const ss_solver *s, int node, float *out);

/* Number of live combos and their combo indices for a player. */
int ss_player_combos(const ss_solver *s, int player, const uint16_t **combos);

#ifdef __cplusplus
}
#endif

#endif
