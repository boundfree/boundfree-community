"""Run with: python -m tools.bfsp_simulator [--help]."""

import argparse
import asyncio

from .server import Faults, SimulatorServer


async def run(args):
    faults = Faults(gap_every=args.fault_gap_every, corrupt_crc_every=args.fault_corrupt_crc_every,
                    disconnect_after=args.fault_disconnect_after, suppress_data=args.fault_suppress_data,
                    suppress_start_response=args.fault_suppress_start_response,
                    delay_start_response_ms=args.fault_delay_start_response_ms,
                    reuse_stream_id=args.fault_reuse_stream_id)
    simulator = SimulatorServer(heartbeat_ms=args.heartbeat_ms, idle_timeout_ms=args.idle_timeout_ms, faults=faults)
    server = await simulator.start(args.host, args.port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server.sockets)
    print(f"BFSP simulator listening on {addresses}", flush=True)
    try:
        async with server:
            await server.serve_forever()
    finally:
        await simulator.close()


def main():
    parser = argparse.ArgumentParser(description="Local BFSP 1.0 synthetic TCP device")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45120)
    parser.add_argument("--heartbeat-ms", type=int, default=1000)
    parser.add_argument("--idle-timeout-ms", type=int, default=5000)
    parser.add_argument("--fault-gap-every", type=int, default=0)
    parser.add_argument("--fault-corrupt-crc-every", type=int, default=0)
    parser.add_argument("--fault-disconnect-after", type=int, default=0)
    parser.add_argument("--fault-suppress-data", action="store_true")
    parser.add_argument("--fault-suppress-start-response", action="store_true")
    parser.add_argument("--fault-delay-start-response-ms", type=int, default=0)
    parser.add_argument("--fault-reuse-stream-id", action="store_true")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or any(getattr(args, key) < 0 for key in (
            "fault_gap_every", "fault_corrupt_crc_every", "fault_disconnect_after", "fault_delay_start_response_ms")):
        parser.error("port or fault value out of range")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
