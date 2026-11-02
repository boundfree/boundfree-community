"""Asynchronous, single-controller BFSP simulator for loopback development."""

import asyncio
from collections import OrderedDict
from dataclasses import dataclass
import json
import math
import struct
import time
import uuid

from . import wire


def _uuid():
    return str(uuid.uuid4())


def _valid_uuid(value):
    try:
        return isinstance(value, str) and str(uuid.UUID(value)) == value
    except (ValueError, AttributeError):
        return False


def _u32(value):
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 0xFFFFFFFF


@dataclass
class Faults:
    gap_every: int = 0
    corrupt_crc_every: int = 0
    disconnect_after: int = 0
    suppress_data: bool = False
    suppress_start_response: bool = False
    delay_start_response_ms: int = 0
    reuse_stream_id: bool = False


class CommandError(Exception):
    def __init__(self, code, params=None, retryable=False):
        super().__init__(code)
        self.error = {"code": code, "params": params or {}, "retryable": retryable}


class SimulatorServer:
    def __init__(self, *, heartbeat_ms=1000, idle_timeout_ms=5000, faults=None):
        if not (100 <= heartbeat_ms <= 10000 and heartbeat_ms * 3 <= idle_timeout_ms <= 30000):
            raise ValueError("heartbeat/idle timeout outside supported range")
        self.heartbeat_ms = heartbeat_ms
        self.idle_timeout_ms = idle_timeout_ms
        self.faults = faults or Faults()
        self.boot_id = _uuid()
        self.owner = None
        self.server = None

    async def start(self, host="127.0.0.1", port=45120):
        self.server = await asyncio.start_server(self._accept, host, port, limit=131072)
        return self.server

    async def _accept(self, reader, writer):
        session = Session(self, reader, writer)
        await session.run()

    async def close(self):
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        if self.owner is not None:
            self.owner.writer.close()
            await self.owner.done.wait()


class Session:
    # The byte budget includes the frame currently being written.
    MAX_QUEUED_BYTES = 2 * 1024 * 1024

    def __init__(self, server, reader, writer):
        self.server, self.reader, self.writer = server, reader, writer
        self.done = asyncio.Event()
        self.parser = wire.StreamParser()
        self.session_id = _uuid()
        self.send_sequence = {}
        self.recv_sequence = {}
        self.max_payload, self.max_json, self.max_channels = wire.MAX_PAYLOAD, wire.MAX_JSON, 256
        self.queue = asyncio.PriorityQueue(maxsize=128)
        self.queue_bytes = 0
        self.queue_order = 0
        self.writer_task = None
        self.producer_task = None
        self.background = set()
        self.last_active = time.monotonic()
        self.next_heartbeat = self.last_active + server.heartbeat_ms / 1000
        self.handshake_deadline = self.last_active + 5
        self.hello = False
        self.state = "Idle"
        self.config_revision = 1
        self.config = {"groups": [{"group_id": "analog-0", "enabled_channel_ids": ["ch1", "ch2"],
                                   "rate_num": 250, "rate_den": 1, "dtype": "int24"}], "device_options": {}}
        self.capture_id = None
        self.next_stream_id = 1
        self.stream_id = None
        self.accepted = False
        self.descriptor = None
        self.sample_index = 0
        self.block_count = 0
        self.start_ticks = 0
        self.last_end = None
        self.last_operation = None
        self.completed = OrderedDict()

    async def run(self):
        self.writer_task = asyncio.create_task(self._writer_loop())
        try:
            while not self.writer.is_closing():
                now = time.monotonic()
                if not self.hello and now >= self.handshake_deadline:
                    break
                if self.hello and now - self.last_active >= self.server.idle_timeout_ms / 1000:
                    break
                if self.hello and now >= self.next_heartbeat:
                    self._send_json(wire.HEARTBEAT, 0, {})
                    self.next_heartbeat = now + self.server.heartbeat_ms / 1000
                try:
                    data = await asyncio.wait_for(self.reader.read(65536), timeout=0.1)
                except asyncio.TimeoutError:
                    data = None
                if data == b"":
                    break
                frames = self.parser.feed(data or b"")
                for frame in frames:
                    if not await self._handle_frame(frame):
                        return
        except (wire.FrameError, ConnectionError, OSError, ValueError, asyncio.CancelledError):
            pass
        finally:
            if self.producer_task:
                self.producer_task.cancel()
                await asyncio.gather(self.producer_task, return_exceptions=True)
            for task in self.background:
                task.cancel()
            await asyncio.gather(*self.background, return_exceptions=True)
            if self.writer_task:
                self.writer_task.cancel()
                await asyncio.gather(self.writer_task, return_exceptions=True)
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except (ConnectionError, OSError):
                pass
            if self.server.owner is self:
                self.server.owner = None
            self.done.set()

    async def _writer_loop(self):
        try:
            while True:
                _, _, frame = await self.queue.get()
                try:
                    self.writer.write(frame)
                    await asyncio.wait_for(self.writer.drain(), 2.0)
                finally:
                    self.queue_bytes -= len(frame)
                    self.queue.task_done()
        except (ConnectionError, OSError, asyncio.TimeoutError):
            self.writer.close()

    def _send(self, kind, stream, payload, *, corrupt=False):
        seq = self.send_sequence.get(stream, 0)
        frame = wire.encode_frame(kind, stream, seq, payload, self.max_payload)
        if self.queue.full() or self.queue_bytes + len(frame) > self.MAX_QUEUED_BYTES:
            self.writer.close()
            raise ConnectionError("outbound queue overflow")
        if corrupt:
            frame = frame[:-1] + bytes([frame[-1] ^ 0x80])
        self.send_sequence[stream] = (seq + 1) & 0xFFFFFFFF
        self.queue_order += 1
        self.queue_bytes += len(frame)
        self.queue.put_nowait((1 if kind == wire.DATA else 0, self.queue_order, frame))

    def _send_json(self, kind, stream, obj):
        self._send(kind, stream, wire.json_bytes(obj, self.max_json))

    async def _flush(self):
        await asyncio.wait_for(self.queue.join(), 3.0)

    def _device(self):
        return {"device_id": "boundfree.simulator.tcp", "name": "BFSP TCP simulator",
                "vendor": "BoundFree", "model": "BFSP simulator", "firmware": "1.0",
                "identity_strength": "software", "capabilities": {
                    "can_configure": True, "can_start_stop": True,
                    "simulated": True,
                    "supports_hardware_timestamps": False, "supports_udp": False,
                    "acquisition_control": "start_stop", "current_config": self.config,
                    "effective_config": self.config, "config_revision": self.config_revision,
                    "config_schema": {"schema_version": 1, "groups": [{
                        "group_id": "analog-0", "max_channels": 32,
                        "rate_hz": {"min": 1, "max": 20000},
                        "dtypes": list(wire.DTYPES)}], "device_options": {}}}}

    async def _handle_frame(self, frame):
        if frame.stream_id != 0 or frame.kind not in (wire.HELLO, wire.COMMAND, wire.HEARTBEAT):
            raise wire.FrameError("invalid client message direction")
        expected = self.recv_sequence.get(0, 0)
        if frame.sequence != expected:
            raise wire.FrameError("invalid client sequence")
        self.recv_sequence[0] = (expected + 1) & 0xFFFFFFFF
        obj = wire.parse_json(frame.payload, 4096 if not self.hello else self.max_json)
        self.last_active = time.monotonic()
        if not self.hello:
            if frame.kind != wire.HELLO:
                raise wire.FrameError("HELLO required")
            return await self._hello(obj)
        if frame.kind == wire.HEARTBEAT:
            return True
        if frame.kind != wire.COMMAND:
            raise wire.FrameError("duplicate HELLO")
        return await self._command(obj)

    async def _hello(self, obj):
        nonce = obj.get("nonce")
        protocol = obj.get("protocol")
        limits = obj.get("receive_limits")
        features = obj.get("features")
        required = obj.get("required_features")
        supported = {"regular_samples.v1", "events.v1"}
        valid_limits = (isinstance(limits, dict) and all(_u32(limits.get(k)) and limits[k] > 0
                        for k in ("max_payload", "max_json", "max_channels", "max_streams")))
        error = None
        if not _valid_uuid(nonce) or not isinstance(protocol, dict) or not valid_limits:
            error = "protocol.invalid_payload"
        elif protocol.get("major") != 1 or protocol.get("minor") != 0:
            error = "protocol.version_mismatch"
        elif (not isinstance(features, list) or not isinstance(required, list)
              or any(not isinstance(v, str) for v in features + required)
              or not set(required) <= supported or "regular_samples.v1" not in features):
            error = "capability.unsupported"
        elif self.server.owner is not None:
            error = "device.busy"
        elif limits["max_payload"] < 4096 or limits["max_json"] < 4096:
            error = "capability.limit"
        if error:
            self._send_json(wire.HELLO_ACK, 0, {"ok": False, "nonce": nonce,
                            "error": {"code": error, "params": {}, "retryable": error == "device.busy"}})
            await self._flush()
            return False
        self.max_payload = min(wire.MAX_PAYLOAD, limits["max_payload"])
        self.max_json = min(wire.MAX_JSON, limits["max_json"], self.max_payload)
        self.max_channels = min(32, limits["max_channels"])
        self.server.owner = self
        self.hello = True
        self._send_json(wire.HELLO_ACK, 0, {
            "ok": True, "nonce": nonce, "protocol": {"major": 1, "minor": 0},
            "receive_limits": {"max_payload": wire.MAX_PAYLOAD, "max_json": wire.MAX_JSON,
                               "max_channels": 32, "max_streams": 1},
            "features": sorted(supported.intersection(features)), "device": self._device(),
            "session_id": self.session_id, "boot_id": self.server.boot_id,
            "heartbeat_ms": self.server.heartbeat_ms, "idle_timeout_ms": self.server.idle_timeout_ms})
        return True

    def _check_config(self, candidate, expected):
        if self.state != "Idle":
            raise CommandError("device.invalid_state")
        if not _u32(expected):
            raise CommandError("protocol.invalid_payload")
        if expected != self.config_revision:
            raise CommandError("device.config_conflict", {"config_revision": self.config_revision})
        if not isinstance(candidate, dict) or set(candidate) != {"groups", "device_options"}:
            raise CommandError("protocol.invalid_payload")
        groups, options = candidate["groups"], candidate["device_options"]
        if not isinstance(groups, list) or len(groups) != 1 or options != {}:
            raise CommandError("capability.unsupported")
        group = groups[0]
        keys = {"group_id", "enabled_channel_ids", "rate_num", "rate_den", "dtype"}
        if not isinstance(group, dict) or set(group) != keys or group["group_id"] != "analog-0":
            raise CommandError("protocol.invalid_payload")
        channels = group["enabled_channel_ids"]
        if (not isinstance(channels, list) or not 1 <= len(channels) <= self.max_channels
                or any(not isinstance(ch, str) or ch not in {f"ch{i}" for i in range(1, 33)} for ch in channels)
                or len(set(channels)) != len(channels)):
            raise CommandError("capability.limit")
        num, den, dtype = group["rate_num"], group["rate_den"], group["dtype"]
        if not _u32(num) or not _u32(den) or not num or not den or not 1 <= num / den <= 20000:
            raise CommandError("capability.limit")
        if not isinstance(dtype, str) or dtype not in wire.DTYPES:
            raise CommandError("capability.unsupported")
        if 48 + len(channels) * wire.DTYPES[dtype][1] > self.max_payload:
            raise CommandError("capability.limit")
        return json.loads(json.dumps(candidate))

    def _make_descriptor(self):
        group = self.config["groups"][0]
        if len(group["enabled_channel_ids"]) > self.max_channels:
            raise CommandError("capability.limit")
        width = wire.DTYPES[group["dtype"]][1]
        max_block = min(4096, (self.max_payload - 48) // (len(group["enabled_channel_ids"]) * width))
        descriptor = {"session_id": self.session_id, "capture_id": self.capture_id, "stream_id": self.stream_id,
            "descriptor_revision": self.config_revision, "name": "Synthetic voltage",
            "signal_type": "voltage", "rate_num": group["rate_num"], "rate_den": group["rate_den"],
            "dtype": group["dtype"], "layout": "channel_major", "sample_index_source": "device",
            "channels": [{"channel_id": ch, "name": ch.upper(), "signal_type": "voltage",
                "unit": "uV", "scale": 0.1 if group["dtype"].startswith("int") else 1,
                "offset": 0} for ch in group["enabled_channel_ids"]],
            "clock": {"clock_id": 1, "ticks_per_second_num": "1000000000", "ticks_per_second_den": "1",
                      "source": "host_monotonic", "epoch": "boot_relative", "simulated": True},
            "max_samples_per_block": max_block}
        try:
            wire.json_bytes(descriptor, self.max_json)
        except ValueError as exc:
            raise CommandError("capability.limit") from exc
        return descriptor

    def _status(self):
        streams = [] if self.capture_id is None else [{"stream_id": self.stream_id,
            "descriptor_revision": self.config_revision, "last_sample_exclusive": str(self.sample_index)}]
        return {"state": self.state, "active_capture_id": self.capture_id,
                "config_revision": self.config_revision, "streams": streams,
                "last_operation": self.last_operation}

    def _require_capture(self, params, state):
        if self.state not in state or not self.capture_id or params.get("capture_id") != self.capture_id:
            raise CommandError("device.invalid_state")

    async def _execute(self, method, params):
        if method == "device.get_info":
            return self._device(), []
        if method == "device.get_status":
            return self._status(), []
        if method in ("config.validate", "config.apply"):
            config = self._check_config(params.get("candidate"), params.get("expected_revision"))
            if method == "config.validate":
                return {"effective_config": config, "errors": [], "warnings": []}, []
            self.config = config
            self.config_revision += 1
            return {"effective_config": self.config, "config_revision": self.config_revision}, []
        if method == "capture.prepare":
            if self.state != "Idle":
                raise CommandError("device.invalid_state")
            if params.get("expected_config_revision") != self.config_revision:
                raise CommandError("device.config_conflict", {"config_revision": self.config_revision})
            if self.next_stream_id > 0xFFFFFFFF:
                raise CommandError("capability.limit", {"resource": "stream_id", "action": "reconnect"})
            self.capture_id = _uuid()
            # Deliberately nonconforming mode is used only by the client adversarial test.
            self.stream_id = 1 if self.server.faults.reuse_stream_id else self.next_stream_id
            try:
                self.descriptor = self._make_descriptor()
            except CommandError:
                self.capture_id, self.stream_id = None, None
                raise
            self.next_stream_id += 1
            self.state, self.accepted, self.sample_index, self.block_count = "Prepared", False, 0, 0
            return {"capture_id": self.capture_id, "stream_ids": [self.stream_id]}, [(wire.DESCRIPTOR, self.stream_id, self.descriptor)]
        if method == "stream.accept":
            self._require_capture(params, {"Prepared"})
            if params.get("streams") != [{"stream_id": self.stream_id, "descriptor_revision": self.config_revision}]:
                raise CommandError("protocol.invalid_payload")
            self.accepted = True
            return {"accepted": True}, []
        if method == "capture.start":
            self._require_capture(params, {"Prepared"})
            if not self.accepted:
                raise CommandError("device.invalid_state")
            self.state = "Streaming"
            self.start_ticks = time.monotonic_ns()
            self.producer_task = asyncio.create_task(self._produce())
            return {"state": "Streaming", "capture_id": self.capture_id}, []
        if method == "capture.stop":
            if self.state == "Idle" and self.last_end and params.get("capture_id") == self.last_end["capture_id"]:
                return {"state": "Idle", "capture_id": self.last_end["capture_id"], "streams": [self.last_end]}, []
            self._require_capture(params, {"Starting", "Streaming", "Stopping"})
            self.state = "Stopping"
            if self.producer_task:
                self.producer_task.cancel()
                await asyncio.gather(self.producer_task, return_exceptions=True)
                self.producer_task = None
            # Drain all DATA before END even though normal control frames have priority.
            await self._flush()
            self.last_end = {"capture_id": self.capture_id, "stream_id": self.stream_id,
                             "last_sample_exclusive": str(self.sample_index), "reason": "normal", "complete": True}
            end = self.last_end
            self._send_json(wire.STREAM_END, self.stream_id, end)
            self.send_sequence.pop(self.stream_id, None)
            self.state, self.capture_id, self.stream_id, self.descriptor, self.accepted = "Idle", None, None, None, False
            return {"state": "Idle", "capture_id": end["capture_id"], "streams": [end]}, []
        if method == "capture.discard":
            self._require_capture(params, {"Prepared"})
            self.send_sequence.pop(self.stream_id, None)
            self.state, self.capture_id, self.stream_id, self.descriptor, self.accepted = "Idle", None, None, None, False
            return {"state": "Idle"}, []
        if method == "time.sync":
            if params.get("clock_id") != 1 or not isinstance(params.get("client_send_ns"), str):
                raise CommandError("protocol.invalid_payload")
            received = str(time.monotonic_ns())
            return {"clock_id": 1, "client_send_ns": params["client_send_ns"],
                    "receive_ticks": received, "send_ticks": str(time.monotonic_ns())}, []
        if method == "device.disconnect":
            if self.state != "Idle":
                raise CommandError("device.busy")
            return {"state": "Idle"}, []
        raise CommandError("capability.unsupported", {"method": method})

    async def _command(self, obj):
        request_id, method, params = obj.get("request_id"), obj.get("method"), obj.get("params")
        if not _valid_uuid(request_id) or not isinstance(method, str) or not isinstance(params, dict):
            raise wire.FrameError("invalid command")
        mutating = method not in ("device.get_info", "device.get_status", "time.sync")
        now = time.monotonic()
        while self.completed and now - next(iter(self.completed.values()))[0] > 120:
            self.completed.popitem(last=False)
        canonical = json.dumps({"method": method, "params": params}, sort_keys=True, separators=(",", ":"))
        previous = self.completed.get(request_id)
        if previous:
            if previous[1] != canonical:
                response = {"request_id": request_id, "ok": False,
                            "error": {"code": "request.conflict", "params": {}, "retryable": False}}
            else:
                response = previous[2]
            self._send_json(wire.RESPONSE, 0, response)
            return True
        try:
            result, followups = await self._execute(method, params)
            response = {"request_id": request_id, "ok": True, "result": result}
        except CommandError as exc:
            response = {"request_id": request_id, "ok": False, "error": exc.error}
            followups = []
        if mutating:
            self.last_operation = {"request_id": request_id, "method": method,
                                   "state": "completed" if response["ok"] else "failed",
                                   "result": response.get("result"), "error": response.get("error")}
            self.completed[request_id] = (now, canonical, response)
            while len(self.completed) > 256:
                self.completed.popitem(last=False)
        faults = self.server.faults
        if method == "capture.start" and response["ok"] and faults.suppress_start_response:
            pass
        elif method == "capture.start" and response["ok"] and faults.delay_start_response_ms:
            task = asyncio.create_task(self._delayed_response(response, faults.delay_start_response_ms / 1000))
            self.background.add(task)
            task.add_done_callback(self.background.discard)
        else:
            self._send_json(wire.RESPONSE, 0, response)
        for kind, stream, value in followups:
            self._send_json(kind, stream, value)
        if method == "device.disconnect" and response["ok"]:
            await self._flush()
            return False
        return True

    async def _delayed_response(self, response, delay):
        await asyncio.sleep(delay)
        self._send_json(wire.RESPONSE, 0, response)

    async def _produce(self):
        group = self.config["groups"][0]
        stream_id = self.descriptor["stream_id"]
        rate = group["rate_num"] / group["rate_den"]
        count = min(self.descriptor["max_samples_per_block"], max(1, round(rate * 0.02)))
        width_code, _ = wire.DTYPES[group["dtype"]]
        channels = len(group["enabled_channel_ids"])
        start = time.monotonic()
        try:
            while True:
                target = start + (self.block_count + 1) * count / rate
                await asyncio.sleep(max(0, target - time.monotonic()))
                self.block_count += 1
                if self.server.faults.gap_every and self.block_count % self.server.faults.gap_every == 0:
                    self.sample_index += count
                    continue
                first = self.sample_index
                ticks = self.start_ticks + first * 1_000_000_000 * group["rate_den"] // group["rate_num"]
                samples = bytearray()
                for ch in range(channels):
                    for index in range(first, first + count):
                        value = math.sin(2 * math.pi * (10 * index / rate + ch * 0.1))
                        if group["dtype"] == "int24":
                            samples.extend((round(value * 1000) & 0xFFFFFF).to_bytes(3, "little"))
                        elif group["dtype"] == "int16":
                            samples.extend(struct.pack("<h", round(value * 1000)))
                        elif group["dtype"] == "int32":
                            samples.extend(struct.pack("<i", round(value * 1000)))
                        elif group["dtype"] == "float32":
                            samples.extend(struct.pack("<f", value * 100))
                        else:
                            samples.extend(struct.pack("<d", value * 100))
                payload = wire.DATA_PREFIX.pack(self.config_revision, width_code, 0, 1, channels, count,
                                                first, ticks, 1, 0, 0) + samples
                self.sample_index += count
                if not self.server.faults.suppress_data:
                    self._send(wire.DATA, stream_id, payload, corrupt=(self.server.faults.corrupt_crc_every > 0 and
                               self.block_count % self.server.faults.corrupt_crc_every == 0))
                if self.server.faults.disconnect_after and self.block_count >= self.server.faults.disconnect_after:
                    self.writer.close()
                    return
        except (ConnectionError, OSError):
            self.writer.close()
