"""Countable real IPC peer for FileOpenController integration tests only."""

import json
import os
from pathlib import Path
import struct
import sys
import time


root = Path(os.environ["BF_RESOURCE_ROOT"])
log_path = root / "requests.jsonl"
handle = None


def log(record):
    with log_path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps({"pid": os.getpid(), **record}) + "\n")


def frame(kind, payload):
    sys.stdout.buffer.write(struct.pack("<BI", kind, len(payload)) + payload)
    sys.stdout.buffer.flush()


def exact(count):
    result = b""
    while len(result) < count:
        part = sys.stdin.buffer.read(count - len(result))
        if not part:
            raise EOFError
        result += part
    return result


try:
    while True:
        kind, length = struct.unpack("<BI", exact(5))
        assert kind == 1
        request = json.loads(exact(length))
        method, params = request["method"], request["params"]
        log({"method": method, "params": params})
        binary = None
        error = None
        if method == "hello":
            result = {"product_version": "test", "ipc_protocol_version": 1}
        elif method == "open":
            if params["path"] == "bad.csv":
                error = {"code": "reader.file_not_found", "params": {"path": "bad.csv"}}
                result = {}
            else:
                assert handle is None, "previous handle must be closed before replacing it"
                handle = params["path"]
                result = {"handle": handle, "fs": 10, "n_channels": 1,
                          "n_samples": 1000, "duration": 100, "format": "CSV",
                          "ch_names": ["ch1"], "events": []}
        elif method == "close":
            assert params["handle"] == handle
            handle = None
            result = {}
        elif method == "get_slice":
            assert params["handle"] == handle
            t0, t1 = params["t0"], params["t1"]
            if t0 == 10:
                (root / "burst-started").touch()
                deadline = time.monotonic() + 5
                while not (root / "burst-release").exists():
                    if time.monotonic() > deadline:
                        raise RuntimeError("burst was not released")
                    time.sleep(0.005)
            if t0 == 20:
                (root / "destroy-started").touch()
                time.sleep(0.2)
            if t0 == 30:
                log({"method": "crash"})
                sys.exit(7)
            count = round((t1 - t0) * 10)
            binary = struct.pack("<" + "f" * count, *([t0 + 1] * count))
            result = {"handle": handle, "t0": t0, "t1": t1, "fs": 10,
                      "unit": "uV", "shape": [1, count], "dtype": "float32-le",
                      "payload_bytes": len(binary)}
        else:
            raise AssertionError(method)
        response = {"id": request["id"], "ok": error is None}
        response["error" if error else "result"] = error if error else result
        frame(1, json.dumps(response).encode("utf-8"))
        if binary is not None:
            frame(2, binary)
except EOFError:
    log({"method": "eof"})
