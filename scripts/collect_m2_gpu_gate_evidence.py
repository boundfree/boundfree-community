#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import date
import json
import os
from pathlib import Path
import platform
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APP = ROOT / "native" / "build" / "bf_app"
REQUIRED_CHANNELS = (64, 128, 256)


@dataclass(frozen=True)
class BenchmarkResult:
    channels: int
    fs: int
    seconds: int
    frames: int
    fps: float
    producer_gate: str
    command: str = ""


@dataclass(frozen=True)
class EvidenceMetadata:
    date: str
    environment_label: str
    os_display: str
    gpu_model: str
    driver_version: str
    qt_version: str
    cpu_observation: str
    gpu_observation: str
    scroll_note: str
    zoom_note: str
    channel_visibility_note: str


def parse_benchmark_json(text: str, command: str = "") -> BenchmarkResult:
    payload = json.loads(text)
    return BenchmarkResult(
        channels=int(payload["channels"]),
        fs=int(payload["fs"]),
        seconds=int(payload["seconds"]),
        frames=int(payload["frames"]),
        fps=float(payload["fps"]),
        producer_gate=str(payload.get("gate", "")),
        command=command,
    )


def _scenario_map(results: list[BenchmarkResult]) -> dict[int, BenchmarkResult]:
    return {result.channels: result for result in results}


def _has_stall(metadata: EvidenceMetadata) -> bool:
    notes = " ".join(
        [
            metadata.scroll_note,
            metadata.zoom_note,
            metadata.channel_visibility_note,
        ]
    ).lower()
    return "stall" in notes or "卡顿" in notes


def _missing_required_metadata(metadata: EvidenceMetadata) -> bool:
    required = [
        metadata.date,
        metadata.environment_label,
        metadata.os_display,
        metadata.gpu_model,
        metadata.driver_version,
        metadata.qt_version,
        metadata.cpu_observation,
        metadata.gpu_observation,
        metadata.scroll_note,
        metadata.zoom_note,
        metadata.channel_visibility_note,
    ]
    return any(not value.strip() for value in required)


def _has_unrecorded_utilization(metadata: EvidenceMetadata) -> bool:
    return (
        metadata.cpu_observation.strip().lower() == "not recorded"
        or metadata.gpu_observation.strip().lower() == "not recorded"
    )


def classify_gate(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    development_reasons: list[str],
) -> str:
    if development_reasons:
        return "Open"
    if _missing_required_metadata(metadata):
        return "Open"

    scenarios = _scenario_map(results)
    if any(channel not in scenarios for channel in REQUIRED_CHANNELS):
        return "Blocked"
    if any(
        scenarios[channel].fs != 1000 or scenarios[channel].seconds != 4
        for channel in REQUIRED_CHANNELS
    ):
        return "Blocked"
    if _has_stall(metadata):
        return "Blocked"

    fps_256 = scenarios[256].fps
    if fps_256 < 30.0:
        return "Blocked"
    if fps_256 >= 55.0 and not _has_unrecorded_utilization(metadata):
        return "Passed"
    return "Accepted with note"


def format_markdown_evidence(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    gate: str,
    development_reasons: list[str],
) -> str:
    scenarios = sorted(results, key=lambda result: result.channels)
    lines = [
        f"### {metadata.date} {metadata.environment_label}",
        "",
        f"- Gate: {gate}",
        f"- OS/display: {metadata.os_display}",
        f"- GPU: {metadata.gpu_model}, driver {metadata.driver_version}",
        f"- Qt: {metadata.qt_version}",
        f"- CPU: {metadata.cpu_observation}",
        f"- GPU usage: {metadata.gpu_observation}",
    ]
    if development_reasons:
        lines.append(f"- Development-only reasons: {', '.join(development_reasons)}")
    lines.extend(
        [
            "",
            "| Channels | fs | Window | Frames | FPS |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for result in scenarios:
        lines.append(
            f"| {result.channels} | {result.fs} | {result.seconds} | "
            f"{result.frames} | {result.fps:.2f} |"
        )
    lines.extend(
        [
            "",
            "Interaction notes:",
            "",
            f"- Scroll: {metadata.scroll_note}",
            f"- Zoom: {metadata.zoom_note}",
            f"- Channel visibility: {metadata.channel_visibility_note}",
        ]
    )
    commands = [result.command for result in scenarios if result.command]
    if commands:
        lines.extend(["", "Commands:", ""])
        lines.extend(f"- `{command}`" for command in commands)
    return "\n".join(lines)


def detect_development_reasons(
    env: dict[str, str],
    gpu_model: str = "",
    driver_version: str = "",
) -> list[str]:
    reasons: list[str] = []
    if env.get("QT_QPA_PLATFORM", "").lower() == "offscreen":
        reasons.append("QT_QPA_PLATFORM=offscreen")
    if "microsoft" in platform.uname().release.lower():
        reasons.append("WSL")
    display = env.get("DISPLAY", "")
    wayland = env.get("WAYLAND_DISPLAY", "")
    if display.startswith(":") and not wayland and env.get("XDG_SESSION_TYPE", "").lower() == "xvfb":
        reasons.append("Xvfb")
    if "mesa" in gpu_model.strip().lower() or "software" in driver_version.strip().lower():
        reasons.append("Mesa/software renderer")
    return reasons


def default_metadata(args: argparse.Namespace) -> EvidenceMetadata:
    return EvidenceMetadata(
        date=args.date,
        environment_label=args.environment_label,
        os_display=args.os_display,
        gpu_model=args.gpu_model,
        driver_version=args.driver_version,
        qt_version=args.qt_version,
        cpu_observation=args.cpu_observation,
        gpu_observation=args.gpu_observation,
        scroll_note=args.scroll_note,
        zoom_note=args.zoom_note,
        channel_visibility_note=args.channel_visibility_note,
    )


def command_for(app: Path, channels: int, frames: int) -> list[str]:
    return [
        str(app),
        "--smoke-waveform-benchmark",
        "--channels",
        str(channels),
        "--fs",
        "1000",
        "--seconds",
        "4",
        "--frames",
        str(frames),
        "--json",
    ]


def dry_run_results(app: Path, frames: int) -> list[BenchmarkResult]:
    results = []
    for channels in REQUIRED_CHANNELS:
        command = command_for(app, channels, frames)
        results.append(
            BenchmarkResult(
                channels=channels,
                fs=1000,
                seconds=4,
                frames=frames,
                fps=0.0,
                producer_gate="not-run",
                command=" ".join(command),
            )
        )
    return results


def run_benchmarks(app: Path, frames: int) -> list[BenchmarkResult]:
    if not app.exists():
        raise FileNotFoundError(f"benchmark app not found: {app}")
    results = []
    for channels in REQUIRED_CHANNELS:
        command = command_for(app, channels, frames)
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        results.append(parse_benchmark_json(completed.stdout, " ".join(command)))
    return results


def load_results(path: Path) -> list[BenchmarkResult]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError("--from-json must contain a JSON array")
    results = []
    for item in payload:
        results.append(parse_benchmark_json(json.dumps(item), command=str(item.get("command", ""))))
    return results


def emit_payload(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    gate: str,
    development_reasons: list[str],
) -> dict[str, object]:
    return {
        "gate": gate,
        "development_reasons": development_reasons,
        "results": [asdict(result) for result in sorted(results, key=lambda item: item.channels)],
        "markdown": format_markdown_evidence(results, metadata, gate, development_reasons),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--from-json", type=Path)
    mode.add_argument("--run", action="store_true")
    parser.add_argument("--acceptance-run", action="store_true")
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--date", default=date.today().isoformat())
    parser.add_argument("--environment-label", default="development")
    parser.add_argument("--os-display", default="not recorded")
    parser.add_argument("--gpu-model", default="not recorded")
    parser.add_argument("--driver-version", default="not recorded")
    parser.add_argument("--qt-version", default="not recorded")
    parser.add_argument("--cpu-observation", default="not recorded")
    parser.add_argument("--gpu-observation", default="not recorded")
    parser.add_argument("--scroll-note", default="not recorded")
    parser.add_argument("--zoom-note", default="not recorded")
    parser.add_argument("--channel-visibility-note", default="not recorded")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    metadata = default_metadata(args)
    development_reasons = ["dry-run"]
    if args.acceptance_run:
        development_reasons = detect_development_reasons(
            os.environ,
            args.gpu_model,
            args.driver_version,
        )

    if args.dry_run:
        results = dry_run_results(args.app, args.frames)
        development_reasons = ["dry-run"]
    elif args.from_json is not None:
        results = load_results(args.from_json)
        if args.acceptance_run:
            development_reasons.append("from-json-import")
    else:
        results = run_benchmarks(args.app, args.frames)

    gate = classify_gate(results, metadata, development_reasons)
    print(json.dumps(emit_payload(results, metadata, gate, development_reasons), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
