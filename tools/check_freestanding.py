#!/usr/bin/env python3
"""Verify the actual P9a ARM objects, library floor, ELF layout and SDK binding."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FLOOR = ROOT / "docs/floor-symbols.txt"
FORBIDDEN = re.compile(
    r"^(?:socket\w*|bind|listen|accept4?|connect|select|pselect\w*|poll|ppoll|"
    r"epoll_\w+|fork|vfork|exec\w*|posix_spawn\w*|waitpid|sigaction|signal|"
    r"sigprocmask|setitimer|getenv|setenv|unsetenv|fopen|fdopen|popen|"
    r"open|close|read|write|lseek|ioctl|fcntl|getpid|getuid|gettimeofday|"
    r"clock_gettime|time|dlopen|dlsym|pthread_\w+|xcb_\w+|SDL_\w+|"
    r"__atomic_\w+|__sync_\w+)$"
)
REQUIRED_FLAGS = {"-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-common", "-fno-stack-protector", "-mcpu=cortex-m3",
                  "-mthumb", "-mfloat-abi=soft"}
LINKER_CONTRACTS = {"__heap_start", "__heap_end", "wmb_app_table", "wmb_app_table_size"}


def run(arguments: list[str], cwd: Path = ROOT) -> str:
    result = subprocess.run(arguments, cwd=cwd, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(shlex.join(arguments) + "\n" + result.stderr[-5000:])
    return result.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def number(value: str | int) -> int:
    return int(value, 0) if isinstance(value, str) else value


@dataclass
class Section:
    name: str
    type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    info: int
    section: int


class Elf:
    def __init__(self, data: bytes):
        require(len(data) >= 52, "truncated ELF header")
        header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
        require(header[0][:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32")
        require(header[1] == 2 and header[2] == 40, "expected an ARM executable")
        require(header[7] & 0xFF000000 == 0x05000000 and not header[7] & 0x400,
                "expected ARM EABI5 soft-float")
        require(header[11] == 40 and header[9] == 32, "unexpected ELF table layout")
        self.data, self.entry = data, header[4]
        self.sections: list[Section] = []
        self.symbols: list[Symbol] = []
        self.segments = []
        raw = []
        for index in range(header[12]):
            offset = header[6] + index * 40
            require(offset + 40 <= len(data), "truncated section table")
            raw.append(struct.unpack_from("<10I", data, offset))
        require(header[13] < len(raw), "invalid section-name table")
        names = self.slice(raw[header[13]][4], raw[header[13]][5])
        for row in raw:
            self.sections.append(Section(self.string(names, row[0]), *row[1:]))
        for section in self.sections:
            if section.type != 2:
                continue
            require(section.entry_size == 16 and section.link < len(self.sections),
                    "invalid symbol table")
            strings = self.contents(self.sections[section.link])
            require(section.size % 16 == 0, "truncated symbol entry")
            for offset in range(0, section.size, 16):
                name, value, size, info, _, owner = struct.unpack_from(
                    "<IIIBBH", self.contents(section), offset)
                self.symbols.append(Symbol(self.string(strings, name), value, size, info, owner))
        for index in range(header[10]):
            offset = header[5] + index * 32
            require(offset + 32 <= len(data), "truncated program table")
            self.segments.append(struct.unpack_from("<8I", data, offset))

    def slice(self, offset: int, size: int) -> bytes:
        require(0 <= offset <= len(self.data) and size <= len(self.data) - offset,
                "ELF range exceeds file")
        return self.data[offset:offset + size]

    @staticmethod
    def string(strings: bytes, offset: int) -> str:
        require(offset < len(strings), "invalid ELF string")
        end = strings.find(b"\0", offset)
        require(end >= 0, "unterminated ELF string")
        return strings[offset:end].decode("ascii")

    def contents(self, section: Section) -> bytes:
        return self.slice(section.offset, section.size)

    def section(self, name: str) -> Section:
        found = [section for section in self.sections if section.name == name]
        require(len(found) == 1, "expected exactly one section " + name)
        return found[0]


def check_image(elf: Elf, metadata: dict, application: bool = False) -> dict:
    require(not any(section.type in (4, 6, 9, 11) and section.size
                    for section in elf.sections), "relocations/dynamic linkage are forbidden")
    require(not any(segment[0] in (2, 3) for segment in elf.segments),
            "dynamic loader/interpreter is forbidden")
    require(not any(symbol.name and symbol.section == 0 for symbol in elf.symbols),
            "unresolved ELF symbol")
    violations = sorted({symbol.name for symbol in elf.symbols if FORBIDDEN.fullmatch(symbol.name)})
    require(not violations, "forbidden runtime symbols: " + ", ".join(violations))
    require(len([symbol for symbol in elf.symbols if symbol.name == "main" and symbol.section]) == 1,
            "expected one boot main")
    code = [section for section in elf.sections if section.flags & 4]
    require(any(section.address <= (elf.entry & ~1) < section.address + section.size
                for section in code), "entry is outside executable code")
    require(bool(elf.entry & 1), "reference entry must be Thumb")
    load_base = number(metadata["load_base"])
    if application:
        binding = elf.section(".wmb_binding")
        require(binding.size == 4 and struct.unpack("<I", elf.contents(binding))[0] == load_base,
                "SDK application address differs from provider")
        leaked = {"XOpenDisplay", "_XReply", "Dispatch", "wmb_gateway_submit"}
        require(not leaked & {symbol.name for symbol in elf.symbols},
                "external application contains retained Xlib or server code")
    else:
        api = elf.section(".wmb_api")
        require(api.address == load_base and api.flags == 2,
                "API must be immutable and first at the configured load base")
        count = metadata["api_count"]
        require(api.size == 16 + count * 4, "incomplete or duplicated API table")
        words = struct.unpack("<" + "I" * (4 + count), elf.contents(api))
        require(words[:4] == (number(metadata["api_magic"]), metadata["api_version"],
                             count, 4 | (4 << 8) | (96 << 16)), "wrong typed ARM ABI header")
        require(all(pointer & 1 and any(section.address <= (pointer & ~1) <
                    section.address + section.size for section in code) for pointer in words[4:]),
                "missing or non-code typed API slot")
        require(min(section.address for section in elf.sections
                    if section.flags & 2 and section.size) == load_base,
                "allocated data precedes the API")
    rom_begin = number(metadata["app_base"] if application else metadata["load_base"])
    rom_end = rom_begin + number(metadata["rom_bytes"])
    ram_begin = number(metadata["app_ram_base"] if application else metadata["ram_base"])
    ram_end = ram_begin + number(metadata["app_ram_bytes"] if application else metadata["ram_bytes"])
    rom_high, ram_high = rom_begin, ram_begin
    for kind, _, virtual, physical, files, memory, flags, _ in elf.segments:
        if kind != 1:
            continue
        if files:
            require(rom_begin <= physical and physical + files <= rom_end,
                    "load image exceeds ROM budget")
            rom_high = max(rom_high, physical + files)
        if flags & 2 and memory:
            require(ram_begin <= virtual and virtual + memory <= ram_end,
                    "allocated memory exceeds RAM budget")
            ram_high = max(ram_high, virtual + memory)
    return {"rom_used": rom_high - rom_begin, "ram_reserved": ram_high - ram_begin,
            "file_bytes": len(elf.data), "sha256": hashlib.sha256(elf.data).hexdigest()}


def archive_symbols(nm: str, paths: list[str]) -> tuple[set[str], set[str]]:
    definitions, requirements = set(), set()
    for path in paths:
        for line in run([nm, "-g", path]).splitlines():
            fields = line.split()
            if len(fields) >= 2 and len(fields[-2]) == 1:
                kind, name = fields[-2:]
                if kind in ("U", "w", "v"):
                    requirements.add(name)
                elif kind.isalpha():
                    definitions.add(name)
    return definitions, requirements


def library_floor(metadata: dict) -> set[str]:
    definitions, imports = archive_symbols(metadata["nm"], metadata["archives"])
    required = imports - definitions - LINKER_CONTRACTS
    require(not any(FORBIDDEN.fullmatch(symbol) for symbol in required),
            "forbidden import at the complete-archive boundary")
    providers, _ = archive_symbols(metadata["nm"], list(metadata["libraries"].values()))
    require(required <= providers, "library floor lacks providers: " + str(sorted(required - providers)))
    return required


def check_floor(required: set[str], floor: Path = FLOOR) -> None:
    allowed = {line for line in floor.read_text().splitlines() if line and not line.startswith("#")}
    require(required <= allowed, "unreviewed target floor: " + ", ".join(sorted(required - allowed)))


def compiler_entries(build: Path) -> list[dict]:
    return json.loads((build / "compile_commands.json").read_text())


def check_compilation(build: Path, metadata: dict) -> int:
    entries = compiler_entries(build)
    selected: dict[str, set[str]] = {}
    compiler = Path(metadata["compiler"]).resolve()
    prefix = compiler.parents[1]
    for entry in entries:
        arguments = shlex.split(entry["command"])
        require(Path(arguments[0]).resolve() == compiler, "host compiler leaked into target cohorts")
        require(REQUIRED_FLAGS <= set(arguments), "missing freestanding/ABI compile flags: " + entry["file"])
        require(not any(argument in ("-fPIC", "-fpic", "-fPIE", "-fpie", "-flto") or
                        argument.startswith(("-flto=", "-Dlinux", "-D__linux", "-D__arm32__"))
                        for argument in arguments), "PIC/LTO or spoofed platform macro in target")
        source = Path(entry["file"]).resolve()
        require(not source.is_relative_to(ROOT / "linux_hosted"),
                "host backend or scaffold compiled into target")
        if any(f"/{name}.dir/" in entry["command"] for name in
               ("wmb_xlib", "wmb_jwm", "wmb_p9a_app", "wmb_app_wrapper", "wmb_app_wrapper_v1")):
            require(not any("_XSERVER64" in argument or "/generated/tinyx" in argument or
                            argument.startswith("-DWMB_DEVEL") for argument in arguments),
                    "server ABI leaked into clients/SDK")
        target_match = re.search(r"/([^/]+)\.dir/", entry["command"])
        if target_match is None:
            raise ValueError("cannot identify target cohort")
        target = target_match[1]
        if source.is_relative_to(ROOT):
            relative = source.relative_to(ROOT).as_posix()
        else:
            relative = str(source)
        if target == "wmb_xfont" and "/src/" in str(source) and not source.is_relative_to(ROOT / "tinyx"):
            relative = "libXfont/src/" + str(source).rsplit("/src/", 1)[1]
        selected.setdefault(target, set()).add(relative)
        output = Path(arguments[arguments.index("-o") + 1])
        if not output.is_absolute():
            output = Path(entry["directory"]) / output
        dependency = Path(str(output) + ".d")
        require(output.exists() and dependency.exists(), "missing actual object/dependency evidence")
        with output.open("rb") as stream:
            object_header = stream.read(20)
        require(object_header[:7] == b"\x7fELF\x01\x01\x01" and
                struct.unpack_from("<HH", object_header, 16) == (1, 40),
                "compiled object is not an actual ARM ELF32 relocatable: " + str(output))
        listing = shlex.split(dependency.read_text().replace("\\\n", " "))
        for name in listing[1:]:
            path = Path(name)
            if not path.is_absolute():
                path = Path(entry["directory"]) / path
            resolved = path.resolve()
            require(resolved.is_relative_to(ROOT) or resolved.is_relative_to(prefix),
                    "host/system header escaped the cross sysroot: " + str(resolved))
            require(resolved.stat().st_mtime_ns <= output.stat().st_mtime_ns,
                    "compiled object is stale; rebuild after changing " + str(resolved))
    frozen = (
        (("wmb_tinyx", "wmb_kdrive"), "tinyx"),
        (("wmb_xlib",), "xlib"), (("wmb_xfont",), "xfont"), (("wmb_jwm",), "jwm")
    )
    for targets, name in frozen:
        expected = {line for line in (ROOT / f"docs/reference/{name}-sources.reference").read_text().splitlines()
                    if line and not line.startswith("#")}
        actual = set().union(*(selected.get(target, set()) for target in targets))
        require(actual == expected, f"freestanding {name} cohort differs from hosted source inventory")
    require(len(selected.get("wmb_zlib", set())) == 6 and len(selected.get("wmb_regex", set())) == 10,
            "missing real dependency compilation")
    require("wmb_makekeys" not in selected, "target executable used for a host build tool")
    return len(entries)


def check_raw_image(raw: bytes, elf: Elf, load_base: int, expected_size: int) -> None:
    require(len(raw) == expected_size, "raw load-image span differs from ELF load segments")
    api = elf.section(".wmb_api")
    require(raw[:api.size] == elf.contents(api), "raw image does not begin with the immutable API")
    for section in elf.sections:
        if not section.flags & 2 or not section.size or section.type == 8:
            continue
        location = None
        for kind, _, virtual, physical, files, _, _, _ in elf.segments:
            if kind == 1 and virtual <= section.address and section.address + section.size <= virtual + files:
                location = physical + section.address - virtual - load_base
                break
        if location is None or location < 0:
            raise ValueError("file-backed section is outside the raw load image")
        require(raw[location:location + section.size] == elf.contents(section),
                "raw image payload differs from ELF section " + section.name)


def verify(build: Path, write_floor: bool = False) -> dict:
    metadata = json.loads((build / "p9a-targets.json").read_text())
    schema = json.loads((ROOT / "transport/api/api.def").read_text())
    require((number(metadata["api_magic"]), metadata["api_version"], metadata["api_count"]) ==
            (number(schema["magic"]), schema["version"], len(schema["functions"])),
            "target metadata differs from the authoritative ABI schema")
    for space in ("rom", "ram"):
        first = number(metadata["load_base"] if space == "rom" else metadata["ram_base"])
        second = number(metadata["app_base"] if space == "rom" else metadata["app_ram_base"])
        length = number(metadata[space + "_bytes"])
        other_length = length if space == "rom" else number(metadata["app_ram_bytes"])
        require(first + length <= second or second + other_length <= first,
                "provider and SDK application " + space + " regions overlap")
    required = library_floor(metadata)
    if write_floor:
        FLOOR.write_text(
            "# P9a Cortex-M3/Thumb ILP32, Arm GNU 14.2, full newlib/libm/libgcc.\n"
            "# Generated from all complete selected archives, not an already-resolved ELF.\n"
            "# Exact providers and image facts are emitted in the build verification report.\n"
            "# Review before changing this allowed floor; never regenerate to bypass a failure.\n"
            + "".join(symbol + "\n" for symbol in sorted(required))
        )
    check_floor(required)
    count = check_compilation(build, metadata)
    results = {}
    for name in ("image", "whole", "application"):
        elf = Elf(Path(metadata[name]).read_bytes())
        results[name] = check_image(elf, metadata, name == "application")
        map_text = Path(metadata[name + "_map"]).read_text()
        require(not any(token in map_text for token in
                        ("libnosys", "librdimon", "libpthread", "libSDL", "libstdc++", ".so")),
                "unsupported runtime or host library linked into " + name)
        if name == "whole":
            discarded = map_text.split("Discarded input sections", 1)[-1].split("Memory Configuration", 1)[0]
            kinds = re.findall(r"^ (\S+)\s", discarded, re.MULTILINE)
            require(all(kind == ".comment" or kind.startswith(".note") for kind in kinds),
                    "section GC hid complete-archive requirements")
            definitions, _ = archive_symbols(metadata["nm"], metadata["archives"])
            require(definitions <= {symbol.name for symbol in elf.symbols if symbol.section},
                    "whole-archive definitions were omitted from the image")
            command = shlex.split((build / "CMakeFiles/wmb_p9a_whole.dir/link.txt").read_text())
            require("-Wl,--no-gc-sections" in command and "-Wl,--whole-archive" in command and
                    "-flto" not in command and "-Wl,--gc-sections" not in command,
                    "whole-image link is not an actual no-GC/no-LTO archive proof")
    raw = Path(metadata["raw"]).read_bytes()
    api = Elf(Path(metadata["image"]).read_bytes())
    check_raw_image(raw, api, number(metadata["load_base"]), results["image"]["rom_used"])
    sdk = Path(metadata["sdk"])
    require(sdk.stat().st_size < 16384, "actual target SDK archive exceeds 16 KiB")
    definitions, imports = archive_symbols(metadata["nm"], [str(sdk)])
    require(definitions == {"wmb_bind"} | {f"wba{index}" for index in range(metadata["api_count"])},
            "unexpected SDK exports")
    require(imports <= {"abort", "fprintf", "fputs", "stderr", "_impure_ptr", "memcpy"},
            "SDK acquired unexpected dependencies")
    report = {
        "scope": "P9a compile/link proof only; platform stubs are deliberately non-runnable, not P9b",
        "compiler": run([metadata["compiler"], "--version"]).splitlines()[0],
        "verified_at_utc": datetime.now(timezone.utc).isoformat(),
        "translation_units": count, "complete_archives": len(metadata["archives"]),
        "floor_requirements": sorted(required), "sdk_bytes": sdk.stat().st_size,
        "api_address": metadata["load_base"], "api_slots": metadata["api_count"],
        "api_bytes": 16 + metadata["api_count"] * 4, "images": results,
    }
    (build / "p9a-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS P9a: {count} real ARM translation units, {len(metadata['archives'])} complete archives")
    print(f"PASS reviewed library/compiler floor: {len(required)} requirements; no OS imports or libnosys")
    print(f"PASS immutable {report['api_bytes']}-byte ABI at {metadata['load_base']}, separate SDK binding, "
          f"raw offset zero, no unresolved symbols/relocations; SDK={sdk.stat().st_size} bytes")
    for name, result in results.items():
        print(f"PASS {name}: ROM={result['rom_used']} RAM-reserved={result['ram_reserved']} bytes")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--write-floor", action="store_true")
    args = parser.parse_args()
    try:
        verify(args.build_dir.resolve(), args.write_floor)
    except (OSError, ValueError, RuntimeError, struct.error) as error:
        print("P9a verification FAIL: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
