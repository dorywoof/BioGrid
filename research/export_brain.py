import argparse
import struct
from pathlib import Path

import numpy as np
import pandas as pd

from model import Connectome

GROUPS = {
    ("HRN_VP5", "left"): 1, ("HRN_VP5", "right"): 2,
    ("TRN_VP3a", "left"): 3, ("TRN_VP3a", "right"): 4,
    ("DNb05", "left"): 5, ("DNb05", "right"): 6,
    ("DNa01", "left"): 7, ("DNa01", "right"): 8,
    ("DNa02", "left"): 9, ("DNa02", "right"): 10,
}
RELAY = 11
RELAY_TAGS = ("VP5+", "VP3+", "VP2+")
CLASSES = {"sensory": 1, "descending": 2, "optic": 3, "visual_projection": 3, "visual_centrifugal": 3}


def main():
    parser = argparse.ArgumentParser(description="Write the corrected whole-brain connectome as a binary file for the game.")
    parser.add_argument("--shiu", type=Path, required=True)
    parser.add_argument("--annotations", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "biogrid" / "data" / "brain.bin")
    args = parser.parse_args()

    brain = Connectome(args.shiu, args.annotations)
    weights = brain.corrected_weights().tocsr()
    weights.sum_duplicates()
    weights.eliminate_zeros()

    meta = brain.meta
    positions = pd.read_csv(args.annotations, sep="\t", usecols=["root_id", "pos_x", "pos_y", "soma_x", "soma_y"],
                            low_memory=False).set_index("root_id").reindex(brain.ids)
    x = positions.soma_x.fillna(positions.pos_x).fillna(0).to_numpy(np.float32)
    y = positions.soma_y.fillna(positions.pos_y).fillna(0).to_numpy(np.float32)
    side = meta.side.map({"left": 0, "right": 1}).fillna(2).to_numpy(np.int8)
    klass = meta.super_class.map(CLASSES).fillna(0).to_numpy(np.uint8)
    group = np.zeros(brain.size, np.uint8)
    cell_type = meta.cell_type.fillna("").to_numpy()
    for (t, s), code in GROUPS.items():
        group[brain.neurons(t, s)] = code
    for i, t in enumerate(cell_type):
        if "l2PN" in t and any(tag in t for tag in RELAY_TAGS):
            group[i] = RELAY

    data = np.clip(np.rint(weights.data), -32767, 32767).astype(np.int16)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(b"FLYB")
        f.write(struct.pack("<III", 1, brain.size, len(data)))
        for array in (x, y, side, klass, group, weights.indptr.astype(np.uint32), weights.indices.astype(np.uint32), data):
            f.write(np.ascontiguousarray(array).tobytes())
    print(f"{brain.size} neurons, {len(data)} connections, {int(np.abs(data).sum())} synapses -> {args.out}")


if __name__ == "__main__":
    main()
