import json
from typing import TextIO

from .protocol import error_response, handle_request


def run_stdio(stdin: TextIO, stdout: TextIO) -> int:
    for line in stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
            response = handle_request(request)
        except json.JSONDecodeError:
            response = error_response(None, "protocol.invalid_json")
        except Exception:  # noqa: BLE001 - stdio must return a protocol envelope.
            response = error_response(None, "internal.unexpected", {"operation": "stdio"})

        stdout.write(json.dumps(response, ensure_ascii=False, separators=(",", ":")))
        stdout.write("\n")
        stdout.flush()
    return 0
