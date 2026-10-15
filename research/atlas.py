from multiprocessing import Pool

import numpy as np
import pandas as pd

from common import RESULTS, arguments, connectome
from model import simulate

STATE = {}


def setup(brain):
    STATE["brain"] = brain
    STATE["weights"] = brain.corrected_weights()


def paired_types(brain, super_class):
    meta = brain.meta[brain.meta["super_class"] == super_class]
    counts = meta.groupby(["cell_type", "side"]).size().unstack(fill_value=0)
    return [t for t in counts.index if counts.loc[t].get("left", 0) >= 1 and counts.loc[t].get("right", 0) >= 1]


def run(task):
    sense, side, descending = task
    brain = STATE["brain"]
    rate = simulate(STATE["weights"], brain.neurons(sense, side))
    rows = []
    for dn in descending:
        rows.append({"sense": sense, "stimulated": side, "dn": dn,
                     "dn_left_hz": rate[brain.neurons(dn, "left")].mean(),
                     "dn_right_hz": rate[brain.neurons(dn, "right")].mean(),
                     "active_neurons": int((rate > 0).sum())})
    return rows


def main():
    args = arguments("Stimulate every paired sensory type on each side and record all paired descending neurons.").parse_args()
    brain = connectome(args)
    senses = [t for t in paired_types(brain, "sensory") if not t.startswith(("R1-6", "R7", "R8"))]
    senses = [t for t in senses if len(brain.neurons(t, "left")) >= 2 and len(brain.neurons(t, "right")) >= 2]
    descending = paired_types(brain, "descending")
    tasks = [(s, side, descending) for s in senses for side in ("left", "right")]
    print(f"{len(senses)} sensory types, {len(descending)} descending types, {len(tasks)} simulations", flush=True)
    setup(brain)
    with Pool(args.workers) as pool:
        rows = [row for chunk in pool.imap_unordered(run, tasks) for row in chunk]
    pd.DataFrame(rows).to_csv(RESULTS / "atlas_responses.csv", index=False)
    summarize(pd.DataFrame(rows))


def summarize(responses, threshold=10.0):
    wide = responses.pivot_table(index=["sense", "dn"], columns="stimulated", values=["dn_left_hz", "dn_right_hz"])
    a = wide[("dn_left_hz", "left")] - wide[("dn_right_hz", "left")]
    b = wide[("dn_right_hz", "right")] - wide[("dn_left_hz", "right")]
    mirrored = (np.sign(a) == np.sign(b)) & (np.minimum(a.abs(), b.abs()) >= threshold)
    lateral = pd.DataFrame({"direction": np.where(a > 0, "ipsilateral", "contralateral"),
                            "strength_hz": np.minimum(a.abs(), b.abs())})[mirrored].reset_index()
    activity = responses.groupby("sense").active_neurons.max()
    lateral["max_active_neurons"] = lateral.sense.map(activity)
    lateral.to_csv(RESULTS / "atlas_lateralized.csv", index=False)
    runaway = activity[activity > 2000]
    pd.Series(runaway.index, name="sense").to_csv(RESULTS / "atlas_runaway.csv", index=False)
    print(f"{lateral.sense.nunique()} senses mirror-lateralize at least one descending type; {len(runaway)} run away")
    print(lateral[lateral.dn.isin(["DNa01", "DNa02", "DNb05"])].to_string(index=False))


if __name__ == "__main__":
    main()
