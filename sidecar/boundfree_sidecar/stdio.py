import json
from typing import BinaryIO

from .framing import FRAME_BINARY, FrameError, read_frame, write_frame, write_json_frame
from .protocol import error_response, handle_request


def run_stdio(stdin: BinaryIO, stdout: BinaryIO) -> int:
    while True:
        try:
            frame = read_frame(stdin)
            if frame is None:
                return 0
            if frame.frame_type != 1:
                raise FrameError("expected JSON request frame")
            try:
                request = json.loads(frame.payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                write_json_frame(stdout, error_response(None, "protocol.invalid_json"))
                continue

            binary_payload = None
            if isinstance(request, dict) and request.get("method") == "record_write":
                payload_frame = read_frame(stdin)
                if payload_frame is None or payload_frame.frame_type != FRAME_BINARY:
                    raise FrameError("record_write requires a binary payload frame")
                binary_payload = payload_frame.payload

            response = handle_request(request, binary_payload)
            response_payload = None
            if (
                isinstance(request, dict)
                and request.get("method") == "get_slice"
                and response.get("ok") is True
            ):
                import numpy as np

                result = response["result"]
                data = np.ascontiguousarray(result.pop("data"), dtype="<f4")
                response_payload = data.tobytes(order="C")
                result["dtype"] = "float32-le"
                result["payload_bytes"] = len(response_payload)

            write_json_frame(stdout, response)
            if response_payload is not None:
                write_frame(stdout, FRAME_BINARY, response_payload)
        except FrameError as exc:
            write_json_frame(stdout, error_response(None, "ipc.invalid_frame", {"reason": str(exc)}))
        except Exception:
            write_json_frame(stdout, error_response(None, "internal.unexpected", {"operation": "stdio"}))
