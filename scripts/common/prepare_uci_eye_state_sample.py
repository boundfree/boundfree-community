#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import h5py
import numpy as np


def load_arff(path: Path) -> tuple[list[str], np.ndarray]:
    channel_names: list[str] = []
    rows: list[list[float]] = []
    in_data = False
    with path.open("r", encoding="utf-8") as source:
        for raw_line in source:
            line = raw_line.strip()
            if not line or line.startswith("%"):
                continue
            lower = line.lower()
            if lower.startswith("@attribute"):
                name = line.split(maxsplit=2)[1].strip("'\"")
                if name != "eyeDetection":
                    channel_names.append(name)
                continue
            if lower == "@data":
                in_data = True
                continue
            if in_data:
                values = next(csv.reader([line]))
                rows.append([float(value) for value in values[: len(channel_names)]])

    if not channel_names or not rows:
        raise ValueError(f"no EEG data found in {path}")
    return channel_names, np.asarray(rows, dtype=np.float32)


def write_csv(path: Path, channel_names: list[str], samples: np.ndarray, fs: float) -> None:
    with path.open("w", encoding="utf-8", newline="") as destination:
        writer = csv.writer(destination)
        writer.writerow(["time_s", *channel_names])
        for index, row in enumerate(samples):
            writer.writerow([f"{index / fs:.6f}", *(f"{float(value):.6f}" for value in row)])


def write_hdf5(path: Path, channel_names: list[str], samples: np.ndarray, fs: float) -> None:
    with h5py.File(path, "w") as destination:
        eeg = destination.create_dataset("eeg", data=samples.T, dtype="float32", compression="gzip")
        eeg.attrs["fs"] = fs
        eeg.attrs["ch_names"] = np.asarray(channel_names, dtype=h5py.string_dtype("utf-8"))
        eeg.attrs["unit"] = "uV"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("arff", type=Path)
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--hdf5", type=Path, required=True)
    parser.add_argument("--fs", type=float, default=128.0)
    args = parser.parse_args()

    channel_names, samples = load_arff(args.arff)
    centered = samples - samples.mean(axis=0, keepdims=True)
    write_csv(args.csv, channel_names, centered, args.fs)
    write_hdf5(args.hdf5, channel_names, centered, args.fs)
    print(
        f"prepared channels={len(channel_names)} samples={len(centered)} fs={args.fs:g} "
        f"csv={args.csv} hdf5={args.hdf5}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
