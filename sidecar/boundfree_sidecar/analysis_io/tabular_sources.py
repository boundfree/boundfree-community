"""Bounded CSV scans and EDF channel slices for the analysis snapshot API."""
import csv
import math
import re

import numpy as np

from .errors import fail
from .sources import unit_factor


class CsvSource:
    def __init__(self, path, unit=None):
        self.path = path
        if unit is not None and unit not in ("uV", "mV", "V"):
            fail("unknown_unit", unit=str(unit))
        self.explicit_unit = unit
        with path.open("r", encoding="utf-8-sig", newline="") as source:
            reader = csv.reader(source)
            headers = next(reader, None)
            if (not headers or headers[0] != "time_s" or len(headers) < 2
                    or len(set(headers)) != len(headers) or any(not name for name in headers)
                    or headers == ["time_s", "label"]):
                fail("source_invalid", path=str(path), reason="required time_s plus unique continuous signal columns")
            self.names, self.units, factors = [], [], []
            for header in headers[1:]:
                match = re.fullmatch(r"(.+?)\s*\[([^\[\]]+)\]\s*", header)
                if match:
                    name, column_unit = match.group(1).strip(), match.group(2).strip()
                else:
                    name, column_unit = header, unit
                if column_unit is None:
                    fail("unknown_unit", unit="unknown")
                factors.append(unit_factor(column_unit))
                self.names.append(name)
                self.units.append(column_unit)
            if len(set(self.names)) != len(self.names):
                fail("source_invalid", path=str(path), reason="duplicate channel names after unit normalization")
            self.factors = np.asarray(factors, dtype=np.float64)
            self.headers = headers
            self.n_samples, self.fs = 0, None
            previous, interval = None, None
            for index, row in enumerate(reader):
                if len(row) != len(headers):
                    fail("source_invalid", path=str(path), reason="CSV row width mismatch")
                try:
                    timestamp = float(row[0])
                    values = [float(value) for value in row[1:]]
                except ValueError:
                    fail("source_invalid", path=str(path), reason="non-numeric CSV samples")
                if not math.isfinite(timestamp) or not all(math.isfinite(value) for value in values):
                    fail("source_invalid", path=str(path), reason="non-finite CSV samples")
                if index == 0:
                    if timestamp != 0.0:
                        fail("source_invalid", path=str(path), reason="CSV time_s must start at source-relative zero")
                else:
                    if timestamp <= previous:
                        fail("discontinuous_time", path=str(path))
                    if index == 1:
                        interval = timestamp
                        self.fs = 1.0 / interval
                    expected = index * interval
                    # Decimal CSV roundoff is allowed; sample-sized jitter or drift is not.
                    tolerance = max(1e-12, abs(interval) * 1e-6, math.ulp(expected) * 8)
                    if abs(timestamp - expected) > tolerance:
                        fail("discontinuous_time", path=str(path))
                previous = timestamp
                self.n_samples += 1
            if self.n_samples < 2:
                fail("source_invalid", path=str(path), reason="at least two CSV samples required to infer fs")
        self.format = "CSV time_s"
        self.hardware = {}
        self.time_axis = "verified-uniform-time_s"
        self.source_metadata = {"csv_unit": unit or "", "input_units": self.units,
                                "input_unit_explicit_by_user": unit is not None,
                                "first_timestamp_s": 0.0}

    def read(self, start, count, channels):
        output = np.empty((len(channels), count), dtype="<f8")
        with self.path.open("r", encoding="utf-8-sig", newline="") as source:
            reader = csv.reader(source)
            if next(reader) != self.headers:
                fail("source_invalid", path=str(self.path), reason="CSV header changed")
            copied = 0
            for index, row in enumerate(reader):
                if index < start:
                    continue
                if index >= start + count:
                    break
                for position, channel in enumerate(channels):
                    output[position, copied] = float(row[channel + 1]) * self.factors[channel]
                copied += 1
        if copied != count:
            fail("source_invalid", path=str(self.path), reason="CSV sample range truncated")
        return output


class EdfSource:
    def __init__(self, path):
        import pyedflib
        self.path = path
        with path.open("rb") as header:
            header.seek(192)
            reserved = header.read(44)
        if reserved.startswith((b"EDF+D", b"BDF+D")):
            fail("discontinuous_time", path=str(path))
        with pyedflib.EdfReader(str(path)) as source:
            if source.filetype not in (pyedflib.FILETYPE_EDF, pyedflib.FILETYPE_EDFPLUS):
                fail("unsupported_source", path=str(path), reason="EDF or continuous EDF+ required")
            count = source.signals_in_file
            self.names = list(source.getSignalLabels())
            frequencies = np.asarray(source.getSampleFrequencies(), dtype=np.float64)
            samples = np.asarray(source.getNSamples(), dtype=np.int64)
            if (not count or not np.isfinite(frequencies).all() or np.any(frequencies <= 0)
                    or np.any(frequencies != frequencies[0]) or np.any(samples != samples[0])):
                fail("source_invalid", path=str(path), reason="EDF channels require identical finite fs and sample count")
            self.fs, self.n_samples = float(frequencies[0]), int(samples[0])
            self.factors = np.asarray([unit_factor(source.getPhysicalDimension(index).strip())
                                       for index in range(count)])
            for index in range(count):
                low, high = source.getPhysicalMinimum(index), source.getPhysicalMaximum(index)
                if not math.isfinite(low) or not math.isfinite(high) or low >= high:
                    fail("source_invalid", path=str(path), reason="invalid EDF physical calibration")
            prefilters = [source.getPrefilter(index) for index in range(count)]
        self.format = "EDF+ continuous" if reserved.startswith(b"EDF+C") else "EDF"
        self.hardware = {"prefilter_text": prefilters}
        self.time_axis = "continuous-edf-records"
        self.source_metadata = {"input_unit_explicit_by_user": False}

    def read(self, start, count, channels):
        import pyedflib
        output = np.empty((len(channels), count), dtype="<f8")
        with pyedflib.EdfReader(str(self.path)) as source:
            for position, channel in enumerate(channels):
                part = source.readSignal(channel, start=start, n=count, digital=False)
                if len(part) != count:
                    fail("source_invalid", path=str(self.path), reason="EDF sample range truncated")
                output[position] = part * self.factors[channel]
        return output
