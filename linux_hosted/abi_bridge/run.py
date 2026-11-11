#!/usr/bin/env python3
"""Verify separate-process SDK applications, copied RPC data and ABI compatibility."""
import argparse
import json
import os
from pathlib import Path
import queue
import re
import resource
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time

MAGIC = 0x57425231
HELLO, STOP, CAPTURE = 0xFFFFFFF0, 0xFFFFFFF1, 0xFFFFFFF2
ROOT = Path(__file__).resolve().parents[2]


class Process:
    def __init__(self, arguments: list[str], environment: dict[str, str]):
        self.process = subprocess.Popen(arguments, env=environment, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
        self.lines: list[str] = []
        self.queue: queue.Queue[str | None] = queue.Queue()
        self.reader = threading.Thread(target=self._read)
        self.reader.start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self.lines.append(line)
            self.queue.put(line)
        self.queue.put(None)

    def expect(self, pattern: str, timeout: float = 20) -> re.Match:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.queue.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty as error:
                raise RuntimeError("Deadline waiting for " + pattern + "\n"
                                   + "".join(self.lines[-30:])) from error
            if line is None:
                raise RuntimeError("Process exited before " + pattern + "\n" + "".join(self.lines[-30:]))
            match = re.search(pattern, line)
            if match:
                return match
        raise RuntimeError("Deadline waiting for " + pattern)

    def finish(self, timeout: float = 25, expected: int = 0) -> str:
        code = self.process.wait(timeout=timeout)
        self.reader.join(timeout=2)
        output = "".join(self.lines)
        if code != expected:
            raise RuntimeError(f"Process exited {code}, expected {expected}\n" + output[-6000:])
        return output

    def cleanup(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.reader.join(timeout=2)
        if self.process.stdout is not None:
            self.process.stdout.close()


def exact(connection: socket.socket, count: int) -> bytes:
    result = bytearray()
    while len(result) < count:
        data = connection.recv(count - len(result))
        if not data:
            raise RuntimeError("Truncated RPC response")
        result.extend(data)
    return bytes(result)


def call(connection: socket.socket, operation: int, sequence: int,
         arguments: dict[int, int] | None = None) -> tuple[int, list[int]]:
    values = [0] * 16
    for index, value in (arguments or {}).items():
        values[index] = value
    body = struct.pack("<iQ16Q17I", 0, 0, *values, *([0] * 17))
    connection.sendall(struct.pack("<4I", MAGIC, len(body) + 16, operation, sequence) + body)
    magic, size, returned, serial = struct.unpack("<4I", exact(connection, 16))
    if magic != MAGIC or not 224 <= size <= 1048576 or (returned, serial) != (operation, sequence):
        raise RuntimeError("Invalid response prefix")
    response = exact(connection, size - 16)
    fields = struct.unpack_from("<iQ16Q17I", response)
    return fields[0], list(fields[2:18])


def control(endpoint: Path) -> socket.socket:
    connection = socket.socket(socket.AF_UNIX)
    connection.settimeout(8)
    connection.connect(str(endpoint))
    pointer, word = struct.calcsize("P"), struct.calcsize("l")
    layout = pointer | (word << 8) | (24 * word << 16)
    status, _ = call(connection, HELLO, 1, {
        0: 0x574D4241, 1: 2, 2: 123, 3: layout,
        4: 1 if struct.pack("=I", 1)[0] else 2,
    })
    if status:
        connection.close()
        raise RuntimeError("Control handshake rejected")
    return connection


def screenshot(path: Path, match: re.Match) -> None:
    values = {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", match[0])}
    if values["parent"] == values["root"] or values["parent"] == values["window"]:
        raise RuntimeError("External window is not actually JWM-decorated")
    data = path.read_bytes()
    header = re.match(rb"P6\n(\d+) (\d+)\n255\n", data)
    if header is None:
        raise RuntimeError("Invalid captured framebuffer")
    width, height = int(header[1]), int(header[2])
    pixels = data[header.end():]
    if len(pixels) != width * height * 3:
        raise RuntimeError("Invalid framebuffer payload size")
    x, y = values["x"], values["y"]
    sample_x, sample_y = x + values["width"] - 12, y + values["height"] - 12
    if not 0 <= sample_x < width or not 0 <= sample_y < height:
        raise RuntimeError("External window is outside the framebuffer")
    position = (sample_y * width + sample_x) * 3
    if pixels[position:position + 3] != b"\0\xff\0":
        raise RuntimeError("The external application's actual green pixels are absent")
    black = sum(
        pixels[(py * width + px) * 3:(py * width + px) * 3 + 3] == b"\0\0\0"
        for py in range(y + 20, min(y + 40, height))
        for px in range(x + 10, min(x + 155, width))
    )
    if black < 20:
        raise RuntimeError("External core-font text was not drawn into the framebuffer")


def protocol_misuse(endpoint: Path) -> None:
    definition = json.loads((ROOT / "transport/api/api.def").read_text())
    slots = {entry["name"]: index for index, entry in enumerate(definition["functions"])}
    with control(endpoint) as connection:
        for sequence, (operation, arguments) in enumerate((
            (slots["XDefaultScreen"], {0: 0xDEADBEEF}),
            (999999, {}),
            (slots["wmbYield"], {0: 1 << 32}),
            (slots["XOpenDisplay"], {0: 2}),
        ), start=2):
            if call(connection, operation, sequence, arguments)[0] == 0:
                raise RuntimeError("Malformed operation/arguments reached Xlib")
        if call(connection, slots["wmbApiRevision"], 999, {})[0] == 0:
            raise RuntimeError("Incorrect request sequence was accepted")
    for magic, length in ((MAGIC ^ 1, 224), (MAGIC, 1048577), (MAGIC, 16)):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(8)
            connection.connect(str(endpoint))
            connection.sendall(struct.pack("<4I", magic, length, HELLO, 1))
            if connection.recv(1):
                raise RuntimeError("Invalid frame prefix was not rejected by disconnect")
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(8)
        connection.connect(str(endpoint))
        if call(connection, HELLO, 1, {0: 0x574D4241, 1: 2, 2: 123, 3: 0, 4: 1})[0] == 0:
            raise RuntimeError("Incompatible C data model was accepted")
    print("PASS protocol: typed/stale handles, operation/sequence, ranges, flags, lengths, magic and data model")


def exercise(args: argparse.Namespace, directory: Path) -> None:
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"}
    endpoint, capture = directory / "rpc.sock", directory / "hello.ppm"
    processes: list[Process] = []
    server = Process([args.server, str(endpoint), "2", str(capture)], environment)
    processes.append(server)
    try:
        server.expect("P8 RPC READY")
        current = Process([args.client, str(endpoint), "1"], environment)
        processes.append(current)
        ready = current.expect(r"P8 HELLO READY: .*")
        with control(endpoint) as connection:
            if call(connection, CAPTURE, 2)[0]:
                raise RuntimeError("Capture request rejected")
        server.expect("P8 RPC CAPTURE READY")
        screenshot(capture, ready)
        current.finish()
        if args.capture:
            shutil.copyfile(capture, args.capture)
        print("PASS external application: ordinary SDK, opaque objects, real JWM frame/text/pixels")
        legacy = Process([args.legacy, str(endpoint), "1"], environment)
        processes.append(legacy)
        legacy.expect(r"P8 HELLO READY: ABI=1 .*")
        legacy.finish()
        print("PASS frozen ABI v1 application against the real v2 service")
        peers = [Process([program, str(endpoint), "300"], environment)
                 for program in (args.client, args.legacy)]
        processes.extend(peers)
        for peer in peers:
            peer.expect(r"P8 HELLO READY: .*iterations=300", timeout=40)
        for peer in peers:
            peer.finish(timeout=40)
        print("PASS E4 RPC: two separate processes / preempting reader threads, 600 exact property cycles")
        protocol_misuse(endpoint)
        with control(endpoint) as connection:
            if call(connection, STOP, 2)[0]:
                raise RuntimeError("Stop request rejected")
        output = server.finish()
        if "ordered shutdown PASS" not in output or "protocol error=" in output:
            raise RuntimeError("RPC host cleanup or WM failed\n" + output[-6000:])
        print("PASS protocol rejection and ordered borrowed-call/GC/socket cleanup")
    finally:
        for process in reversed(processes):
            process.cleanup()

    old_endpoint = directory / "old.sock"
    old = Process([args.server, str(old_endpoint), "1"], environment)
    try:
        old.expect("P8 RPC READY")
        newer = Process([args.client, str(old_endpoint), "1"], environment)
        try:
            output = newer.finish(expected=1)
            if "provider version is too old" not in output or "HELLO READY" in output:
                raise RuntimeError("Newer application did not fail before accessing missing slots")
        finally:
            newer.cleanup()
        with control(old_endpoint) as connection:
            if call(connection, STOP, 2)[0]:
                raise RuntimeError("Old-service stop rejected")
        old.finish()
        print("PASS newer application rejects older insufficient service before display open")
    finally:
        old.cleanup()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--legacy", required=True)
    parser.add_argument("--capture", type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    with tempfile.TemporaryDirectory(prefix="wmb-p8-") as directory:
        exercise(args, Path(directory))


if __name__ == "__main__":
    main()
