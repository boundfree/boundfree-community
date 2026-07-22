import csv
import math
from pathlib import Path
from typing import Any

from ..readers.registry import reader_for_path


def _segment_markers(markers: list[dict[str, Any]], t0: float, t1: float) -> list[tuple[float, str]]:
    selected: list[tuple[float, str]] = []
    for marker in markers:
        time = marker.get("time")
        label = marker.get("label")
        if (
            isinstance(time, (int, float))
            and not isinstance(time, bool)
            and math.isfinite(float(time))
            and isinstance(label, str)
            and t0 <= float(time) <= t1
        ):
            selected.append((float(time) - t0, label))
    return selected


def export_segment(
    source: Path,
    t0: float,
    t1: float,
    destination: Path,
    markers: list[dict[str, Any]],
) -> dict[str, Any]:
    reader = reader_for_path(source)
    if reader is None:
        raise ValueError(f"unsupported source format: {source.suffix.lower()}")
    meta = reader.metadata()
    if not (math.isfinite(t0) and math.isfinite(t1) and 0.0 <= t0 < t1 <= meta.duration):
        raise ValueError("invalid export range")

    import numpy as np

    slice_data = reader.get_slice(t0, t1)
    data = np.asarray(slice_data.data, dtype=np.float32)
    if data.ndim != 2 or data.shape[0] != meta.n_channels:
        raise ValueError("reader returned invalid slice shape")
    selected_markers = _segment_markers(markers, t0, t1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    suffix = destination.suffix.lower()

    if suffix == ".csv":
        with destination.open("w", encoding="utf-8", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["time_s", *meta.ch_names])
            for sample_index in range(data.shape[1]):
                writer.writerow([sample_index / meta.fs, *data[:, sample_index].tolist()])
        if selected_markers:
            marker_path = destination.with_name(destination.stem + "_markers.csv")
            with marker_path.open("w", encoding="utf-8", newline="") as output:
                writer = csv.writer(output)
                writer.writerow(["time_s", "label"])
                writer.writerows(selected_markers)
    elif suffix in (".h5", ".hdf5"):
        import h5py

        with h5py.File(destination, "w") as output:
            eeg = output.create_dataset("eeg", data=data, dtype="float32")
            eeg.attrs["fs"] = meta.fs
            eeg.attrs["ch_names"] = np.asarray(meta.ch_names, dtype=h5py.string_dtype("utf-8"))
            eeg.attrs["unit"] = "uV"
            if selected_markers:
                events = output.require_group("events")
                events.create_dataset("time", data=np.asarray([value[0] for value in selected_markers], dtype=np.float64))
                events.create_dataset(
                    "label",
                    data=np.asarray([value[1] for value in selected_markers], dtype=h5py.string_dtype("utf-8")),
                )
    elif suffix == ".edf":
        import pyedflib

        writer = pyedflib.EdfWriter(str(destination), meta.n_channels)
        try:
            writer.set_number_of_annotation_signals(max(1, len(selected_markers)))
            writer.setSignalHeaders(
                [
                    {
                        "label": name[:16],
                        "dimension": "uV",
                        "sample_frequency": meta.fs,
                        "physical_min": -3200.0,
                        "physical_max": 3200.0,
                        "digital_min": -32768,
                        "digital_max": 32767,
                        "prefilter": "",
                        "transducer": "",
                    }
                    for name in meta.ch_names
                ]
            )
            writer.writeSamples([data[index] for index in range(meta.n_channels)])
            for time, label in selected_markers:
                writer.writeAnnotation(time, -1, label)
        finally:
            writer.close()
    else:
        raise ValueError(f"unsupported export format: {suffix}")

    return {
        "path": str(destination),
        "n_channels": int(data.shape[0]),
        "n_samples": int(data.shape[1]),
        "marker_count": len(selected_markers),
    }
