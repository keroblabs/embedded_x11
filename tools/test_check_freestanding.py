#!/usr/bin/env python3
"""Mutate real cross-linked images to prove P9a verification fails closed."""
import copy
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
from check_freestanding import Elf, FORBIDDEN, check_floor, check_image, check_raw_image

BUILD = Path(sys.argv.pop(1)).resolve()
METADATA = json.loads((BUILD / "p9a-targets.json").read_text())


class FreestandingChecks(unittest.TestCase):
    def setUp(self) -> None:
        self.data = Path(METADATA["whole"]).read_bytes()
        self.elf = Elf(self.data)

    def changed_api(self, word: int, value: int) -> Elf:
        data = bytearray(self.data)
        struct.pack_into("<I", data, self.elf.section(".wmb_api").offset + word * 4, value)
        return Elf(bytes(data))

    def test_real_image_is_valid(self) -> None:
        check_image(self.elf, METADATA)

    def test_missing_and_reordered_api_header(self) -> None:
        for word, value in ((0, 0), (1, 0), (2, METADATA["api_count"] - 1), (3, 0)):
            with self.assertRaisesRegex(ValueError, "ABI header"):
                check_image(self.changed_api(word, value), METADATA)

    def test_missing_invalid_or_non_thumb_function(self) -> None:
        for pointer in (0, 0x20000001, 0x08000200):
            with self.assertRaisesRegex(ValueError, "API slot"):
                check_image(self.changed_api(4, pointer), METADATA)

    def test_section_must_be_readonly_and_at_base(self) -> None:
        for attribute, value in (("flags", 3), ("address", self.elf.section(".wmb_api").address + 4)):
            mutated = copy.deepcopy(self.elf)
            setattr(mutated.section(".wmb_api"), attribute, value)
            with self.assertRaisesRegex(ValueError, "immutable"):
                check_image(mutated, METADATA)

    def test_relocations_unresolved_symbols_and_native_code(self) -> None:
        self.elf.section(".text").type = 9
        with self.assertRaisesRegex(ValueError, "relocations"):
            check_image(self.elf, METADATA)
        self.elf = Elf(self.data)
        symbol = next(symbol for symbol in self.elf.symbols if symbol.name == "Dispatch")
        symbol.section = 0
        with self.assertRaisesRegex(ValueError, "unresolved"):
            check_image(self.elf, METADATA)
        symbol.section = 2
        symbol.name = "pthread_create"
        with self.assertRaisesRegex(ValueError, "forbidden"):
            check_image(self.elf, METADATA)

    def test_rom_and_ram_budgets_are_real_bounds(self) -> None:
        for field in ("rom_bytes", "ram_bytes"):
            metadata = {**METADATA, field: "64"}
            with self.assertRaisesRegex(ValueError, "budget"):
                check_image(self.elf, metadata)

    def test_separate_sdk_binding_and_library_independence(self) -> None:
        data = bytearray(Path(METADATA["application"]).read_bytes())
        elf = Elf(bytes(data))
        struct.pack_into("<I", data, elf.section(".wmb_binding").offset, 0x08001000)
        with self.assertRaisesRegex(ValueError, "address differs"):
            check_image(Elf(bytes(data)), METADATA, True)
        next(symbol for symbol in elf.symbols if symbol.name == "main").name = "XOpenDisplay"
        with self.assertRaisesRegex(ValueError, "boot main|retained Xlib"):
            check_image(elf, METADATA, True)

    def test_floor_does_not_bless_unknown_imports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            floor = Path(directory) / "floor.txt"
            floor.write_text("# reviewed\nmemcpy\n")
            check_floor({"memcpy"}, floor)
            with self.assertRaisesRegex(ValueError, "unreviewed"):
                check_floor({"memcpy", "hidden_syscall"}, floor)

    def test_truncated_or_wrong_architecture_input(self) -> None:
        with self.assertRaisesRegex(ValueError, "truncated"):
            Elf(self.data[:20])
        data = bytearray(self.data)
        data[4] = 2
        with self.assertRaisesRegex(ValueError, "ELF32"):
            Elf(bytes(data))

    def test_raw_image_valid_header_cannot_hide_corrupted_code(self) -> None:
        elf = Elf(Path(METADATA["image"]).read_bytes())
        raw = bytearray(Path(METADATA["raw"]).read_bytes())
        base = int(METADATA["load_base"], 0)
        check_raw_image(bytes(raw), elf, base, len(raw))
        raw[elf.section(".text").address - base] ^= 1
        with self.assertRaisesRegex(ValueError, "payload differs"):
            check_raw_image(bytes(raw), elf, base, len(raw))

    def test_forbidden_family_coverage(self) -> None:
        for name in ("socket", "select", "poll", "fork", "execve", "sigaction",
                     "getenv", "fopen", "dlopen", "pthread_create", "SDL_Init",
                     "__atomic_compare_exchange_8"):
            self.assertIsNotNone(FORBIDDEN.fullmatch(name), name)
        for name in ("memcpy", "strtol", "sqrt", "__aeabi_dmul"):
            self.assertIsNone(FORBIDDEN.fullmatch(name), name)

    def test_actual_linker_enforces_rom_and_ram_budgets(self) -> None:
        original = (BUILD / "generated/p9a/wmb.ld").read_text()
        command = shlex.split((BUILD / "CMakeFiles/wmb_p9a_whole.dir/link.txt").read_text())
        with tempfile.TemporaryDirectory(prefix="wmb-p9a-negative-", dir=BUILD) as directory:
            path = Path(directory)
            for region in ("ROM", "RAM"):
                script = path / (region + ".ld")
                script.write_text(re.sub(r"(" + region + r" \([^\n]+LENGTH = )\S+",
                                         r"\g<1>64", original))
                arguments = command.copy()
                arguments[arguments.index("-o") + 1] = str(path / "invalid.elf")
                arguments = ["-T" + str(script) if argument.startswith("-T") else
                             "-Wl,-Map," + str(path / "invalid.map") if argument.startswith("-Wl,-Map,")
                             else argument for argument in arguments]
                result = subprocess.run(arguments, cwd=BUILD, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stderr, "overflowed|will not fit")

    def test_actual_link_requires_real_regex_floor(self) -> None:
        arguments = shlex.split((BUILD / "CMakeFiles/wmb_p9a_whole.dir/link.txt").read_text())
        with tempfile.TemporaryDirectory(prefix="wmb-p9a-negative-", dir=BUILD) as directory:
            path = Path(directory)
            arguments[arguments.index("-o") + 1] = str(path / "invalid.elf")
            arguments = ["-Wl,-Map," + str(path / "invalid.map") if argument.startswith("-Wl,-Map,")
                         else argument for argument in arguments
                         if not argument.endswith("libwmb_regex.a")]
            result = subprocess.run(arguments, cwd=BUILD, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("undefined reference to `tre_regcomp", result.stderr)


if __name__ == "__main__":
    unittest.main()
