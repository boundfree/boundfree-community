import argparse
import sys

from .stdio import run_stdio
from .version import load_version


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="boundfree_sidecar")
    parser.add_argument("--version", action="store_true", help="Print sidecar version.")
    parser.add_argument("--stdio", action="store_true", help="Run JSON line stdio protocol.")
    args = parser.parse_args(argv)

    version = load_version()
    if args.version:
        print(
            f"{version.product_name} {version.version} "
            f"ipc_protocol_version={version.ipc_protocol_version}"
        )
        return 0
    if args.stdio:
        if hasattr(sys.stdin, "reconfigure"):
            sys.stdin.reconfigure(encoding="utf-8")
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", line_buffering=True, write_through=True)
        if hasattr(sys.stderr, "reconfigure"):
            sys.stderr.reconfigure(encoding="utf-8")
        return run_stdio(sys.stdin, sys.stdout)

    parser.print_help()
    return 0
