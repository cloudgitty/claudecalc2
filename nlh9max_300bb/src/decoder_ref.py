"""Independent reference decoder, written from the output format description alone.

It does not import the packer or the solver readers: it parses `preflop.json`,
`preflop_ev.bin` and `postflop/<spot>.bin.gz` the way a port would, so that
`src/validate.py` can compare it against the packer's own view of the data.

API
  load_preflop(out_dir) -> (records, ev)   ev[i] = float32 array(169) for records[i]
  hands_of(record)      -> {hand label: {action code: percent}}
  load_postflop(path)   -> {"header": dict, "flops": {index: {node name: array(1326, nact)}},
                            "reach": {index: array(2, 1326)}}
"""
import gzip
import json
import os
import struct

import numpy as np

RANKS = "AKQJT98765432"


def labels():
    out = []
    for i in range(13):
        for j in range(13):
            hi, lo = RANKS[min(i, j)], RANKS[max(i, j)]
            out.append(hi + hi if i == j else hi + lo + ("s" if i < j else "o"))
    return out


LABELS = labels()


def load_preflop(out_dir):
    doc = json.load(open(os.path.join(out_dir, "preflop.json")))
    recs = doc["records"]
    raw = np.fromfile(os.path.join(out_dir, "preflop_ev.bin"), dtype="<f2")
    assert raw.size == 169 * len(recs), (raw.size, len(recs))
    ev = raw.reshape(len(recs), 169).astype(np.float32)
    return recs, ev


def hands_of(record):
    out = {}
    if not record["hands"]:
        return out
    for item in record["hands"].split(" "):
        label, rest = item.split("=")
        d = {}
        for part in rest.split(","):
            i = 1
            while i < len(part) and not part[i].isdigit():
                i += 1
            d[part[:i]] = int(part[i:])
        out[label] = d
    return out


def load_postflop(path):
    raw = gzip.decompress(open(path, "rb").read())
    assert raw[:8] == b"NLHPOST1", path
    hlen = struct.unpack_from("<I", raw, 8)[0]
    header = json.loads(raw[12:12 + hlen])
    body = raw[12 + hlen:]
    flops, reach = {}, {}
    for f in header["flops"]:
        off = f["offset"]
        r = np.frombuffer(body, np.uint8, 2 * 1326, off).reshape(2, 1326)
        off += 2 * 1326
        reach[f["index"]] = r.astype(np.float32) / 255.0 * f["reach_scale"]
        nodes = {}
        for nd in f["nodes"]:
            na = len(nd["actions"])
            if na == 0:
                nodes[nd["name"]] = None
                continue
            q = np.frombuffer(body, np.uint8, 1326 * na, off).reshape(1326, na)
            off += 1326 * na
            nodes[nd["name"]] = q
        flops[f["index"]] = nodes
    return {"header": header, "flops": flops, "reach": reach}
