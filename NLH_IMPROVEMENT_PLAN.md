# NLH high-rake builds: improvement plan (items 1-8) and crash checkpoints

This plan improves the three finished builds - `nlh9max/` (100bb), `nlh9max_200bb/` and
`nlh9max_300bb/` - by eight measured weaknesses. It also says, for every step, where the work is
saved, so that a container restart or crash costs minutes, not hours.

Nothing here changes the output formats: `preflop.json`, `preflop_ev.bin`,
`postflop/<pottype>_<hero>_vs_<villain>.bin.gz`, `test_vectors.json` and the `combos.json`
enumeration stay exactly as delivered. All 100 flops stay in every final pass.

---

## 1. The eight improvements

| # | Improvement | Problem it fixes (measured in the current builds) |
| --- | --- | --- |
| 1 | Fit R on all 100 flops instead of 12 | R is fitted on 12 calibration flops per spot; that sampling noise keeps the preflop <-> R loop from settling (300bb: BTN +/-4, SB +/-5 points) |
| 2 | Converge the preflop <-> R loop | No build met the task's "RFI widths change <= 0.5 points" rule |
| 3 | Converge the rake-free control the same way | The control still moved the BTN by 7-14 points between rounds, so the "rake tightens ranges" check is uncertain in size |
| 4 | Hand-type features in the R regression | Unsolved position pairs use strength buckets only; playability (pairs, suited, connected, suited aces) is lost there |
| 5 | Money-conserving payoffs | The card-removal approximation creates +0.066 bb per hand (summed over 8 seats) with no rake |
| 6 | Un-pruned preflop tree | Cold 4-bets, cold 5-bets and multiway 3-bet pots are cut out (60,343 -> 8,057 nodes at 100bb) |
| 7 | Two postflop raises per street | Only one raise per street; deep raising wars cannot happen |
| 8 | River in the R measurement | Every postflop solve ends at the turn with an equity showdown; river bluffs, river stack-offs and implied odds are missing, which understates speculative hands most at 300bb |

---

## 2. Order of work and why

Items 4-8 change the **model**; items 1-3 **tune** it. Any loop I settle now would be undone by the
model changes, so the order is: change the model, measure its cost, then run one clean, converged
loop per depth. The 4 cores would otherwise sit idle during the engineering, so they run a cheap
interim improvement (Phase 0) meanwhile.

```
Phase 0  interim refit on existing solves (item 1)      background, ~1 h per depth
Phase A  engineering: items 5, 6, 4, 7, 8                 mostly my time, small tests
Phase B  measurement gate (cost of each change)          100bb first, ~half a day
Phase C  converged runs per depth: 100bb, 200bb, 300bb   items 8, 2, 1, 3
Phase D  validation, report, zip per depth
```

### Phase 0 - interim refit (item 1, background)
Each depth already has 1,200 final postflop solves. Refit R from all 100 flops, re-solve preflop
once, publish as an interim update with the measured change in RFI widths. Superseded by Phase C.

### Phase A - engineering
Each change must pass its check before it is accepted.

| Item | Change | Acceptance check |
| --- | --- | --- |
| 5 | Per terminal, rescale payoffs so all players' expected payoffs sum to exactly pot - rake under the current ranges | Rake 0 and R = 1: summed EV = 0 (now +0.066 bb/hand). Becomes a permanent validation check |
| 6 | Compute terminal values from shared 169x169 matrices instead of one stored matrix per terminal (the full tree would otherwise need ~7 GB), then remove the pruning rules | The pruned tree gives identical results to today; then time and memory of the full tree are measured |
| 4 | R regression features: strength bucket + pair, suited, connectedness, suited ace; per-class R stays for solved spots | Leave-one-spot-out cross-validation: prediction error must drop vs buckets only |
| 7 | `max_raises` 2 in the postflop solver | Rake unit tests pass; one spot compared with the 1-raise tree |
| 8 | Full flop-to-river solves for a sample of flops, used to measure a **river correction** (ratio of river R to turn-end R by pot type, position and hand class); the loop keeps the cheap turn-end solves and applies the correction | Memory, time and size of the correction measured on one spot |
| - | **Mid-solve checkpoints in the postflop driver** (needed for the long river solves, see section 4) | Kill a solve mid-way, restart, results identical |

### Phase B - measurement gate (100bb first)
Measure the cost per solve with 2 raises and with the river at each depth, and the preflop
iteration time on the un-pruned tree. Decision rules, written to `PROGRESS.md` before anything is
cut:
- **Two raises:** everywhere if a full pass costs at most ~3x today; otherwise in calibration only.
- **River sample:** 12 flops per spot. If one river solve needs more than ~4 GB or ~1 hour, 6 flops
  for that depth.
- **Flops:** the final pass keeps all 100 flops.

### Phase C - converged runs (per depth, 100bb then 200bb then 300bb)
1. **River correction** (item 8): one-off river calibration solves.
2. **Convergence loop** (item 2): each round uses 30 calibration flops per spot (50 for SB vs BB
   and BTN vs BB). R is a running average over all rounds with slowly decaying weights, not a
   50/50 blend. Stop when the RFI widths move <= 0.5 points for **two rounds in a row**; cap 10
   rounds.
3. **Final preflop solve** on the un-pruned tree.
4. **Full postflop pass**, 100 flops, every reachable spot.
5. **Polish** (item 1): refit R on all 100 flops, re-solve preflop; if any width moves more than
   0.5 points, one more round.
6. **Rake variants** (item 3): rake 0 control, cap 3bb, rake 5% - each through the same
   convergence loop, each with its own river correction (12 flops, BTN vs BB and CO vs BB only).

### Phase D - validation and reporting (per depth)
The existing section 5 checks plus three new ones: payoff conservation, a convergence log showing
the 0.5-point rule met, and the measured river error (turn-end vs river R; how much the root
strategies move on the river-sampled flops). `REPORT.md` updated with every change and its
measured effect; new zip.

### Cost estimate

| Depth | Core-hours | Wall clock on 4 cores |
| --- | --- | --- |
| 100bb | ~70 | ~18 h |
| 200bb | ~130 | ~33 h |
| 300bb | ~200 | ~50 h |
| **Total** | **~400** | **~4-5 days**, plus ~1 day of engineering |

The river calibration and the larger convergence loop are most of the cost; Phase B replaces these
estimates with measured ones before the big runs start.

---

## 3. Two kinds of checkpoint

**Local checkpoints** live in each build's `work/` folder on the container disk. They survive a
container *restart* (every restart so far kept the disk: 4 restarts across the 200bb and 300bb
builds, no file lost). They do **not** survive the container being replaced by a fresh one, because
`work/` is not in git (0.7-1.4 GB per depth).

**Durable checkpoints** are pushed to the `part-b-solver` branch on GitHub. They survive anything.
They are kept small: the fitted R (`fit.json`, `rfile.bin`), the preflop strategy and EV of each
finished round, the packed outputs and the logs - enough to resume the loop without re-running
finished rounds. Bulky solve files (~130 MB per postflop pass) stay local; if they were lost, only
the postflop pass in progress is redone, not the loop.

---

## 4. Where the checkpoints are

| Step | Local checkpoint (what is saved, when) | Resumes by | Max loss on a crash | Durable checkpoint (git) |
| --- | --- | --- | --- | --- |
| Equity table (reused; only if recomputed) | none (single 22-25 min run) | re-running it | ~25 min | `eq1326.bin` and `eq169.bin` (7.3 MB together) committed once, so a fresh container never recomputes them |
| Preflop solve (every run) | `<prefix>.state`: regrets and strategy sums **every 50 iterations** (exists today) | `./build/preflop` loads the state automatically | 50 iterations: ~15 s at 100bb, ~1-2 min on the un-pruned tree | after each finished run: `pre.strat`, `pre.ev`, `pre.reach`, `pre.log` (~25 MB per run; committed per round) |
| Postflop solve, turn-end (calibration and final pass) | each finished solve writes `<flop>.bin` + `.json`; the runner skips finished solves (exists today) | `src/run_postflop.py` re-run | the 4 solves in flight: up to ~4 min each at 300bb | per spot after each pass: a compact summary (EV per class, exploitability) for the R fit; the packed `postflop/*.bin.gz` after the final pass |
| Postflop solve, river (new, item 8) | **new:** solver state saved every 10 minutes to `work/ckpt/<spot>_<flop>.bin` (the solver already has `ss_save_state` / `ss_load_state`; the driver will call them), deleted when the solve finishes | the driver loads the checkpoint if it exists | 10 min per solve in flight (without it: up to ~1 h each) | river correction table after the river calibration |
| R fit (each round) | `fit.json`, `rfile.bin` in the round folder | the pipeline skips rounds whose files exist | seconds | committed per round |
| Convergence loop (item 2) | one folder per round (`work/r<N>/`); the running average of R is recomputed from the committed per-round `rfile.bin` files | `src/pipeline.sh` re-run: finished rounds skipped, the open round resumes mid-step | the step in progress (one preflop checkpoint interval or the postflop solves in flight) | each finished round: R, preflop result, widths in `PROGRESS.md` |
| Final postflop pass | as for postflop solves | runner skip | 4 solves in flight | `postflop/*.bin.gz` and the per-spot summaries when the pass ends; `PROGRESS.md` every 2 h with the solve count |
| Rake variants | same folders and rules as the main loop (`work/var_<name>_r<N>/`) | pipeline skip | step in progress | per round, as the main loop |
| Packing, validation, report | outputs written in seconds | re-run | seconds | outputs, `validation.md`, `REPORT.md`, zip |

**Rule of thumb:** after a restart the worst case is about 10 minutes of lost compute (one river
checkpoint interval); typical is under 5 minutes.

### What keeps a crash from costing more than the table says
- **Resumable pipeline.** `src/pipeline.sh` checks for each step's output and skips finished work;
  it is simply started again after a restart.
- **Watcher always running.** The one long outage (6.5 h at 300bb) happened while no watcher was
  running and the session looked idle. A background watcher stays on for the whole run, re-armed
  every 2 hours; after a restart I restart the pipeline and the watcher straight away.
- **Pushes on a schedule.** Durable checkpoints are committed and pushed after every round and at
  least every 2 hours during long passes, so a replaced container loses at most one round's
  bulky solve files.
- **Restart log.** Every restart is written to `work/pipeline.log` and `PROGRESS.md` with the time
  lost.

---

## 5. Where I stop and ask
- Any flop cut, or any spot dropped from the final pass.
- A depth whose measured cost exceeds the estimate above by more than 50%.
- The loop failing to converge within 10 rounds: I report the residual swing instead of forcing it.
