"""Independent wire fixtures for bounded, one-click hardware evidence capture."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import sys
import threading
import time
import zipfile

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
try:
    import rt_bci_stream_diagnostic as stream
finally:
    sys.path.pop(0)


def frame(counter, end=0xC0):
    return bytes([0xA0, counter]) + bytes([0x0D, 0x0A, 0xA0]) * 8 + bytes(6) + bytes([end])


@contextmanager
def device(mode="success"):
    requests, sockets, senders = [], [], []
    done = threading.Event()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def respond(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except OSError:
                pass

        def do_GET(self):
            requests.append(("GET", self.path))
            if self.path == "/board":
                self.respond(200, {"board_connected": True, "num_channels": 16 if mode == "sixteen" else 8,
                                   "password": "never-log-this-password"})
            elif self.path == "/tcp":
                self.respond(200, {"connected": mode == "busy"})
            elif self.path == "/stream/start":
                def send():
                    data = (frame(254) + b"\r\n" + frame(255) + frame(255) + frame(0)
                            if mode == "duplicate" else frame(254) + frame(255) + frame(0) + frame(1))
                    if mode == "bad_end":
                        data += frame(2, 0xC1)
                    if mode != "no_data":
                        for pos in range(0, len(data), 7):
                            if done.is_set():
                                break
                            try:
                                sockets[-1].sendall(data[pos:pos+7])
                            except OSError:
                                break
                            time.sleep(0.001)
                sender = threading.Thread(target=send, daemon=True)
                senders.append(sender)
                sender.start()
                if mode == "start_timeout":
                    time.sleep(0.25)
                self.respond(200, {"ok": True})
            elif self.path == "/stream/stop":
                self.respond(500 if mode == "stop_rejected" and ("GET", "/stream/start") in requests else 200, {"ok": True})
            else:
                self.respond(200, {})

        def do_POST(self):
            requests.append(("POST", self.path))
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            assert self.request_version == "HTTP/1.0"
            assert "Content-Length" in dict(self.headers.raw_items())
            assert self.headers.get("Content-Type") is None
            obj = json.loads(body)
            if self.path == "/tcp":
                assert obj["delimiter"] is True and obj["output"] == "raw"
                if mode == "tcp_rejected":
                    self.respond(400, {"error": "Invalid JSON"})
                    return
                if mode != "no_callback":
                    sockets.append(socket.create_connection((obj["ip"], obj["port"]), timeout=1))
            else:
                assert obj == {"command": "~6"}
                if mode == "command_rejected":
                    self.respond(400, {"error": "command rejected"})
                    return
            self.respond(200, {"ok": True})
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.01), daemon=True)
    worker.start()
    try:
        yield server.server_address[1], requests
    finally:
        done.set()
        for sender in senders:
            sender.join(1)
        for connection in sockets:
            connection.close()
        server.shutdown()
        server.server_close()
        worker.join(1)


def unzip(outcome):
    with zipfile.ZipFile(outcome["archive"]) as archive:
        assert archive.testzip() is None
        files = {Path(name).name: archive.read(name) for name in archive.namelist()}
    assert b"never-log-this-password" not in b"".join(files.values())
    return files


@pytest.mark.parametrize("mode", ["success", "duplicate", "bad_end", "no_data", "stop_rejected", "start_timeout"])
def test_short_capture_stops_once_and_preserves_raw(tmp_path, mode):
    with device(mode) as (port, requests):
        outcome = stream.run_diagnostic(tmp_path, host="127.0.0.1", port=port, timeout=0.15, data_port=0, duration=0.2)
    result = outcome["result"]
    files = unzip(outcome)
    assert result["start_attempted"] and result["stop_attempted"]
    assert requests == [("GET", "/board"), ("GET", "/tcp"), ("GET", "/all"),
                        ("GET", "/stream/stop"), ("POST", "/tcp"), ("POST", "/command"),
                        ("GET", "/stream/start"), ("GET", "/stream/stop")]
    assert result["wire_analysis"] == stream.inspect_frames(files["stream.raw"])
    assert result["wire_analysis"]["raw_bytes"] <= stream.RAW_LIMIT
    if mode == "duplicate":
        assert result["wire_analysis"]["first_8_channel_strict_fault"]["reason"] == "duplicate_counter"
    if mode == "success":
        assert files["stream.raw"] == frame(254) + frame(255) + frame(0) + frame(1)
        assert result["wire_analysis"]["counter_deltas"] == {"1": 3}
    if mode == "no_data":
        assert not files["stream.raw"]
    if mode == "stop_rejected":
        assert not result["stop_http_accepted"]
    if mode == "start_timeout":
        assert "error" in result and result["stop_http_accepted"]


@pytest.mark.parametrize("mode", ["busy", "sixteen", "tcp_rejected", "command_rejected", "no_callback"])
def test_refusals_do_not_start_or_retry(tmp_path, mode):
    with device(mode) as (port, requests):
        outcome = stream.run_diagnostic(tmp_path, host="127.0.0.1", port=port, timeout=0.15, data_port=0, duration=0.1)
    unzip(outcome)
    assert not outcome["result"]["start_attempted"]
    assert outcome["result"]["sampling_skipped"]
    assert ("GET", "/stream/start") not in requests
    assert requests.count(("POST", "/tcp")) <= 1


def test_offline_device_saves_failure(tmp_path):
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        outcome = stream.run_diagnostic(tmp_path, host="127.0.0.1", port=reserved.getsockname()[1], timeout=0.1, data_port=0)
    assert outcome["result"]["sampling_skipped"] == "board_unavailable"
    unzip(outcome)


def test_inspector_handles_damaged_frame_tail_and_binary_markers():
    raw = frame(10) + frame(11, 0xC1) + frame(12) + b"\xa0\x0d"
    result = stream.inspect_frames(raw)
    assert result["candidate_33_byte_c0_frames"] == 2
    assert result["first_8_channel_strict_fault"] == {"offset": 33, "reason": "invalid_frame_end"}
    assert result["unexamined_tail_bytes"] == 2
