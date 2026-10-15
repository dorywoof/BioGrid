from pathlib import Path

import numpy as np
import pandas as pd
import scipy.sparse as sp

FAST_SIGN = {"acetylcholine": 1.0, "gaba": -1.0, "glutamate": -1.0, "histamine": -1.0}
MODULATORY = {"dopamine", "serotonin", "octopamine"}
TRANSMITTERS = set(FAST_SIGN) | MODULATORY
ANNOTATION_COLUMNS = ["root_id", "cell_type", "cell_sub_class", "super_class", "side", "top_nt", "known_nt"]


def first_transmitter(text):
    if not isinstance(text, str):
        return None
    for token in text.replace(";", ",").split(","):
        token = token.strip()
        if token in TRANSMITTERS:
            return token
    return None


class Connectome:
    def __init__(self, shiu, annotations):
        shiu = Path(shiu)
        completeness = pd.read_csv(shiu / "Completeness_783.csv", index_col=0)
        edges = pd.read_parquet(
            shiu / "Connectivity_783.parquet",
            columns=["Presynaptic_Index", "Postsynaptic_Index", "Connectivity", "Excitatory x Connectivity"],
        )
        self.ids = completeness.index.to_numpy()
        self.size = len(self.ids)
        self.pre = edges["Presynaptic_Index"].to_numpy()
        self.post = edges["Postsynaptic_Index"].to_numpy()
        self.count = edges["Connectivity"].to_numpy(np.float32)
        self.shiu_signed = edges["Excitatory x Connectivity"].to_numpy(np.float32)
        meta = pd.read_csv(annotations, sep="\t", usecols=ANNOTATION_COLUMNS, low_memory=False)
        self.meta = meta.set_index("root_id").reindex(self.ids)
        self.index_of = {root: i for i, root in enumerate(self.ids)}

    def neurons(self, cell_type, side=None):
        mask = self.meta["cell_type"] == cell_type
        if side is not None:
            mask &= self.meta["side"] == side
        return np.flatnonzero(mask.to_numpy())

    def matrix(self, signed, keep=None):
        pre, post, data = self.pre, self.post, signed
        if keep is not None:
            pre, post, data = pre[keep], post[keep], data[keep]
        return sp.csr_matrix((data, (pre, post)), shape=(self.size, self.size))

    def shiu_weights(self):
        return self.matrix(self.shiu_signed)

    def transmitters(self):
        known = self.meta["known_nt"].map(first_transmitter)
        return known.where(known.notna(), self.meta["top_nt"]).to_numpy()

    def corrected_weights(self):
        sign = np.array([0.0 if nt in MODULATORY else FAST_SIGN.get(nt, 1.0) for nt in self.transmitters()], np.float32)
        sensory = (self.meta["super_class"] == "sensory").to_numpy()
        kenyon = self.meta["cell_type"].fillna("").str.startswith("KC").to_numpy()
        keep = ~sensory[self.post] & ~kenyon[self.pre] & ~kenyon[self.post] & (sign[self.pre] != 0)
        return self.matrix(self.count * sign[self.pre], keep)


def simulate(weights, stimulated, rate_hz=100.0, duration_ms=500.0, dt=0.1, seed=0, muted=None):
    rng = np.random.default_rng(seed)
    n = weights.shape[0]
    v_rest, v_threshold, tau_membrane, tau_synapse, refractory = -52.0, -45.0, 20.0, 5.0, 2.2
    synapse_mv, poisson_mv = 0.275, 0.275 * 250
    delay = int(round(1.8 / dt))
    stimulated = np.asarray(stimulated, dtype=np.int64)
    v = np.full(n, v_rest, np.float32)
    g = np.zeros(n, np.float32)
    blocked = np.zeros(n, np.float32)
    pending = [np.zeros(n, np.float32) for _ in range(delay)]
    spikes = np.zeros(n, np.int32)
    p = rate_hz * dt / 1000.0
    for step in range(int(duration_ms / dt)):
        slot = step % delay
        g += pending[slot]
        pending[slot] = np.zeros(n, np.float32)
        if len(stimulated):
            g[stimulated[rng.random(len(stimulated)) < p]] += poisson_mv
        free = blocked <= 0
        v[free] += (v_rest - v[free] + g[free]) * (dt / tau_membrane)
        g -= g * (dt / tau_synapse)
        blocked -= dt
        fired = np.flatnonzero(v > v_threshold)
        if muted is not None and len(fired):
            v[fired[muted[fired]]] = v_rest
            fired = fired[~muted[fired]]
        if len(fired):
            v[fired] = v_rest
            g[fired] = 0
            blocked[fired] = refractory
            spikes[fired] += 1
            pending[slot] += np.asarray(weights[fired].sum(axis=0)).ravel() * synapse_mv
    return spikes / (duration_ms / 1000.0)


def mask(size, indices):
    out = np.zeros(size, bool)
    out[np.asarray(indices, dtype=np.int64)] = True
    return out
