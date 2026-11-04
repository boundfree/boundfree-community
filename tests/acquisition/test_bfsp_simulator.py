import asyncio
import copy
import unittest
import uuid

from tools.bfsp_simulator import wire
from tools.bfsp_simulator.server import Faults, SimulatorServer


def uid():
    return str(uuid.uuid4())


class Peer:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer
        self.parser = wire.StreamParser()
        self.sequence = 0
        self.frames = []

    async def send(self, kind, obj):
        data = wire.encode_frame(kind, 0, self.sequence, wire.json_bytes(obj))
        self.sequence += 1
        self.writer.write(data)
        await self.writer.drain()

    async def recv(self, kind, timeout=2):
        async def find():
            while True:
                for i, frame in enumerate(self.frames):
                    if frame.kind == kind:
                        return self.frames.pop(i)
                data = await self.reader.read(65536)
                if not data:
                    raise EOFError
                self.frames.extend(self.parser.feed(data))
        return await asyncio.wait_for(find(), timeout)

    async def hello(self):
        nonce = uid()
        await self.send(wire.HELLO, {"protocol": {"major": 1, "minor": 0}, "client_name": "test",
            "client_version": "test", "receive_limits": {"max_payload": 1048576, "max_json": 65536,
            "max_channels": 256, "max_streams": 16}, "features": ["regular_samples.v1", "events.v1"],
            "required_features": ["regular_samples.v1"], "nonce": nonce})
        ack = wire.parse_json((await self.recv(wire.HELLO_ACK)).payload)
        assert ack["nonce"] == nonce
        return ack

    async def command(self, method, params=None, request_id=None):
        request_id = request_id or uid()
        await self.send(wire.COMMAND, {"request_id": request_id, "method": method, "params": params or {}})
        response = wire.parse_json((await self.recv(wire.RESPONSE)).payload)
        assert response["request_id"] == request_id
        return response

    async def close(self):
        self.writer.close()
        await self.writer.wait_closed()


class WireTests(unittest.TestCase):
    def test_crc_and_fragmented_coalesced_frames(self):
        self.assertEqual(wire.crc32c(b"123456789"), 0xE3069283)
        frame = wire.encode_frame(wire.HEARTBEAT, 0, 2, b"{}")
        # Independently published heartbeat_empty vector from the BFSP plan.
        self.assertEqual(frame.hex(), "4246535001090000180000000200000000000000020000007b7d449fb296")
        parser = wire.StreamParser()
        self.assertEqual(parser.feed(b"noise" + frame[:3]), [])
        self.assertEqual(parser.feed(frame[3:7]), [])
        frames = parser.feed(frame[7:] + frame)
        self.assertEqual([f.payload for f in frames], [b"{}", b"{}"])

    def test_bad_crc_resync_timeout_and_invalid_json(self):
        good = wire.encode_frame(wire.HEARTBEAT, 0, 0, b"{}")
        bad = good[:-1] + bytes([good[-1] ^ 1])
        parser = wire.StreamParser(frame_timeout=1)
        self.assertEqual(parser.feed(bad + good)[0].payload, b"{}")
        parser.feed(good[:12], now=10)
        self.assertEqual(parser.feed(now=12), [])
        self.assertEqual(parser.timeouts, 1)
        for payload in (b'{"x":1,"x":2}', b'{"x":NaN}', b'[]'):
            with self.assertRaises(ValueError):
                wire.parse_json(payload)


class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.simulator = SimulatorServer()
        server = await self.simulator.start(port=0)
        self.port = server.sockets[0].getsockname()[1]
        self.peers = []

    async def asyncTearDown(self):
        for peer in self.peers:
            await peer.close()
        await self.simulator.close()

    async def peer(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        peer = Peer(reader, writer)
        self.peers.append(peer)
        return peer

    async def prepare(self, peer, revision):
        prepare = await peer.command("capture.prepare", {"expected_config_revision": revision})
        self.assertTrue(prepare["ok"])
        capture = prepare["result"]["capture_id"]
        stream = prepare["result"]["stream_ids"][0]
        descriptor_frame = await peer.recv(wire.DESCRIPTOR)
        descriptor = wire.parse_json(descriptor_frame.payload)
        self.assertEqual(descriptor["capture_id"], capture)
        self.assertEqual(descriptor_frame.stream_id, stream)
        accept = await peer.command("stream.accept", {"capture_id": capture,
            "streams": [{"stream_id": stream, "descriptor_revision": revision}]})
        self.assertTrue(accept["ok"])
        return capture, descriptor

    async def test_config_capture_stop_and_dedup(self):
        peer = await self.peer()
        ack = await peer.hello()
        self.assertTrue(ack["ok"])
        config = copy.deepcopy(ack["device"]["capabilities"]["current_config"])
        config["groups"][0]["dtype"] = "float32"
        self.assertTrue((await peer.command("config.validate", {"candidate": config, "expected_revision": 1}))["ok"])
        request_id = uid()
        applied = await peer.command("config.apply", {"candidate": config, "expected_revision": 1}, request_id)
        self.assertEqual(applied["result"]["config_revision"], 2)
        repeated = await peer.command("config.apply", {"candidate": config, "expected_revision": 1}, request_id)
        self.assertEqual(applied, repeated)
        conflict = await peer.command("config.apply", {"candidate": config, "expected_revision": 2}, request_id)
        self.assertEqual(conflict["error"]["code"], "request.conflict")
        capture, descriptor = await self.prepare(peer, 2)
        self.assertEqual(descriptor["dtype"], "float32")
        self.assertTrue((await peer.command("capture.start", {"capture_id": capture}))["ok"])
        data = await peer.recv(wire.DATA)
        fields = wire.DATA_PREFIX.unpack_from(data.payload)
        self.assertEqual((fields[0], fields[1], fields[4]), (2, 4, 2))
        self.assertEqual(fields[6], 0)
        self.assertEqual(len(data.payload), 48 + fields[4] * fields[5] * 4)
        stop = await peer.command("capture.stop", {"capture_id": capture})
        end = wire.parse_json((await peer.recv(wire.STREAM_END)).payload)
        self.assertEqual(stop["result"]["streams"], [end])
        self.assertTrue(end["complete"])
        self.assertGreater(int(end["last_sample_exclusive"]), 0)
        status = await peer.command("device.get_status")
        self.assertEqual(status["result"]["state"], "Idle")

    async def test_invalid_state_config_and_discard(self):
        peer = await self.peer()
        ack = await peer.hello()
        config = copy.deepcopy(ack["device"]["capabilities"]["current_config"])
        bad = await peer.command("config.apply", {"candidate": config, "expected_revision": 99})
        self.assertEqual(bad["error"]["code"], "device.config_conflict")
        prepared = await peer.command("capture.prepare", {"expected_config_revision": 1})
        capture = prepared["result"]["capture_id"]
        await peer.recv(wire.DESCRIPTOR)
        start = await peer.command("capture.start", {"capture_id": capture})
        self.assertEqual(start["error"]["code"], "device.invalid_state")
        self.assertTrue((await peer.command("capture.discard", {"capture_id": capture}))["ok"])

    async def test_start_response_lost_status_and_gap(self):
        self.simulator.faults = Faults(gap_every=2, suppress_start_response=True)
        peer = await self.peer()
        await peer.hello()
        capture, _ = await self.prepare(peer, 1)
        request_id = uid()
        await peer.send(wire.COMMAND, {"request_id": request_id, "method": "capture.start",
                                      "params": {"capture_id": capture}})
        first = wire.DATA_PREFIX.unpack_from((await peer.recv(wire.DATA)).payload)
        second = wire.DATA_PREFIX.unpack_from((await peer.recv(wire.DATA)).payload)
        self.assertEqual(first[6] + first[5] * 2, second[6])
        status = (await peer.command("device.get_status"))["result"]
        self.assertEqual(status["state"], "Streaming")
        self.assertEqual(status["active_capture_id"], capture)
        self.assertEqual(status["last_operation"]["request_id"], request_id)
        self.assertTrue((await peer.command("capture.stop", {"capture_id": capture}))["ok"])

    async def test_one_controller_and_lease_disconnect(self):
        self.simulator.heartbeat_ms, self.simulator.idle_timeout_ms = 100, 300
        first = await self.peer()
        first_ack = await first.hello()
        self.assertTrue(first_ack["ok"])
        capture, _ = await self.prepare(first, 1)
        self.assertTrue((await first.command("capture.start", {"capture_id": capture}))["ok"])
        await first.recv(wire.DATA)
        second = await self.peer()
        self.assertEqual((await second.hello())["error"]["code"], "device.busy")
        await asyncio.sleep(0.5)
        third = await self.peer()
        third_ack = await third.hello()
        self.assertTrue(third_ack["ok"])
        self.assertNotEqual(first_ack["session_id"], third_ack["session_id"])
        self.assertEqual((await third.command("device.get_status"))["result"]["state"], "Idle")

    async def test_three_captures_use_new_streams_and_reject_old_capture(self):
        peer = await self.peer()
        await peer.hello()
        old_capture, old_stream = None, None
        for expected_stream in (1, 2, 3):
            capture, descriptor = await self.prepare(peer, 1)
            self.assertEqual(descriptor["stream_id"], expected_stream)
            self.assertNotEqual(capture, old_capture)
            if old_capture:
                stale = await peer.command("capture.start", {"capture_id": old_capture})
                self.assertEqual(stale["error"]["code"], "device.invalid_state")
            self.assertTrue((await peer.command("capture.start", {"capture_id": capture}))["ok"])
            if old_capture:
                stale = await peer.command("capture.stop", {"capture_id": old_capture})
                self.assertEqual(stale["error"]["code"], "device.invalid_state")
                status = (await peer.command("device.get_status"))["result"]
                self.assertEqual((status["state"], status["active_capture_id"], status["streams"][0]["stream_id"]),
                                 ("Streaming", capture, expected_stream))
            block = await peer.recv(wire.DATA)
            self.assertEqual(block.stream_id, expected_stream)
            self.assertEqual(wire.DATA_PREFIX.unpack_from(block.payload)[6], 0)
            stopped = await peer.command("capture.stop", {"capture_id": capture})
            end_frame = await peer.recv(wire.STREAM_END)
            end = wire.parse_json(end_frame.payload)
            self.assertEqual((end_frame.stream_id, end["stream_id"], end["capture_id"]),
                             (expected_stream, expected_stream, capture))
            self.assertEqual(stopped["result"]["streams"], [end])
            # Any DATA read before END belongs only to the capture just stopped.
            self.assertTrue(all(frame.stream_id == expected_stream for frame in peer.frames if frame.kind == wire.DATA))
            peer.frames = [frame for frame in peer.frames if frame.kind != wire.DATA]
            old_capture, old_stream = capture, expected_stream
        self.assertEqual(old_stream, 3)

    async def test_stream_id_exhaustion_requires_new_session(self):
        peer = await self.peer()
        await peer.hello()
        self.simulator.owner.next_stream_id = 0x100000000
        response = await peer.command("capture.prepare", {"expected_config_revision": 1})
        self.assertEqual(response["error"]["code"], "capability.limit")
        self.assertEqual(response["error"]["params"]["action"], "reconnect")
        status = (await peer.command("device.get_status"))["result"]
        self.assertEqual((status["state"], status["active_capture_id"]), ("Idle", None))


if __name__ == "__main__":
    unittest.main()
