"""Representative flops (stage 1): cluster the 1,755 strategically different flops.

usage: python3 src/flops.py [count=100] [out=flops.json]

Every flop of 22,100 is mapped to its suit-isomorphism class (1,755 classes). Each class
gets features: high, middle and low rank, paired / trips, suit pattern (rainbow, two-tone,
monotone), connectivity (gaps between distinct ranks, straight possible), and a
range-advantage proxy (broadway count and whether the high card is an A or K, which favour
the preflop raiser). Clusters are formed inside each texture stratum (suit pattern x
paired), with the number of clusters per stratum proportional to its share of the 22,100
flops (at least one), by k-medoids on standardised features. The medoid flop represents the
cluster; its weight is the cluster's share of all flops. Deterministic (fixed seed).
"""
import json
import random
import sys
from itertools import combinations

RANKS = "23456789TJQKA"
SUITS = "cdhs"


def canon(flop):
    """Canonical suit-isomorphic form: rank-sorted cards with suits relabelled in order."""
    best = None
    import itertools
    for perm in itertools.permutations(range(4)):
        cards = sorted(((c >> 2), perm[c & 3]) for c in flop)
        cards.sort(key=lambda x: (-x[0], x[1]))
        key = tuple(cards)
        if best is None or key < best:
            best = key
    return best


def features(key):
    ranks = sorted((r for r, _ in key), reverse=True)
    suits = [s for _, s in key]
    ns = len(set(suits))
    distinct = sorted(set(ranks), reverse=True)
    paired = len(distinct) < 3
    trips = len(distinct) == 1
    hi, mid, lo = ranks
    # connectivity: span of distinct ranks (ace also low), straight draw potential
    rs = set(distinct)
    if 12 in rs:
        rs_low = rs | {-1}
    else:
        rs_low = rs
    best_span = min(max(w) - min(w) for w in [sorted(rs)] + ([sorted((rs - {12}) | {-1})] if 12 in rs else []))
    straight_possible = len(distinct) == 3 and best_span <= 4
    broadway = sum(1 for r in ranks if r >= 8)
    return {
        "hi": hi, "mid": mid, "lo": lo,
        "paired": int(paired), "trips": int(trips),
        "suits": {1: "monotone", 2: "two-tone", 3: "rainbow"}[ns],
        "span": best_span if len(distinct) == 3 else 13,
        "straight": int(straight_possible),
        "broadway": broadway,
        "ace_king_high": int(hi >= 11),
    }


def vec(f):
    return [f["hi"] / 12, f["mid"] / 12, f["lo"] / 12, min(f["span"], 13) / 13, f["straight"] * 0.6,
            f["broadway"] / 3 * 0.6, f["ace_king_high"] * 0.6]


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b))


def kmedoids(points, weights, k, rng, iters=50):
    n = len(points)
    if k >= n:
        return list(range(n)), list(range(n))
    # k-means++ style init (deterministic seed)
    meds = [max(range(n), key=lambda i: weights[i])]
    while len(meds) < k:
        d = [min(dist(points[i], points[m]) for m in meds) * weights[i] for i in range(n)]
        tot = sum(d)
        x = rng.random() * tot
        acc = 0
        for i in range(n):
            acc += d[i]
            if acc >= x:
                meds.append(i)
                break
    for _ in range(iters):
        assign = [min(range(k), key=lambda j: dist(points[i], points[meds[j]])) for i in range(n)]
        new = []
        for j in range(k):
            mem = [i for i in range(n) if assign[i] == j]
            if not mem:
                new.append(meds[j])
                continue
            best = min(mem, key=lambda c: sum(weights[i] * dist(points[i], points[c]) for i in mem))
            new.append(best)
        if new == meds:
            break
        meds = new
    assign = [min(range(k), key=lambda j: dist(points[i], points[meds[j]])) for i in range(n)]
    return meds, assign


def card_str(r, s):
    return RANKS[r] + SUITS[s]


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    out = sys.argv[2] if len(sys.argv) > 2 else "flops.json"
    classes = {}
    for flop in combinations(range(52), 3):
        k = canon(flop)
        classes[k] = classes.get(k, 0) + 1
    assert len(classes) == 1755, len(classes)
    keys = sorted(classes)
    feats = {k: features(k) for k in keys}
    strata = {}
    for k in keys:
        f = feats[k]
        s = (f["suits"], "trips" if f["trips"] else "paired" if f["paired"] else "unpaired")
        strata.setdefault(s, []).append(k)
    total = sum(classes.values())
    # allocate clusters proportional to frequency (largest remainder), at least 1
    shares = {s: sum(classes[k] for k in ks) / total * count for s, ks in strata.items()}
    alloc = {s: max(1, int(v)) for s, v in shares.items()}
    while sum(alloc.values()) < count:
        s = max(shares, key=lambda s: shares[s] - alloc[s])
        alloc[s] += 1
    while sum(alloc.values()) > count:
        s = max((s for s in alloc if alloc[s] > 1), key=lambda s: alloc[s] - shares[s])
        alloc[s] -= 1
    rng = random.Random(20261103)
    result = []
    for s in sorted(strata):
        ks = strata[s]
        pts = [vec(feats[k]) for k in ks]
        w = [classes[k] for k in ks]
        meds, assign = kmedoids(pts, w, min(alloc[s], len(ks)), rng)
        for j, m in enumerate(meds):
            members = [ks[i] for i in range(len(ks)) if assign[i] == j]
            wt = sum(classes[k] for k in members)
            key = ks[m]
            result.append({
                "flop": "".join(card_str(r, sv) for r, sv in key),
                "weight": wt / total,
                "raw_flops": wt,
                "classes": len(members),
                "stratum": f"{s[0]} {s[1]}",
                "features": feats[key],
            })
    result.sort(key=lambda x: (-x["weight"], x["flop"]))
    for i, r in enumerate(result):
        r["index"] = i
    json.dump({"count": len(result), "total_flops": total, "method": __doc__.strip().splitlines()[0],
               "flops": result}, open(out, "w"), indent=1)
    print(f"{len(result)} flops, weights sum {sum(r['weight'] for r in result):.6f}")
    for s in sorted(alloc):
        print(f"  {s}: {alloc[s]} clusters ({shares[s]:.1f} by frequency), {len(strata[s])} classes")


if __name__ == "__main__":
    main()
