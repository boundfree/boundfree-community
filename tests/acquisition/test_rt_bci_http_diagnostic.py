"""Exercise the diagnostic on independent local HTTP services; no hardware."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import socket
import threading
import sys
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("http_diagnostic", ROOT / "tools/rt_bci_http_diagnostic.py")
diagnostic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostic)
sys.path.insert(0, str(ROOT / "tools"))
try:
    import rt_bci_quick_diagnostic as quick
finally:
    sys.path.pop(0)


@contextmanager
def serving(server):
    worker = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.02), daemon=True)
    worker.start()
    try:
        yield server.server_address[1]
    finally:
        server.shutdown()
        server.server_close()
        worker.join(2)


@pytest.fixture
def device():
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            requests.append(("GET", self.path, self.request_version, list(self.headers.raw_items()), b""))
            obj = {"connected": False} if self.path == "/tcp" else {"board_connected": True, "password": "not-for-report"}
            self.respond(200, obj)

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            fields = list(self.headers.raw_items())
            requests.append(("POST", self.path, self.request_version, fields, body))
            # Deliberately emulate a deficient parser: same valid JSON, but it
            # fails with a lower-case length header. This is not hardware proof.
            if "Content-Length" not in [k for k, _ in fields]:
                self.respond(400, {"error": "Invalid JSON"})
                return
            obj = json.loads(body)
            if obj.get("port"):
                with socket.create_connection((obj["ip"], obj["port"]), timeout=1):
                    self.respond(200, {"connected": True})
            else:
                self.respond(200, {"ok": True})

        def respond(self, status, obj):
            body = json.dumps(obj).encode()
            self.send_response(status)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with serving(ThreadingHTTPServer(("127.0.0.1", 0), Handler)) as port:
        yield port, requests


def test_information_mode_is_read_only_and_redacts(tmp_path, device):
    port, requests = device
    recorder = diagnostic.Recorder(tmp_path / "report")
    info = diagnostic.collect_info("127.0.0.1", port, 2, recorder)
    assert [r[:2] for r in requests] == [("GET", "/board"), ("GET", "/tcp"), ("GET", "/all")]
    assert info["/board"]["content"]["password"] == "[redacted]"
    assert "not-for-report" not in (recorder.directory / "http.jsonl").read_text()


@pytest.mark.parametrize("style,status", [("reference", 200), ("canonical", 200), ("lowercase", 400)])
def test_one_explicit_tcp_configuration_and_no_adc_commands(tmp_path, device, style, status):
    port, requests = device
    recorder = diagnostic.Recorder(tmp_path / "report")
    diagnostic.test_tcp("127.0.0.1", port, 2, 0, style,
                        {"/tcp": {"status": 200, "content": {"connected": False}}}, recorder)
    assert len(requests) == 1
    method, path, version, fields, body = requests[0]
    assert (method, path) == ("POST", "/tcp")
    assert version == ("HTTP/1.0" if style == "reference" else "HTTP/1.1")
    assert json.loads(body)["delimiter"] is True
    assert len(body) == int(dict((k.lower(), v) for k, v in fields)["content-length"])
    events = [json.loads(line) for line in (recorder.directory / "http.jsonl").read_text().splitlines()]
    assert events[0]["response"]["status"] == status
    assert events[-1]["signal_data_read"] is False


def test_tcp_refuses_active_or_unknown_owner(tmp_path, device):
    port, requests = device
    for content in ({"connected": True}, {}, "invalid"):
        with pytest.raises(ValueError, match="active/unknown"):
            diagnostic.test_tcp("127.0.0.1", port, 2, 0, "reference",
                                {"/tcp": {"status": 200, "content": content}}, diagnostic.Recorder(tmp_path / str(len(list(tmp_path.iterdir())))))
    assert not requests


def test_proxy_preserves_openbci_format_and_rejection(tmp_path, device):
    port, requests = device
    recorder = diagnostic.Recorder(tmp_path / "report")
    proxy = diagnostic.proxy_server("127.0.0.1", port, "127.0.0.1", 0, 2, recorder)
    body = b'{"ip":"127.0.0.1","port":0,"delimiter":true}'
    with serving(proxy) as proxy_port:
        for lower, expected in [(False, 200), (True, 400)]:
            fields = [("Host", f"127.0.0.1:{proxy_port}"), ("Content-Length", str(len(body)))]
            if lower:
                fields = [(k.lower(), v) for k, v in fields]
            status, _, payload = diagnostic.exchange("127.0.0.1", proxy_port, "POST", "/tcp", body,
                                                      version="1.0", headers=fields)
            assert status == expected
            if lower:
                assert json.loads(payload)["error"] == "Invalid JSON"
        status, _, _ = diagnostic.exchange("127.0.0.1", proxy_port, "GET", "/not-allowed")
        assert status == 400
    assert len(requests) == 2
    for _, _, version, fields, captured in requests:
        assert version == "HTTP/1.0" and captured == body
        assert dict((k.lower(), v) for k, v in fields)["host"] == f"127.0.0.1:{port}"
    events = [json.loads(line) for line in (recorder.directory / "http.jsonl").read_text().splitlines()]
    assert sum(e["kind"] == "openbci_received" for e in events) == 2


def archive_files(outcome):
    with zipfile.ZipFile(outcome["archive"]) as archive:
        assert archive.testzip() is None
        return {Path(name).name: archive.read(name).decode("utf-8-sig") for name in archive.namelist()}


def test_one_click_saves_one_zip_and_never_starts_adc(tmp_path, device):
    port, requests = device
    progress = []
    outcome = quick.run_diagnostic(tmp_path, progress.append, host="127.0.0.1", port=port, timeout=1, data_port=0)
    assert [r[:2] for r in requests] == [("GET", "/board"), ("GET", "/tcp"), ("GET", "/all"), ("POST", "/tcp")]
    assert outcome["result"]["tcp_test"]["callback_observed"] is True
    files = archive_files(outcome)
    assert {"http.jsonl", "device-info.json", "运行日志.txt", "result.json", "检查结果.txt"} <= files.keys()
    assert "not-for-report" not in "\n".join(files.values())
    assert outcome["result"]["acquisition_started"] is False
    assert len(progress) == 3


def test_one_click_archives_unreachable_device(tmp_path):
    with socket.socket() as unavailable:
        unavailable.bind(("127.0.0.1", 0))  # Reserved but not listening.
        outcome = quick.run_diagnostic(tmp_path, host="127.0.0.1", port=unavailable.getsockname()[1], timeout=0.1, data_port=0)
    assert outcome["result"]["tcp_test"]["skipped"] == "board_unavailable"
    assert len(archive_files(outcome)["http.jsonl"].splitlines()) == 5


def test_one_click_archives_occupied_listening_port(tmp_path, device):
    port, requests = device
    with socket.socket() as occupied:
        occupied.bind(("127.0.0.1", 0))
        occupied.listen()
        outcome = quick.run_diagnostic(tmp_path, host="127.0.0.1", port=port, timeout=1, data_port=occupied.getsockname()[1])
    assert "error" in outcome["result"]
    assert all(r[0] == "GET" for r in requests)
    files = archive_files(outcome)
    assert "Traceback" in files["运行日志.txt"] and "run_error" in files["http.jsonl"]
