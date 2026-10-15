import json
import re

import numpy as np
import pandas as pd

from common import RESULTS, arguments, connectome
from model import simulate

MN9 = 720575940660219265


def sugar_neurons(brain, shiu):
    notebook = json.loads((shiu / "example.ipynb").read_text())
    source = "\n".join("".join(cell["source"]) for cell in notebook["cells"])
    block = re.search(r"neu_sugar = \[(.*?)\]", source, re.S).group(1)
    return np.array([brain.index_of[int(r)] for r in re.findall(r"\d{18}", block) if int(r) in brain.index_of])


def main():
    args = arguments("Positive and negative controls for the Shiu and corrected whole-brain models.").parse_args()
    brain = connectome(args)
    sugar = sugar_neurons(brain, args.shiu)
    bitter = np.flatnonzero((brain.meta["cell_sub_class"] == "bitter").to_numpy())
    looming = np.flatnonzero(brain.meta["cell_type"].isin(["LC4", "LPLC2"]).to_numpy())
    giant_fiber = brain.neurons("DNp01")
    mn9 = brain.index_of[MN9]
    conditions = {
        "no input": np.array([], dtype=np.int64),
        "sugar": sugar,
        "sugar + bitter": np.concatenate([sugar, bitter]),
        "looming (LC4 + LPLC2)": looming,
    }
    rows = []
    for model, weights in (("Shiu et al. 2024", brain.shiu_weights()), ("corrected", brain.corrected_weights())):
        for name, stimulated in conditions.items():
            rate = simulate(weights, stimulated)
            rows.append({"model": model, "condition": name, "MN9_hz": rate[mn9],
                         "giant_fiber_hz": rate[giant_fiber].mean(), "active_neurons": int((rate > 0).sum())})
            print(rows[-1], flush=True)
    pd.DataFrame(rows).to_csv(RESULTS / "controls.csv", index=False)


if __name__ == "__main__":
    main()
