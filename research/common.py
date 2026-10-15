import argparse
from pathlib import Path

from model import Connectome

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"
FIGURES = HERE / "figures"


def arguments(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--shiu", type=Path, required=True, help="clone of github.com/philshiu/Drosophila_brain_model")
    parser.add_argument("--annotations", type=Path, required=True,
                        help="flywire_annotations/supplemental_files/Supplemental_file1_neuron_annotations.tsv")
    parser.add_argument("--workers", type=int, default=4)
    return parser


def connectome(args):
    return Connectome(args.shiu, args.annotations)
