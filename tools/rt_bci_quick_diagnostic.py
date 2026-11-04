#!/usr/bin/env python3
"""One-click Windows diagnostic. Frozen distribution needs no Python install."""
from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import traceback
import zipfile

import rt_bci_http_diagnostic as diagnostic

TOOL_VERSION = "2026.10.29.1"


def log_directory():
    base = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
    for candidate in (base / "诊断日志", Path.home() / "Downloads" / "BoundFree诊断日志",
                      Path(tempfile.gettempdir()) / "BoundFree诊断日志"):
        try:
            candidate.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryFile(dir=candidate):
                pass
            return candidate
        except OSError:
            continue
    raise OSError("无法创建诊断日志目录")


def run_diagnostic(parent, progress=lambda text: None, *, host="192.168.4.1", port=80,
                   timeout=4, data_port=6677):
    """One info round and at most one reference TCP request. Always archive errors."""
    parent = Path(parent)
    parent.mkdir(parents=True, exist_ok=True)
    name = datetime.now().strftime("ESP32-诊断-%Y%m%d-%H%M%S-%f")
    recorder = diagnostic.Recorder(parent / name)
    result = {"tool_version": TOOL_VERSION, "host": host, "http_port": port,
              "acquisition_started": False, "automatic_retries": 0,
              "frozen": bool(getattr(sys, "frozen", False)), "python_version": sys.version.split()[0]}
    message = "检测完成，日志已保存。"
    with (recorder.directory / "运行日志.txt").open("w", encoding="utf-8") as output, \
            redirect_stdout(output), redirect_stderr(output):
        try:
            recorder.write({"kind": "run_start", **result})
            progress("正在读取设备信息…")
            info = diagnostic.collect_info(host, port, timeout, recorder)
            (recorder.directory / "device-info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            board = info.get("/board", {})
            tcp = info.get("/tcp", {})
            board_content = board.get("content")
            tcp_content = tcp.get("content")
            ready = board.get("status") == 200 and isinstance(board_content, dict) and board_content.get("board_connected") is True
            idle = tcp.get("status") == 200 and isinstance(tcp_content, dict) and tcp_content.get("connected") is False
            busy = isinstance(board_content, dict) and any(board_content.get(key) is True for key in
                ("busy", "streaming", "is_streaming", "tcp_connected", "client_connected"))
            if not ready:
                result["tcp_test"] = {"skipped": "board_unavailable"}
                message = "设备尚未就绪，诊断日志已保存。"
            elif not idle or busy:
                result["tcp_test"] = {"skipped": "active_or_unknown_connection"}
                message = "检测到已有连接或状态不明，诊断日志已保存。"
            else:
                progress("正在检查数据连接…")
                result["tcp_test"] = diagnostic.test_tcp(host, port, timeout, data_port, "reference", info, recorder)
            recorder.write({"kind": "run_complete", **result})
        except Exception as exc:
            result["error"] = {"type": type(exc).__name__, "detail": str(exc)}
            recorder.write({"kind": "run_error", **result})
            traceback.print_exc()
            message = "检查遇到问题，诊断日志已保存。"
    progress("正在保存诊断日志…")
    result["message"] = message
    (recorder.directory / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (recorder.directory / "检查结果.txt").write_text(message + "\n请将同名 ZIP 日志文件发回聊天。\n", encoding="utf-8-sig")
    archive = parent / (name + ".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as out:
        for file in sorted(recorder.directory.iterdir()):
            if file.is_file():
                out.write(file, arcname=name + "/" + file.name)
    return {"archive": str(archive), "message": message, "result": result}


def show_log(archive):
    if os.name == "nt":
        explorer = Path(os.environ.get("WINDIR", r"C:\Windows")) / "explorer.exe"
        subprocess.Popen([str(explorer), "/select,", str(Path(archive).resolve())])


class DiagnosticWindow:
    def __init__(self, root, *, parent=None, options=None, reveal=True, complete=None,
                 runner=run_diagnostic, sampling=False):
        from tkinter import ttk
        import tkinter as tk
        self.root, self.options = root, options or {}
        self.parent, self.reveal, self.complete = parent, reveal, complete
        self.runner = runner
        self.events = queue.Queue()
        self.done, self.closing, self.archive = False, False, None
        root.title("ESP32 采集诊断" if sampling else "ESP32 设备诊断")
        icon = (Path(sys._MEIPASS) / "BoundFree_app_icon.ico" if getattr(sys, "frozen", False)
                else Path(__file__).resolve().parents[1] / "native/assets/branding/BoundFree_app_icon.ico")
        try:
            root.iconbitmap(str(icon))
        except (tk.TclError, OSError):
            pass
        root.geometry("560x270")
        root.resizable(False, False)
        frame = ttk.Frame(root, padding=24)
        frame.pack(fill="both", expand=True)
        self.heading = ttk.Label(frame, text="正在检查设备采集" if sampling else "正在检查设备连接", font=("Microsoft YaHei UI", 14, "bold"))
        self.heading.pack(anchor="w")
        description = ("自动短时采集约 3 秒并停止，保存原始数据与通信日志。请保持设备热点连接，并关闭上位机。"
                       if sampling else "请保持设备热点连接，并关闭其他采集软件。")
        self.description = ttk.Label(frame, text=description, wraplength=505)
        self.description.pack(anchor="w", pady=(10, 18))
        self.bar = ttk.Progressbar(frame, mode="indeterminate")
        self.bar.pack(fill="x")
        self.bar.start(15)
        self.status = tk.StringVar(value="准备检查…")
        ttk.Label(frame, textvariable=self.status, wraplength=505).pack(anchor="w", pady=(12, 14))
        actions = ttk.Frame(frame)
        actions.pack(side="bottom", fill="x")
        self.open_button = ttk.Button(actions, text="打开日志位置", command=self.open_logs, state="disabled")
        self.open_button.pack(side="left")
        self.close_button = ttk.Button(actions, text="关闭", command=self.close, state="disabled")
        self.close_button.pack(side="right")
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self.start)

    def start(self):
        def worker():
            try:
                parent = self.parent or log_directory()
                result = self.runner(parent, lambda text: self.events.put(("progress", text)), **self.options)
                self.events.put(("done", result))
            except Exception as exc:
                self.events.put(("error", str(exc)))
        threading.Thread(target=worker, daemon=True).start()
        self.root.after(100, self.poll)

    def poll(self):
        while not self.events.empty():
            kind, value = self.events.get_nowait()
            if kind == "progress":
                self.status.set(value)
                continue
            self.done = True
            self.bar.stop()
            self.bar.configure(mode="determinate", value=100)
            self.close_button.configure(state="normal")
            if kind == "done":
                self.archive = value["archive"]
                self.heading.configure(text="诊断日志已保存")
                self.description.configure(text=value["message"] + " 请将 ZIP 日志文件发回聊天。")
                self.status.set(Path(self.archive).name)
                self.open_button.configure(state="normal")
                if self.reveal and not self.closing:
                    self.open_logs()
                if self.complete:
                    self.complete(value)
            else:
                self.heading.configure(text="无法保存诊断日志")
                self.description.configure(text="请把工具放到可写入的文件夹后重试。")
                self.status.set(value)
            if self.closing:
                self.root.destroy()
                return
        if not self.done:
            self.root.after(100, self.poll)

    def open_logs(self):
        try:
            show_log(self.archive)
        except OSError:
            self.status.set("日志位置：" + str(Path(self.archive).parent))

    def close(self):
        if self.done:
            self.root.destroy()
        else:
            # Finish the bounded check and save its log even if the window closes.
            self.closing = True
            self.root.withdraw()


def main():
    import tkinter as tk
    root = tk.Tk()
    if len(sys.argv) == 4 and sys.argv[1] == "--smoke-ui":
        # Isolated loopback endpoint supplied by the packaging acceptance test.
        parent = Path(sys.argv[2])
        port = int(sys.argv[3])
        def complete(outcome):
            outcome["ui"] = {"heading": window.heading.cget("text"),
                "open_enabled": str(window.open_button.cget("state")) == "normal",
                "close_enabled": str(window.close_button.cget("state")) == "normal",
                "automatic": True}
            (parent / "ui-result.json").write_text(json.dumps(outcome, ensure_ascii=False, indent=2), encoding="utf-8")
            root.after(2000, root.destroy)
        window = DiagnosticWindow(root, parent=parent,
            options={"host": "127.0.0.1", "port": port, "data_port": 0, "timeout": 0.5},
            reveal=False, complete=complete)
        root.title("BoundFree ESP32 诊断验证")
    else:
        DiagnosticWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
