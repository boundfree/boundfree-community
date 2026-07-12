#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
import json


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
