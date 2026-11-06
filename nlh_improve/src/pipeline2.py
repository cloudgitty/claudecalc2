"""Phase C of the improvement plan: one converged run for one stack depth (resumable).

usage (from a build folder such as nlh9max_300bb/):
    python3 ../nlh_improve/src/pipeline2.py ../nlh_improve/plan_<depth>.json

The plan file sets the gate decisions (tree options, raises, river sample, flop counts, rounds).
Every step writes its output into work2/; a step whose output exists is skipped, so after a
crash the command is simply run again (see NLH_IMPROVEMENT_PLAN.md section 4 for the
checkpoints). After every finished round the small results are committed and pushed (durable
checkpoints): fit.json, rfile.bin, the preflop strategy/EV/log and progress lines.

Steps
 0 tree from config + plan options; config2.json written
 1 start ranges: the build's final preflop solution; on a new tree a cold fixed-prior solve, then
   one with R from the build's last fit
 2 river correction: turn-end and river solves on `river_flops` flops for every spot,
   fit both, src/river_corr.py
 3 loop rounds r1..: spots -> calibration solves (calib_flops, more for the slow spots) -> fit ->
   rmodel (hand-type regression + river correction) -> running average of R (weights 1, 2, 3 ...)
   -> preflop solve warm-started from the previous round -> widths. Stop when the RFI widths of
   UTG..BTN and SB move <= 0.5 points for two rounds in a row (max_rounds cap)
 4 final preflop solve (more iterations, warm)
 5 full postflop pass, all flops
 6 polish: refit R on all flops, re-solve; one more round if a width moves > 0.5 points
 7 rake variants through the same loop (warm-started from the main solution)
 8 pack, validate, rake report
"""
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
BIN = os.path.join(ENGINE, "build")
SEATS7 = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB"]


def log(msg):
    line = f"{time.strftime('%H:%M', time.gmtime())} {msg}"
    with open("work2/pipeline2.log", "a") as f:
        f.write(line + "\n")
    print(line, flush=True)


def sh(cmd, env=None, out=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    with open(out, "a") if out else open(os.devnull, "w") as fh:
        r = subprocess.run(cmd, shell=True, env=e, stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise SystemExit(f"step failed ({r.returncode}): {cmd}")


def py(script, args, env=None, out=None):
    sh(f"python3 {os.path.join(HERE, script)} {args}", env, out)


def done(path):
    return os.path.exists(path)


def commit(msg):
    """Durable checkpoint: commit and push the small results (work2/ is ignored, so the files to
    keep are copied into checkpoints/ first)."""
    sh("git add -A checkpoints PROGRESS2.md 2>/dev/null; git commit -qm \"" + msg +
       "\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: "
       "https://claude.ai/code/session_01Xh1SLG9q2fz5cKSV5JmQFu\" || true; "
       "for i in 1 2 3 4; do git push -q -u origin part-b-solver && break; sleep $((2**i)); done")


def keep(round_dir, name, milestone=False):
    """Copy a finished step's results into checkpoints/<name>/ for the durable checkpoint. Every
    round keeps only small files (summary, log, gzipped R fit); milestones (end of a loop, final
    solve, polish) also keep the gzipped R file and a float16 gzipped preflop strategy and reach,
    enough to restart the next step on a fresh container."""
    import gzip
    import numpy as np
    dst = os.path.join("checkpoints", name)
    os.makedirs(dst, exist_ok=True)
    for f in ("summary.json", "pre.log", "report.txt", "corr.json", "corr.txt"):
        p = os.path.join(round_dir, f)
        if os.path.exists(p):
            shutil.copy(p, dst)
    for f in ("fit.json",) + (("rfile.bin",) if milestone else ()):
        p = os.path.join(round_dir, f)
        if os.path.exists(p):
            with open(p, "rb") as a, gzip.open(os.path.join(dst, f + ".gz"), "wb", compresslevel=9) as b:
                b.write(a.read())
    if milestone:
        for f, dt in (("pre.strat", np.float32), ("pre.ev", np.float32)):
            p = os.path.join(round_dir, f)
            if os.path.exists(p):
                arr = np.fromfile(p, dtype=dt).astype(np.float16)
                with gzip.open(os.path.join(dst, f + ".f16.gz"), "wb", compresslevel=9) as b:
                    b.write(arr.tobytes())
        p = os.path.join(round_dir, "pre.reach")
        if os.path.exists(p):
            shutil.copy(p, dst)


def widths(summary_path):
    s = json.load(open(summary_path))
    w = dict(s["rfi"])
    return [w[k] for k in SEATS7]


def preflop(d, iters, rfile, warm=None, rake=None):
    """Preflop solve in d (resumes from d/pre.state on its own); warm = previous state file."""
    env = {"PF_WARM": warm} if warm and not done(f"{d}/pre.state") else {}
    rk = f" {rake[0]} {rake[1]}" if rake else ""
    sh(f"{BIN}/preflop work2 {iters} {d}/pre {rfile} 4{rk}", env)
    py("preflop_report.py", f"{d}/pre --json {d}/summary.json", out=f"{d}/report.txt")


def calib_flops(plan, spot):
    n = plan["calib_flops_slow"] if spot in plan.get("slow_spots", []) else plan["calib_flops"]
    step = 100 / n
    return sorted({int(i * step) for i in range(n)})


def solve_calib(plan, ranges, out, rake_env, flops_override=None, only=None, jobs=4):
    spots = json.load(open(f"{ranges}/spots.json"))
    groups = {}
    for name in spots:
        if only and name not in only:
            continue
        fl = flops_override or calib_flops(plan, name)
        groups.setdefault(tuple(fl), []).append(name)
    for fl, names in groups.items():
        py("run_postflop.py", f"{ranges} {out} {','.join(map(str, fl))} {','.join(names)} {jobs}",
           env=rake_env, out=f"{out}.log")


def fit_round(plan, d, prev_pre, ranges, solves, rake_env, history):
    shutil.copy(f"{ranges}/spots.json", f"{d}/spots_used.json")
    py("fit_r.py", f"{ranges} {solves} {d}/fit.json", out=f"{d}/fit.txt")
    env = dict(rake_env)
    if os.path.exists("work2/river/corr.json"):
        env["RIVER_CORR"] = "work2/river/corr.json"
    py("rmodel.py", f"fitted work2 {d}/fit.json {prev_pre} {d}/rfile_raw.bin", env=env, out=f"{d}/rmodel.txt")
    # running average over rounds, weights 1, 2, 3, ... (newer rounds count more)
    sys.path.insert(0, HERE)
    import numpy as np
    from rmodel import read_rfile_full, write_rfile
    files = history + [f"{d}/rfile_raw.bin"]
    runs = [read_rfile_full(f, "work2") for f in files]
    wts = np.arange(1, len(runs) + 1, dtype=float)
    out = []
    for es in zip(*runs):
        rho = float(sum(w * e[1] for w, e in zip(wts, es)) / wts.sum())
        Rs = [sum(w * np.asarray(e[2][k]) for w, e in zip(wts, es)) / wts.sum() for k in range(len(es[0][2]))]
        out.append((es[0][0], rho, Rs))
    write_rfile(f"{d}/rfile.bin", out)


def loop(plan, tag, start_pre, rake=None, max_rounds=None):
    """Convergence loop; returns the last round directory."""
    rake_env = {"RAKE_PCT": str(rake[0]), "RAKE_CAP": str(rake[1])} if rake else {}
    if rake:
        cfg = json.load(open("work2/config2.json"))
        cfg["rake_pct"], cfg["rake_cap_bb"] = rake
        json.dump(cfg, open(f"work2/config_{tag}.json", "w"), indent=1)
        rake_env["NLH_CONFIG"] = f"work2/config_{tag}.json"
    else:
        rake_env["NLH_CONFIG"] = "work2/config2.json"
    prev_pre, prev_w, stable, history = start_pre, None, 0, []
    if os.path.exists(start_pre + ".strat") and os.path.exists(os.path.join(os.path.dirname(start_pre), "summary.json")):
        prev_w = widths(os.path.join(os.path.dirname(start_pre), "summary.json"))
    for k in range(1, (max_rounds or plan["max_rounds"]) + 1):
        d = f"work2/{tag}_r{k}"
        os.makedirs(d, exist_ok=True)
        if not done(f"{d}/summary.json"):
            if not done(f"{d}/rfile.bin"):
                py("spots.py", f"{prev_pre} {d}/ranges", env=rake_env, out=f"{d}/spots.txt")
                solve_calib(plan, f"{d}/ranges", f"{d}/solves", rake_env)
                fit_round(plan, d, prev_pre, f"{d}/ranges", f"{d}/solves", rake_env, history)
            warm = prev_pre + ".state" if os.path.exists(prev_pre + ".state") else None
            preflop(d, plan["round_iterations"], f"{d}/rfile.bin", warm, rake)
            keep(d, f"{tag}_r{k}")
            w = widths(f"{d}/summary.json")
            move = max(abs(a - b) for a, b in zip(w, prev_w)) if prev_w else 99.0
            with open("PROGRESS2.md", "a") as f:
                f.write(f"- {tag} round {k}: RFI {'/'.join(f'{x:.1f}' for x in w)}; largest move {move:.2f}\n")
            commit(f"{os.path.basename(os.getcwd())}: {tag} round {k} (largest RFI move {move:.2f})")
        history.append(f"{d}/rfile_raw.bin")
        w = widths(f"{d}/summary.json")
        move = max(abs(a - b) for a, b in zip(w, prev_w)) if prev_w else 99.0
        stable = stable + 1 if move <= 0.5 else 0
        log(f"{tag} round {k}: largest RFI move {move:.2f} (stable rounds {stable})")
        prev_pre, prev_w = f"{d}/pre", w
        if stable >= 2:
            break
    return prev_pre, stable >= 2


def main():
    plan = json.load(open(sys.argv[1]))
    os.makedirs("work2", exist_ok=True)
    os.makedirs("checkpoints", exist_ok=True)
    # 0: config and tree
    if not done("work2/tree.txt"):
        cfg = json.load(open("config.json"))
        cfg.update(plan["config"])
        json.dump(cfg, open("work2/config2.json", "w"), indent=1)
        py("preflop_tree.py", "work2/config2.json work2/tree.json", out="work2/tree.txt.log")
        for f in ("eq169.bin", "eq1326.bin"):
            shutil.copy(f"work/{f}", f"work2/{f}")
        log(f"tree: {open('work2/tree.txt.log').readline().strip()}")
    os.environ["NLH_CONFIG"] = "work2/config2.json"
    os.environ["NLH_TREE"] = "work2/tree.json"
    # 1: start ranges. Same tree as the build: its final solution. A new (larger) tree: a cold solve
    #    with the fixed priors, then one with R from the build's last fit (Phase 0 refit if present)
    start = "work/final/pre"
    if open("work2/tree.txt").read() != open("work/tree.txt").read():
        if not done("work2/start/summary.json"):
            os.makedirs("work2/start0", exist_ok=True)
            os.makedirs("work2/start", exist_ok=True)
            if not done("work2/start0/summary.json"):
                py("rmodel.py", "fixed work2 work2/start0/rfile.bin", out="work2/start0/rmodel.txt")
                preflop("work2/start0", plan["final_iterations"], "work2/start0/rfile.bin")
            fit = plan.get("start_fit") or next(f for f in ("work/p0/fit.json", "work/final/fit.json",
                                                             "work/s4/fit.json") if os.path.exists(f))
            py("rmodel.py", f"fitted work2 {fit} work2/start0/pre work2/start/rfile.bin", out="work2/start/rmodel.txt")
            preflop("work2/start", plan["final_iterations"], "work2/start/rfile.bin", "work2/start0/pre.state")
            keep("work2/start", "start", milestone=True)
            commit(f"{os.path.basename(os.getcwd())}: start solve on the new preflop tree")
            log(f"start solve on the new tree done (R from {fit})")
        start = "work2/start/pre"
    # 2: river correction
    if plan.get("river_flops") and not done("work2/river/corr.json"):
        os.makedirs("work2/river", exist_ok=True)
        py("spots.py", f"{start} work2/river/ranges", out="work2/river/spots.txt")
        fl = sorted({int(i * 100 / plan["river_flops"]) for i in range(plan["river_flops"])})
        rs = plan.get("river_spots")
        solve_calib(plan, "work2/river/ranges", "work2/river/turn", {}, fl, rs)
        renv = {"RIVER": "1", "MEM_MB": str(plan.get("river_mem_mb", 3500))}
        if plan.get("river_reduced"):
            renv["RIVER_REDUCED"] = str(plan["river_reduced"])
        solve_calib(plan, "work2/river/ranges", "work2/river/river", renv, fl, rs, plan.get("river_jobs", 4))
        py("fit_r.py", "work2/river/ranges work2/river/turn work2/river/fit_turn.json", out="work2/river/fit.txt")
        py("fit_r.py", "work2/river/ranges work2/river/river work2/river/fit_river.json", out="work2/river/fit.txt")
        py("river_corr.py", "work2/river/fit_turn.json work2/river/fit_river.json work2/river/corr.json",
           out="work2/river/corr.txt")
        keep("work2/river", "river")
        commit(f"{os.path.basename(os.getcwd())}: river correction")
        log("river correction done")
    # 3: main loop
    last, conv = loop(plan, "main", start)
    keep(os.path.dirname(last), "main_last", milestone=True)
    commit(f"{os.path.basename(os.getcwd())}: main loop {'converged' if conv else 'at the round cap'}")
    log(f"main loop {'converged' if conv else 'stopped at the round cap'}: {last}")
    # 4: final preflop
    if not done("work2/final/summary.json"):
        os.makedirs("work2/final", exist_ok=True)
        shutil.copy(os.path.join(os.path.dirname(last), "rfile.bin"), "work2/final/rfile.bin")
        preflop("work2/final", plan["final_iterations"], "work2/final/rfile.bin", last + ".state")
        keep("work2/final", "final", milestone=True)
        commit(f"{os.path.basename(os.getcwd())}: final preflop solve")
    # 5: full postflop pass
    if not done("work2/final/pass_done"):
        py("spots.py", "work2/final/pre work2/final/ranges", out="work2/final/spots.txt")
        py("run_postflop.py", "work2/final/ranges work2/final/pre_solves all all 4", out="work2/stage7.log")
        open("work2/final/pass_done", "w").write("ok\n")
        log("full postflop pass done")
    # 6: polish on all flops
    if not done("work2/polish/summary.json"):
        os.makedirs("work2/polish", exist_ok=True)
        fit_round(plan, "work2/polish", "work2/final/pre", "work2/final/ranges", "work2/final/pre_solves", {},
                  [f"{os.path.dirname(last)}/rfile_raw.bin"])
        preflop("work2/polish", plan["final_iterations"], "work2/polish/rfile.bin", "work2/final/pre.state")
        keep("work2/polish", "polish", milestone=True)
        move = max(abs(a - b) for a, b in zip(widths("work2/polish/summary.json"), widths("work2/final/summary.json")))
        with open("PROGRESS2.md", "a") as f:
            f.write(f"- polish (R on all 100 flops): largest RFI move {move:.2f}\n")
        commit(f"{os.path.basename(os.getcwd())}: polish on all flops (move {move:.2f})")
        log(f"polish: largest RFI move {move:.2f}")
    # 7: rake variants
    for tag, rk in (("rake0", (0.0, 0.0)), ("cap3", (0.10, 3.0)), ("pct5", (0.05, 5.0))):
        last_v, conv_v = loop(plan, tag, "work2/polish/pre", rake=rk)
        log(f"variant {tag} {'converged' if conv_v else 'at the round cap'}: {last_v}")
        open(f"work2/{tag}_last.txt", "w").write(last_v + "\n")
    log("PIPELINE2_DONE")


if __name__ == "__main__":
    main()
