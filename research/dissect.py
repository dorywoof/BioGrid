from multiprocessing import Pool

import numpy as np
import pandas as pd

from common import RESULTS, arguments, connectome
from model import mask, simulate

SENSES = {"humidity (VP5)": "HRN_VP5", "cooling (VP3a)": "TRN_VP3a", "heating (VP2)": "TRN_VP2"}
FAMILY_TAGS = ("VP5+", "VP3+", "VP2+")
STATE = {}


def steering(rate, brain):
    return rate[brain.neurons("DNb05", "left")].mean(), rate[brain.neurons("DNb05", "right")].mean()


def run(task):
    kind, sense, side, muted = task
    brain, weights = STATE["brain"], STATE["weights"]
    rate = simulate(weights, brain.neurons(SENSES[sense], side), duration_ms=400.0,
                    muted=None if muted is None else mask(brain.size, muted))
    left, right = steering(rate, brain)
    return {"kind": kind, "sense": sense, "stimulated": side, "muted": "" if muted is None else ";".join(map(str, muted)),
            "dnb05_left_hz": left, "dnb05_right_hz": right}


def main():
    parser = arguments("Single-neuron and group in-silico knockouts of the thermo-hygrosensory steering pathway.")
    parser.add_argument("--random-controls", type=int, default=10)
    args = parser.parse_args()
    brain = connectome(args)
    weights = brain.corrected_weights()
    STATE.update(brain=brain, weights=weights)

    cell_type = brain.meta["cell_type"].fillna("").to_numpy()
    family = np.array([i for i, t in enumerate(cell_type) if "l2PN" in t and any(tag in t for tag in FAMILY_TAGS)])
    targets = np.concatenate([brain.neurons("DNb05", "left"), brain.neurons("DNb05", "right")])
    pd.DataFrame({"index": family, "root_id": brain.ids[family], "cell_type": cell_type[family],
                  "side": brain.meta["side"].to_numpy()[family],
                  "synapses_onto_DNb05": np.asarray(abs(weights[family][:, targets]).sum(axis=1)).ravel()}
                 ).to_csv(RESULTS / "vp_projection_family.csv", index=False)

    super_class = brain.meta["super_class"].to_numpy()
    intact = {(s, side): simulate(weights, brain.neurons(SENSES[s], side), duration_ms=400.0)
              for s in SENSES for side in ("left", "right")}
    candidates = sorted({i for s in ("humidity (VP5)", "cooling (VP3a)") for i in np.flatnonzero(intact[(s, "left")] > 0)
                         if super_class[i] not in ("sensory", "descending")})
    rng = np.random.default_rng(0)
    tasks = [("intact", s, side, None) for s in SENSES for side in ("left", "right")]
    tasks += [("single", s, "left", [i]) for s in ("humidity (VP5)", "cooling (VP3a)") for i in candidates]
    tasks += [("family", s, side, list(family)) for s in SENSES for side in ("left", "right")]
    for s in SENSES:
        pool_ids = np.setdiff1d(np.flatnonzero((intact[(s, "left")] > 0) | (intact[(s, "right")] > 0)), family)
        pool_ids = pool_ids[np.isin(super_class[pool_ids], ["sensory", "descending"], invert=True)]
        for k in range(args.random_controls):
            muted = list(rng.choice(pool_ids, size=min(len(family), len(pool_ids)), replace=False))
            tasks += [(f"random {k}", s, side, muted) for side in ("left", "right")]
    print(f"family {len(family)} neurons, {len(candidates)} single-knockout candidates, {len(tasks)} simulations", flush=True)

    with Pool(args.workers) as pool:
        rows = list(pool.imap(run, tasks, chunksize=4))
    out = pd.DataFrame(rows)
    out["cell_type"] = [cell_type[int(m)] if k == "single" else "" for k, m in zip(out.kind, out.muted)]
    out.to_csv(RESULTS / "knockouts.csv", index=False)

    singles = out[out.kind == "single"].copy()
    base = out[(out.kind == "intact") & (out.stimulated == "left")].set_index("sense").dnb05_left_hz
    singles["drop"] = 1 - singles.dnb05_left_hz / singles.sense.map(base)
    print(singles.sort_values("drop", ascending=False).groupby("sense").head(8).to_string(index=False))


if __name__ == "__main__":
    main()
