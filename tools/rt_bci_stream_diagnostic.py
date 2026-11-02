#!/usr/bin/env python3
"""One-click bounded RT-BCI sampling trace. No automatic retries or layout guesses."""
from collections import Counter
from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime
import hashlib
import json
from pathlib import Path
import socket
import sys
import threading
import time
import traceback
import zipfile

import rt_bci_http_diagnostic as http
import rt_bci_quick_diagnostic as quick

TOOL_VERSION = "2026.10.30.1"
RAW_LIMIT = 256 * 1024


def inspect_frames(raw):
    """Describe the wire bytes; never resynchronize them into a claimed EEG recording."""
    frames, skipped, transitions = [], 0, Counter()
    first_fault = None
    position = 0
    strict_counter = None
    while position + 33 <= len(raw):
        if raw[position] != 0xA0 or raw[position + 32] != 0xC0:
            skipped += 1
            if frames and first_fault is None:
                first_fault = {"offset": position, "reason": "unexpected_frame_start" if raw[position] != 0xA0 else "invalid_frame_end"}
            position += 1
            continue
        counter = raw[position + 1]
        if frames:
            transitions[(counter - frames[-1][1]) % 256] += 1
        if strict_counter is not None and first_fault is None:
            delta = (counter - strict_counter) % 256
            if delta == 0 or delta >= 128:
                first_fault = {"offset": position, "reason": "duplicate_counter" if delta == 0 else "ambiguous_counter",
                               "previous_counter": strict_counter, "current_counter": counter, "delta": delta}
        strict_counter = counter
        frames.append((position, counter))
        position += 33
        if raw[position:position + 2] == b"\r\n":
            position += 2
    return {"raw_bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "candidate_33_byte_c0_frames": len(frames), "skipped_bytes": skipped,
            "unexamined_tail_bytes": len(raw) - position,
            "counter_deltas": {str(k): v for k, v in sorted(transitions.items())},
            "first_8_channel_strict_fault": first_fault,
            "first_frame_offsets_and_counters": frames[:24],
            "note": "Candidate scan only; no CRC, ADC identity, bank order or sample-rate verification. Tail may be cut by diagnostic stop."}


def accepted(status, body):
    if not 200 <= status < 300:
        return False
    try:
        obj = json.loads(body)
    except (ValueError, UnicodeError):
        return True
    return not isinstance(obj, dict) or (obj.get("success") is not False and obj.get("ok") is not False
                                        and not obj.get("error"))


def capture_stream(host, port, timeout, data_port, recorder, result, progress, duration):
    local = http.route_ip(host, port)
    ready, stop_receiver = threading.Event(), threading.Event()
    raw = bytearray()
    chunks, peer_info, receive_errors = [], [], []
    started = time.monotonic()
    result.update({"local_address": local, "requested_sample_rate": 250, "requested_channels": 8,
                   "capture_seconds": duration, "raw_limit_bytes": RAW_LIMIT})
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind((local, data_port))
        listener.listen(2)
        listener.settimeout(0.1)
        result["listen_port"] = listener.getsockname()[1]

        def receive():
            try:
                while not stop_receiver.is_set():
                    try:
                        connection, address = listener.accept()
                    except socket.timeout:
                        continue
                    if address[0] != host:
                        connection.close()
                        continue
                    with connection:
                        peer_info.append(list(address))
                        connection.settimeout(0.1)
                        ready.set()
                        while not stop_receiver.is_set():
                            try:
                                data = connection.recv(min(16384, RAW_LIMIT - len(raw)))
                            except socket.timeout:
                                continue
                            if not data:
                                receive_errors.append("peer_closed")
                                return
                            chunks.append({"elapsed_ms": round((time.monotonic() - started) * 1000, 3),
                                           "offset": len(raw), "bytes": len(data)})
                            raw.extend(data)
                            if len(raw) >= RAW_LIMIT:
                                receive_errors.append("raw_limit_reached")
                                return
                    return
            except OSError as exc:
                receive_errors.append(type(exc).__name__)

        worker = threading.Thread(target=receive, daemon=True)
        worker.start()
        start_attempted = False
        try:
            status, _, payload = http.exchange(host, port, "GET", "/stream/stop",
                timeout=timeout, recorder=recorder)
            result["prepare_stop_http_status"] = status
            if not accepted(status, payload):
                result["sampling_skipped"] = "prepare_stop_rejected"
                return
            progress("正在建立数据连接…")
            config = {"delimiter": True, "ip": local, "latency": 10000, "output": "raw", "port": result["listen_port"]}
            body = json.dumps(config, sort_keys=True, separators=(",", ":")).encode("ascii")
            status, _, payload = http.exchange(host, port, "POST", "/tcp", body,
                version="1.0", timeout=timeout, recorder=recorder)
            result["tcp_http_status"] = status
            if not accepted(status, payload) or not ready.wait(timeout):
                result["sampling_skipped"] = "tcp_rejected_or_no_callback"
                return
            # Match the application's selected 8-channel / 250 Hz workflow.
            status, _, payload = http.exchange(host, port, "POST", "/command", b'{"command":"~6"}',
                version="1.0", timeout=timeout, recorder=recorder)
            result["command_http_status"] = status
            if not accepted(status, payload):
                result["sampling_skipped"] = "rate_command_rejected"
                return
            progress("正在短时采集，随后自动停止…")
            result["bytes_before_start_request"] = len(raw)
            start_attempted = True
            result["start_attempted"] = True
            status, _, payload = http.exchange(host, port, "GET", "/stream/start",
                timeout=timeout, recorder=recorder)
            result["start_http_status"] = status
            result["acquisition_started"] = accepted(status, payload)
            if result["acquisition_started"]:
                stop_receiver.wait(duration)
        finally:
            # A start timeout may still have reached the device: always attempt
            # exactly one stop, without retransmitting start or configuration.
            if start_attempted:
                progress("正在停止并保存日志…")
                result["bytes_before_stop_request"] = len(raw)
                result["stop_attempted"] = True
                try:
                    status, _, payload = http.exchange(host, port, "GET", "/stream/stop",
                        timeout=timeout, recorder=recorder)
                    result["stop_http_status"] = status
                    result["stop_http_accepted"] = accepted(status, payload)
                except Exception as exc:
                    result["stop_error"] = type(exc).__name__
            stop_receiver.set()
            worker.join(1)
            if worker.is_alive():
                raise RuntimeError("receiver did not stop")
            result["callback_peer"] = peer_info[0] if peer_info else None
            result["receiver_events"] = receive_errors
            (recorder.directory / "stream.raw").write_bytes(raw)
            (recorder.directory / "tcp-chunks.json").write_text(json.dumps(chunks, indent=2) + "\n", encoding="utf-8")
            result["wire_analysis"] = inspect_frames(raw)
            recorder.write({"kind": "stream_capture", **result})


def run_diagnostic(parent, progress=lambda text: None, *, host="192.168.4.1", port=80,
                   timeout=4, data_port=6677, duration=3):
    if not 0 < duration <= 3 or not 0 < timeout <= 4:
        raise ValueError("diagnostic time limits")
    parent = Path(parent)
    parent.mkdir(parents=True, exist_ok=True)
    name = datetime.now().strftime("ESP32-采集诊断-%Y%m%d-%H%M%S-%f")
    recorder = http.Recorder(parent / name)
    result = {"tool_version": TOOL_VERSION, "host": host, "http_port": port,
              "acquisition_started": False, "start_attempted": False, "automatic_retries": 0,
              "frozen": bool(getattr(sys, "frozen", False)), "python_version": sys.version.split()[0]}
    message = "检查完成，原始数据与日志已保存。"
    with (recorder.directory / "运行日志.txt").open("w", encoding="utf-8") as output, \
            redirect_stdout(output), redirect_stderr(output):
        try:
            recorder.write({"kind": "run_start", **result})
            progress("正在读取设备信息…")
            info = http.collect_info(host, port, timeout, recorder)
            (recorder.directory / "device-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            board, tcp = info.get("/board", {}), info.get("/tcp", {})
            b, t = board.get("content"), tcp.get("content")
            if board.get("status") != 200 or not isinstance(b, dict) or b.get("board_connected") is not True:
                result["sampling_skipped"] = "board_unavailable"
            elif (tcp.get("status") != 200 or not isinstance(t, dict) or t.get("connected") is not False
                  or any(b.get(k) is True for k in ("busy", "streaming", "is_streaming", "tcp_connected", "client_connected"))):
                result["sampling_skipped"] = "active_or_unknown_connection"
            elif b.get("num_channels") != 8:
                result["sampling_skipped"] = "requires_reported_8_channels"
            else:
                capture_stream(host, port, timeout, data_port, recorder, result, progress, duration)
            if result.get("sampling_skipped"):
                message = "本次未启动采集，原因已记录，日志已保存。"
            elif not result.get("stop_http_accepted"):
                message = "设备未确认停流，请关闭工具并重启设备；日志已保存。"
            recorder.write({"kind": "run_complete", **result})
        except Exception as exc:
            result["error"] = {"type": type(exc).__name__, "detail": str(exc)}
            recorder.write({"kind": "run_error", **result})
            traceback.print_exc()
            message = ("设备未确认停流，请关闭工具并重启设备；日志已保存。" if result["start_attempted"]
                       and not result.get("stop_http_accepted") else "检查遇到问题，诊断日志已保存。")
    progress("正在保存诊断日志…")
    result["message"] = message
    (recorder.directory / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (recorder.directory / "检查结果.txt").write_text(message + "\nZIP 包含短时原始采集字节，请发回聊天以核对数据帧。\n", encoding="utf-8-sig")
    archive = parent / (name + ".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as out:
        for file in sorted(recorder.directory.iterdir()):
            if file.is_file():
                out.write(file, arcname=name + "/" + file.name)
    return {"archive": str(archive), "message": message, "result": result}


def main():
    import tkinter as tk
    root = tk.Tk()
    if len(sys.argv) == 4 and sys.argv[1] == "--smoke-ui":
        parent, port = Path(sys.argv[2]), int(sys.argv[3])
        def complete(outcome):
            outcome["ui"] = {"heading": window.heading.cget("text"),
                "open_enabled": str(window.open_button.cget("state")) == "normal",
                "close_enabled": str(window.close_button.cget("state")) == "normal"}
            (parent / "ui-result.json").write_text(json.dumps(outcome, ensure_ascii=False, indent=2), encoding="utf-8")
            root.after(2000, root.destroy)
        window = quick.DiagnosticWindow(root, parent=parent,
            options={"host": "127.0.0.1", "port": port, "data_port": 0, "timeout": 0.5, "duration": 0.3},
            runner=run_diagnostic, sampling=True, reveal=False, complete=complete)
        root.title("BoundFree ESP32 采集诊断验证")
    else:
        quick.DiagnosticWindow(root, runner=run_diagnostic, sampling=True)
    root.mainloop()


if __name__ == "__main__":
    main()
