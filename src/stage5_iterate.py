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
    # warm-up: the starting responses are crude (e.g. 40% fold to a 3-bet), and a
    # simultaneous update from them oscillates (BB 3-bets 60%+, LJ opens 60%).  So the
    # response nodes are first iterated to a fixed point against the starting opens.
    import os
    def resume(name):
        if os.path.exists(W(name + ".json")):
            s.load(W(name + ".npz")); return json.load(open(W(name + ".json")))
        return None
    hw = resume("ckpt_warmup")
    if not os.path.exists(W("ckpt_main.json")):
        hw, cw = s.iterate(20, tag="warm-up (opens fixed)", update_open=False, ckpt=W("ckpt_warmup"), hist=hw)
        json.dump(hw, open(W("hist_warmup.json"), "w"))
        progress(f"(5) warm-up converged={cw} after {len(hw)} sweeps")
    hm = resume("ckpt_main")
    hist, conv = s.iterate(12, tag="main", ckpt=W("ckpt_main"), hist=hm)
    s.save(W("state_main.npz"))
    json.dump(hist, open(W("hist_main.json"), "w"))
    progress(f"(5) iterations done: converged={conv} after {len(hist)} rounds ({(time.time()-t0)/60:.0f} min)")


if __name__ == "__main__":
    main()
