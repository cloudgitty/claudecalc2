"""ctypes wrapper around engine.c (built on first import)."""
import ctypes, os, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SO = os.path.join(HERE, "libplo6.so")
SRC = os.path.join(HERE, "engine.c")

if not os.path.exists(SO) or os.path.getmtime(SO) < os.path.getmtime(SRC):
    tmp = SO + f".{os.getpid()}.tmp"   # build aside, then rename (running jobs keep the old inode)
    subprocess.check_call(["gcc", "-O3", "-march=native", "-fopenmp", "-shared", "-fPIC", SRC, "-o", tmp])
    os.replace(tmp, SO)

lib = ctypes.CDLL(SO)
lib.init_engine()

def _p(a):
    return a.ctypes.data_as(ctypes.c_void_p)

for name, res in [("eval5_cards", ctypes.c_uint32), ("omaha_eval", ctypes.c_uint32),
                  ("class_code_cards", ctypes.c_int64), ("engine_threads", ctypes.c_int)]:
    getattr(lib, name).restype = res


lib.exact_hvh.restype = ctypes.c_double
lib.mc_hvh.restype = ctypes.c_double


def exact_hvh(h, v):
    return lib.exact_hvh((ctypes.c_int * 6)(*h), (ctypes.c_int * 6)(*v))


def mc_hvh(h, v, n, seed=1):
    return lib.mc_hvh((ctypes.c_int * 6)(*h), (ctypes.c_int * 6)(*v), ctypes.c_int(n), ctypes.c_uint64(seed))


def threads():
    return lib.engine_threads()


def enumerate_all():
    out = np.empty(20358520, dtype=np.int64)
    lib.enumerate_all(_p(out))
    return out


def class_codes(cards):
    cards = np.ascontiguousarray(cards, dtype=np.int32).reshape(-1, 6)
    out = np.empty(len(cards), dtype=np.int64)
    lib.class_codes_batch(ctypes.c_int64(len(cards)), _p(cards), _p(out))
    return out


def omaha_eval(h, b):
    h = (ctypes.c_int * 6)(*h); b = (ctypes.c_int * 5)(*b)
    return lib.omaha_eval(h, b)


def eval5(c):
    return lib.eval5_cards((ctypes.c_int * 5)(*c))


def sample_reps(codes, seed):
    codes = np.ascontiguousarray(codes, dtype=np.int64)
    out = np.empty((len(codes), 6), dtype=np.uint8)
    lib.sample_reps(ctypes.c_int64(len(codes)), _p(codes), ctypes.c_uint64(seed), _p(out))
    return out


class Pools:
    """Concatenated villain pools (each an (n,6) uint8 array of concrete hands)."""
    def __init__(self, pools):
        self.arr = np.ascontiguousarray(np.concatenate(pools), dtype=np.uint8)
        lens = np.array([len(p) for p in pools], dtype=np.int64)
        self.len = lens
        self.off = np.concatenate([[0], np.cumsum(lens)[:-1]]).astype(np.int64)


def sim_tasks(codes, pidx, ntr, pools, seed):
    codes = np.ascontiguousarray(codes, dtype=np.int64)
    pidx = np.ascontiguousarray(pidx, dtype=np.int32)
    ntr = np.ascontiguousarray(ntr, dtype=np.int32)
    n = len(codes)
    outs = [np.zeros(n) for _ in range(5)]
    lib.sim_tasks(ctypes.c_int64(n), _p(codes), _p(pidx), _p(ntr), _p(pools.arr), _p(pools.off),
                  _p(pools.len), ctypes.c_uint64(seed), *[_p(o) for o in outs])
    return outs  # sum, sumsq, ws, ls, trials played


def sample_values(codes, pool, vals, m, seed):
    codes = np.ascontiguousarray(codes, dtype=np.int64)
    pool = np.ascontiguousarray(pool, dtype=np.uint8)
    vals = np.ascontiguousarray(vals, dtype=np.float32)
    nv = vals.shape[1]
    out = np.empty((len(codes), nv), dtype=np.float32)
    lib.sample_values(ctypes.c_int64(len(codes)), _p(codes), _p(pool), ctypes.c_int64(len(pool)),
                      _p(vals), ctypes.c_int(nv), ctypes.c_int(m), ctypes.c_uint64(seed), _p(out))
    return out


def sim_postflop_p(n, poolA, poolB, pot0, stack0, mq, K, m2, VAL, BLF, RAI, RCALL, seed):
    poolA = np.ascontiguousarray(poolA, dtype=np.uint8); poolB = np.ascontiguousarray(poolB, dtype=np.uint8)
    outs = [np.zeros(n) for _ in range(4)]
    d = ctypes.c_double
    lib.sim_postflop_p(ctypes.c_int64(n), _p(poolA), ctypes.c_int64(len(poolA)), _p(poolB),
                       ctypes.c_int64(len(poolB)), d(pot0), d(stack0), ctypes.c_int(mq), ctypes.c_int(K),
                       ctypes.c_int(m2), d(VAL), d(BLF), d(RAI), d(RCALL), ctypes.c_uint64(seed),
                       *[_p(o) for o in outs])
    return outs  # share_oop, eq_oop, share_ip, eq_ip


def sim_postflop(n, poolA, poolB, pot0, stack0, mq, bet_th, call_margin, raise_th, seed):
    poolA = np.ascontiguousarray(poolA, dtype=np.uint8); poolB = np.ascontiguousarray(poolB, dtype=np.uint8)
    outs = [np.zeros(n) for _ in range(4)]
    lib.sim_postflop(ctypes.c_int64(n), _p(poolA), ctypes.c_int64(len(poolA)), _p(poolB),
                     ctypes.c_int64(len(poolB)), ctypes.c_double(pot0), ctypes.c_double(stack0),
                     ctypes.c_int(mq), ctypes.c_double(bet_th), ctypes.c_double(call_margin),
                     ctypes.c_double(raise_th), ctypes.c_uint64(seed), *[_p(o) for o in outs])
    return outs  # share_oop, eq_oop, share_ip, eq_ip
