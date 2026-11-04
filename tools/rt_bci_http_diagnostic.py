#!/usr/bin/env python3
"""RT-BCI HTTP diagnostics, Python 3.10+, standard library only.

Default: read-only /board, /tcp and /all. --proxy forwards user-issued OpenBCI
requests. --test-tcp explicitly configures one data callback, without starting
acquisition or sending an ADC command. No request is automatically retried.
"""
import argparse
from datetime import datetime
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import re
import socket
import sys
import threading
import time

LIMIT = 64 * 1024
SENSITIVE = re.compile(r"pass|pwd|psk|token|secret|credential|authorization|cookie|api.?key", re.I)
PATHS = {"/board", "/all", "/tcp", "/command", "/stream/start", "/stream/stop"}


def redact(value):
    if isinstance(value, dict):
        return {k: "[redacted]" if SENSITIVE.search(k) else redact(v) for k, v in value.items()}
    if isinstance(value, list):
        return [redact(v) for v in value]
    return value


def body_record(body):
    text = body.decode("utf-8", errors="replace")
    try:
        value = redact(json.loads(body))
    except (ValueError, UnicodeError):
        value = "[redacted: sensitive text]" if SENSITIVE.search(text) else text
    return {"bytes": len(body), "sha256": hashlib.sha256(body).hexdigest(), "content": value,
            "utf8": "[redacted: sensitive text]" if SENSITIVE.search(text) else text}


def safe_headers(headers):
    return [[k, "[redacted]" if SENSITIVE.search(k) else v] for k, v in headers]


def route_ip(host, port=80):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect((host, port))  # Select the OS route; no datagram is sent.
        return sock.getsockname()[0]


class Recorder:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=False)
        self.lock = threading.Lock()

    def write(self, event):
        event = {"time": datetime.now().astimezone().isoformat(), **event}
        with self.lock, (self.directory / "http.jsonl").open("a", encoding="utf-8") as out:
            out.write(json.dumps(event, ensure_ascii=False) + "\n")


def exchange(host, port, method, path, body=b"", *, version="1.1", headers=None, timeout=10, recorder=None):
    """Preserve explicit header spelling/version; no proxy, redirect or retry."""
    fields = list(headers) if headers is not None else [("Host", f"{host}:{port}")]
    if method == "POST" and not any(k.lower() == "content-length" for k, _ in fields):
        fields.append(("Content-Length", str(len(body))))
    request = {"method": method, "path": path, "http_version": version,
               "headers": safe_headers(fields), "body": body_record(body)}
    started = time.monotonic()
    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    connection._http_vsn = 10 if version == "1.0" else 11
    connection._http_vsn_str = "HTTP/" + version
    try:
        connection.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
        for key, value in fields:
            connection.putheader(key, value)
        connection.endheaders(body if body else None)
        response = connection.getresponse()
        payload = response.read(LIMIT + 1)
        if len(payload) > LIMIT:
            raise ValueError("response exceeds 64 KiB")
        event = {"kind": "http", "request": request, "response": {
            "status": response.status, "reason": response.reason,
            "headers": safe_headers(response.getheaders()), "body": body_record(payload)},
            "elapsed_ms": round((time.monotonic() - started) * 1000)}
        if recorder:
            recorder.write(event)
        return response.status, response.getheaders(), payload
    except (OSError, ValueError, http.client.HTTPException) as exc:
        if recorder:
            recorder.write({"kind": "http", "request": request, "error": type(exc).__name__,
                            "elapsed_ms": round((time.monotonic() - started) * 1000)})
        raise
    finally:
        connection.close()


def collect_info(host, port, timeout, recorder):
    results = {}
    for path in ("/board", "/tcp", "/all"):
        try:
            status, _, body = exchange(host, port, "GET", path, timeout=timeout, recorder=recorder)
            results[path] = {"status": status, **body_record(body)}
            print(f"GET {path}: HTTP {status}\n{json.dumps(results[path]['content'], ensure_ascii=False)}")
        except (OSError, ValueError, http.client.HTTPException) as exc:
            results[path] = {"error": type(exc).__name__}
            print(f"GET {path}: {type(exc).__name__}")
    return results


def test_tcp(host, port, timeout, listen_port, style, info, recorder):
    state = info.get("/tcp", {})
    content = state.get("content")
    if state.get("status") != 200 or not isinstance(content, dict) or content.get("connected") is not False:
        raise ValueError("TCP state is not explicitly disconnected; refusing to replace an active/unknown connection")
    local = route_ip(host, port)
    accepted = []
    connections = []
    stopped = threading.Event()
    observed = threading.Event()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind((local, listen_port))
        listener.listen(2)
        listener.settimeout(timeout)
        actual_port = listener.getsockname()[1]

        def receive_callback():
            try:
                peer, address = listener.accept()
                with peer:
                    if address[0] != host:
                        return
                    connections.append(peer)
                    accepted.append(address)
                    observed.set()
                    stopped.wait(timeout)  # Do not read signal data or start acquisition.
            except OSError:
                pass

        worker = threading.Thread(target=receive_callback, daemon=True)
        worker.start()
        obj = {"ip": local, "port": actual_port, "output": "raw", "delimiter": True, "latency": 10000}
        body = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("ascii")
        fields = [("Host", f"{host}:{port}"), ("Content-Length", str(len(body)))]
        version = "1.0"
        if style != "reference":
            version = "1.1"
            fields.insert(1, ("Content-Type", "application/json"))
        if style == "lowercase":
            fields = [(k.lower(), v) for k, v in fields]
        print(f"One POST /tcp, format={style}, callback={local}:{actual_port}\n{body.decode()}")
        try:
            status, _, payload = exchange(host, port, "POST", "/tcp", body, version=version,
                                          headers=fields, timeout=timeout, recorder=recorder)
            if 200 <= status < 300:
                observed.wait(timeout)
            print(f"POST /tcp: HTTP {status}\n{json.dumps(body_record(payload)['content'], ensure_ascii=False)}")
            print(f"TCP callback observed: {bool(accepted)}; acquisition was not started.")
            recorder.write({"kind": "tcp_callback", "observed": bool(accepted),
                            "peer": accepted[0] if accepted else None, "signal_data_read": False})
            return {"http_status": status, "response": body_record(payload),
                    "callback_observed": bool(accepted), "local_address": local,
                    "listen_port": actual_port, "format": style, "acquisition_started": False}
        finally:
            stopped.set()
            for peer in connections:
                peer.close()
            worker.join(0.1)


def proxy_server(host, port, bind, listen_port, timeout, recorder):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, *_):
            pass

        def handle_request(self):
            if self.client_address[0] not in {bind, "127.0.0.1"}:
                self.send_error(403)
                return
            if self.path not in PATHS or self.headers.get("Transfer-Encoding") or self.request_version not in {"HTTP/1.0", "HTTP/1.1"}:
                self.send_error(400, "Unsupported endpoint or transfer encoding")
                return
            lengths = self.headers.get_all("Content-Length", [])
            try:
                if len(lengths) > 1:
                    raise ValueError("duplicate Content-Length")
                length = int(lengths[0]) if lengths else 0
                if not 0 <= length <= LIMIT:
                    raise ValueError("request body limit")
            except ValueError:
                self.send_error(400)
                return
            self.connection.settimeout(timeout)
            try:
                body = self.rfile.read(length)
                if len(body) != length:
                    raise ValueError("incomplete body")
                incoming = list(self.headers.raw_items())
                recorder.write({"kind": "openbci_received", "request": {
                    "method": self.command, "path": self.path, "http_version": self.request_version,
                    "headers": safe_headers(incoming), "body": body_record(body)}})
                forwarded = [(k, f"{host}:{port}" if k.lower() == "host" else v) for k, v in incoming]
                if not any(k.lower() == "host" for k, _ in forwarded):
                    forwarded.insert(0, ("Host", f"{host}:{port}"))
                status, headers, payload = exchange(host, port, self.command, self.path, body,
                    version=self.request_version.removeprefix("HTTP/"), headers=forwarded,
                    timeout=timeout, recorder=recorder)
                self.send_response(status)
                for key, value in headers:
                    if key.lower() not in {"content-length", "transfer-encoding", "connection"}:
                        self.send_header(key, value)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                print(f"OpenBCI {self.command} {self.path}: HTTP {status}", flush=True)
            except (OSError, ValueError, http.client.HTTPException) as exc:
                recorder.write({"kind": "proxy_error", "error": type(exc).__name__})
                self.send_error(502, "Device exchange failed")

        do_GET = do_POST = do_DELETE = do_OPTIONS = handle_request

    server = ThreadingHTTPServer((bind, listen_port), Handler)
    server.daemon_threads = True
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--output", type=Path)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--proxy", action="store_true", help="Forward OpenBCI commands, including user-issued start/stop")
    modes.add_argument("--test-tcp", action="store_true", help="Explicitly configure one TCP callback; never start sampling")
    parser.add_argument("--listen-port", type=int, default=80, help="HTTP proxy port; OpenBCI needs port 80")
    parser.add_argument("--data-port", type=int, default=6677)
    parser.add_argument("--format", choices=("reference", "canonical", "lowercase"), default="reference")
    args = parser.parse_args()
    try:
        host = str(ipaddress.IPv4Address(args.host))
        if not 1 <= args.port <= 65535 or not 1 <= args.listen_port <= 65535 or not 0 <= args.data_port <= 65535:
            raise ValueError("invalid port")
        if not 0.1 <= args.timeout <= 30:
            raise ValueError("timeout must be 0.1..30 seconds")
        output = args.output or Path(__file__).resolve().parent / datetime.now().strftime("ESP32-HTTP-%Y%m%d-%H%M%S-%f")
        recorder = Recorder(output)
        print(f"Report directory: {recorder.directory}", flush=True)
        if args.proxy:
            bind = route_ip(host, args.port)
            print(f"OpenBCI static target IP: {bind} (computer Wi-Fi IP, NOT the ESP32 address).", flush=True)
            print("This proxy forwards the actions you issue in OpenBCI, including start/stop. Ctrl+C to exit.", flush=True)
            with proxy_server(host, args.port, bind, args.listen_port, args.timeout, recorder) as server:
                server.serve_forever()
        else:
            info = collect_info(host, args.port, args.timeout, recorder)
            (recorder.directory / "device-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            if args.test_tcp:
                test_tcp(host, args.port, args.timeout, args.data_port, args.format, info, recorder)
    except KeyboardInterrupt:
        print("Stopped.")
    except (OSError, ValueError, http.client.HTTPException) as exc:
        print(f"Diagnostic failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    raise SystemExit(main())
