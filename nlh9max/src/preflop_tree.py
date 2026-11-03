"""Preflop game tree for the 8-handed table (stage 1).

usage: python3 src/preflop_tree.py [config.json] [out.json]

Builds every action sequence allowed by config.json (see REPORT.md, "Preflop tree")
and writes it as flat arrays for the C solver (work/tree.json) with a summary.

Players are seats 0..7 = UTG, UTG1, LJ, HJ, CO, BTN, SB, BB. Amounts are in bb and
are the total a player has put in this hand ("raise to").

Rules (defaults chosen where the task is silent are marked DEFAULT):
- Unopened pot: fold or open to 3bb (SB 3.5bb). SB may complete to 1bb when folded to.
  BB facing a limp: check (flop, limped pot) or raise to 3bb (DEFAULT size; the raise
  is then treated as an open). Folded to BB: BB wins the blinds.
- Facing an open: fold, call, 3-bet to open x 3 (in position) or x 5 (out of position);
  with at least one caller the raise is a squeeze: x 4 / x 6.
  "In position" for a 3-bet: the 3-bettor acts after the opener postflop (SB, BB never).
- Facing a 3-bet: fold, call, 4-bet to 3-bet x 2.5 if the 4-bettor is in position
  against the 3-bettor, x 2.8 if out of position; all-in if that reaches 40% of the
  effective stack.
- Facing a 4-bet: fold, call, all-in (5-bet). Facing an all-in: fold, call.
- Any raise reaching the stack is all-in. No more than max_flop_players players may
  see the flop: a call that would make more is not offered (DEFAULT).
- Terminals: everybody folded to one player (no rake), a flop with 2 or 3 players,
  or an all-in showdown (all chips in before the flop; raked since a flop is dealt).
"""
import json
import sys

SEATS = ["UTG", "UTG1", "LJ", "HJ", "CO", "BTN", "SB", "BB"]
SB, BB = 6, 7


def postflop_order(p):
    """Postflop acting order: SB first, BB second, then UTG..BTN."""
    return {SB: 0, BB: 1}.get(p, p + 2)


def in_position(a, b):
    """True if seat a acts after seat b postflop."""
    return postflop_order(a) > postflop_order(b)


class Builder:
    def __init__(self, cfg):
        self.cfg = cfg
        self.stack = float(cfg["stack_bb"])
        self.nodes = []

    def new(self, **kw):
        self.nodes.append(kw)
        return len(self.nodes) - 1

    def build(self):
        inv = [0.0] * 8
        inv[SB], inv[BB] = 0.5, 1.0
        st = dict(inv=inv, folded=[False] * 8, level=0, to=1.0, last_raise_to=1.0,
                  aggressor=None, opener=None, callers=[], acted=[False] * 8,
                  path=[], limped=False, allin=False, vol=[])
        return self.node(st, 0)

    def flop_count(self, st):
        return sum(1 for p in range(8) if not st["folded"][p] and (st["acted"][p] or p in (SB, BB)) and st["inv"][p] >= st["to"] - 1e-9)

    def next_to_act(self, st, start):
        """Next seat after start (exclusive... inclusive of start) that still has to act."""
        for k in range(8):
            p = (start + k) % 8
            if st["folded"][p]:
                continue
            if not st["acted"][p] or st["inv"][p] < st["to"] - 1e-9:
                if st["inv"][p] >= self.stack - 1e-9:
                    continue  # all-in already
                return p
        return None

    def terminal(self, st):
        live = [p for p in range(8) if not st["folded"][p]]
        pot = sum(st["inv"])
        if len(live) == 1:
            return self.new(kind="fold", winner=live[0], inv=list(st["inv"]), pot=pot, path=st["path"])
        allin = any(st["inv"][p] >= self.stack - 1e-9 for p in live)
        kind = "allin" if allin else "flop"
        ptype = {0: "limp", 1: "srp", 2: "3bp", 3: "4bp"}.get(st["level"], "5bp")
        return self.new(kind=kind, players=live, inv=list(st["inv"]), pot=pot, pot_type=ptype,
                        aggressor=st["aggressor"], path=st["path"])

    def node(self, st, start):
        p = self.next_to_act(st, start)
        if p is None:
            return self.terminal(st)
        live = [q for q in range(8) if not st["folded"][q]]
        if len(live) == 1:
            return self.terminal(st)
        acts = []
        cfg = self.cfg
        to, level = st["to"], st["level"]
        # fold
        if st["inv"][p] < to - 1e-9:
            acts.append(("f", None))
        # check / call / complete
        if st["inv"][p] >= to - 1e-9:
            acts.append(("k", to))                      # BB check after limp
        else:
            if level == 0 and p == SB and cfg.get("sb_limp", True):
                acts.append(("c", to))                  # complete
            elif level >= 1:
                n_in = sum(1 for q in live if q != p and (st["acted"][q] or q in (SB, BB)) and st["inv"][q] >= to - 1e-9)
                # players still to act who have money in already (blinds) are not counted until they call
                committed = [q for q in live if q != p and st["inv"][q] >= to - 1e-9]
                # DEFAULT: 3-way pots only when single-raised. Facing a 3-bet or more, a call
                # is offered only when the raiser is the only player matched so far; players
                # still to act after a call can then only fold or raise (the pot stays heads-up)
                cap = cfg["max_flop_players"] if level == 1 else 2
                if len(committed) + 1 <= cap:
                    acts.append(("c", to))
        # DEFAULT pruning: facing a 3-bet or more, a player who has not yet put chips in
        # voluntarily (cold; the blinds count as cold) may only fold
        if level >= 2 and p != st["opener"] and p not in st["vol"]:
            acts = [("f", None)]
        # raises
        r = self.raise_size(st, p) if len(acts) > 1 or level < 2 or p == st["opener"] or p in st["vol"] else None
        if r is not None:
            acts.append(("a" if r >= self.stack - 1e-9 else "r", r))
        kids = []
        nid = self.new(kind="dec", player=p, actions=[a for a, _ in acts], amounts=[x for _, x in acts],
                       children=None, path=st["path"], level=level, inv=list(st["inv"]))
        for a, amt in acts:
            s2 = json.loads(json.dumps(st))
            s2["path"] = st["path"] + [f"{SEATS[p]}:{a}" + (f"{amt:g}" if a in "ra" and False else "")]
            s2["acted"][p] = True
            if a not in ("f", "k"):
                s2["vol"] = st["vol"] + [p]
            if a == "f":
                s2["folded"][p] = True
            elif a in ("c", "k"):
                s2["inv"][p] = min(amt, self.stack)
                if level == 0 and p == SB:
                    s2["limped"] = True
                elif level >= 1:
                    s2["callers"] = st["callers"] + [p]
            else:
                s2["inv"][p] = min(amt, self.stack)
                s2["to"] = s2["inv"][p]
                s2["level"] = level + 1
                s2["aggressor"] = p
                if level == 0 or (level == 0 and st["limped"]):
                    s2["opener"] = p
                if st["limped"] and level == 0:
                    s2["opener"] = p
                s2["callers"] = []
                s2["path"][-1] = f"{SEATS[p]}:{a}{amt:g}"
            kids.append(self.node(s2, (p + 1) % 8))
        self.nodes[nid]["children"] = kids
        return nid

    def raise_size(self, st, p):
        cfg, level, to = self.cfg, st["level"], st["to"]
        S = self.stack
        if level == 0:
            if st["limped"]:
                return cfg["bb_iso_vs_limp_bb"] if p == BB else None
            return cfg["sb_open_bb"] if p == SB else cfg["open_bb"]
        if level == 1:
            opener = st["opener"]
            ip = in_position(p, opener)
            mult = cfg["threebet_ip_x"] if ip else cfg["threebet_oop_x"]
            if st["callers"]:
                mult += cfg["squeeze_extra_x"]
            return min(to * mult, S)
        if level == 2:
            ip = in_position(p, st["aggressor"])
            size = to * (cfg["fourbet_ip_x"] if ip else cfg["fourbet_oop_x"])
            if size >= cfg["fourbet_allin_frac"] * S:
                return S
            return min(size, S)
        if level == 3:
            return S if to < S - 1e-9 else None
        return None


def main():
    cfgp = sys.argv[1] if len(sys.argv) > 1 else "config.json"
    outp = sys.argv[2] if len(sys.argv) > 2 else "work/tree.json"
    cfg = json.load(open(cfgp))
    b = Builder(cfg)
    b.build()
    nodes = b.nodes
    kinds = {}
    for n in nodes:
        kinds[n["kind"]] = kinds.get(n["kind"], 0) + 1
    flops = {}
    for n in nodes:
        if n["kind"] in ("flop", "allin"):
            k = (n["kind"], n["pot_type"], len(n["players"]))
            flops[k] = flops.get(k, 0) + 1
    print(f"{len(nodes)} nodes: {kinds}")
    for k in sorted(flops):
        print("  ", k, flops[k])
    json.dump({"seats": SEATS, "stack": b.stack, "nodes": nodes}, open(outp, "w"))
    write_text(nodes, outp.rsplit(".", 1)[0] + ".txt")


PTYPES = {"limp": 0, "srp": 1, "3bp": 2, "4bp": 3, "5bp": 4}


def write_text(nodes, path):
    """Flat text tree for the C solver, one node per line (see src/preflop.c)."""
    with open(path, "w") as f:
        f.write(f"{len(nodes)}\n")
        for i, n in enumerate(nodes):
            inv = " ".join(f"{x:g}" for x in n["inv"])
            if n["kind"] == "dec":
                acts = " ".join(f"{a} {c}" for a, c in zip(n["actions"], n["children"]))
                f.write(f"{i} D {n['player']} {len(n['actions'])} {acts} {inv}\n")
            elif n["kind"] == "fold":
                f.write(f"{i} F {n['winner']} {inv}\n")
            else:
                k = "P" if n["kind"] == "flop" else "A"
                pl = " ".join(str(x) for x in n["players"])
                ag = -1 if n["aggressor"] is None else n["aggressor"]
                f.write(f"{i} {k} {PTYPES[n['pot_type']]} {ag} {len(n['players'])} {pl} {inv}\n")


if __name__ == "__main__":
    main()
