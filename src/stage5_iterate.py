"""Stage 5: iterate best responses (damped 0.5) from the starting ranges until
convergence (two consecutive rounds within tolerance) or 12 rounds."""
import json, sys, time
import numpy as np
from common import W, progress
from solve import Solver, R0_DEFAULT


def load_r0(shift=0.0):
    d = json.load(open(W("r0.json")))["R0"]
    return {tuple(k.split(",")): v + shift for k, v in d.items()}


def main():
    t0 = time.time()
    s = Solver(R0=load_r0(), log=progress)
    w0 = s.widths()
    progress("(5) start RFI widths: " + " ".join(f"{k}={w0[k][0]:.1f}" for k in w0 if k.endswith("RFI")))
    hist, conv = s.iterate(12, tag="main")
    s.save(W("state_main.npz"))
    json.dump(hist, open(W("hist_main.json"), "w"))
    progress(f"(5) iterations done: converged={conv} after {len(hist)} rounds ({(time.time()-t0)/60:.0f} min)")


if __name__ == "__main__":
    main()
