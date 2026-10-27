"""Iterative best-response solver for PLO6 6-max 200bb preflop (heads-up model).

Every heads-up confrontation (opener O, responder D) is a 5-node tree:
  O1  O open-raises or folds                (RFI, responders act in seat order)
  D1  D folds / calls / 3-bets              (defend vs open)
  O2  O folds / calls / 4-bets (pot)        (opener vs 3-bet)
  D2  D folds / calls / jams                (3-bettor vs 4-bet)
  O3  O folds / calls the jam               (internal, not output)
All 15 pairs are solved; 24 nodes are output.  EVs are in bb relative to folding
at the node; postflop is replaced by realised equity R * eq * pot."""
import os, json, time
import numpy as np
import engine
from common import W, classes

SEATS = ["LJ", "HJ", "CO", "BTN", "SB", "BB"]
STACK = 200.0
INIT_OPEN = {"LJ": 20, "HJ": 25, "CO": 32, "BTN": 48, "SB": 42}
R0_DEFAULT = {("srp", "ip"): 1.00, ("srp", "oop"): 0.82, ("srp", "sbcold"): 0.72,
              ("3bp", "ip"): 1.00, ("3bp", "oop"): 0.82, ("4bp", "ip"): 0.90, ("4bp", "oop"): 0.82}

OUTPUT = {}
for o in ["LJ", "HJ", "CO", "BTN", "SB"]: OUTPUT[f"{o}_RFI"] = ("O1", o, None)
for o in ["LJ", "HJ", "CO", "BTN", "SB"]: OUTPUT[f"BB_vs_{o}"] = ("D1", o, "BB")
OUTPUT["SB_vs_CO"] = ("D1", "CO", "SB"); OUTPUT["SB_vs_BTN"] = ("D1", "BTN", "SB"); OUTPUT["BTN_vs_CO"] = ("D1", "CO", "BTN")
for o in ["LJ", "HJ", "CO", "BTN", "SB"]: OUTPUT[f"{o}_vs_BB3bet"] = ("O2", o, "BB")
OUTPUT["CO_vs_BTN3bet"] = ("O2", "CO", "BTN"); OUTPUT["BTN_vs_SB3bet"] = ("O2", "BTN", "SB")
OUTPUT["BB_vs_BTN4bet"] = ("D2", "BTN", "BB"); OUTPUT["BB_vs_SB4bet"] = ("D2", "SB", "BB")
OUTPUT["SB_vs_BTN4bet"] = ("D2", "BTN", "SB"); OUTPUT["BTN_vs_CO4bet"] = ("D2", "CO", "BTN")
SPOT_ORDER = ["LJ_RFI", "HJ_RFI", "CO_RFI", "BTN_RFI", "SB_RFI", "BB_vs_LJ", "BB_vs_HJ", "BB_vs_CO", "BB_vs_BTN",
              "BB_vs_SB", "SB_vs_CO", "SB_vs_BTN", "BTN_vs_CO", "LJ_vs_BB3bet", "HJ_vs_BB3bet", "CO_vs_BB3bet",
              "BTN_vs_BB3bet", "SB_vs_BB3bet", "CO_vs_BTN3bet", "BTN_vs_SB3bet", "BB_vs_BTN4bet", "BB_vs_SB4bet",
              "SB_vs_BTN4bet", "BTN_vs_CO4bet"]


class Pair:
    def __init__(self, O, D, open_size):
        self.O, self.D = O, D
        self.key = f"{O}-{D}"
        self.o_amt = open_size + (1.0 if O == "SB" else 0.0)          # SB opens to 4 (default)
        self.o_post = 0.5 if O == "SB" else 0.0
        self.d_post = {"BB": 1.0, "SB": 0.5}.get(D, 0.0)
        self.dead = 1.5 - self.o_post - self.d_post
        self.T = 12.0 if D in ("SB", "BB") else 10.5
        self.F = 3 * self.T + self.dead                                 # pot-sized 4-bet
        self.d_ip = (D not in ("SB", "BB")) or (D == "BB" and O == "SB")
        self.pot_srp = 2 * self.o_amt + self.dead
        self.pot3 = 2 * self.T + self.dead
        self.pot4 = 2 * self.F + self.dead
        self.potj = 2 * STACK + self.dead
        self.d_pos = "ip" if self.d_ip else "oop"
        self.o_pos = "oop" if self.d_ip else "ip"
        self.d_srp = ("srp", "sbcold") if D == "SB" else ("srp", self.d_pos)


def round5(x):
    return np.round(np.asarray(x) * 20) / 20


def ramp(margin, w, half=0.01):
    """Linear ramp over +-half of the reaching combos around the indifference point."""
    f = np.zeros(len(margin))
    sel = w > 0
    if not sel.any(): return f
    m = margin[sel]; ww = w[sel]
    o = np.argsort(m, kind="stable")
    cw = np.cumsum(ww[o]); tot = cw[-1]
    q = (cw - ww[o] / 2) / tot                      # mid cumulative position, ascending margin
    q0 = ww[m < 0].sum() / tot                       # indifference point
    fr = np.clip((q - q0) / (2 * half) + 0.5, 0, 1)
    out = np.empty(len(m)); out[o] = fr
    f[sel] = out
    return f


class Solver:
    def __init__(self, R0=None, open_size=3.0, removal=True, seed=1, log=print, EPL=None):
        self.codes, self.combos = classes()
        self.n = len(self.codes)
        self.w = self.combos.astype(np.float64)
        b = np.load(W("bands.npz")); self.band = b["band"].astype(np.int64); self.pct = b["pct"]
        self.nb = int(self.band.max()) + 1
        if EPL is None:
            E = []; PL = []
            for k in range(self.nb):
                d = np.load(W(f"eqm_band{k:02d}.npz")); E.append(d["eq"]); PL.append(d["pl"])
            EPL = (np.stack(E, 1), np.stack(PL, 1))
        self.E = EPL[0].astype(np.float32); self.PL = EPL[1].astype(np.float32)
        self.R0 = dict(R0 or R0_DEFAULT)
        self.removal = removal
        self.pairs = {}
        for i, o in enumerate(SEATS[:5]):
            for d in SEATS[i + 1:]:
                p = Pair(o, d, open_size); self.pairs[p.key] = p
        self.grp = (self.codes >> 12)                         # rank multiset id
        _, self.ginv = np.unique(self.grp, return_inverse=True)
        self.gw = np.bincount(self.ginv, weights=self.w)
        self.rng = np.random.default_rng(seed)
        self.seed = seed
        self.corr = {}                                       # range key -> (idx, d_eq, d_pl)
        self.log = log
        self.init_strategies()

    # ---------------------------------------------------------------- ranges
    def init_strategies(self):
        pct = self.pct.astype(np.float64)
        n = self.n
        self.open = {o: (pct <= INIT_OPEN[o]).astype(float) for o in INIT_OPEN}
        self.S = {}
        for key, p in self.pairs.items():
            r = (pct <= 6).astype(float)
            c = ((pct > 6) & (pct <= (35 if p.D == "BB" else 20))).astype(float)
            # O2: 4-bet top 15% of the opening range, call next 45%
            op = INIT_OPEN[p.O]
            o_r = (pct <= 0.15 * op).astype(float); o_c = ((pct > 0.15 * op) & (pct <= 0.6 * op)).astype(float)
            d_j = (pct <= 0.35 * 6).astype(float); d_c = ((pct > 2.1) & (pct <= 4.2)).astype(float)
            o3 = (pct <= 0.5 * 0.15 * op).astype(float)
            self.S[key] = {"D1c": c, "D1r": r, "O2c": o_c, "O2r": o_r, "D2c": d_c, "D2r": d_j, "O3c": o3}

    def ranges(self, p):
        S = self.S[p.key]; op = self.open[p.O]; w = self.w
        return {"open": w * op, "dcall": w * S["D1c"], "d3": w * S["D1r"],
                "ocall3": w * op * S["O2c"], "o4": w * op * S["O2r"],
                "dcall4": w * S["D1r"] * S["D2c"], "djam": w * S["D1r"] * S["D2r"],
                "ocalljam": w * op * S["O2r"] * S["O3c"]}

    def eqv(self, rkey, V):
        tot = V.sum()
        if tot <= 0: V = self.w; tot = V.sum()
        wb = np.bincount(self.band, weights=V, minlength=self.nb) / tot
        eq = self.E @ wb.astype(np.float32); pl = self.PL @ wb.astype(np.float32)
        eq = eq.astype(np.float64); pl = pl.astype(np.float64)
        if rkey in self.corr:
            idx, de, dp = self.corr[rkey]
            eq[idx] += de; pl[idx] += dp
        return eq, pl

    def realise(self, R0key, pl, reach):
        r0 = self.R0[R0key]
        m = reach > 0
        if not m.any(): return np.full(self.n, r0)
        mu = np.average(pl[m], weights=reach[m]); sd = np.sqrt(np.average((pl[m] - mu) ** 2, weights=reach[m]))
        z = np.clip((pl - mu) / max(sd, 1e-9), -2, 2)
        return r0 * (1 + 0.15 * z)

    def respond(self, V, vals, salt):
        """Villain action probabilities given the hero class (card removal), V = villain weights."""
        if V.sum() <= 0:
            out = np.zeros((self.n, vals.shape[1])); out[:, 0] = 1; return out
        if not self.removal:
            p = (vals * V[:, None]).sum(0) / V.sum()
            return np.tile(p, (self.n, 1))
        idx = self.rng.choice(self.n, 200_000, p=V / V.sum())
        pool = engine.sample_reps(self.codes[idx], int(self.rng.integers(1 << 62)))
        raw = engine.sample_values(self.codes, pool, vals[idx], 48, int(self.rng.integers(1 << 62)))
        bad = raw[:, 0] < 0                          # no compatible villain hand: use the plain mean
        if bad.any():
            raw[bad] = ((vals * V[:, None]).sum(0) / V.sum()).astype(np.float32)
        out = np.empty((self.n, vals.shape[1]))
        for k in range(vals.shape[1]):              # average within rank multiset (kills MC noise)
            g = np.bincount(self.ginv, weights=raw[:, k] * self.w) / self.gw
            out[:, k] = g[self.ginv]
        return out

    # ---------------------------------------------------------------- one BR sweep
    def sweep(self):
        new = {}; aux = {}
        val_O2 = {}
        resp_D1 = {}
        for key, p in self.pairs.items():
            S = self.S[key]; Rg = self.ranges(p); op = self.open[p.O]
            eq = {k: self.eqv(f"{k}:{key}", v) for k, v in Rg.items()}
            # ---- O3: opener facing the jam
            reach_o3 = self.w * op * S["O2r"]
            v_o3 = eq["djam"][0] * p.potj - (STACK - p.F)
            o3c = ramp(v_o3, reach_o3)
            # ---- D2: 3-bettor facing the 4-bet
            reach_d2 = self.w * S["D1r"]
            Rd4 = self.realise(("4bp", p.d_pos), eq["o4"][1], reach_d2)
            v_d2c = Rd4 * eq["o4"][0] * p.pot4 - (p.F - p.T)
            ro3 = self.respond(Rg["o4"], np.stack([1 - S["O3c"], S["O3c"]], 1), 1)
            v_d2j = ro3[:, 0] * (p.T + p.F + p.dead) + ro3[:, 1] * (eq["ocalljam"][0] * p.potj - (STACK - p.T))
            d2r, d2c = self.decide(v_d2c, v_d2j, reach_d2)
            v_d2max = np.maximum(0, np.maximum(v_d2c, v_d2j))
            # ---- O2: opener facing the 3-bet
            reach_o2 = self.w * op
            Ro3 = self.realise(("3bp", p.o_pos), eq["d3"][1], reach_o2)
            v_o2c = Ro3 * eq["d3"][0] * p.pot3 - (p.T - p.o_amt)
            rd2 = self.respond(Rg["d3"], np.stack([1 - S["D2c"] - S["D2r"], S["D2c"], S["D2r"]], 1), 2)
            Ro4 = self.realise(("4bp", p.o_pos), eq["dcall4"][1], reach_o2)
            v_jamcall = eq["djam"][0] * p.potj - (STACK - p.o_amt)
            v_o2r = (rd2[:, 0] * (p.T + p.o_amt + p.dead)
                     + rd2[:, 1] * (Ro4 * eq["dcall4"][0] * p.pot4 - (p.F - p.o_amt))
                     + rd2[:, 2] * np.maximum(-(p.F - p.o_amt), v_jamcall))
            o2r, o2c = self.decide(v_o2c, v_o2r, reach_o2)
            val_O2[key] = np.maximum(0, np.maximum(v_o2c, v_o2r))
            # ---- D1: responder facing the open
            reach_d1 = self.w
            Rd = self.realise(p.d_srp, eq["open"][1], reach_d1)
            v_d1c = Rd * eq["open"][0] * p.pot_srp - (p.o_amt - p.d_post)
            ro2 = self.respond(Rg["open"], np.stack([1 - S["O2c"] - S["O2r"], S["O2c"], S["O2r"]], 1), 3)
            Rd3 = self.realise(("3bp", p.d_pos), eq["ocall3"][1], reach_d1)
            v_d1r = (ro2[:, 0] * (p.o_amt + p.dead + p.d_post)
                     + ro2[:, 1] * (Rd3 * eq["ocall3"][0] * p.pot3 - (p.T - p.d_post))
                     + ro2[:, 2] * (-(p.T - p.d_post) + v_d2max))
            d1r, d1c = self.decide(v_d1c, v_d1r, reach_d1)
            # responder's action probabilities given the opener's hand (for O1)
            resp_D1[key] = self.respond(self.w, np.stack([1 - S["D1c"] - S["D1r"], S["D1c"], S["D1r"]], 1), 4)
            new[key] = {"D1c": d1c, "D1r": d1r, "O2c": o2c, "O2r": o2r, "D2c": d2c, "D2r": d2r, "O3c": o3c}
            aux[key] = {"D1": (v_d1c, v_d1r, reach_d1), "O2": (v_o2c, v_o2r, reach_o2),
                        "D2": (v_d2c, v_d2j, reach_d2), "O3": (None, v_o3, reach_o3)}
        # ---- O1: RFI
        new_open = {}; v_open = {}
        for i, o in enumerate(SEATS[:5]):
            ev = np.zeros(self.n); pf = np.ones(self.n)
            for d in SEATS[i + 1:]:
                p = self.pairs[f"{o}-{d}"]; S = self.S[p.key]
                f, c, r = resp_D1[p.key].T
                eqc, plc = self.eqv(f"dcall:{p.key}", self.w * S["D1c"])
                Ro = self.realise(("srp", p.o_pos), plc, self.w * self.open[o])
                v_called = Ro * eqc * p.pot_srp - (p.o_amt - p.o_post)
                v_3b = -(p.o_amt - p.o_post) + val_O2[p.key]
                ev += pf * (c * v_called + r * v_3b)
                pf = pf * f
            ev += pf * 1.5
            v_open[o] = ev
            new_open[o] = ramp(ev, self.w)
        self.aux = aux; self.v_open = v_open
        return new, new_open

    def decide(self, v_call, v_raise, reach):
        r = ramp(v_raise - np.maximum(v_call, 0), reach)
        c = ramp(v_call, reach)
        return r, (1 - r) * c

    # ---------------------------------------------------------------- iteration
    def spot_freqs(self, name, S=None, open_=None):
        S = S or self.S; open_ = open_ or self.open
        node, o, d = OUTPUT[name]
        if node == "O1": return np.zeros(self.n), open_[o]
        s = S[f"{o}-{d}"]
        if node == "D1": return s["D1c"], s["D1r"]
        if node == "O2":
            m = open_[o] > 0
            return np.where(m, s["O2c"], 0), np.where(m, s["O2r"], 0)
        if node == "D2":
            m = s["D1r"] > 0
            return np.where(m, s["D2c"], 0), np.where(m, s["D2r"], 0)

    def quantise(self, c, r):
        r5 = round5(r); c5 = np.minimum(round5(c), 1 - r5)
        return np.maximum(c5, 0), r5

    def widths(self, S=None, open_=None):
        out = {}
        tot = self.w.sum()
        for name in SPOT_ORDER:
            c, r = self.spot_freqs(name, S, open_)
            out[name] = (100 * (self.w * r).sum() / tot, 100 * (self.w * (c + r)).sum() / tot)
        return out

    def step(self, damp=0.5, update_open=True):
        new, new_open = self.sweep()
        oldS = {k: dict(v) for k, v in self.S.items()}; old_open = dict(self.open)
        wold = self.widths()
        for key in self.S:
            s = self.S[key]; nw = new[key]
            for a, b in (("D1c", "D1r"), ("O2c", "O2r"), ("D2c", "D2r")):
                c = damp * s[a] + (1 - damp) * nw[a]; r = damp * s[b] + (1 - damp) * nw[b]
                s[a], s[b] = self.quantise(c, r)
            s["O3c"] = round5(damp * s["O3c"] + (1 - damp) * nw["O3c"])
        if update_open:
            for o in self.open:
                self.open[o] = round5(damp * self.open[o] + (1 - damp) * new_open[o])
        wnew = self.widths()
        dw = max(max(abs(wnew[k][0] - wold[k][0]), abs(wnew[k][1] - wold[k][1])) for k in SPOT_ORDER)
        dfs = []
        for name in SPOT_ORDER:
            c0, r0 = self.spot_freqs(name, oldS, old_open); c1, r1 = self.spot_freqs(name, self.S, self.open)
            m = (c0 + r0 > 0) | (c1 + r1 > 0)
            if m.any():
                dfs.append(100 * np.average(np.abs(c1 - c0)[m] + np.abs(r1 - r0)[m], weights=self.w[m]))
        return dw, float(np.mean(dfs)), float(np.max(dfs)), wnew

    def iterate(self, max_rounds=12, tol_w=0.3, tol_f=2.0, tag="main", update_open=True, ckpt=None, hist=None):
        hist = list(hist or []); ok = 0
        for x in hist: ok = ok + 1 if (x["dw"] <= tol_w and x["mean_df"] <= tol_f) else 0
        if ok >= 2: return hist, True
        for it in range(len(hist) + 1, max_rounds + 1):
            t0 = time.time()
            dw, mf, mxf, wn = self.step(update_open=update_open)
            ok = ok + 1 if (dw <= tol_w and mf <= tol_f) else 0
            line = (f"[{tag}] round {it}: max width change {dw:.2f} pts, mean byte-freq change {mf:.2f} pts "
                    f"(worst spot {mxf:.2f}); RFI widths " +
                    " ".join(f"{o}={wn[o + '_RFI'][0]:.1f}" for o in INIT_OPEN) + f" ({time.time()-t0:.0f}s)")
            self.log(line)
            hist.append(dict(round=it, dw=dw, mean_df=mf, max_df=mxf, widths=wn))
            if ckpt:                                  # resume point after a restart
                self.save(ckpt + ".npz"); json.dump(hist, open(ckpt + ".json", "w"))
            if ok >= 2: break
        return hist, ok >= 2

    def save(self, path):
        d = {f"open_{o}": v for o, v in self.open.items()}
        for k, s in self.S.items():
            for a, v in s.items(): d[f"{k}_{a}"] = v
        np.savez_compressed(path, **{k: v.astype(np.float32) for k, v in d.items()})

    def load(self, path):
        d = np.load(path)
        for o in self.open: self.open[o] = d[f"open_{o}"].astype(np.float64)
        for k, s in self.S.items():
            for a in s: s[a] = d[f"{k}_{a}"].astype(np.float64)
