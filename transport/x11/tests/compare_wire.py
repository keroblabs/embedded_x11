"""Compare defined X11 wire bytes, not host-specific IDs or undefined padding."""

import argparse
import json
import struct
from pathlib import Path


EXPECTED = {
    1: {1}, 2: {8}, 3: {18}, 4: {18}, 5: {3, 14},
    6: {20}, 7: {15}, 8: {16}, 9: {65}, 10: {55},
}


def normalize(path: Path) -> dict:
    raw = path.read_bytes()
    line, position = raw.index(b"\n"), 0
    header = raw[:line].decode("ascii").split()
    if len(header) != 8 or header[0] != "WMBWIRE1" or header[1] not in ("l", "B"):
        raise ValueError(f"{path}: invalid wire journal header")
    endian = "<" if header[1] == "l" else ">"
    root, cmap, visual, base, mask = (int(value, 16) for value in header[2:7])
    sequence = int(header[7])
    position = line + 1
    streams = {ord("T"): bytearray(), ord("R"): bytearray()}
    steps = {ord("T"): 0, ord("R"): 0}
    requests: dict[int, tuple[int, int]] = {}
    records: list[dict] = []
    seen: dict[int, set[int]] = {step: set() for step in EXPECTED}

    def integer(data: bytes | bytearray, offset: int, width: int = 4) -> int:
        return struct.unpack_from(endian + ("I" if width == 4 else "H"), data, offset)[0]

    def resource(value: int) -> int:
        if not value:
            return 0
        if value == root:
            return 0xFFFFFFF0
        if value == cmap:
            return 0xFFFFFFF1
        if value == visual:
            return 0xFFFFFFF2
        return value & mask if value & ~mask == base & ~mask else value

    def ids(data: bytearray, offsets: tuple[int, ...] | list[int]) -> None:
        for offset in offsets:
            struct.pack_into(endian + "I", data, offset, resource(integer(data, offset)))

    def canonical(packet: bytes, opcode: int, reply: bool) -> str:
        data = bytearray(packet)
        if reply:
            data[2:4] = b"\0\0"
            if opcode == 3:
                ids(data, (8, 28))
                data[42:44] = b"\0\0"
            elif opcode == 14:
                ids(data, (8,))
                data[22:32] = bytes(10)
            elif opcode == 20:
                data[20:32] = bytes(12)
                payload = integer(data, 16) * data[1] // 8
                data[32 + payload:] = bytes(len(data) - 32 - payload)
            elif opcode == 15:
                data[1] = 0
                ids(data, (8, 12))
                data[18:32] = bytes(14)
                ids(data, list(range(32, len(data), 4)))
            elif opcode == 16:
                data[1] = 0
                data[12:32] = bytes(20)
        else:
            if opcode in (8, 3, 14, 15, 55):
                data[1] = 0
            if opcode == 1:
                ids(data, (4, 8, 24))
            elif opcode in (8, 3, 14, 20, 15):
                ids(data, (4,))
            elif opcode == 18:
                ids(data, (4,))
                data[17:20] = bytes(3)
                payload = integer(data, 20) * data[16] // 8
                data[24 + payload:] = bytes(len(data) - 24 - payload)
            elif opcode == 65:
                ids(data, (4, 8))
            elif opcode == 55:
                ids(data, (4, 8))
        return data.hex()

    while position < len(raw):
        if len(raw) - position < 6:
            raise ValueError(f"{path}: truncated journal record")
        direction, step = raw[position:position + 2]
        size = struct.unpack_from("<I", raw, position + 2)[0]
        position += 6
        if direction not in streams or size > len(raw) - position:
            raise ValueError(f"{path}: invalid journal record")
        stream = streams[direction]
        if not stream:
            steps[direction] = step
        stream.extend(raw[position:position + size])
        position += size
        while len(stream) >= (4 if direction == ord("T") else 32):
            if direction == ord("T"):
                words = integer(stream, 2, 2)
                if not words:
                    if len(stream) < 8:
                        break
                    words = integer(stream, 4)
                length = words * 4
            else:
                length = 32 + (integer(stream, 4) * 4 if stream[0] in (1, 35) else 0)
            if length < 4 or length > 1024 * 1024:
                raise ValueError(f"{path}: invalid packet size {length}")
            if len(stream) < length:
                break
            packet = bytes(stream[:length])
            del stream[:length]
            if direction == ord("T"):
                sequence += 1
                opcode, packet_step = packet[0], steps[direction]
                requests[sequence & 0xFFFF] = (opcode, packet_step)
                if packet_step in EXPECTED and opcode in EXPECTED[packet_step]:
                    seen[packet_step].add(opcode)
                    records.append({"step": packet_step, "kind": "request", "opcode": opcode,
                                    "bytes": canonical(packet, opcode, False)})
            elif packet[0] in (0, 1):
                packet_sequence = integer(packet, 2, 2)
                if packet_sequence not in requests:
                    raise ValueError(f"{path}: response sequence {packet_sequence} has no request")
                opcode, packet_step = requests[packet_sequence]
                if packet[0] == 0:
                    raise ValueError(f"{path}: unexpected protocol error {packet[1]}")
                if packet_step in EXPECTED and opcode in EXPECTED[packet_step]:
                    records.append({"step": packet_step, "kind": "reply", "opcode": opcode,
                                    "bytes": canonical(packet, opcode, True)})
            if not stream:
                steps[direction] = step

    if any(streams.values()) or seen != EXPECTED:
        raise ValueError(f"{path}: incomplete packet stream or missing scripted calls: {seen}")
    expected_replies = {(5, 3), (5, 14), (6, 20), (7, 15), (8, 16)}
    actual_replies = {(record["step"], record["opcode"]) for record in records
                      if record["kind"] == "reply"}
    if actual_replies != expected_replies:
        raise ValueError(f"{path}: incomplete reply capture: {actual_replies}")
    records.sort(key=lambda item: (item["step"], item["kind"], item["opcode"]))
    return {"version": 1, "byte_order": header[1], "records": records}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--record", type=Path)
    parser.add_argument("--compare", type=Path)
    arguments = parser.parse_args()
    actual = normalize(arguments.capture)
    if arguments.record:
        arguments.record.write_text(json.dumps(actual, indent=2) + "\n", encoding="utf-8")
    if arguments.compare:
        expected = json.loads(arguments.compare.read_text(encoding="utf-8"))
        if actual != expected:
            for left, right in zip(actual["records"], expected["records"]):
                if left != right:
                    raise SystemExit(f"Wire mismatch: step {left['step']} {left['kind']} "
                                     f"opcode {left['opcode']}\nactual={left}\nreference={right}")
            raise SystemExit("Wire capture record counts or metadata differ")
        print("PASS P5 wire: ten stock-Xlib API calls, requests and replies match")


if __name__ == "__main__":
    main()
