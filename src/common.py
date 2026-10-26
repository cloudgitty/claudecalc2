import os, time, json
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work")
os.makedirs(WORK, exist_ok=True)


def W(name):
    return os.path.join(WORK, name)


def R(name):
    return os.path.join(ROOT, name)


def progress(line):
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    with open(R("PROGRESS.md"), "a") as f:
        f.write(f"- {stamp} {line}\n")
    print(line, flush=True)


_cls = None


def classes():
    global _cls
    if _cls is None:
        d = np.load(W("classes.npz"))
        _cls = (d["codes"], d["combos"])
    return _cls


def decode(codes):
    """class codes -> ranks (n,6) desc order, suits (n,6) canonical."""
    codes = np.asarray(codes, dtype=np.int64)
    s = np.empty((len(codes), 6), dtype=np.int8)
    r = np.empty((len(codes), 6), dtype=np.int8)
    v = codes.copy()
    for i in range(5, -1, -1):
        s[:, i] = v & 3; v >>= 2
    for i in range(5, -1, -1):
        r[:, i] = v % 13; v //= 13
    return r, s
