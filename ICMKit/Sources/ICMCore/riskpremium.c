#include "riskpremium.h"
#include "icm.h"

int risk_required_equity(int n, const double *stacks, int npay, const double *payouts,
                         int hero, int villain, double *required)
{
    if (n < 2 || n > ICM_MAX_PLAYERS || hero < 0 || hero >= n || villain < 0 || villain >= n ||
        hero == villain || !required)
        return -1;
    double eff = stacks[hero] < stacks[villain] ? stacks[hero] : stacks[villain];
    if (!(eff > 0.0)) return -1;

    double s[ICM_MAX_PLAYERS], eq[ICM_MAX_PLAYERS];
    for (int i = 0; i < n; i++) s[i] = stacks[i];
    if (icm_equity(n, s, npay, payouts, eq)) return -1;
    double fold = eq[hero];

    s[hero] = stacks[hero] + eff;
    s[villain] = stacks[villain] - eff;
    if (icm_equity(n, s, npay, payouts, eq)) return -1;
    double win = eq[hero];

    s[hero] = stacks[hero] - eff;
    s[villain] = stacks[villain] + eff;
    if (icm_equity(n, s, npay, payouts, eq)) return -1;
    double lose = eq[hero];

    if (!(win > lose)) return -1;
    *required = (fold - lose) / (win - lose);
    return 0;
}

int risk_premium(int n, const double *stacks, int npay, const double *payouts,
                 int hero, int villain, double *premium)
{
    double req;
    if (risk_required_equity(n, stacks, npay, payouts, hero, villain, &req)) return -1;
    *premium = req - 0.5;
    return 0;
}

int stack_role(int n, const double *stacks, int player, double big_blind)
{
    double total = 0.0, top = -1.0, second = -1.0;
    int leader = -1;
    for (int i = 0; i < n; i++) {
        total += stacks[i];
        if (stacks[i] > top) { second = top; top = stacks[i]; leader = i; }
        else if (stacks[i] > second) second = stacks[i];
    }
    if (player == leader) {
        if (top >= 2.0 * second || top >= 0.4 * total) return ROLE_RUNAWAY_LEADER;
        return ROLE_CHIP_LEADER;
    }
    double avg = total / n;
    if (stacks[player] < 0.6 * avg || stacks[player] <= 15.0 * big_blind) return ROLE_SHORT;
    return ROLE_MID;
}

const char *stack_role_name(int role)
{
    switch (role) {
    case ROLE_SHORT: return "short stack";
    case ROLE_MID: return "mid stack";
    case ROLE_CHIP_LEADER: return "chip leader";
    case ROLE_RUNAWAY_LEADER: return "runaway chip leader";
    default: return "?";
    }
}
