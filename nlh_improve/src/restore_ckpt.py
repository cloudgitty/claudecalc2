"""Restore work2/<step>/ from the durable checkpoint checkpoints/<step>/ on a fresh container.

usage (from a build folder): python3 ../nlh_improve/src/restore_ckpt.py <step> [<step> ...]

Milestones hold the preflop strategy and EV as gzipped float16 (NLH_IMPROVEMENT_PLAN.md section 3):
they are widened back to float32 and every (node, hand) strategy is renormalised to sum to 1
(float16 rounding leaves sums off by up to ~1e-3). Round checkpoints hold only the summary, log
and R fit; those are copied as they are. Existing files in work2/<step>/ are never overwritten."""
import gzip
import json
import os
import shutil
import sys

import numpy as np


def restore(step):
    src, dst = os.path.join("checkpoints", step), os.path.join("work2", step)
    os.makedirs(dst, exist_ok=True)
    tree = json.load(open(os.environ.get("NLH_TREE", "work2/tree.json")))["nodes"]
    for f in sorted(os.listdir(src)):
        out = os.path.join(dst, f[:-3] if f.endswith(".gz") else f)
        if f.endswith(".f16.gz"):
            out = os.path.join(dst, f[:-7])
        if os.path.exists(out):
            continue
        p = os.path.join(src, f)
        if f.endswith(".f16.gz"):
            a = np.frombuffer(gzip.open(p).read(), dtype=np.float16).astype(np.float32)
            if f.startswith("pre.strat"):
                off = 0
                for n in tree:
                    if n["kind"] == "dec":
                        k = len(n["actions"])
                        blk = a[off:off + k * 169].reshape(k, 169)
                        blk /= np.maximum(blk.sum(0, keepdims=True), 1e-30)
                        off += k * 169
                assert off == a.size, (off, a.size)
            a.tofile(out)
        elif f.endswith(".gz"):
            with gzip.open(p) as g, open(out, "wb") as o:
                o.write(g.read())
        else:
            shutil.copy(p, out)
    print(f"restored {dst}: {', '.join(sorted(os.listdir(dst)))}")


if __name__ == "__main__":
    for s in sys.argv[1:]:
        restore(s)
