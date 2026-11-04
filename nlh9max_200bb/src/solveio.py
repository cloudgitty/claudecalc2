"""Reader for postflop solve files (<spot>/<idx>_<flop>.bin, written by src/tools/postflop.c)."""
import json

import numpy as np

ACT_NAMES = {1: "fold", 2: "check", 3: "call", 4: "bet", 5: "raise", 6: "allin"}


def read_solve(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:6] == b"NLHPF1", path
    off = 8
    nn = int(np.frombuffer(data, "<i4", 1, off)[0])
    off += 4
    nodes = []
    for _ in range(nn):
        player, nact = np.frombuffer(data, "<i4", 2, off)
        off += 8
        if nact == 0:
            nodes.append(None)
            continue
        acts = np.frombuffer(data, "<i4", nact, off).tolist()
        off += 4 * nact
        amts = np.frombuffer(data, "<f4", nact, off).tolist()
        off += 4 * nact
        strat = np.frombuffer(data, "<f4", nact * 1326, off).reshape(nact, 1326)
        off += 4 * nact * 1326
        nodes.append(dict(player=int(player), actions=acts, amounts=amts, strat=strat))
    ev = np.frombuffer(data, "<f4", 2 * 1326, off).reshape(2, 1326)
    off += 4 * 2 * 1326
    br = np.frombuffer(data, "<f4", 2 * 1326, off).reshape(2, 1326)
    off += 4 * 2 * 1326
    ranges = np.frombuffer(data, "<f4", 2 * 1326, off).reshape(2, 1326)
    off += 4 * 2 * 1326
    assert off == len(data), (off, len(data))
    meta = json.load(open(path[:-4] + ".json"))
    return dict(nodes=nodes, ev=ev, br=br, ranges=ranges, meta=meta)
