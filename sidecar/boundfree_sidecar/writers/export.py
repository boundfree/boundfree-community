import csv
import math
import json
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
    return export_samples(data, meta.fs, meta.ch_names, t0, t1, destination, markers,
                          unit=meta.unit, channel_units=slice_data.channel_units, sample_times=slice_data.sample_times)


def export_samples(
    data: Any,
    fs: float,
    ch_names: list[str],
    t0: float,
    t1: float,
    destination: Path,
    markers: list[dict[str, Any]],
    *, unit="uV", channel_units=None, sample_times=None, provenance=None,
) -> dict[str, Any]:
    """Encode samples prepared by Native DSP; timestamps are source-relative."""
    import numpy as np

    data = np.asarray(data, dtype=np.float32)
    if data.ndim != 2 or not data.shape[1] or data.shape[0] != len(ch_names) or not ch_names:
        raise ValueError("invalid export sample shape")
    if not math.isfinite(fs) or fs <= 0 or np.isinf(data).any():
        raise ValueError("export samples and sampling rate must be finite")
    units = channel_units or [unit] * len(ch_names)
    if not isinstance(unit, str) or not unit or len(units) != len(ch_names) or any(not isinstance(u,str) or not u or len(u)>64 for u in units):
        raise ValueError("invalid signal units")
    times = None if sample_times is None else np.asarray(sample_times,dtype=np.float64)
    if times is not None and (times.shape != (data.shape[1],) or not np.isfinite(times).all() or np.any(np.diff(times)<0)):
        raise ValueError("invalid signal time axis")
    regular = times is None or np.allclose(np.diff(times),1/fs,rtol=1e-5,atol=1e-9)
    if destination.suffix.lower() not in (".h5",".hdf5") and (not regular or not np.isfinite(data).all()):
        raise ValueError("This selection contains gaps. Choose HDF5 to preserve its time axis and missing values.")
    selected_markers = _segment_markers(markers, t0, t1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    suffix = destination.suffix.lower()

    if suffix == ".csv":
        with destination.open("w", encoding="utf-8", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["time_s", *ch_names])
            for sample_index in range(data.shape[1]):
                writer.writerow([sample_index / fs, *data[:, sample_index].tolist()])
        if selected_markers:
            marker_path = destination.with_name(destination.stem + "_markers.csv")
            with marker_path.open("w", encoding="utf-8", newline="") as output:
                writer = csv.writer(output)
                writer.writerow(["time_s", "label"])
                writer.writerows(selected_markers)
        else:
            # An overwritten export must not retain annotations from an older file.
            destination.with_name(destination.stem + "_markers.csv").unlink(missing_ok=True)
    elif suffix in (".h5", ".hdf5"):
        import h5py

        with h5py.File(destination, "w") as output:
            eeg = output.create_dataset("eeg", data=data, dtype="float32")
            eeg.attrs["fs"] = fs
            eeg.attrs["ch_names"] = np.asarray(ch_names, dtype=h5py.string_dtype("utf-8"))
            eeg.attrs["unit"] = unit
            if channel_units: eeg.attrs["channel_units"] = np.asarray(units,dtype=h5py.string_dtype("utf-8"))
            if times is not None: output.create_dataset("sample_times",data=times-t0,dtype="float64")
            if provenance: output.attrs["boundfree_signal_provenance"] = json.dumps(provenance,ensure_ascii=False,allow_nan=False)
            if selected_markers:
                events = output.require_group("events")
                events.create_dataset("time", data=np.asarray([value[0] for value in selected_markers], dtype=np.float64))
                events.create_dataset(
                    "label",
                    data=np.asarray([value[1] for value in selected_markers], dtype=h5py.string_dtype("utf-8")),
                )
    elif suffix == ".edf":
        import pyedflib

        limits = [3200.0 if units[c] == "uV" and np.max(np.abs(data[c])) <= 3200 else max(1.,float(math.ceil(np.max(np.abs(data[c]))*1.001))) for c in range(len(ch_names))]
        if any(limit>9999999 for limit in limits):
            raise ValueError("EDF physical range is too large; use HDF5 to preserve these values.")
        writer = pyedflib.EdfWriter(str(destination), len(ch_names))
        try:
            writer.set_number_of_annotation_signals(max(1, len(selected_markers)))
            writer.setSignalHeaders(
                [
                    {
                        "label": name[:16],
                        "dimension": units[channel_index].replace("µ", "u").replace("μ", "u"),
                        "sample_frequency": fs,
                        "physical_min": -limits[channel_index],
                        "physical_max": limits[channel_index],
                        "digital_min": -32768,
                        "digital_max": 32767,
                        "prefilter": "",
                        "transducer": "",
                    }
                    for channel_index, name in enumerate(ch_names)
                ]
            )
            writer.writeSamples([data[index] for index in range(len(ch_names))])
            for time, label in selected_markers:
                writer.writeAnnotation(time, -1, label)
        finally:
            writer.close()
    else:
        raise ValueError(f"unsupported export format: {suffix}")

    if suffix == ".csv" and (unit != "uV" or channel_units or provenance):
        destination.with_suffix(destination.suffix+".meta.json").write_text(json.dumps({"schema":"boundfree.signal-export.v1","unit":unit,"channel_units":units,"provenance":provenance or {}},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    return {
        "path": str(destination),
        "n_channels": int(data.shape[0]),
        "n_samples": int(data.shape[1]),
        "marker_count": len(selected_markers),
    }
