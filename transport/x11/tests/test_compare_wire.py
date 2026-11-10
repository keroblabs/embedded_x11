import json
import struct
import tempfile
import unittest
from pathlib import Path

from compare_wire import normalize


FIXTURE = Path(__file__).resolve().parents[3] / "docs/reference/xlib-wire-golden-16.json"


def journal(reference: dict) -> bytes:
    data = bytearray(b"WMBWIRE1 l fffffff0 fffffff1 fffffff2 0 ffffffff 0\n")
    sequence = 0
    for request in reference["records"]:
        if request["kind"] != "request":
            continue
        sequence += 1
        step, opcode = request["step"], request["opcode"]
        packet = bytes.fromhex(request["bytes"])
        data.extend(bytes((ord("T"), step)) + struct.pack("<I", len(packet)) + packet)
        for reply in reference["records"]:
            if reply["kind"] == "reply" and reply["step"] == step and reply["opcode"] == opcode:
                body = bytearray.fromhex(reply["bytes"])
                struct.pack_into("<H", body, 2, sequence)
                data.extend(bytes((ord("R"), step)) + struct.pack("<I", len(body)) + body)
    return bytes(data)


class WireComparisonTests(unittest.TestCase):
    def setUp(self) -> None:
        self.reference = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "capture.wire"
        self.addCleanup(self.directory.cleanup)

    def test_roundtrip(self) -> None:
        self.path.write_bytes(journal(self.reference))
        self.assertEqual(normalize(self.path), self.reference)

    def test_payload_change_is_not_masked(self) -> None:
        changed = json.loads(json.dumps(self.reference))
        record = next(item for item in changed["records"]
                      if item["step"] == 4 and item["kind"] == "request")
        packet = bytearray.fromhex(record["bytes"])
        packet[-1] ^= 1
        record["bytes"] = packet.hex()
        self.path.write_bytes(journal(changed))
        self.assertNotEqual(normalize(self.path), self.reference)

    def test_only_undefined_padding_is_ignored(self) -> None:
        changed = json.loads(json.dumps(self.reference))
        record = next(item for item in changed["records"]
                      if item["step"] == 5 and item["opcode"] == 3 and item["kind"] == "reply")
        packet = bytearray.fromhex(record["bytes"])
        packet[42:44] = b"\xaa\xbb"
        record["bytes"] = packet.hex()
        self.path.write_bytes(journal(changed))
        self.assertEqual(normalize(self.path), self.reference)

    def test_truncation_is_rejected(self) -> None:
        self.path.write_bytes(journal(self.reference)[:-1])
        with self.assertRaises(ValueError):
            normalize(self.path)


if __name__ == "__main__":
    unittest.main()
